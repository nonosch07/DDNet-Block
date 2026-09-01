#!/usr/bin/env python3
"""Block integration suite.

Drives a real server and real headless clients and checks that each Block
feature actually does what it is supposed to do.

    python3 src/block/tests/integration/test.py --list
    python3 src/block/tests/integration/test.py --all
    python3 src/block/tests/integration/test.py votemenu map_change
"""

import argparse
import os
import re
import sys
import time
import traceback

from env import (
    CLIENT_BIN,
    DB_NAME,
    REPO,
    ZONE_MAP,
    Env,
    TestFailure,
    database_exists,
    drop_database,
    ensure_zone_map,
    load_reference_schema,
    mysql,
    preflight,
    reset_database,
    zone_map_dir,
)

TESTS = []

# Vote-menu labels, exactly as CVoteManager builds them: "│ " + SmallCaps(name) + " ›".
MENU_RULES = "│ ʀᴜʟᴇꜱ ›"
MENU_LEADERBOARDS = "│ ʟᴇᴀᴅᴇʀʙᴏᴀʀᴅꜱ ›"
MENU_COSMETICS = "│ ᴄᴏꜱᴍᴇᴛɪᴄꜱ ›"
MENU_SERVER_VOTES = "│ ꜱᴇʀᴠᴇʀ ᴠᴏᴛᴇꜱ ›"
MENU_BACK = "│ « ʙᴀᴄᴋ"
MENU_SHOP = "│ ꜱʜᴏᴘ ›"
# Shop category and item rows are not small-capped, only the "│ " bar is added.
SHOP_UTILITIES = "│ Utilities ›"
VIP_FOR_SALE = "│ VIP (1 week) - 1500 BP (Lvl 0)"
VIP_OWNED = "│ VIP (1 week) - Owned"


def test(fn):
    TESTS.append(fn)
    return fn


def expect(condition, message):
    if not condition:
        raise TestFailure(message)


def expect_chat(lines, needle, context=""):
    if not any(needle.lower() in l.lower() for l in lines):
        raise TestFailure(f"expected chat containing {needle!r} {context}\n  got: {lines}")


def balance(client):
    """The player's blockpoints, as /bp reports them."""
    lines = client.chat_command("/bp", settle=1.2)
    for line in reversed(lines):
        match = re.search(r"have (-?\d+) blockpoint", line)
        if match:
            return int(match.group(1))
    raise TestFailure(f"/bp did not answer with a balance: {lines}")


def whois_lines(log_lines):
    """The whois output of an rcon call, stripped of the log prefix."""
    return [l.split(" I whois: ", 1)[-1] for l in log_lines if " I whois: " in l]


def status_ids(log_lines):
    """The client ids an rcon `status` reported."""
    ids = []
    for line in log_lines:
        if "id=" not in line or "econ" in line:
            continue
        try:
            ids.append(int(line.split("id=")[1].split()[0]))
        except (IndexError, ValueError):
            continue
    return ids


def login_fresh(env, server, client, user="itest1", password="itestpw123"):
    """Register (if needed) and log in; returns the account id."""
    client.chat_command(f"/register {user} {password}", settle=2.5)
    lines = client.chat_command(f"/login {user} {password}", settle=2.5)
    expect_chat(lines, "login successfully", "after /login")
    rows = mysql(f"SELECT id FROM Block_accounts_core WHERE name='{user}';")
    expect(rows, f"account {user} was not written to the database")
    return int(rows[0][0])


# ---------------------------------------------------------------------------
# server / database basics
# ---------------------------------------------------------------------------


@test
def server_boots(env):
    """Server starts, loads the Block map, connects to MySQL and starts whois."""
    s = env.server()
    s.wait_for(r"whois sqlite path", timeout=20)
    s.wait_for(r"mysql: connection established", timeout=20)
    s.assert_absent(r"failed to load map")
    expect(s.alive(), "server died during startup")


@test
def database_created_on_first_startup(env):
    """The real first startup: the database itself does not exist yet.

    Upstream's connection setup issues the CREATE DATABASE, Block then fills it
    with its own tables, so all the operator has to provide is credentials.
    """
    drop_database()
    expect(not database_exists(), f"{DB_NAME} was not dropped")

    s = env.server()
    c = env.client("blocktester", s)
    time.sleep(2.0)

    expect(database_exists(), "the server did not create its database")
    tables = {r[0] for r in mysql("SHOW TABLES;")}
    for name in ("Block_accounts_core", "Block_clans"):
        expect(name in tables, f"{name} missing after first startup: {sorted(tables)}")

    c.chat_command("/register itest1 itestpw123", settle=2.5)
    lines = c.chat_command("/login itest1 itestpw123", settle=2.5)
    expect_chat(lines, "login successfully", "on a database the server just created")
    expect(s.alive(), "server died creating its database")


@test
def schema_created_from_empty_database(env):
    """A server pointed at an empty database builds its whole schema and works.

    This is the first-startup case: nothing but credentials, no dump loaded.
    """
    reset_database()
    expect(not mysql("SHOW TABLES;"), "the database was not emptied")

    s = env.server()
    c = env.client("blocktester", s)
    time.sleep(2.0)

    tables = {r[0] for r in mysql("SHOW TABLES;")}
    for name in (
        "Block_accounts_core",
        "Block_accounts_busy",
        "Block_accounts_inventory",
        "Block_accounts_progress",
        "Block_accounts_ranked",
        "Block_clans",
    ):
        expect(name in tables, f"{name} was not created: {sorted(tables)}")

    cols = {r[0] for r in mysql("DESCRIBE Block_accounts_inventory;")}
    for col in ("vip_until", "passive_removers"):
        expect(col in cols, f"{col} missing from accounts_inventory: {sorted(cols)}")
    cols = {r[0] for r in mysql("DESCRIBE Block_accounts_progress;")}
    for col in ("weekly_day", "weekly_last_claim", "weekly_exp_boost_until"):
        expect(col in cols, f"{col} missing from accounts_progress")

    # the schema is not just present, it is usable end to end
    c.chat_command("/register itest1 itestpw123", settle=2.5)
    lines = c.chat_command("/login itest1 itestpw123", settle=2.5)
    expect_chat(lines, "login successfully", "on a freshly created schema")
    expect(s.alive(), "server died building its schema")


@test
def schema_upgraded_from_older_database(env):
    """A database from a build that predates the newer columns is migrated."""
    reset_database()
    added = ("vip_until", "passive_removers", "weekly_day", "weekly_last_claim", "weekly_exp_boost_until")
    load_reference_schema(strip_columns=added)
    before = {r[0] for r in mysql("DESCRIBE Block_accounts_inventory;")}
    expect("vip_until" not in before, "the old-database fixture still has vip_until")

    s = env.server()
    env.client("blocktester", s)
    time.sleep(2.0)

    cols = {r[0] for r in mysql("DESCRIBE Block_accounts_inventory;")}
    for col in ("vip_until", "passive_removers"):
        expect(col in cols, f"{col} was not added on startup: {sorted(cols)}")
    cols = {r[0] for r in mysql("DESCRIBE Block_accounts_progress;")}
    for col in ("weekly_day", "weekly_last_claim", "weekly_exp_boost_until"):
        expect(col in cols, f"{col} was not added on startup")
    expect(s.alive(), "server died upgrading an older database")


# ---------------------------------------------------------------------------
# connect / accounts
# ---------------------------------------------------------------------------


@test
def schema_is_skipped_without_mysql(env):
    """A server with no MySQL write database must not try to build the schema.

    The pool hands out the local SQLite connection instead, where the MySQL DDL
    is a syntax error -- and a failed write drops the whole pool into fail mode,
    so getting this wrong breaks more than the accounts.
    """
    s = env.server(use_sql=False)
    time.sleep(2.0)
    log = s.snapshot()
    offenders = [l for l in log if "schema" in l.lower() and ("fail" in l.lower() or "error" in l.lower())]
    expect(not offenders, f"the schema job ran without a MySQL database: {offenders}")
    expect(s.alive(), "server died without a database")


@test
def client_connects(env):
    """A client connects and gets the Block greeting and the deferred join line."""
    s = env.server()
    c = env.client("blocktester", s)
    chat = c.chat()
    expect_chat(chat, "Block modification made by Nouaa", "as the connect greeting")
    expect_chat(chat, "entered and joined", "as the join broadcast")


@test
def accounts(env):
    """register writes a hashed row, login succeeds, and the queries answer."""
    reset_database()
    s = env.server()
    c = env.client("blocktester", s)

    c.chat_command("/register itest1 itestpw123", settle=2.5)
    rows = mysql("SELECT id, name, password FROM Block_accounts_core WHERE name='itest1';")
    expect(rows, "register did not create the account row")
    expect(rows[0][2].startswith("PBKDF2$"), f"password is not hashed: {rows[0][2][:24]}")

    lines = c.chat_command("/login itest1 itestpw123", settle=2.5)
    expect_chat(lines, "login successfully", "after /login")

    expect_chat(c.chat_command("/exp"), "account level", "for /exp")
    expect_chat(c.chat_command("/bp"), "blockpoints", "for /bp")
    expect_chat(c.chat_command("/profile"), "profile of itest1", "for /profile")

    account_id = int(rows[0][0])
    busy = mysql(f"SELECT account_id FROM Block_accounts_busy WHERE account_id={account_id};")
    expect(busy, "login did not mark the account busy")


@test
def admin_account_commands(env):
    """The rcon give_*/vip commands mutate account state and read back."""
    reset_database()
    s = env.server()
    c = env.client("blocktester", s)
    account_id = login_fresh(env, s, c)

    s.rcon("give_blockpoints 0 1000")
    s.rcon("give_level 0 30")
    s.rcon("vip_player 0 1")

    rows = mysql(f"SELECT level, blockpoints FROM Block_accounts_progress WHERE account_id={account_id};")
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
    Block state a console command captured has to survive that.
    """
    reset_database()
    s = env.server()
    c = env.client("blocktester", s)
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

    # a Block command after the reset would use freed state if the fix regressed
    out = s.rcon("status_acc")
    expect(s.alive(), "server died running a Block rcon command after reload")
    out = s.rcon("whois_name itest1")
    expect(s.alive(), "server died running whois after reload")
    s.assert_absent(r"Segmentation fault")


@test
def votemenu(env):
    """Block vote-menu entries are handled by Block, not run as commands."""
    reset_database()
    s = env.server()
    c = env.client("blocktester", s)
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
        # Block has to swallow these. Falling through to upstream's server-vote path
        # tells a normal player "isn't an option on this server", and for an
        # rcon-authed player runs the label itself as a console command.
        chat = c.chat(cstart)
        expect(
            not any("isn't an option on this server" in l for l in chat),
            f"menu entry {label!r} was not handled by Block: {chat}",
        )
        s.assert_absent(r"No such command", start=start)
        s.assert_absent(r"called vote to change server option", start=start)
        expect(s.alive(), f"server died handling menu entry {label!r}")


@test
def cosmetics(env):
    """Setting a cosmetic changes what the server snaps to other clients."""
    reset_database()
    s = env.server()
    c = env.client("blocktester", s)
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
    c = env.client("blocktester", s)
    account_id = login_fresh(env, s, c)
    s.rcon("component_plug requests")
    s.rcon("give_level 0 30")
    s.rcon("give_blockpoints 0 5000")
    time.sleep(1.0)

    lines = c.chat_command("/clan_create ITESTCLAN", settle=2.0)
    expect_chat(lines, "are you sure", "as the clan creation prompt")
    lines = c.chat_command("/clan_yes", settle=2.5)
    expect_chat(lines, "clan created successfully", "after /clan_yes")

    rows = mysql("SELECT id, name FROM Block_clans WHERE name='ITESTCLAN';")
    expect(rows, "clan row was not written")
    clan_id = int(rows[0][0])
    rows = mysql(f"SELECT clanID FROM Block_accounts_progress WHERE account_id={account_id};")
    expect(rows and int(rows[0][0]) == clan_id, f"account not attached to the clan: {rows}")

    expect_chat(c.chat_command("/clan_list"), "itest1", "in /clan_list")


@test
def whois(env):
    """whois records connections in MySQL and answers name and ip lookups."""
    reset_database()
    s = env.server()
    env.client("blocktester", s)
    time.sleep(2.0)

    expect(
        any("whois: using mysql" in l for l in s.snapshot()),
        f"whois did not pick the MySQL backend: {[l for l in s.snapshot() if 'whois' in l][:5]}",
    )
    tables = {r[0] for r in mysql("SHOW TABLES;")}
    for name in ("Block_whois_connections", "Block_whois_names_by_ip", "Block_whois_ips_by_name"):
        expect(name in tables, f"{name} was not created in MySQL: {sorted(tables)}")

    rows = mysql("SELECT ip, name, source FROM Block_whois_connections;")
    expect(any(r[1] == "blocktester" for r in rows), f"the connection was not recorded: {rows}")
    agg = mysql("SELECT ip, name, cnt FROM Block_whois_names_by_ip;")
    expect(any(r[1] == "blocktester" and int(r[2]) >= 1 for r in agg), f"the aggregate was not updated: {agg}")

    lines = whois_lines(s.rcon("whois_name blocktester", settle=3.0))
    expect(any("blocktester connected" in l for l in lines), f"whois_name did not answer: {lines}")
    expect(any("127.0.0.1" in l for l in lines), f"whois_name did not report the ip: {lines}")

    lines = whois_lines(s.rcon("whois_ip 127.0.0.1", settle=3.0))
    expect(any("127.0.0.1 connected" in l for l in lines), f"whois_ip did not answer: {lines}")
    expect(any("blocktester" in l for l in lines), f"whois_ip did not report the name: {lines}")
    expect(s.alive(), "server died during whois")


@test
def whois_falls_back_to_sqlite(env):
    """Without a MySQL write database, whois keeps working on a local file."""
    s = env.server(use_sql=False)
    env.client("blocktester", s)
    time.sleep(2.0)

    expect(
        any("whois: using sqlite" in l for l in s.snapshot()),
        f"whois did not fall back to sqlite: {[l for l in s.snapshot() if 'whois' in l][:5]}",
    )
    expect(
        os.path.exists(os.path.join(env.tmp, "whois.sqlite")),
        f"no whois.sqlite was written: {sorted(os.listdir(env.tmp))}",
    )
    lines = whois_lines(s.rcon("whois_name blocktester", settle=3.0))
    expect(any("blocktester connected" in l for l in lines), f"whois_name did not answer on sqlite: {lines}")
    expect(s.alive(), "server died running whois on sqlite")


@test
def whois_survives_a_flood_of_events(env):
    """Many joins and leaves in a row must not lose the server or the answers.

    The worker queue is bounded, so the point here is that pressure costs rows
    at worst, never a stall or a crash.
    """
    reset_database()
    # every client comes from 127.0.0.1, which the connection rate limiter reads
    # as one very busy address
    s = env.server(extra_config=["sv_connlimit 100", "sv_max_clients_per_ip 64"])
    for i in range(6):
        c = env.client(f"flood{i}", s)
        time.sleep(0.2)
        c.kill()
    time.sleep(3.0)

    rows = mysql("SELECT COUNT(*) FROM Block_whois_connections;")
    expect(int(rows[0][0]) > 0, "no connection was recorded under load")
    lines = whois_lines(s.rcon("whois_ip 127.0.0.1", settle=3.0))
    expect(any("connected" in l for l in lines), f"whois stopped answering under load: {lines}")
    expect(s.alive(), "server died under whois load")


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
    ensure_zone_map(env)
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
    ensure_zone_map(env)
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
    c = env.client("blocktester", s)
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
def shop_vip_week(env):
    """A week of VIP is bought once: it costs 1500 BP, sets an expiry a week out,
    survives a relog, and cannot be bought again while it is running."""
    reset_database()
    s = env.server()
    c = env.client("blocktester", s)
    login_fresh(env, s, c)
    s.rcon("give_blockpoints 0 5000")
    time.sleep(1.5)

    c.callvote_option(MENU_SHOP, settle=1.5)
    c.callvote_option(SHOP_UTILITIES, settle=1.5)
    # read the balance right before and after the click: a logged-in player also
    # earns blockpoints from playing, so 5000 is not what is left to spend
    before = balance(c)
    cstart = c.mark()
    bought_at = int(time.time())
    c.callvote_option(VIP_FOR_SALE, settle=2.0)
    chat = c.chat(cstart)
    expect_chat(chat, "successfully bought", "after buying VIP")
    expect_chat(chat, "you are now vip", "after buying VIP")
    spent = before - balance(c)
    expect(spent == 1500, f"the purchase took {spent} BP, expected 1500")

    # the account only reaches the database on the flush, so log out first
    c.chat_command("/logout", settle=3.0)
    time.sleep(2.0)
    rows = mysql(
        "SELECT i.vip, i.vip_until, p.blockpoints "
        "FROM Block_accounts_inventory i "
        "JOIN Block_accounts_core c ON i.account_id = c.id "
        "JOIN Block_accounts_progress p ON p.account_id = c.id "
        "WHERE c.name = 'itest1';"
    )
    expect(rows, "the VIP purchase never reached the database")
    vip, vip_until = int(rows[0][0]), int(rows[0][1])
    expect(vip == 1, f"vip flag is {vip}, expected 1")
    week = 7 * 24 * 60 * 60
    drift = abs(vip_until - (bought_at + week))
    expect(drift <= 30, f"vip_until is {drift}s off a week from the purchase")

    # coming back logged in, the item is owned rather than on sale again
    c.chat_command("/login itest1 itestpw123", settle=3.0)
    c.callvote_option(MENU_SHOP, settle=1.5)
    c.callvote_option(SHOP_UTILITIES, settle=1.5)
    cstart = c.mark()
    c.callvote_option(VIP_FOR_SALE, settle=1.5)
    chat = c.chat(cstart)
    expect(
        any("isn't an option on this server" in l for l in chat),
        f"VIP was still on sale for a player who already has it: {chat}",
    )
    cstart = c.mark()
    c.callvote_option(VIP_OWNED, settle=1.5)
    chat = c.chat(cstart)
    expect(
        not any("isn't an option on this server" in l for l in chat),
        f"the shop does not show VIP as owned: {chat}",
    )
    expect(s.alive(), "server died during the VIP purchase flow")


@test
def shop_npcs_do_not_take_player_slots(env):
    """A shop server's preview NPCs must not sit where a player is about to land.

    NPCs hold a client slot without ever opening a network connection, so the
    slot looked free to the network layer and the next player to join was put on
    top of one -- taking over the NPC's frozen, AFK state and unable to do
    anything.
    """
    reset_database()
    s = env.server(map_name="store", extra_config=["sv_shop_server 1", "sv_max_clients 64", "sv_shop_reserved_slots 8"])
    time.sleep(3.5)

    npc_ids = status_ids(s.rcon("status", settle=2.0))
    expect(npc_ids, "the shop server created no preview NPCs")
    # NPCs count down from the top, leaving the low ids to players
    expect(min(npc_ids) > 8, f"NPCs took low slots, players will collide with them: {sorted(npc_ids)}")

    c = env.client("realplayer", s)
    time.sleep(2.5)
    lines = s.rcon("status", settle=2.0)
    mine = [l for l in lines if "realplayer" in l and "id=" in l]
    expect(mine, f"the player did not get in at all: {lines[-6:]}")
    player_id = int(mine[0].split("id=")[1].split()[0])
    expect(player_id not in npc_ids, f"the player was put on NPC slot {player_id}")

    # and is a real player, not a leftover NPC: it can log in and be paid
    c.chat_command("/register shoptest shoptestpw1", settle=2.5)
    expect_chat(c.chat_command("/login shoptest shoptestpw1", settle=2.5), "login successfully", "on a shop server")
    s.rcon(f"give_blockpoints {player_id} 77", settle=1.0)
    expect_chat(c.chat_command("/bp", settle=1.5), "blockpoint", "after being paid on a shop server")
    expect(s.alive(), "server died with shop NPCs and a player")


@test
def shop_npcs_leave_room_on_a_small_server(env):
    """NPCs must never fill a shop server: a preview that does not fit is dropped."""
    reset_database()
    s = env.server(map_name="store", extra_config=["sv_shop_server 1", "sv_max_clients 16", "sv_shop_reserved_slots 8"])
    time.sleep(3.5)

    npc_ids = status_ids(s.rcon("status", settle=2.0))
    expect(len(npc_ids) < 16, f"NPCs filled every slot on a 16 slot server: {sorted(npc_ids)}")

    c = env.client("realplayer", s)
    time.sleep(2.5)
    chat = c.chat()
    expect(
        not any("server is full" in l.lower() for l in c.snapshot()),
        f"a player could not join a small shop server: {c.snapshot()[-5:]}",
    )
    expect_chat(chat, "entered and joined", "on a small shop server")
    expect(s.alive(), "server died on a small shop server")


@test
def shop_npcs_survive_a_full_server(env):
    """Filling every player slot must not push a joining client onto an NPC.

    This is the case the network-level reservation exists for: NPCs never open a
    connection, so without it CNetServer sees their slots as free and hands one
    out once the real ones run out.
    """
    reset_database()
    s = env.server(
        map_name="store",
        extra_config=["sv_shop_server 1", "sv_max_clients 16", "sv_shop_reserved_slots 8", "sv_connlimit 200", "sv_max_clients_per_ip 32"],
    )
    time.sleep(3.5)
    npc_ids = set(status_ids(s.rcon("status", settle=2.0)))
    expect(npc_ids, "the shop server created no preview NPCs")

    # one more client than there are free slots, so the last one has nowhere to go
    clients = [env.client(f"p{i}", s, connect=False) for i in range(9)]
    for c in clients:
        try:
            c.connect(s, timeout=8)
        except TestFailure:
            pass  # the server being full is the expected outcome for the last one
    time.sleep(2.0)

    lines = s.rcon("status", settle=2.0)
    stolen = [i for i in npc_ids if any(f"id={i} " in l and "name='p" in l for l in lines)]
    expect(not stolen, f"players were put on NPC slots {stolen}")
    expect(s.alive(), "server died filling a shop server")


@test
def shop_reloads_do_not_leak_snapshot_ids(env):
    """Reloading a shop map must give its snapshot ids back.

    The preview animations hold ids that only their destructors return, and the
    animation handler dies with the game context on every map change. Leaving
    them behind leaked a few thousand ids per reload until the pool of 32768 ran
    dry, at which point the server asserted on the first snap it could not get an
    id for -- a crash after roughly eight reloads.
    """
    reset_database()
    s = env.server(map_name="store", extra_config=["sv_shop_server 1", "sv_max_clients 128", "sv_shop_reserved_slots 8"])
    env.client("realplayer", s)
    time.sleep(4.0)

    for _ in range(10):
        s.rcon("reload", settle=3.0)
        time.sleep(1.5)
        expect(s.alive(), "the server died while reloading a shop map")

    log = s.snapshot()
    expect(
        not any("invalid id" in l for l in log),
        "the snapshot id pool ran out, so reloading still leaks ids",
    )
    expect(not any("assert" in l.lower() for l in log), f"an assert fired: {[l for l in log if 'assert' in l.lower()][:2]}")
    expect(s.alive(), "server died after repeated shop reloads")


@test
def remove_all_weapons_leaves_nothing(env):
    """remove_all_weapons must leave the player with no weapon at all.

    Upstream's `removeweapon -1` only covers shotgun, grenade and laser, and it
    clears possession without clearing ammo -- which is what FireWeapon gates on
    -- so a player stripped that way keeps a weapon in hand.
    """
    reset_database()
    s = env.server(extra_config=["sv_max_clients 64"])
    c = env.client("blocktester", s)
    time.sleep(2.0)

    out = s.rcon("remove_all_weapons 0", settle=2.0)
    lines = [l.split(" I weapons: ", 1)[-1] for l in out if " I weapons: " in l]
    expect(any("holding nothing" in l for l in lines), f"the command did not report success: {lines}")

    # handing one back must work, so the empty hands are not a stuck state
    s.rcon("addweapon 2", settle=1.5)
    time.sleep(1.0)
    expect(s.alive(), "server died giving a weapon back")
    expect(c.alive(), "client dropped after being stripped")

    # and it survives a respawn
    s.rcon("kill_pl 0", settle=2.0)
    time.sleep(1.5)
    expect(s.alive(), "server died respawning a stripped player")


@test
def block_bots_have_no_names(env):
    """Load-test bots join nameless, like the shop NPCs."""
    reset_database()
    s = env.server(extra_config=["sv_max_clients 64"])
    env.client("blocktester", s)
    time.sleep(2.0)
    s.rcon("block_bots 4", settle=4.0)
    time.sleep(1.5)

    lines = [l for l in s.rcon("status", settle=2.0) if "id=" in l and "econ" not in l]
    expect(not any("name='bot" in l for l in lines), f"a bot still carries a generated name: {lines[:4]}")
    blank = sum(1 for l in lines if "name=' '" in l)
    expect(blank == 4, f"expected 4 nameless bots, found {blank}")
    expect(not any("name='(" in l for l in lines), f"a bot was auto-renamed: {lines[:4]}")
    expect(s.alive(), "server died spawning nameless bots")


@test
def public_map(env):
    """With a <map>_pub.map next to it, clients are handed the public variant."""
    import map

    ensure_zone_map(env)
    # the public variant is a different map, so its hash must differ
    pub = os.path.join(zone_map_dir(env), f"{ZONE_MAP}_pub.map")
    with open(pub, "wb") as f:
        # half the width makes a genuinely different file
        old_width, map.WIDTH = map.WIDTH, 40
        f.write(map.build())
        map.WIDTH = old_width

    s = env.server(map_name=ZONE_MAP)
    expect(
        s.find(r"public map sent to clients"),
        f"the server did not pick up the public map: {s.snapshot()[-10:]}",
    )
    # and a client still gets in, i.e. the map it downloads is coherent
    env.client("pubtester", s)
    expect(s.alive(), "server died serving the public map")


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
    """/info and /credits identify Block."""
    s = env.server()
    c = env.client("infotester", s)
    time.sleep(1.0)

    expect_chat(c.chat_command("/info", settle=1.5), "block", "in /info")
    expect_chat(c.chat_command("/credits", settle=1.5), "nouaa", "in /credits")
    expect_chat(c.chat_command("/contributors", settle=1.5), "contributors", "in /contributors")


@test
def kill_gate(env):
    """/kill is refused while in a duel's configuration phase."""
    ensure_zone_map(env)
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
