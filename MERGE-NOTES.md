# MERGE-NOTES.md — Blockworlds ported to current DDNet

Result of rebuilding Blockworlds on top of upstream DDNet instead of merging two
years of upstream history into the fork.

* **Old fork:** based on upstream `c0ff4c35f4` (2024-11-05, just after `18.7-rc1`).
  `version.h` claimed `19.6` / `DDNET_VERSION_NUMBER 18080`; both were wrong.
* **New base:** `upstream/master` `2744c61dfa` (2026-08-19, post `20.0-rc5`).
* **Delta skipped over:** 922 files, +135 871 / −72 170.

Branches: `vanilla-ddnet` (pristine upstream) → `bw-next` (BW on top).
The old fork is preserved untouched on `master` and `update-ddnet`.

## Why not a merge

`git merge-base master upstream/master` resolved to a commit from **June 2010**:
this repository's DDNet history is a re-hashed copy (20 935 of `master`'s 21 331
commits match an upstream commit by author-date and subject but carry different
SHAs). Any merge would have replayed ~16 years of DDNet as conflicts. A first
attempt — stitching the histories with a zero-diff merge commit and then merging
release checkpoint by release checkpoint — is on `update-ddnet` and got through
18.8 before the approach was changed to this one.

## Footprint

|  | old fork | now |
|---|---|---|
| upstream files modified | 71 | **37** |
| lines added to upstream files | ~5 000 | **1 296** |
| upstream files deleted | 848 (all `data/`) | **0** |
| BW-owned code | 33 083 lines | unchanged, all under `src/blockworlds/` |

Every remaining edit is inside `// --- BW BEGIN ... BW END ---` delimiters and
documented in [MODDING.md](MODDING.md).

---

## Upstream changes that affected BW

### Reorganisations
* `base/system.{h,cpp}` split into ~20 headers → one BW umbrella, `bw_base.h`.
* `engine/shared/http.{h,cpp}` → `engine/http.{h,cpp}` + `http_curl.{h,cpp}`;
  `CHttpRequest` became the `IHttpRequest` interface plus a `CreateHttpRequest`
  factory, and `CHttp` became `IHttp` + `CreateEngineHttp()`.
* `game/generated/` → `generated/`.
* `gamemodes/DDRace.{h,cpp}` → `ddnet.{h,cpp}`, `CGameControllerDDRace` →
  `CGameControllerDDNet`.

### API changes
* `IServer::ClientAuthed()` → `GetAuthedState() != AUTHED_NO` (12 sites).
* `IServer::GetClientAddr(int, char*, int)` → `ClientAddrString()`; BW's
  tolerant version is `BwClientAddr`.
* `IServer::GetMapName()` gone → `g_Config.m_SvMap`.
* `SnapNewId()` returns `std::optional<int>`; `CEntity::GetId()` likewise.
* `SnapNewItem` takes a filled object instead of returning a pointer into the
  snapshot buffer — every BW cosmetic snap was rewritten.
* `CEntity` gained a `SnapFreeId` flag, `CPickup` a `Flags` parameter,
  `CSnapContext` the snapping client id.
* `CEnvPoint::m_Time` became `CFixedTime`; `CCharacterCore::WeaponStat` became
  `CWeaponStat`; team state moved to `enum class ETeamState`;
  `clamp`/`maximum`/`minimum` gave way to `std::clamp`/`std::max`/`std::min`.
* `CPlayer::m_Score` removed in favour of `IGameController::SnapPlayerScore`.
* `CSaveTee::Load` and `RedirectClient` signatures changed.

### Features upstream grew that BW no longer needs to add
* Headless "debug dummy" clients — BW's bots ride on those now instead of a
  `STATE_NPC` client state and a parallel path through `CNetServer`.
* `IConsole::ICommandInfo::Flags()`, pickup `m_Flags`, antibot ABI 11 /
  128 clients, and a `dbg_sql` switch: all things BW had patched in.

---

## Non-obvious resolutions

**1. The SQL result convention inverted.**
Upstream flipped `Connect`/`PrepareStatement`/`Step`/`ExecuteUpdate` *and* the
pool's read/write callbacks from "returns true on failure" to "returns true on
success". Every BW query was written against the old rule. 180 conditions and
26 callbacks in `accounts.cpp`, `clans.cpp` and `whois.cpp` were inverted,
including the BEGIN/COMMIT/ROLLBACK guards and the `while(Step(&End,...) && !End)`
row loops. Symptom before the fix: the server started, connected to MySQL, then
reported "clear all logins failed", entered fail mode and loaded nothing.

**2. `ai_bot.h` redefined `MAX_CLIENTS`.**
```c
#ifndef MAX_CLIENTS
#define MAX_CLIENTS 64
#endif
```
`MAX_CLIENTS` is an enum, so that `#ifndef` is always true and every translation
unit including `ai_bot.h` afterwards saw 64. Invisible while upstream was also
64; upstream is 128 now, so `CWhoIs` was allocated at 1120 bytes by one TU and
constructed as 1632 by another — a 1 KB heap overflow on start. Found with
AddressSanitizer. **Real pre-existing BW bug.**

**3. BW's rcon commands were registered inside upstream's function.**
~40 of them lived in `CGameContext::RegisterDDRaceCommands()`, so moving BW's
*functions* out left the registrations behind and none of the commands existed.
They are registered from `CBlockworlds::OnConsoleInit()` now. Related: commands
registered by the facade must unwrap their console userdata as `CBlockworlds *`
— `whois_name` segfaulted until all six moved commands were fixed.

**4. `ProgressVoteOptions` cannot be wrapped in a loop.**
A first attempt implemented BW's `FlushAll` as "call upstream until finished".
Upstream never resets `m_SendVoteIndex` to `-1` when done, so the server hung
the moment a player logged in and the cosmetics menu was sent. BW's real change
is inside the function: a defaulted `FlushAll` parameter plus two hooks.

**5. The async SQL result queues are drained from `CPlayer::Tick`.**
Missing that hook meant `/register` really did write the account to MySQL and
the player never heard back. Now `CBwPlayer::Tick`.

**6. `ExecuteUpdate(nullptr, ...)` crashed the SQL worker.**
Upstream's `CMysqlConnection::ExecuteUpdate` dereferences its row-count pointer
unconditionally; BW passes `nullptr` for BEGIN/COMMIT/ROLLBACK at 21 call sites,
so creating a clan segfaulted the worker thread and took the server down. The
old fork carried this guard; it was missed at first because upstream had
independently fixed the other three mysql bugs BW had patched (int64 bind slot,
float buffer length, freeing a non-existent result set). Found with gdb.

**7. `TakeDamage`'s `Source` parameter was dead.**
Both components receiving it ignore it (the one use is commented out), so
upstream's signature is left alone.

**8. Client assets.**
The old fork deleted 848 files under `data/`. On the new base `data/` is left
untouched (the server never reads skins/audio/flags) and BW only *adds* three
files. That removes the single largest source of recurring merge conflicts.

---

## Database

The migration was verified against the **live pre-update Blockworlds database**
(12 accounts, 1 clan): `sql/migration_weekly_rewards.sql` applies cleanly and
leaves the data intact.

Doing so surfaced a **pre-existing schema gap**: `schema.sql` and the account
code have expected `Blockworlds_accounts_inventory.passive_removers` for a while
but no migration ever shipped, so every login on an older database failed with
`Unknown column 'i.passive_removers' in 'SELECT'`.
`sql/migration_passive_removers.sql` is new and fixes that.

Note the deployed database also has `skinmani_body` / `skinmani_feet` columns
that `schema.sql` does not mention — `schema.sql` is out of date in that
direction and is worth reconciling separately.

---

## Finishing the port: the integration layer

The first pass moved BW's own 33k lines across, got the build, data, database
and lifecycle layers working, and shipped. What it had **not** done was port the
BW code that used to live *inside* upstream function bodies. BW compiled and
ran, but in most upstream functions nothing called into it.

That single gap explains every symptom that came back from real play:

| Symptom | Cause |
|---|---|
| Segfault on `change_map` after logging in | BW commands were registered with the `CBlockworlds *`. `CGameContext::Clear()` destroys and placement-news the context on a map change, while `OnConsoleInit` runs once at startup, so all ~120 commands held a freed pointer. |
| Vote menu ran its own entries as commands (`No such command: │`) | `OnCallVoteNetMessage` never called `HandleCosmeticsVote()`, so menu entries fell through to the server vote list — and for an rcon-authed player, into upstream's "run it as a command" branch. |
| `c_set_gundesign` changed nothing on screen | `CPlayer::Snap` had lost its whole BW block, and `CProjectile::Snap` its gundesign. The cosmetic was stored and never sent. |
| 1on1 reached the prep zone but F3/F4 did nothing | `OnVoteNetMessage` never routed to the duel's own vote. |
| Blocks never credited anything | `CBlockTracker::Tick()` was never called, and `Freeze`/`Unfreeze`/hook/hammer/laser impacts never reported to it. |

### How it was closed

Not from memory — that is what left the gaps. `src/blockworlds/tests/missing_hooks.py`
diffs the pre-port base against the old fork **per function**, and reports every
BW insertion that has no corresponding hook in the current tree. It started at
93 functions / 1 717 lines and was worked to zero; it exits non-zero while
anything is outstanding, so a future merge that drops a hook fails loudly.

It also carries a table of the insertions deliberately *not* ported, each with a
reason — upstream renamed the function (`UnFreeze` → `Unfreeze`), moved it
(the mute commands into `mutes.cpp`), adopted the feature itself
(`sv_register_community_token`), or the old code was dead
(`m_IsClientDummy`, whose only use was already commented out).

---

## Verification

Build: from scratch, **zero warnings**.

* **382 unit tests** (`./build-verify/testrunner`) — 370 upstream plus 12 BW.
* **21 integration tests** (`src/blockworlds/tests/integration/bw_test.py --all`),
  each driving a real `DDNet-Server` and real headless `DDNet` clients over a
  FIFO and econ, against a throwaway `bw_itest` MySQL database.
* **`missing_hooks.py` exits 0.**

The integration suite generates its own map (`bw_map.py`) because no map in
`data/maps` has a `game_zones` group: without one, `/1on1` always answers
"This map does not have any 1on1 spawn positions defined" and no event has
anywhere to spawn.

### What the tests cover

| Test | What it proves |
|---|---|
| `server_boots`, `client_connects`, `schema_migrations_apply` | boot, MySQL, whois SQLite, the schema and its migrations |
| `accounts`, `admin_account_commands` | register, login, the account row, the admin give/set commands |
| `map_change_and_reload` | `reload` twice and `change_map` while logged in, no crash — the reported segfault |
| `votemenu` | Rules → Back → Leaderboards → Back → Cosmetics, asserting the client never sees "isn't an option on this server" and the server never logs "No such command" |
| `cosmetics` | the setters run and the profile reads back |
| `clans` | create, the database row, membership, `/clan_list` |
| `whois`, `components`, `chatfilter`, `vpn_detection` | subsystem wiring and runtime plug/unplug |
| `oneonone`, `oneonone_cancel` | a real duel with two clients: invite → accept → prep → one F3 acknowledged but not starting → second F3 starts it; F4 from both aborts |
| `kill_gate` | `/kill` refused during a duel's configuration phase |
| `public_map` | a `<map>_pub.map` is picked up and clients still connect |
| `ip_whitelist` | add, list, remove |
| `moderation` | the mute announcement, the spelled-out refusal, and that a muted message does not reach chat |
| `clan_chat` | team chat refuses without a clan and does **not** fall through to public chat |
| `info_and_credits` | `/info`, `/credits`, `/contributors` |

Four snapshot-level unit tests (`BwSnap`) build a real server and map, put two
players ingame, snap one as the other sees them and read the
`CNetObj_ClientInfo` back out of the snapshot builder — the only way to tell
"the cosmetic is applied" from "the cosmetic is stored but never sent". Four
more (`BwHit`) cover the passive/protected rules for bullets and lasers.

Every one of these was checked for teeth by disabling the hook it covers and
confirming it fails.

### Still needs a human

| Area | Why |
|---|---|
| What a cosmetic actually looks like | the tests prove the right bytes reach the client; whether the skin looks right is a visual judgement |
| Shop, shop preview, NPCs | needs `sv_shop_server 1` and the store map |
| The BW tiles (VIP gate, wayblock, random cosmetics) | needs a player to walk onto them; the generated test map has the quad layers but not the tiles |
| Events played out (LMB, TDM, zCatch, zCatch-grenade, BombTag) | needs several real players moving |
| Blocktracker EXP and anti-farm | needs two players actually blocking each other |
| Discord webhooks, Agones, port proxy, AI bot | need their external environment |
