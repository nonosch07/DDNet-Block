#!/usr/bin/env python3
"""List Block integration that upstream files are still missing.

The port moved Block's own code into src/block/, but Block also inserted code
*inside* upstream function bodies. This walks the old fork and reports, per
function, which of those insertions do not yet have a matching `BLOCK BEGIN` block
in the current tree -- so the port can be finished mechanically instead of from
memory.

    python3 src/block/tests/missing_hooks.py            # summary
    python3 src/block/tests/missing_hooks.py -v gamecontext.cpp
"""

import argparse
import difflib
import re
import subprocess
import sys

# What to diff: pristine upstream against the branch Block lives on, so the
# difference is Block's own work and nothing else.
#
# BASE used to be the commit the old fork was cut from. That worked only while
# the fork stayed on that upstream: once upstream is merged in, every function
# upstream touched since then also shows up as "added", and the report drowns in
# changes that were never Block's. vanilla-ddnet is moved to upstream by the
# merge itself, so pointing at it keeps this honest with no commit to bump.
BASE = "vanilla-ddnet"
OLD = "master"

# upstream file -> class whose methods we split on
FILES = {
    "src/game/server/gamecontext.cpp": "CGameContext",
    "src/game/server/player.cpp": "CPlayer",
    "src/game/server/entities/character.cpp": "CCharacter",
    "src/game/server/entities/projectile.cpp": "CProjectile",
    "src/game/server/entities/laser.cpp": "CLaser",
    "src/game/server/gamecontroller.cpp": "IGameController",
    "src/game/server/teams.cpp": "CGameTeams",
    "src/game/server/gameworld.cpp": "CGameWorld",
    "src/game/server/ddracechat.cpp": "CGameContext",
    "src/game/server/ddracecommands.cpp": "CGameContext",
    "src/game/gamecore.cpp": "CCharacterCore",
    "src/engine/server/server.cpp": "CServer",
    "src/engine/shared/netban.cpp": "CNetBan",
    "src/engine/shared/network_server.cpp": "CNetServer",
    "src/engine/server/antibot.cpp": "CAntibot",
    "src/engine/server/register.cpp": "CRegister",
}

# Insertions we deliberately do not port, with the reason.
WONTPORT = {
    ("src/game/server/gamecontext.cpp", "SnapLaserObject"): "Block snaps its own laser (CBlock::SnapLaserObject)",
    ("src/game/server/gamecontext.cpp", "SnapPickup"): "upstream grew the Flags parameter itself",
    ("src/game/server/gamecontext.cpp", "RegisterChatCommands"): "Block commands are registered from CBlock::OnConsoleInit",
    ("src/game/server/gamecontext.cpp", "RegisterDDRaceCommands"): "same",
    ("src/game/server/gamecontext.cpp", "Construct"): "ported as CBlock::OnConstruct",
    ("src/game/server/gamecontext.cpp", "Destruct"): "ported as CBlock::OnDestruct",
    ("src/game/server/gamecontroller.cpp", "OnPlayerDisconnect"): "ported into CGameControllerBlock",
    ("src/game/server/gamecontroller.cpp", "Snap"): "ported as OnSnapGameInfo/OnSnapGameInfoEx",
    ("src/game/server/gamecontroller.cpp", "OnCharacterSpawn"): "ported into CGameControllerBlock",
    # upstream moved or split the function, so the hook lives under another name
    ("src/game/server/gamecontext.cpp", "OnClientConnected"): "ported into CreatePlayer, which upstream split out",
    ("src/game/server/gamecontext.cpp", "OnPostSnap"): "upstream renamed it OnPostGlobalSnap",
    # cosmetic-only differences in the old fork that carry no behaviour
    ("src/game/server/gamecontext.cpp", "AddVote"): "comment only",
    ("src/game/server/gamecontext.cpp", "CreateDamageInd"): "commented-out line",
    ("src/game/server/gamecontext.cpp", "SendRecord"): "comment only",
    ("src/game/server/gamecontext.cpp", "OnClientDirectInput"): "empty brace reshuffle",
    ("src/game/server/entities/character.cpp", "Spawn"): "ported as CBlock::OnCharacterSpawn, called from CGameControllerBlock",
    ("src/game/server/entities/character.cpp", "SnapCharacter"): "upstream moved the afk emote into DetermineEyeEmote, hooked there",
    ("src/game/server/entities/character.cpp", "UnFreeze"): "upstream renamed it Unfreeze, hooked there",
    # upstream dropped the position argument from CCharacter::TakeDamage
    ("src/game/server/entities/character.cpp", "HandleJetpack"): "TakeDamage signature drift",
    ("src/game/server/entities/character.cpp", "HandleNinja"): "TakeDamage signature drift",
    ("src/game/server/ddracechat.cpp", "ConCredits"): "upstream removed /credits; Block registers its own ConCredits",
    ("src/game/server/gamecontroller.cpp", "Tick"): "ported as CGameControllerBlock::Tick",
    # upstream moved the mute commands into their own file
    ("src/game/server/ddracecommands.cpp", "ConMuteId"): "hooked in mutes.cpp, where upstream moved it",
    ("src/game/server/ddracecommands.cpp", "ConMuteIp"): "hooked in mutes.cpp, where upstream moved it",
    ("src/game/server/ddracecommands.cpp", "ConUnmute"): "hooked in mutes.cpp, where upstream moved it",
    ("src/game/server/ddracecommands.cpp", "ConUnmuteId"): "hooked in mutes.cpp, where upstream moved it",
    ("src/engine/shared/netban.cpp", "ConBan"): "upstream moved it to CServerBan::ConBanExt, hooked in server.cpp",
    ("src/engine/server/register.cpp", "OnConfigChange"): "upstream adopted sv_register_community_token itself",
    ("src/engine/shared/network_server.cpp", "Send"): "Block bots use upstream's m_DebugDummy, which Send already skips",
    # the old fork added a CClient::STATE_NPC; the port uses upstream's
    # m_DebugDummy instead, which already covers these
    ("src/engine/server/server.cpp", "GetClientAddr"): "NPCs use m_DebugDummyAddr",
    ("src/engine/server/server.cpp", "GetClientInfo"): "NPCs are STATE_INGAME debug dummies",
    ("src/engine/server/server.cpp", "ClientClan"): "same",
    ("src/engine/server/server.cpp", "ClientCountry"): "same",
    ("src/engine/server/server.cpp", "ClientIngame"): "same",
    ("src/engine/server/server.cpp", "ClientName"): "same",
    ("src/engine/server/server.cpp", "RedirectClient"): "ported as CBlock::RedirectClient",
    ("src/engine/server/server.cpp", "GetMapInfo"): "upstream removed it; the pub map is served from SendMap/SendMapData",
    ("src/engine/server/server.cpp", "SendRconLogLine"): "upstream dropped sv_show_ips in favour of the per-client flag",
    # the moderator rcon log moved to OnNetMsgRconCmd, where upstream split the
    # handler out; m_IsClientDummy was computed but its only use was already
    # commented out in the old fork, so it is dead code and not ported
    ("src/engine/server/server.cpp", "ProcessClientPacket"): "hooked in OnNetMsgRconCmd; m_IsClientDummy was dead code",
}


def git_show(rev, path):
    r = subprocess.run(["git", "show", f"{rev}:{path}"], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def split_functions(text, cls):
    """{name: [lines]} for every `... Class::Name(` definition at column 0."""
    if text is None:
        return {}
    lines = text.split("\n")
    pat = re.compile(r"^[A-Za-z_][\w :*&<>]*\b" + re.escape(cls) + r"::(\w+)\s*\(")
    out = {}
    for i, line in enumerate(lines):
        m = pat.match(line)
        if not m:
            continue
        j = i
        while j < len(lines) and "{" not in lines[j]:
            j += 1
        depth, k = 0, j
        while k < len(lines):
            depth += lines[k].count("{") - lines[k].count("}")
            if depth == 0:
                break
            k += 1
        out.setdefault(m.group(1), lines[i : k + 1])
    return out


def hooked_functions(path, cls):
    """Function names in the current tree that contain a `BLOCK BEGIN` marker."""
    try:
        lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    except OSError:
        return set()
    pat = re.compile(r"^[A-Za-z_][\w :*&<>]*\b" + re.escape(cls) + r"::(\w+)\s*\(")
    cur, hooked = None, set()
    for line in lines:
        m = pat.match(line)
        if m:
            cur = m.group(1)
        if "BLOCK BEGIN" in line and cur:
            hooked.add(cur)
    return hooked


def analyse(path, cls):
    old = split_functions(git_show(OLD, path), cls)
    base = split_functions(git_show(BASE, path), cls)
    hooked = hooked_functions(path, cls)
    rows = []
    for name in sorted(set(old) & set(base)):
        diff = list(difflib.unified_diff(base[name], old[name], lineterm="", n=0))
        added = [l[1:] for l in diff if l.startswith("+") and not l.startswith("+++")]
        if not added:
            continue
        rows.append(
            {
                "name": name,
                "added": added,
                "hooked": name in hooked,
                "wontport": WONTPORT.get((path, name)),
            }
        )
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("filter", nargs="?", default="", help="only files matching this substring")
    ap.add_argument("-v", "--verbose", action="store_true", help="print the missing Block lines")
    args = ap.parse_args()

    total_missing = todo_fns = 0
    for path, cls in FILES.items():
        if args.filter and args.filter not in path:
            continue
        rows = analyse(path, cls)
        todo = [r for r in rows if not r["hooked"] and not r["wontport"]]
        if not rows:
            continue
        done = len(rows) - len(todo)
        missing = sum(len(r["added"]) for r in todo)
        total_missing += missing
        todo_fns += len(todo)
        status = "OK " if not todo else "TODO"
        print(f"[{status}] {path}")
        print(f"        {done}/{len(rows)} functions done, {missing} Block lines missing")
        for r in sorted(todo, key=lambda r: -len(r["added"])):
            print(f"          - {r['name']} (+{len(r['added'])})")
            if args.verbose:
                for line in r["added"]:
                    print(f"              {line}")
        print()

    print(f"== {todo_fns} functions still need a Block hook, {total_missing} lines ==")
    return 1 if todo_fns else 0


if __name__ == "__main__":
    sys.exit(main())
