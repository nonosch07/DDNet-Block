#!/usr/bin/env python3
"""Block load test: a full server, driven through every feature it has.

Fills a 128-slot server with bots, connects real headless clients on top, and
walks them through the whole Block feature set while sampling what the server
costs. Built for two questions that only show up under load:

  * where does the CPU actually go when the server is full
  * does anything leak, stall or fall over while it is

    python3 src/block/tests/loadtest.py                  # measure
    python3 src/block/tests/loadtest.py --profile        # + gprof profile
    python3 src/block/tests/loadtest.py --bots 64        # smaller run

The server binary defaults to build-profile/DDNet-Server, which is the
RelWithDebInfo build: a Debug build spends most of its time in libstdc++'s
checked iterators and would profile that instead of the game.
"""

import argparse
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "integration"))

# The profiling build, not the Debug one the integration suite uses: a Debug
# server spends most of its time inside libstdc++'s checked containers, so a
# profile of it measures _GLIBCXX_DEBUG rather than the game. env reads this at
# import time, hence the assignment before the import rather than after.
DEFAULT_SERVER_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "build-profile", "DDNet-Server")
os.environ.setdefault("BLOCK_SERVER_BIN", os.path.abspath(DEFAULT_SERVER_BIN))

from env import (
    SERVER_BIN,
    Env,
    TestFailure,
    mysql,
    reset_database,
)

DEFAULT_BOTS = 127
TICK_RATE = 50


class Sampler:
    """Reads the server process's own accounting, once a second.

    /proc is the honest source here: it counts what the kernel charged the
    process, threads included, with no instrumentation in the way.
    """

    def __init__(self, pid):
        self.pid = pid
        self.samples = []
        self._clk = os.sysconf("SC_CLK_TCK")
        self._page = os.sysconf("SC_PAGE_SIZE")

    def _read(self):
        with open(f"/proc/{self.pid}/stat") as f:
            fields = f.read().rsplit(") ", 1)[1].split()
        utime, stime = int(fields[11]), int(fields[12])
        threads = int(fields[17])
        with open(f"/proc/{self.pid}/statm") as f:
            rss_pages = int(f.read().split()[1])
        return (utime + stime) / self._clk, rss_pages * self._page, threads

    def sample(self, label):
        cpu, rss, threads = self._read()
        self.samples.append({"t": time.time(), "label": label, "cpu": cpu, "rss": rss, "threads": threads})

    def report(self):
        out = []
        for prev, cur in zip(self.samples, self.samples[1:]):
            wall = cur["t"] - prev["t"]
            if wall <= 0:
                continue
            out.append({
                "label": cur["label"],
                "wall": wall,
                "cpu_pct": 100.0 * (cur["cpu"] - prev["cpu"]) / wall,
                "rss_mb": cur["rss"] / (1024 * 1024),
                "threads": cur["threads"],
                # a tick has 20 ms of wall clock at 50 Hz; this is how much
                # of it the server actually spent on the CPU
                "ms_per_tick": 1000.0 * (cur["cpu"] - prev["cpu"]) / (wall * TICK_RATE),
            })
        return out


def drive_features(env, server, client, log):
    """Walk one real client through everything the gamemode offers."""

    def step(name, fn):
        start = time.time()
        try:
            fn()
            log(f"  {name}: ok ({time.time() - start:.1f}s)")
        except Exception as exc:  # noqa: BLE001 - a failing step must not end the run
            log(f"  {name}: FAILED {exc}")

    step(
        "account",
        lambda: (
            client.chat_command("/register loadtest loadtestpw1", settle=2.0),
            client.chat_command("/login loadtest loadtestpw1", settle=2.0),
        ),
    )
    step(
        "blockpoints",
        lambda: (
            server.rcon("give_blockpoints 0 500000", settle=0.5),
            client.chat_command("/bp", settle=1.0),
        ),
    )
    step("votemenu", lambda: [client.callvote_option(page, settle=0.8) for page in ("│ ʀᴜʟᴇꜱ ›", "│ « ʙᴀᴄᴋ", "│ ʟᴇᴀᴅᴇʀʙᴏᴀʀᴅꜱ ›", "│ « ʙᴀᴄᴋ", "│ ꜱᴇʀᴠᴇʀ ɪɴꜰᴏꜱ ›", "│ « ʙᴀᴄᴋ", "│ ꜱᴇʀᴠᴇʀ ᴠᴏᴛᴇꜱ ›", "│ « ʙᴀᴄᴋ")])
    step("shop", lambda: [client.callvote_option(entry, settle=0.8) for entry in ("│ ꜱʜᴏᴘ ›", "│ Utilities ›", "│ VIP (1 week) - 1500 BP (Lvl 0)", "│ « ʙᴀᴄᴋ")])
    step("cosmetics", lambda: [client.callvote_option(entry, settle=0.8) for entry in ("│ « ʙᴀᴄᴋ", "│ ᴄᴏꜱᴍᴇᴛɪᴄꜱ ›", "│ « ʙᴀᴄᴋ")])
    step(
        "clans",
        lambda: (
            client.chat_command("/clan create LoadClan", settle=1.5),
            client.chat_command("/clan info", settle=1.0),
            client.chat_command("/c hello from the load test", settle=1.0),
        ),
    )
    step(
        "whois",
        lambda: (
            server.rcon("whois_name loadtester", settle=1.5),
            server.rcon("whois_ip 127.0.0.1", settle=1.5),
            server.rcon("whois_acc loadtest", settle=1.5),
        ),
    )
    step(
        "events",
        lambda: (
            server.rcon("component_plug events", settle=1.0),
            server.rcon("events_list", settle=0.5),
            server.rcon("events_start lmb", settle=2.0),
            server.rcon("events_stop", settle=1.5),
            server.rcon("events_start tdm", settle=2.0),
            server.rcon("events_stop", settle=1.5),
            server.rcon("events_start zcatch", settle=2.0),
            server.rcon("events_stop", settle=1.5),
            server.rcon("events_start bombtag", settle=2.0),
            server.rcon("events_stop", settle=1.5),
        ),
    )
    step(
        "moderation",
        lambda: (
            server.rcon("ip_whitelist_add 203.0.113.9", settle=0.5),
            server.rcon("ip_whitelist_list", settle=0.5),
            server.rcon("mutes", settle=0.5),
        ),
    )
    step(
        "leaderboards",
        lambda: (
            server.rcon("top_level", settle=1.0),
            server.rcon("top_blockpoints", settle=1.0),
            server.rcon("top_clans", settle=1.0),
        ),
    )
    step("chat", lambda: [client.say(f"load test message {i}", settle=0.15) for i in range(10)])


def run(args):
    def log(msg):
        print(msg, flush=True)

    if not os.path.exists(SERVER_BIN):
        raise TestFailure(
            f"no server binary at {SERVER_BIN}\n"
            "build it with:\n"
            "  cmake -B build-profile -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCLIENT=OFF "
            '-DTOOLS=OFF -DSERVER=ON -DMYSQL=ON -DANTIBOT=OFF -DCMAKE_CXX_FLAGS="-pg" '
            '-DCMAKE_C_FLAGS="-pg" -DCMAKE_EXE_LINKER_FLAGS="-pg" && ninja -C build-profile DDNet-Server'
        )

    reset_database()
    env = Env(keep=True)
    result = {"bots": args.bots, "seconds": args.seconds}
    try:
        log(f"starting server ({args.bots} bots, {args.seconds}s of load)")
        log(f"  binary: {SERVER_BIN}")
        server = env.server(
            extra_config=[
                "sv_max_clients 128",
                "sv_testing_commands 1",
                "sv_connlimit 200",
                "sv_max_clients_per_ip 128",
                "sv_spamprotection 0",
                "sv_whois_snapshot_minutes 1",
            ]
        )
        sampler = Sampler(server.proc.pid)
        sampler.sample("boot")

        log("connecting a real client")
        client = env.client("loadtester", server)
        time.sleep(1.0)
        sampler.sample("1 player")

        log(f"spawning {args.bots} bots")
        t0 = time.time()
        server.rcon(f"block_bots {args.bots}", settle=max(6.0, args.bots / 15.0))
        log(f"  spawned in {time.time() - t0:.1f}s")
        sampler.sample("bots joining")

        log("driving the feature set")
        drive_features(env, server, client, log)
        sampler.sample("features under load")

        log(f"holding {args.bots} bots for {args.seconds}s")
        for _ in range(args.seconds):
            time.sleep(1.0)
            sampler.sample(f"{args.bots} bots idle")

        result["alive"] = server.alive()
        result["errors"] = [l for l in server.snapshot() if any(w in l.lower() for w in ("assert", "sanitizer", "segfault", "failed on all databases"))]
        result["whois_rows"] = int(mysql("SELECT COUNT(*) FROM Block_whois_connections;")[0][0])
        result["samples"] = sampler.report()

        log("shutting the server down cleanly")
        server.rcon("shutdown", settle=1.0)
        for _ in range(60):
            if not server.alive():
                break
            time.sleep(0.5)
        result["clean_exit"] = not server.alive()
        result["tmp"] = env.tmp
    finally:
        env.close()
    return result


def summarise(result):
    print()
    print("=" * 72)
    print(f"load test: {result['bots']} bots, {result['seconds']}s hold")
    print("=" * 72)
    print(f"{'phase':<26}{'cpu %':>9}{'ms/tick':>10}{'rss MB':>9}{'threads':>9}")
    for s in result["samples"]:
        print(f"{s['label']:<26}{s['cpu_pct']:>9.1f}{s['ms_per_tick']:>10.2f}{s['rss_mb']:>9.1f}{s['threads']:>9}")
    idle = [s for s in result["samples"] if "idle" in s["label"]]
    if idle:
        avg_cpu = sum(s["cpu_pct"] for s in idle) / len(idle)
        avg_tick = sum(s["ms_per_tick"] for s in idle) / len(idle)
        rss_drift = idle[-1]["rss_mb"] - idle[0]["rss_mb"]
        print()
        print(f"steady state: {avg_cpu:.1f}% of a core, {avg_tick:.2f} ms per 20 ms tick")
        print(f"headroom:     {100.0 * (1.0 - avg_tick / 20.0):.0f}% of the tick budget left")
        print(f"rss drift over the hold: {rss_drift:+.1f} MB")
    print(f"survived: {result['alive']}, clean exit: {result['clean_exit']}")
    print(f"whois rows recorded: {result['whois_rows']}")
    if result["errors"]:
        print("ERRORS:")
        for line in result["errors"][:10]:
            print("  " + line)
    else:
        print("no asserts, sanitizer reports or database failures in the log")
    print(f"run directory: {result['tmp']}")


def profile(result, out_dir):
    """Turn the gmon.out the run left behind into a readable profile."""
    gmon = os.path.join(result["tmp"], "gmon.out")
    if not os.path.exists(gmon):
        print(f"no gmon.out in {result['tmp']} (was the server built with -pg?)")
        return None
    binary = SERVER_BIN
    txt = subprocess.run(
        ["gprof", "-b", "-p", "-q", binary, gmon],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "gprof.txt")
    with open(path, "w") as f:
        f.write(txt)
    print(f"gprof output: {path}")
    return path


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bots", type=int, default=DEFAULT_BOTS)
    p.add_argument("--seconds", type=int, default=30, help="how long to hold the load")
    p.add_argument("--profile", action="store_true", help="collect a gprof profile afterwards")
    p.add_argument("--out", default="loadtest-out", help="where to write the profile")
    args = p.parse_args()

    result = run(args)
    summarise(result)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "loadtest.json"), "w") as f:
        json.dump(result, f, indent=2)
    print(f"measurements: {os.path.join(args.out, 'loadtest.json')}")
    if args.profile:
        profile(result, args.out)
    return 0 if result["alive"] and result["clean_exit"] and not result["errors"] else 1


if __name__ == "__main__":
    sys.exit(main())
