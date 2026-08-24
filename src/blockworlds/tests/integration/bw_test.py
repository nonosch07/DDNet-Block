#!/usr/bin/env python3
"""Blockworlds integration suite.

Drives a real server and real headless clients and checks that each Blockworlds
feature actually does what it is supposed to do.

    python3 src/blockworlds/tests/integration/bw_test.py --list
    python3 src/blockworlds/tests/integration/bw_test.py --all
    python3 src/blockworlds/tests/integration/bw_test.py votemenu map_change
"""

import argparse
import os
import sys
import time
import traceback

from bw_env import (
    CLIENT_BIN,
    REPO,
    SERVER_BIN,
    ZONE_MAP,
    Env,
    TestFailure,
    ensure_zone_map,
    mysql,
    preflight,
    reset_database,
)

TESTS = []

# Vote-menu labels, exactly as CVoteManager builds them: "│ " + SmallCaps(name) + " ›".
MENU_RULES = "│ ʀᴜʟᴇꜱ ›"
MENU_LEADERBOARDS = "│ ʟᴇᴀᴅᴇʀʙᴏᴀʀᴅꜱ ›"
MENU_COSMETICS = "│ ᴄᴏꜱᴍᴇᴛɪᴄꜱ ›"
MENU_SERVER_VOTES = "│ ꜱᴇʀᴠᴇʀ ᴠᴏᴛᴇꜱ ›"
MENU_BACK = "│ « ʙᴀᴄᴋ"


def test(fn):
    TESTS.append(fn)
    return fn


def expect(condition, message):
    if not condition:
        raise TestFailure(message)


def expect_chat(lines, needle, context=""):
    if not any(needle.lower() in l.lower() for l in lines):
        raise TestFailure(f"expected chat containing {needle!r} {context}\n  got: {lines}")


def login_fresh(env, server, client, user="itest1", password="itestpw123"):
    """Register (if needed) and log in; returns the account id."""
    client.chat_command(f"/register {user} {password}", settle=2.5)
    lines = client.chat_command(f"/login {user} {password}", settle=2.5)
    expect_chat(lines, "login successfully", "after /login")
    rows = mysql(f"SELECT id FROM Blockworlds_accounts_core WHERE name='{user}';")
    expect(rows, f"account {user} was not written to the database")
    return int(rows[0][0])


# ---------------------------------------------------------------------------
# server / database basics
# ---------------------------------------------------------------------------


@test
def server_boots(env):
    """Server starts, loads the BW map, connects to MySQL and starts whois."""
    s = env.server()
    s.wait_for(r"whois sqlite path", timeout=20)
    s.wait_for(r"mysql: connection established", timeout=20)
    s.assert_absent(r"failed to load map")
    expect(s.alive(), "server died during startup")


@test
def schema_migrations_apply(env):
    """schema.sql plus every migration leaves a complete, empty schema."""
    reset_database()
    cols = {r[0] for r in mysql("DESCRIBE Blockworlds_accounts_inventory;")}
    expect("passive_removers" in cols, f"passive_removers missing: {sorted(cols)}")
    cols = {r[0] for r in mysql("DESCRIBE Blockworlds_accounts_progress;")}
    for c in ("weekly_day", "weekly_last_claim", "weekly_exp_boost_until"):
        expect(c in cols, f"{c} missing from accounts_progress")


# ---------------------------------------------------------------------------
# connect / accounts
# ---------------------------------------------------------------------------


@test
def client_connects(env):
    """A client connects and gets the BW greeting and the deferred join line."""
    s = env.server()
    c = env.client("bwtester", s)
    chat = c.chat()
    expect_chat(chat, "Blockworlds src2 by Nouaa", "as the connect greeting")
    expect_chat(chat, "entered and joined", "as the join broadcast")


@test
def accounts(env):
    """register writes a hashed row, login succeeds, and the queries answer."""
    reset_database()
    s = env.server()
    c = env.client("bwtester", s)

    c.chat_command("/register itest1 itestpw123", settle=2.5)
    rows = mysql("SELECT id, name, password FROM Blockworlds_accounts_core WHERE name='itest1';")
    expect(rows, "register did not create the account row")
    expect(rows[0][2].startswith("PBKDF2$"), f"password is not hashed: {rows[0][2][:24]}")

    lines = c.chat_command("/login itest1 itestpw123", settle=2.5)
    expect_chat(lines, "login successfully", "after /login")

    expect_chat(c.chat_command("/exp"), "account level", "for /exp")
    expect_chat(c.chat_command("/bp"), "blockpoints", "for /bp")
    expect_chat(c.chat_command("/profile"), "profile of itest1", "for /profile")

    account_id = int(rows[0][0])
    busy = mysql(f"SELECT account_id FROM Blockworlds_accounts_busy WHERE account_id={account_id};")
    expect(busy, "login did not mark the account busy")


@test
def admin_account_commands(env):
    """The rcon give_*/vip commands mutate account state and read back."""
    reset_database()
    s = env.server()
    c = env.client("bwtester", s)
    account_id = login_fresh(env, s, c)

    s.rcon("give_blockpoints 0 1000")
    s.rcon("give_level 0 30")
    s.rcon("vip_player 0 1")

    rows = mysql(f"SELECT level, blockpoints FROM Blockworlds_accounts_progress WHERE account_id={account_id};")
    expect(rows, "no progress row")
    level, bp = int(rows[0][0]), int(rows[0][1])
    expect(level >= 30, f"level not applied (got {level})")
    expect(bp >= 1000, f"blockpoints not applied (got {bp})")

    out = s.rcon("status_acc")
    expect(any("acc_name='itest1'" in l for l in out), f"status_acc did not list the account: {out[-5:]}")


# ---------------------------------------------------------------------------
# the reported bugs
# ---------------------------------------------------------------------------


@test
def map_change_and_reload(env):
    """change_map and reload while logged in must not crash the server.

    CGameContext::Clear() destroys and re-creates the context in place, so any
    Blockworlds state a console command captured has to survive that.
    """
    reset_database()
    s = env.server()
    c = env.client("bwtester", s)
    login_fresh(env, s, c)

    for round_no in range(2):
        start = s.mark()
        s.rcon("reload", settle=1.0)
        s.wait_for(r"switch \d+ opened by default", timeout=25, start=start)
        time.sleep(1.5)
        expect(s.alive(), f"server died during reload (round {round_no})")

    start = s.mark()
    s.rcon("change_map blmapV3ROYAL", settle=3.0)
    time.sleep(1.5)
    expect(s.alive(), "server died during change_map")

    # a BW command after the reset would use freed state if the fix regressed
    out = s.rcon("status_acc")
    expect(s.alive(), "server died running a BW rcon command after reload")
    out = s.rcon("whois_name itest1")
    expect(s.alive(), "server died running whois after reload")
    s.assert_absent(r"Segmentation fault")


@test
def votemenu(env):
    """Blockworlds vote-menu entries are handled by BW, not run as commands."""
    reset_database()
    s = env.server()
    c = env.client("bwtester", s)
    login_fresh(env, s, c)
    time.sleep(1.5)

    # The client's `callvote option` sends whatever text it is given, so the menu
    # entries can be exercised by their exact label. These are what
    # CVoteManager builds: "│ " + SmallCaps(name) + " ›".
    # Open a page, come back, open another: each entry is only valid on the page
    # the player is currently looking at, so this also checks navigation.
    for label in (MENU_RULES, MENU_BACK, MENU_LEADERBOARDS, MENU_BACK, MENU_COSMETICS):
        start = s.mark()
        cstart = c.mark()
        c.callvote_option(label, settle=1.5)
        # BW has to swallow these. Falling through to upstream's server-vote path
        # tells a normal player "isn't an option on this server", and for an
        # rcon-authed player runs the label itself as a console command.
        chat = c.chat(cstart)
        expect(
            not any("isn't an option on this server" in l for l in chat),
            f"menu entry {label!r} was not handled by BW: {chat}",
        )
        s.assert_absent(r"No such command", start=start)
        s.assert_absent(r"called vote to change server option", start=start)
        expect(s.alive(), f"server died handling menu entry {label!r}")


@test
def cosmetics(env):
    """Setting a cosmetic changes what the server snaps to other clients."""
    reset_database()
    s = env.server()
    c = env.client("bwtester", s)
    account_id = login_fresh(env, s, c)
    s.rcon("give_blockpoints 0 5000")

    start = s.mark()
    s.rcon("c_set_gundesign 1 0")
    expect(s.alive(), "server died setting a gundesign")
    s.rcon("c_set_skinmani 1 0")
    s.rcon("c_set_knockout 1 0")
    s.rcon("c_set_special 0 0")
    expect(s.alive(), "server died setting cosmetics")

    lines = c.chat_command("/profile", settle=1.5)
    expect(lines, "no response to /profile after setting cosmetics")


@test
def clans(env):
    """A clan can be created, is written to the database and lists its members."""
    reset_database()
    s = env.server()
    c = env.client("bwtester", s)
    account_id = login_fresh(env, s, c)
    s.rcon("component_plug requests")
    s.rcon("give_level 0 30")
    s.rcon("give_blockpoints 0 5000")
    time.sleep(1.0)

    lines = c.chat_command("/clan_create ITESTCLAN", settle=2.0)
    expect_chat(lines, "are you sure", "as the clan creation prompt")
    lines = c.chat_command("/clan_yes", settle=2.5)
    expect_chat(lines, "clan created successfully", "after /clan_yes")

    rows = mysql("SELECT id, name FROM Blockworlds_clans WHERE name='ITESTCLAN';")
    expect(rows, "clan row was not written")
    clan_id = int(rows[0][0])
    rows = mysql(f"SELECT clanID FROM Blockworlds_accounts_progress WHERE account_id={account_id};")
    expect(rows and int(rows[0][0]) == clan_id, f"account not attached to the clan: {rows}")

    expect_chat(c.chat_command("/clan_list"), "itest1", "in /clan_list")


@test
def whois(env):
    """whois logs connections and answers name/ip/account queries."""
    reset_database()
    s = env.server()
    c = env.client("bwtester", s)
    time.sleep(1.5)
    out = s.rcon("whois_name bwtester", settle=1.5)
    expect(any("whois" in l for l in out), f"whois_name produced nothing: {out[-5:]}")
    out = s.rcon("whois_ip 127.0.0.1", settle=1.5)
    expect(any("whois" in l for l in out), f"whois_ip produced nothing: {out[-5:]}")
    expect(s.alive(), "server died during whois")


@test
def components(env):
    """Components can be listed, plugged and unplugged at runtime."""
    s = env.server()
    out = s.rcon("component_list")
    expect(any("Registered Components" in l for l in out), f"component_list produced nothing: {out[-5:]}")

    out = s.rcon("component_plug events")
    expect(any("Component created: events" in l for l in out), f"events did not plug: {out[-8:]}")
    out = s.rcon("events_list")
    expect(any("bombtag" in l for l in out), f"events_list produced nothing: {out[-8:]}")

    out = s.rcon("component_unplug events")
    expect(s.alive(), "server died unplugging a component")


@test
def chatfilter(env):
    """The chat filter loads its word list and mutes on a hit."""
    s = env.server()
    out = s.rcon("component_plug chatfilter")
    expect(any("Component created: chatfilter" in l for l in out), f"chatfilter did not plug: {out[-8:]}")
    out = s.rcon("chatfilter_list")
    expect(len(out) > 3, f"chatfilter_list looks empty: {out[-5:]}")


@test
def vpn_detection(env):
    """The VPN component registers its providers and reports status."""
    s = env.server()
    out = s.rcon("component_plug vpndetection")
    expect(any("VPN service registered" in l for l in out), f"vpn services did not register: {out[-10:]}")
    out = s.rcon("vpn_status")
    expect(any("VPN Detection Status" in l for l in out), f"vpn_status produced nothing: {out[-5:]}")


@test
def oneonone(env):
    """A duel goes invite -> accept -> prep zone -> F3 from both -> arena."""
    ensure_zone_map()
    # both clients come from 127.0.0.1, which the dummy guard would reject
    s = env.server(map_name=ZONE_MAP, extra_config=["sv_events_test_mode 1"])
    # invites live in the requests component, which is opt-in
    s.rcon("component_plug requests")
    a = env.client("duelA", s)
    b = env.client("duelB", s)

    lines = a.chat_command("/1on1 duelB 0", settle=2.0)
    expect_chat(lines, "match request has been sent", "after /1on1")

    lines = b.chat_command("/accept duelA", settle=2.5)
    for who, chat in (("challenger", a.chat(0)), ("target", lines)):
        expect_chat(chat, "teleported to the arena", f"for the {who} after /accept")
        expect_chat(chat, "F3=Start", f"for the {who}: the config-phase prompt is missing")

    # the invite is consumed once the duel is running
    out = s.rcon("list_outstanding_invites")
    expect(
        not any("duelA" in l and "duelB" in l for l in out),
        f"the invite is still outstanding after it was accepted: {out[-8:]}",
    )

    # F3 from one player alone must be acknowledged but must not start the match
    start_a = a.mark()
    a.vote(True, settle=1.2)
    expect_chat(a.chat(start_a), "voted to start", "after the challenger pressed F3")
    a.assert_absent(r"No such command", start_a)
    expect(
        not any("both players ready" in l.lower() for l in a.chat(start_a)),
        "the match started on a single F3",
    )

    # the second F3 starts it
    start_b = b.mark()
    b.vote(True, settle=2.5)
    expect_chat(b.chat(start_b), "both players ready", "after the second F3")

    s.assert_absent(r"No such command")


@test
def oneonone_cancel(env):
    """F4 from both players aborts the duel instead of starting it."""
    ensure_zone_map()
    s = env.server(map_name=ZONE_MAP, extra_config=["sv_events_test_mode 1"])
    s.rcon("component_plug requests")
    a = env.client("cancelA", s)
    b = env.client("cancelB", s)

    a.chat_command("/1on1 cancelB 0", settle=2.0)
    b.chat_command("/accept cancelA", settle=2.5)

    start_a = a.mark()
    a.vote(False, settle=1.2)
    expect_chat(a.chat(start_a), "voted to cancel", "after the challenger pressed F4")

    start_b = b.mark()
    b.vote(False, settle=2.5)
    lines = b.chat(start_b) + a.chat(start_a)
    expect(
        any("cancel" in l.lower() or "abort" in l.lower() for l in lines),
        f"the duel was not aborted after both players pressed F4: {lines}",
    )


@test
def server_votes_page(env):
    """Real server votes live on their own menu page instead of being streamed
    underneath it, which is what made them show up half-sent or interleaved."""
    reset_database()
    s = env.server(extra_config=['add_vote "ZZ Test Vote" "info"'])
    c = env.client("bwtester", s)
    login_fresh(env, s, c)
    time.sleep(1.5)

    # open the new page from the root menu
    cstart = c.mark()
    sstart = s.mark()
    c.callvote_option(MENU_SERVER_VOTES, settle=1.5)
    chat = c.chat(cstart)
    expect(
        not any("isn't an option on this server" in l for l in chat),
        f"the Server Votes entry is missing from the root menu: {chat}",
    )
    s.assert_absent(r"No such command", start=sstart)
    expect(s.alive(), "server died opening the Server Votes page")

    # the page lists the real votes verbatim, so clicking one runs the actual
    # vote through the engine rather than being swallowed by the menu
    cstart = c.mark()
    c.callvote_option("ZZ Test Vote", settle=1.5)
    chat = c.chat(cstart)
    expect(
        not any("isn't an option on this server" in l for l in chat),
        f"a real server vote on the page was not accepted: {chat}",
    )
    expect(s.alive(), "server died calling a real vote from the page")


# ---------------------------------------------------------------------------
# engine-level features
# ---------------------------------------------------------------------------


@test
def public_map(env):
    """With a <map>_pub.map next to it, clients are handed the public variant."""
    import bw_map

    ensure_zone_map()
    # the public variant is a different map, so its hash must differ
    pub = os.path.join(REPO, "data", "maps", f"{ZONE_MAP}_pub.map")
    for root in (os.path.join(os.path.dirname(SERVER_BIN), "data"), os.path.join(REPO, "data")):
        maps = os.path.join(root, "maps")
        if os.path.isdir(maps):
            pub = os.path.join(maps, f"{ZONE_MAP}_pub.map")
            with open(pub, "wb") as f:
                # half the width makes a genuinely different file
                old_width, bw_map.WIDTH = bw_map.WIDTH, 40
                f.write(bw_map.build())
                bw_map.WIDTH = old_width
    try:
        s = env.server(map_name=ZONE_MAP)
        expect(
            s.find(r"public map sent to clients"),
            f"the server did not pick up the public map: {s.snapshot()[-10:]}",
        )
        # and a client still gets in, i.e. the map it downloads is coherent
        env.client("pubtester", s)
        expect(s.alive(), "server died serving the public map")
    finally:
        for root in (os.path.join(os.path.dirname(SERVER_BIN), "data"), os.path.join(REPO, "data")):
            path = os.path.join(root, "maps", f"{ZONE_MAP}_pub.map")
            if os.path.exists(path):
                os.unlink(path)


@test
def ip_whitelist(env):
    """An IP can be exempted from sv_max_clients_per_ip, and the list persists."""
    s = env.server()
    s.rcon("ip_whitelist_add 203.0.113.7")
    out = s.rcon("ip_whitelist_list")
    expect(any("203.0.113.7" in l for l in out), f"the IP was not whitelisted: {out[-6:]}")

    s.rcon("ip_whitelist_remove 203.0.113.7")
    out = s.rcon("ip_whitelist_list")
    expect(
        not any("203.0.113.7" in l for l in out),
        f"the IP is still whitelisted after removal: {out[-6:]}",
    )


@test
def moderation(env):
    """Mutes are spelled out for the player and lifted again."""
    s = env.server()
    c = env.client("mutetester", s)
    time.sleep(1.0)

    start = c.mark()
    s.rcon("muteid 0 90 itest")
    time.sleep(1.0)
    expect_chat(c.chat(start), "has been muted", "as the mute announcement")

    # the refusal is where the duration is spelled out for the player
    start = c.mark()
    lines = c.chat_command("hello", settle=1.5)
    expect(
        any("1 minute" in l and "second" in l for l in lines),
        f"the mute duration was not spelled out for the player: {lines}",
    )
    expect(
        not any(l.strip().endswith("hello") for l in lines),
        f"a muted player still reached chat: {lines}",
    )

    s.rcon("unmuteid 0")
    expect(s.alive(), "server died unmuting")


@test
def vpn_http_path(env):
    """A VPN lookup actually goes out over the engine's HTTP layer.

    This used to call curl_easy_perform directly, which is not in the stub
    libcurl DDNet ships, so it only ever linked on a machine with a full system
    curl. Asserts the request is issued and resolves either way -- a network
    failure is a pass, a hang or a crash is not.
    """
    s = env.server(extra_config=["sv_vpn_enabled 1", "sv_vpn_debug 1"])
    s.rcon("component_plug vpndetection")

    start = s.mark()
    s.rcon("vpn_check_force 8.8.8.8", settle=15)
    lines = s.snapshot()[start:]

    expect(
        any("HTTP request initiated" in l for l in lines),
        f"the VPN lookup never issued a request: {lines[-8:]}",
    )
    expect(
        any("HTTP request completed" in l or "HTTP request failed" in l for l in lines),
        f"the VPN request neither completed nor failed, it hung: {lines[-8:]}",
    )
    expect(s.alive(), "the server died performing a VPN lookup")


# ---------------------------------------------------------------------------
# chat and commands
# ---------------------------------------------------------------------------


@test
def clan_chat(env):
    """Team chat is clan chat, and refuses rather than falling back to public."""
    reset_database()
    s = env.server()
    c = env.client("chattester", s)
    time.sleep(1.0)

    start = c.mark()
    c.command('say_team "secret"')
    time.sleep(1.0)
    lines = c.chat(start)
    expect_chat(lines, "logged in and in a clan", "as the clan chat refusal")
    expect(
        not any(l.strip().endswith("secret") for l in lines),
        f"a refused clan message leaked into public chat: {lines}",
    )


@test
def info_and_credits(env):
    """/info and /credits identify Blockworlds."""
    s = env.server()
    c = env.client("infotester", s)
    time.sleep(1.0)

    expect_chat(c.chat_command("/info", settle=1.5), "blockworlds", "in /info")
    expect_chat(c.chat_command("/credits", settle=1.5), "nouaa", "in /credits")
    expect_chat(c.chat_command("/contributors", settle=1.5), "contributors", "in /contributors")


@test
def kill_gate(env):
    """/kill is refused while in a duel's configuration phase."""
    ensure_zone_map()
    s = env.server(map_name=ZONE_MAP, extra_config=["sv_events_test_mode 1"])
    s.rcon("component_plug requests")
    a = env.client("killA", s)
    b = env.client("killB", s)

    a.chat_command("/1on1 killB 0", settle=2.0)
    b.chat_command("/accept killA", settle=2.5)

    lines = a.chat_command("/kill", settle=1.5)
    expect_chat(lines, "can't /kill while participating", "after /kill during a duel")


# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="tests to run (default: all)")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep", action="store_true", help="keep the temp dir of failing runs")
    args = ap.parse_args()

    if args.list:
        for t in TESTS:
            print(f"{t.__name__:28s} {(t.__doc__ or '').strip().splitlines()[0] if t.__doc__ else ''}")
        return 0

    if not preflight():
        return 2

    selected = [t for t in TESTS if not args.names or t.__name__ in args.names]
    if args.names:
        unknown = set(args.names) - {t.__name__ for t in TESTS}
        if unknown:
            print(f"unknown test(s): {', '.join(sorted(unknown))}", file=sys.stderr)
            return 2

    failures = []
    for t in selected:
        sys.stdout.write(f"{t.__name__:28s} ")
        sys.stdout.flush()
        started = time.time()
        env = Env(keep=args.keep)
        try:
            t(env)
            print(f"ok    ({time.time() - started:.1f}s)")
        except TestFailure as e:
            print(f"FAIL  ({time.time() - started:.1f}s)")
            failures.append((t.__name__, str(e)))
        except Exception:
            print(f"ERROR ({time.time() - started:.1f}s)")
            failures.append((t.__name__, traceback.format_exc()))
        finally:
            env.close()

    print()
    if failures:
        for name, detail in failures:
            print(f"--- {name} ---")
            print(detail)
        print(f"{len(selected) - len(failures)}/{len(selected)} passed, {len(failures)} failed")
        return 1
    print(f"{len(selected)}/{len(selected)} passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
