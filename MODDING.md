# MODDING.md — how Block sits on top of DDNet

Block is a server-side mod of DDNet. This file documents every place Block
touches upstream code, why it is there, and what to do at the next upstream
merge.

---

## 1. Where Block code lives

Everything the mod owns is under `src/block/` (~33k lines, 150 files).
Nothing outside it is Block-only, with two exceptions noted in §3.

| Path | What |
|---|---|
| `context.{h,cpp}` | `CBlock` — the facade `CGameContext` owns. All subsystems, all Block console commands, all lifecycle hooks. |
| `player.{h,cpp}` | `CBlockPlayer` — every per-player Block field and method. |
| `character.{h,cpp}` | `CBlockCharacter` — every per-character Block field and method. |
| `gamecontroller.{h,cpp}` | `CGameControllerBlock : CGameControllerDDNet` — the Block gamemode. |
| `base.h` | Umbrella replacing the old `<base/system.h>`. |
| `util.{h,cpp}` | Helpers for engine APIs upstream removed (`BlockClientAddr`, `BlockIsClientsSameAddr`, `time_localtime_safe`). |
| `config.{h,cpp}` | Block's two float cvars, registered on the console directly. |
| `version.h` | `BLOCK_VERSION`, kept out of upstream's `version.h`. |
| `commands.cpp` | The Block chat commands (~77). |
| `config_variables.h` | 194 Block config variables. |
| `accounts, clans, whois, blocktracker, password_hash` | Account system and friends. |
| `components/` | Component framework + events, VPN detection, chat filter, AI bot, client detect, port proxy, Agones, promises, requests, 1on1 manager. |
| `cosmetics/`, `shop/`, `zones/`, `votes/`, `discord/`, `entities/`, `utils/` | The rest of the feature set. |
| `external/json-modern/` | Vendored nlohmann/json. |
| `sql/` | Shared DDL helpers, plus the reference dump. The schema is created by the server on startup (`CAccounts::EnsureSchema`, `CClanManager::EnsureSchema`), not by loading the dump. |
| `tests/` | Block unit tests, built into `testrunner`. |

**Adding a Block source file needs no build change** — `cmake/block.cmake`
globs the directory.

---

## 2. The three attachment points

Block state hangs off upstream objects through exactly one member each, so
upstream's headers barely change:

```cpp
class CPlayer     { CBlockPlayer    m_Block; CBlockPlayer    &Block(); };   // + friend CBlockPlayer, CBlock
class CCharacter  { CBlockCharacter m_Block; CBlockCharacter &Block(); };   // + friend CBlockCharacter
class CGameContext{ CBlock *m_pBw; CBlock &Block(); }; // + friend CBlock
```

The `friend` declarations exist because the moved code used to live *inside*
those classes and still needs the access it had; that keeps upstream from having
to grow accessors for Block's benefit.

Block code reaches everything through `Block()`:
`pPlayer->Block().GetPlayerLevel()`, `GameServer()->Block().Accounts()`,
`pChr->Block().Core()`.

---

## 3. Every upstream file Block touches

**37 files, +1,296 / −70 lines.** Every edit is wrapped in

```cpp
// --- BLOCK BEGIN: <what and why> ---
...
// --- BLOCK END ---
```

### Build (1 file)
| File | Lines | What |
|---|---|---|
| `CMakeLists.txt` | +10 | Three `include()`s: `cmake/block.cmake` (sources), `cmake/block_data.cmake` (Block's maps/word list into `EXPECTED_DATA`), `cmake/block_tests.cmake` (Block tests into `testrunner`). |

### Engine (10 files)
| File | Lines | What |
|---|---|---|
| `src/engine/shared/console.cpp/.h`, `src/engine/console.h` | +98 | `Deregister()` and `UnChain()` — runtime command removal, needed by `component_plug`/`component_unplug`. Plus printing on `CFGFLAG_ANNOUNCE`. |
| `src/engine/shared/config.h` | +4 | `CFGFLAG_ANNOUNCE`. |
| `src/engine/shared/config_variables.h` | +4 | Includes Block's cvar list — at the "add config variables for mods below this comment" line upstream provides. |
| `src/engine/http.h/.cpp`, `src/engine/shared/http_curl.cpp` | +83 | `PUT` / `PUT_JSON`, mirroring `POST` (Agones needs it), and opt-in `CaptureResponseHeaders()` so the VPN component can read `Retry-After` / `X-TTL` / `X-RateLimit-Reset` and back off a rate-limited API. Off by default, so upstream's own requests pay nothing. |
| `src/engine/server/databases/connection_pool.h/.cpp` | +15 | `ISqlData::m_Critical`: a critical query is not dismissed during shutdown or fail mode and always goes to the primary write connection, so account and clan saves survive a restart. |
| `src/engine/server/server.cpp/.h` | +393 | The public map, the per-IP whitelist, the Discord rcon log, faster rcon command delivery, the component join veto, the browser clan/score override, `status` hiding admin addresses, and the shutdown flush. See §4b. |
| `src/engine/shared/network.h`, `network_server.cpp` | +37 | The per-IP whitelist store and the `sv_max_clients_per_ip` exemption. |
| `src/engine/shared/netban.h/.cpp` | +15 | `CNetBan::BlockOnUnban`, a virtual the client leaves empty and `CServerBan` uses to log unbans to Discord. |
| `src/engine/server/antibot.cpp` | +35 | Antibot reports reach the moderators online and the antibot webhook. |

### Game (13 files)
| File | Lines | What |
|---|---|---|
| `src/game/server/gamecontext.cpp/.h` | +217 | The `CBlock` member and the hook calls (see §4). |
| `src/game/server/player.cpp/.h` | +64 | `CBlockPlayer` member; lifecycle calls, the per-viewer snap hooks, the duel/event spawn override, the vote-menu resend. |
| `src/game/server/entities/character.cpp/.h` | +110 | `CBlockCharacter` member; the Block tiles, the weapon and hammer hooks, freeze/unfreeze block tracking, the death hooks, telekinesis and the passive/protected snap flags. |
| `src/game/server/entities/projectile.cpp`, `laser.cpp` | +27 | Gundesign bullets and the passive/protected hit rules. |
| `src/game/server/mutes.cpp`, `ddracecommands.cpp`, `ddracechat.cpp` | +71 | Moderation logging, the `/kill` event gate, `/info`, `/credits`, moderator-only `/tele cursor`, and the event-team invite refusal. |
| `src/game/server/gamecontroller.cpp/.h` | +31 | Three virtuals so a gamemode can replace behaviour instead of Block editing function bodies: `SendLeaveMessage`, `OnSnapGameInfo`, `OnSnapGameInfoEx`. The leave broadcast moved into `SendLeaveMessage` unchanged. |
| `src/game/server/teams.cpp/.h` | +50 | Event-owned teams (`SetTeamEvent`/`IsTeamEvent`, honoured by `KillTeam`) and the per-team invite toggle. |
| `src/game/gamecore.cpp/.h` | +27 | `mutable m_Protected` / `m_Passive` on `CCharacterCore` and five guards (hook attach, hook drag, collision push, deferred tick, move). Block's passive protection: upstream's `m_Solo`/`m_CollisionDisabled`/`m_HookHitDisabled` each cover only part of it and carry other meaning, so Block keeps its own two flags. `mutable` is what lets Block set them through upstream's `const Core()`. |
| `src/game/server/entities/projectile.h` | +3 | `StartTick()` accessor for the moving-effect zone. |

### Data / infra (not upstream code)
`data/chatfilter_words.txt`, `data/maps/blmapV3ROYAL.map`, `data/maps/store.map`,
`Dockerfile.block`, `Dockerfile.buildtools`, `start.sh`, `LICENSE-Block.txt`,
`scripts/port_proxy/`, `.gitlab/block.yml` (upstream's `.gitlab/build.yml`
is left alone).

---

## 4. The hook calls

Every one is a single line inside a marked block; the logic lives in
`CBlock`, `CBlockPlayer` or `CBlockCharacter`. Keeping it this way is the whole
point: an upstream merge conflicts on a line, not on a feature.

`src/block/tests/missing_hooks.py` is the checklist. It diffs the pre-port
base against the old fork per function and reports any Block insertion that has no
corresponding hook today, with a table of the ones deliberately not ported and
why. **It must exit 0.** Run it after every merge:

```bash
python3 src/block/tests/missing_hooks.py       # summary
python3 src/block/tests/missing_hooks.py -v gamecontext.cpp
```

### 4a. Game

| Upstream function | Hook |
|---|---|
| ctor / dtor | `OnConstruct(!Resetting)` / `OnDestruct()` |
| `OnConsoleInit` / `OnInit` / `OnShutdown` | same-named hooks |
| `OnTick` | `OnTickEarly()`, `OnTickAfterController()`, `OnPlayerTick(i)` (inside the player loop, to keep ordering), `SkipVoteParticipant(i)`, `OnPostTick()` |
| `OnSnap` / `OnPostGlobalSnap` | `OnSnap(ClientId)` / `OnPostSnap()` |
| `OnClientConnected` / `OnClientEnter` / `OnClientDrop` | same-named hooks |
| `OnSetAuthed` | `OnSetAuthed()` — component fan-out; losing rcon clears the cosmetics rcon granted |
| `CreateExplosion` | `ExplosionSkipsTarget()`, `BlockTracker().OnPlayerImpacted()` |
| `ProgressVoteOptions` | `AllowServerVoteStreaming()`, `SendVoteListHeader()`, plus a defaulted `FlushAll` parameter |
| `OnCallVoteNetMessage` / `OnVoteNetMessage` | `OnCallVote()` (the vote menu owns its entries) / `OnVote()` (F3/F4 during a duel) |
| `CallVote` | `VoteOnCooldown()` |
| `SendVoteSet` / `SendVoteStatus` | `OwnsVoteUi()` — a duel being configured keeps its own overlay |
| `OnSayNetMessage` | `OnTeamChat()` (team chat is clan chat), `OnPublicChat()` (event silence, chat filter), `OnPublicChatSent()` (Discord relay) |
| `WhisperId` | `OnWhisper()` |
| `SendChat` | `IsChatBlocked()` |
| `ProcessSpamProtection` | `FormatDuration()` |
| `OnSetTeamNetMessage` / `OnKillNetMessage` | `OnJoinSpectators()` / `BlocksSelfKill()` |
| `OnStartInfoNetMessage` / `OnChangeInfoNetMessage` | `isInEvent()` — identity is frozen mid-event |
| `IsClientPlayer`, `List`, `ConHotReload` | NPC slots are skipped; hot reload rebuilds the shop preview |
| `ConForceVote` | the `lmb` type |
| controller selection | `new CGameControllerBlock(this)` replaces the default DDNet controller |
| `CPlayer::Snap` | `OnSnapClientInfo()` / `OnSnapPlayerInfo()` / `OnSnapDDNetPlayer()` |
| `CPlayer::TryRespawn` | `OverrideSpawnPos()` — duels and events place their own participants |
| `CPlayer::OnPredictedEarlyInput` | `OnPlayerEnterMenu()` |
| `CCharacter::HandleTiles` | `Block().OnHandleTiles()` — the three Block tiles |
| `CCharacter::FireWeapon` | `Block().BlocksFire()`, `Block().OnHammerHit()`, `Block().HammerUnfreezes()` |
| `CCharacter::HandleWeapons` / `SetWeapon` | `Block().HandleGrenadeAmmoRegen()` / ammo-regen reset |
| `CCharacter::Tick` | `Block().OnHookAttach()`, `Block().OnTick()` |
| `CCharacter::Freeze` / `Unfreeze` | `BlockTracker().OnPlayerFreeze/OnPlayerUnfreeze()` |
| `CCharacter::Die` / `Destroy` | `OnCharacterDie()` + `OnCharacterDied()` / `Block().OnDestroy()` |
| `CCharacter::Snap` | `Block().OnSnapDDNetCharacter()` |
| `CCharacter::DDRaceTick` / `TickDeferred` / `ReleaseHook` | telekinesis and the hook rainbow |
| `CProjectile::Snap` / `Tick` | `OnSnapProjectile()`, `FilterHitTarget()`, `OnProjectileGunImpact()` |
| `CLaser::HitCharacter` | `IntersectLaserTarget()`, `OnLaserHit()` |
| `CGameTeams::OnFinish` / `OnCharacterDeath` / `SetClientInvited` | race EXP / event-owned teams / the invite refusal |
| `CGameControllerBlock::Tick` | `BlockTracker().Tick()` |
| `CCharacterCore::Move` | passive and protected tees neither push nor are pushed |

### 4b. Engine

The engine cannot reach `CBlock` — that dependency runs the wrong way — so
these are `CServer` members prefixed `Block` plus one static on the webhook.

| Upstream function | Hook |
|---|---|
| `CServer::LoadMap` | `BlockLoadPubMap()` |
| `SendMap`, `SendMapData`, `CacheServerInfo`, `UpdateRegisterServerInfo` | `BlockMapSha256/BlockMapCrc/BlockMapSize/BlockMapData` — what the client is told about the map |
| `CacheServerInfo`, `CacheServerInfoSixup` | `BlockServerInfoClan()`, `BlockServerInfoScore()` |
| `NewClientCallback`, `NewClientNoAuthCallback` | `BlockComponentsRejectJoin()` |
| `DelClientCallback` | a redirected client reports "changed server" |
| `Run` (interrupt and shutdown) | `BlockPreShutdownFlush()` |
| `ConKick`, `OnNetMsgRconCmd`, `CServerBan::ConBanExt`, `CNetBan::ConUnban` | `CDiscordWebhook::SendRconLog()` |
| `ConStatus` | admin addresses hidden from lesser ranks, proxied port shown |
| `UpdateClientRconCommands` and its caller | `sv_send_rcon_cmds_per_tick`, `sv_send_rcon_cmds_clients_per_tick` |
| `Kick` | an NPC slot is not a client to kick |
| `CNetServer::TryAcceptClient` | the per-IP whitelist exemption |

---

## 5. Deliberate duplication

Two pieces of upstream logic are copied into Block rather than shared. Re-check
them when upstream changes the originals:

* `CBlock::SnapLaserObject` mirrors `CGameContext::SnapLaserObject`, which
  hardcodes `m_Flags = 0`. Block's cosmetic lasers need `LASERFLAG_NO_PREDICT`.
* `CGameControllerBlock::OnPlayerConnect` repeats the three score-init lines from
  `CGameControllerDDNet::OnPlayerConnect` because Block has to withhold the join
  broadcast and send its own greeting.

---

## 6. Signature changes that were avoided

Block used to change upstream signatures. All of these are gone; do not
re-introduce them:

| Block used to | Instead |
|---|---|
| `TakeDamage(Force, **Source**, Dmg, From, Weapon)` | `Source` was never read by either consumer; dropped. |
| `Spawn/OnCharacterSpawn/ForceSpawn(..., **doEvent**)` | `CBlock::m_SuppressSpawnEvent`, set around savegame restores and forced event spawns. |
| `CPlayer::m_Score` | Upstream removed it; Block's score display is `CGameControllerBlock::SnapPlayerScore`. |
| `Core()` losing `const` | Block's two flags are `mutable`; `CBlockCharacter::Core()` gives writable access where Block needs it. |
| `SnapLaserObject(..., Flags)` returning `bool` | Block snaps its own (§5). |
| `RedirectClient(..., Verbose, Force)` | `CBlock::RedirectClient(id, port, Force)`. |
| `IServer::BotJoin/BotLeave`, `STATE_NPC`, `NET_CONNSTATE_BOT` | `CBlock::BotJoin/BotLeave` on upstream's debug-dummy clients. |
| `IServer::IsClientsSameAddr`, `GetClientAddr(char*)` | `BlockIsClientsSameAddr`, `BlockClientAddr` in `util`. |
| `base/system.h: time_localtime_safe` | `util`. |
| `MACRO_CONFIG_FLT` through 6 files | `CBlockConfig`, two cvars on the console. |

---

## 7. Procedure for the next upstream merge

```bash
git fetch upstream
git checkout vanilla-ddnet && git merge --ff-only upstream/master
git checkout block-next && git merge vanilla-ddnet
```

Expect conflicts only where a marked block sits in a line upstream also changed.
For each:

1. **Take upstream's version of the surrounding code**, then re-apply the Block
   block on top. Never revert an upstream change to keep Block's old call working.
2. If upstream removed something a Block block depends on, check §6 first — the
   answer is often that upstream has since grown its own version of it, in which
   case delete Block's and use upstream's.
3. `data/` never conflicts: Block ships no deletions there, only three added files.

Then:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCLIENT=OFF -DTOOLS=OFF -DSERVER=ON -DMYSQL=ON \
      -DANTIBOT=OFF -DDOWNLOAD_GTEST=ON
ninja -C build && ninja -C build testrunner && ./build/testrunner
```

A clean build is **zero warnings**; the suite is **382 tests**. Then the
integration suite, which drives a real server and real headless clients:

```bash
python3 src/block/tests/integration/test.py --all   # 22 tests
python3 src/block/tests/missing_hooks.py               # must exit 0
```

### Things to check by hand after a merge

* **The SQL result convention.** Upstream flipped `Connect` / `PrepareStatement`
  / `Step` / `ExecuteUpdate` and the pool callbacks from "true on failure" to
  "true on success" once already. If it moves again, every query in
  `accounts.cpp`, `clans.cpp` and `whois.cpp` silently inverts. The symptom is
  the server starting fine and then reporting "… failed on all databases".
* **`MAX_CLIENTS`.** Block once `#define`d it to 64 in a header; when upstream went
  to 128 that became a heap overflow. Never shadow it.
* **`class` vs `struct` in forward declarations.** MSVC mangles the tag into the
  symbol name, so declaring `class CSnapContext;` for something upstream defines
  as a `struct` links fine on gcc and fails on Windows with an unresolved
  external. Clang catches it with `-Wmismatched-tags`, which the macOS job
  builds with — if that job errors, fix the tag rather than silencing it.
* **libcurl is a stub.** `ddnet-libs/curl` exports only the symbols the engine
  itself uses. Anything calling curl directly links locally against a full
  system curl and then fails in CI — `curl_easy_perform` is the one that bites.
  Use `IHttp` / `CreateHttpRequest`; Block owns no direct curl dependency.
* **`std::format`.** libstdc++ only shipped it in GCC 13 and CI still builds on
  Ubuntu 22.04. Use `str_format`.
* **clang-tidy.** `check-clang-tidy` wires clang-tidy in as the compiler wrapper,
  so any finding fails the build. `scripts/fix_clang_tidy.py` reproduces that
  job locally and can apply the fixes:

  ```bash
  scripts/fix_clang_tidy.py --check   # report only, what CI does
  scripts/fix_clang_tidy.py           # apply fixes to src/block
  ```

  Review what it changes. Its renames can collide a local with the parameter it
  shadows, and `modernize-avoid-bind` cannot handle parameter packs — the script
  rebuilds afterwards so that breakage surfaces at once, but it cannot judge
  whether a rename reads well.
* **Console userdata.** Block commands are registered with `GameServer()` — a
  `CGameContext *` — and their callbacks unwrap it as one, reaching Block through
  `pSelf->Block()`. This is not cosmetic: `CGameContext::Clear()` destroys and
  placement-news the context on every map change while `OnConsoleInit` runs only
  once at startup, so the `CGameContext *` address is stable and a
  `CBlock *` is not. Registering with `this` gives every Block command a
  dangling pointer and the server segfaults on the first `change_map`.

---

## 8. Known gaps

* `component_plug` in `autoexec_server.cfg` does not work: the autoexec runs
  before `CGameContext::OnConsoleInit`, so Block's commands do not exist yet. Plug
  components over rcon/econ after start. This predates the port.
* `src/block/components/events/{clanwar,colorsoldiers,priv_tdm}.h` are
  boilerplate with no implementation, kept intentionally for future work.
