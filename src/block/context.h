#ifndef BLOCK_CONTEXT_H
#define BLOCK_CONTEXT_H

#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <block/base.h>
#include <block/blocktracker.h>
#include <block/cosmetics/animations.h>
#include <block/cosmetics/cosmetics.h>
#include <block/shop/preview.h>
#include <block/zones/zonemanager.h>

#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class CAccounts;
class CClanManager;
class CGameContext;
struct CSnapContext; // struct, not class: MSVC mangles the tag into the symbol
class CPlayer;
class CWhoIs;
struct CWhoIsResult;
class IEngine;
class IHttp;
class IServer;

// Everything Block hangs off the game context.
//
// CGameContext owns one CBlock and forwards a handful of lifecycle hooks
// to it; all Block subsystems, chat commands and per-round state live here instead
// of being spliced into upstream's gamecontext.{h,cpp}.
class CBlock
{
public:
	explicit CBlock(CGameContext *pGameServer);
	~CBlock();

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;
	IConsole *Console() const;
	IEngine *Engine() const;
	IHttp *Http() const { return m_pHttp; }
	void SetHttp(IHttp *pHttp) { m_pHttp = pHttp; }

	// --- subsystems ---
	CAccounts *Accounts() { return m_pAccounts; }
	CClanManager *Clans() { return m_pClans; }
	CWhoIs *WhoIs() { return m_pWhoIs; }
	CZoneManager *ZoneManager() { return &m_ZoneManager; }
	CShopPreview *ShopPreview() { return &m_ShopPreview; }
	CCosmeticsHandler *Cosmetics() { return &m_CosmeticsHandler; }
	CBlockTracker &BlockTracker() { return m_BlockTracker; }
	CAnimationHandler *Animations() { return &m_Animations; }

	// --- lifecycle hooks, called from the matching CGameContext methods ---
	void OnConstruct(bool FirstInit);
	void OnDestruct();
	void OnConsoleInit();
	void OnInit();
	void OnShutdown();
	void OnTick();
	void OnSnap(int SnappingClient);
	void OnClientConnected(int ClientId);
	void OnSetAuthed(int ClientId, int Level) const;
	void OnPostSnap();
	void OnClientEnter(int ClientId);
	void OnClientDrop(int ClientId, const char *pReason);
	void PreShutdownFlush();
	bool IsSilentDrop(int ClientId) const;

	// --- per-tick hooks, each called from the matching spot in CGameContext ---
	void OnTickEarly(); // component OnTick fan-out
	void OnTickAfterController(); // Block world ticks: events, animations, zones, shop
	void BLOCK_OnTick(); // Block event bookkeeping
	void OnPlayerTick(int ClientId) const; // per-second account/passive/cosmetic upkeep
	void OnPostTick(); // component OnPostTick fan-out + whois maintenance
	bool SkipVoteParticipant(int ClientId) const; // 1on1 prep players do not vote

	// --- per-viewer snap hooks ---
	// Cosmetics depend on who is looking and on the current tick, so they are
	// applied to a copy of upstream's cached client info rather than to the
	// cache itself.
	void OnSnapClientInfo(int ClientId, int SnappingClient, CNetObj_ClientInfo *pClientInfo);
	void OnSnapPlayerInfo(int ClientId, int SnappingClient, CNetObj_PlayerInfo *pPlayerInfo) const;
	void OnSnapDDNetPlayer(int ClientId, CNetObj_DDNetPlayer *pDDNetPlayer) const;

	// --- net message hooks, return true when Block consumed the message ---
	bool OnCallVote(const CNetMsg_Cl_CallVote *pMsg, int ClientId) const;
	bool OnVote(const CNetMsg_Cl_Vote *pMsg, int ClientId);
	// Block has no DDRace teams to talk to, so team chat is clan chat.
	// Always consumes the message, including when it has to refuse it.
	bool OnTeamChat(int ClientId, const char *pMessage) const;
	// Returns true when the message must not reach public chat: silenced during
	// LMB and TDM, or caught by the chat filter.
	bool OnPublicChat(int ClientId, const char *pMessage) const;
	// Relays what was actually said to Discord, once it has gone out.
	void OnPublicChatSent(int ClientId, const char *pCensoredMessage, const char *pMessage) const;
	// Returns true when a whisper must be refused: neither side of a whisper may
	// be a player inside LMB or TDM, and the filter applies here too.
	bool OnWhisper(int ClientId, int VictimId, const char *pMessage) const;
	// Joining spectators means leaving the event or duel you are in, and during
	// a duel's preparation it is refused outright. True when Block handled it.
	bool OnJoinSpectators(int ClientId) const;
	// Self-kill is up to the event: a duel forbids it while being configured,
	// TDM only allows it once you have been frozen long enough, and so on.
	bool BlocksSelfKill(int ClientId);
	// "1 hour 5 minutes" rather than "3900 seconds": mute messages are read by
	// players, not by admins.
	static void FormatDuration(int Seconds, char *pBuf, size_t Size);
	// True when the client must not receive public chat at all: LMB and TDM
	// participants play in silence.
	bool IsChatBlocked(int ClientId) const;
	// Weaponkit and event-start votes share a server-wide cooldown, so one player
	// cannot re-run them back to back. True when the vote must not start.
	bool VoteOnCooldown(int ClientId, const char *pCmd);
	// A player configuring a duel has the duel's own F3/F4 overlay on screen;
	// broadcast vote traffic would overwrite it.
	bool OwnsVoteUi(int ClientId) const;

	// --- gameplay hooks ---
	void OnCharacterSpawn(class CCharacter *pChr) const;
	// Runs before upstream builds the kill message. Returns true when the death
	// was a block that the tracker has already announced, or when killer or
	// victim is in an event, both of which suppress the normal kill message.
	bool OnCharacterDie(class CCharacter *pChr, int Killer);
	// Runs once the character is gone: passive zone bookkeeping and the
	// component fan-out.
	void OnCharacterDied(class CCharacter *pChr, int Killer, int Weapon);
	// Finishing the race pays EXP, rate-limited per player and per session so a
	// short map cannot be farmed.
	void OnRaceFinish(class CPlayer *pPlayer) const;
	// A 1on1 participant or an event participant spawns where the match puts
	// them, not where the map's spawn tiles do. Returns true when it picked a
	// position, in which case the controller's CanSpawn must not run.
	bool OverrideSpawnPos(int ClientId, vec2 *pSpawnPos);
	// Opening the vote menu resends the cosmetics pages, unless the player is in
	// an event -- there the menu stays empty so it cannot distract them.
	void OnPlayerEnterMenu(int ClientId) const;
	int SnapPlayerScore(int SnappingClient, class CPlayer *pPlayer) const;
	void OnSnapGameInfo(int SnappingClient, CNetObj_GameInfo *pGameInfo);
	void OnSnapGameInfoEx(int SnappingClient, CNetObj_GameInfoEx *pGameInfoEx);
	void OnCharacterTakeDamage(class CCharacter *pChar, vec2 Force, int Dmg, int From, int Weapon);
	bool ExplosionSkipsTarget(int Owner, class CCharacter *pTarget) const;

	// --- projectile hooks ---
	// A gundesign replaces the bullet entirely: the cosmetic is snapped in place
	// of the normal projectile, and its impact effect replaces the damage
	// indicator. Both return true when Block took over.
	bool OnSnapProjectile(int Type, int Owner, vec2 Pos, vec2 Direction, int EntityId, int SnappingClient);
	bool OnProjectileGunImpact(int Owner, vec2 Pos, vec2 Direction, class CCharacter *pTargetChr);
	// Passive and protected players are not hit by anyone's bullets.
	class CCharacter *FilterHitTarget(class CCharacter *pOwnerChar, class CCharacter *pTargetChr) const;

	// --- laser hooks ---
	// A laser must pass *through* passive and protected players rather than stop
	// on them, so the skip has to happen inside the intersection search. Upstream
	// has no predicate overload, so Block does its own search over the characters.
	class CCharacter *IntersectLaserTarget(vec2 Pos0, vec2 Pos1, vec2 &NewPos, class CCharacter *pOwnerChar, int CollideWith, bool DontHitSelf) const;
	void OnLaserHit(int Owner, class CCharacter *pHit);

	// Upstream's CGameContext::SnapLaserObject hardcodes m_Flags to 0. Block's
	// cosmetic lasers must be sent with LASERFLAG_NO_PREDICT, so Block snaps its own.
	void SnapLaserObject(const CSnapContext &Context, int SnapId, const vec2 &To, const vec2 &From,
		int StartTick, int Owner = -1, int LaserType = -1, int Subtype = -1, int SwitchNumber = -1, int Flags = 0) const;

	// Savegame restores must not look like a real spawn to the event components.
	bool m_SuppressSpawnEvent = false;

	// --- helpers used across Block code ---
	CPlayer *GetPlayer(int ClientId) const;
	CPlayer *GetPlayerByName(const char *pName) const;
	int GetNextClientID() const;
	bool isInEvent(int pPlayerID);
	static SHA256_DIGEST HashPassword(const char *pPassword);
	static int GetTilePositions(int TileId, CGameContext *pSelf, std::vector<vec2> &vResult);
	static int GetSwitchTilePositions(int Type, int Delay, int Number, CGameContext *pSelf, std::vector<vec2> &vResult);

	// --- chat helpers (format-string convenience over upstream's plain versions) ---
	void SendChatTarget(int To, const char *pText) const;
	void SendChatAccount(int AccountId, const char *pText, int VersionFlags = 3 /* FLAG_SIX | FLAG_SIXUP */) const;
	void SendChatClan(int ClanId, const char *pText, int VersionFlags = 3 /* FLAG_SIX | FLAG_SIXUP */, int From = -1) const;
	void SendChatTeam(int Team, const char *pText) const;

	template<typename... TArgs>
	void SendChatTarget(int To, const char *pFmt, TArgs &&...Args) const
	{
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), pFmt, std::forward<TArgs>(Args)...);
		SendChatTarget(To, aBuf);
	}
	template<typename... TArgs>
	void SendChatTargetAccount(int To, const char *pFmt, TArgs &&...Args) const
	{
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), pFmt, std::forward<TArgs>(Args)...);
		SendChatAccount(To, aBuf);
	}
	template<typename... TArgs>
	void SendChatClan(int ClanId, const char *pFmt, TArgs &&...Args) const
	{
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), pFmt, std::forward<TArgs>(Args)...);
		SendChatClan(ClanId, aBuf);
	}
	template<typename... TArgs>
	void SendChatTeam(int Team, const char *pFmt, TArgs &&...Args) const
	{
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), pFmt, std::forward<TArgs>(Args)...);
		SendChatTeam(Team, aBuf);
	}

	// --- votes / votemenu ---
	void SendCosmeticsVoteOptions(int ClientId) const;
	bool HandleCosmeticsVote(const CNetMsg_Cl_CallVote *pMsg, int ClientId) const;
	void SetVoteDescriptionAtIndex(int *pIndex, const char *pStr, CNetMsg_Sv_VoteOptionListAdd *pOptionMsg);
	void CreateStripline(char *pDst, int DstSize, const char *pTitle);
	void ClearVotes(int ClientId) const;
	void RemoveVoteByDescription(const char *pDescription) const;
	void UpdateWeaponkitsVoteOption() const;
	void UpdateLMBVoteOption() const;

	// --- deferred join/leave chat until the entry checks cleared a client ---
	void HoldJoinMessage(int ClientId) const;
	void ReleaseJoinMessage(int ClientId) const;
	void SendPendingJoinMessage(int ClientId) const;

	// --- mutes (upstream keeps these private on CGameContext) ---
	void AddIpMuteSilent(const NETADDR *pAddr, int Secs, const char *pReason) const;
	int GetRemainingMuteSecondsPublic(int ClientId) const;

	// --- broadcast helpers ---
	// Block code calls both shapes: upstream's (text, client, important) and Block's
	// own (client, format, args...).
	void SendBroadcast(const char *pText, int ClientId, bool IsImportant = true) const;
	void SendBroadcast(int To, const char *pText) const;
	template<typename... TArgs>
	void SendBroadcast(int To, const char *pFmt, TArgs &&...Args) const
	{
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), pFmt, std::forward<TArgs>(Args)...);
		SendBroadcast(To, aBuf);
	}

	// --- runtime chat-command list push (component_plug/unplug) ---
	// Upstream sends the chat command list once on client enter; components add
	// and remove commands while the server runs, so Block pushes the deltas itself.
	void SendChatCmdGroupStart(int ClientId) const;
	void SendChatCmdGroupEnd(int ClientId) const;
	void SendChatCmdAdd(const IConsole::ICommandInfo *pCommandInfo, int ClientId) const;
	void SendChatCmdRem(const IConsole::ICommandInfo *pCommandInfo, int ClientId) const;

	// Called from CGameContext::ProgressVoteOptions.
	bool AllowServerVoteStreaming(int ClientId) const;
	void SendVoteListHeader(int ClientId);

	// --- NPC/bot client slots (shop NPCs, AI bots) ---
	// Block used to add a STATE_NPC client state to the server. Upstream has since
	// grown its own headless client concept for debug dummies, so Block rides on
	// that instead: a bot is a normal client slot flagged as a debug dummy, which
	// already keeps it out of the server info and gives it a synthetic address.
	void BotJoin(int BotId, const char *pName) const;
	static void ConBots(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveAllWeapons(IConsole::IResult *pResult, void *pUserData);
	void BotLeave(int BotId, bool Silent = false) const;

	// Upstream's IServer::RedirectClient(id, port) refuses clients that are too
	// old and puts the slot into STATE_REDIRECTED. The Block port proxy needs a
	// forced variant that does neither, because it hands the client to a server
	// on another port that is about to take over the same slot.
	void RedirectClient(int ClientId, int Port, bool Force = false) const;

	// --- server browser ---
	// What the master server and the browser should show for this client: the
	// Block clan rather than the vanilla one, and the account level as the
	// score. Both are empty/zero for a player who is not logged in.
	const char *ServerInfoClan(int ClientId);
	int ServerInfoScore(int ClientId) const;

	// --- moderation logging ---
	// Sends a line to the rcon-log Discord webhook, naming who ran the command.
	// Silently does nothing when no webhook is configured.
	[[gnu::format(printf, 3, 4)]] void LogModeration(int ExecutorId, const char *pFmt, ...) const;

	// --- misc ---
	void CreateExplosionVisual(vec2 Pos, CClientMask Mask = CClientMask().set()) const;
	// CGameContext::Teleport is private upstream; CBlock is a friend.
	void Teleport(class CCharacter *pChr, vec2 Pos);
	void RegisterBlockChatCommands() const;
	void ProcessComponentsQueue();
	// for components
	bool DeferVote(const char *pDescription, const char *pCommand);

	bool m_WeaponkitsAllowed = false;
	int64_t m_LastGlobalWeaponkitsVoteCall = 0;
	int64_t m_LastGlobalEventVoteCall = 0;
	int64_t m_LastBestPlayerBroadcast = 0;
	uint32_t m_NextUniqueClientId = 1;

	// IP-based daily reward guard: ip string -> last yyyymmdd a reward was granted
	std::unordered_map<std::string, int> m_WeeklyRewardClaimedByIp;

	std::vector<std::shared_ptr<CWhoIsResult>> m_vWhoisResults;
	int64_t m_aWhoisCooldown[MAX_CLIENTS]{};
	std::queue<std::string> m_ComponentsQueue;
	std::vector<std::pair<std::string, std::string>> m_vDeferredVotes;

	// --- console commands ---
	static void Con1on1(IConsole::IResult *pResult, void *pUserData);
	static void Con1on1Accept(IConsole::IResult *pResult, void *pUserData);
	static void Con1on1Decline(IConsole::IResult *pResult, void *pUserData);
	static void Con1on1Ready(IConsole::IResult *pResult, void *pUserData);
	static void ConAcceptBlockpointsRequest(IConsole::IResult *pResult, void *pUserData);
	static void ConAccountHelp(IConsole::IResult *pResult, void *pUserData);
	static void ConAccountLogout(IConsole::IResult *pResult, void *pUserData);
	static void ConAdminSetPassword(IConsole::IResult *pResult, void *pUserData);
	static void ConBanhammer(IConsole::IResult *pResult, void *pUserData);
	static void ConBuy(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeName(IConsole::IResult *pResult, void *pUserData);
	static void ConChangePassword(IConsole::IResult *pResult, void *pUserData);
	static void ConClanAccept(IConsole::IResult *pResult, void *pUserData);
	static void ConClanCreate(IConsole::IResult *pResult, void *pUserData);
	static void ConClanDecline(IConsole::IResult *pResult, void *pUserData);
	static void ConClanDelete(IConsole::IResult *pResult, void *pUserData);
	static void ConClanExp(IConsole::IResult *pResult, void *pUserData);
	static void ConClanHelp(IConsole::IResult *pResult, void *pUserData);
	static void ConClanInvite(IConsole::IResult *pResult, void *pUserData);
	static void ConClanLeave(IConsole::IResult *pResult, void *pUserData);
	static void ConClanList(IConsole::IResult *pResult, void *pUserData);
	static void ConClanNo(IConsole::IResult *pResult, void *pUserData);
	static void ConClanRemove(IConsole::IResult *pResult, void *pUserData);
	static void ConClanRename(IConsole::IResult *pResult, void *pUserData);
	static void ConClanRole(IConsole::IResult *pResult, void *pUserData);
	static void ConClanTransfer(IConsole::IResult *pResult, void *pUserData);
	static void ConClanYes(IConsole::IResult *pResult, void *pUserData);
	static void ConComponentList(IConsole::IResult *pResult, void *pUserData);
	static void ConComponentPlug(IConsole::IResult *pResult, void *pUserData);
	static void ConComponentUnPlug(IConsole::IResult *pResult, void *pUserData);
	static void ConContributors(IConsole::IResult *pResult, void *pUserData);
	static void ConCredits(IConsole::IResult *pResult, void *pUserData);
	static void ConCreateTDM(IConsole::IResult *pResult, void *pUserData);
	static void ConDeathnote(IConsole::IResult *pResult, void *pUserData);
	static void ConDeclineBlockpointsRequest(IConsole::IResult *pResult, void *pUserData);
	static void ConDisplayBlockpoints(IConsole::IResult *pResult, void *pUserData);
	static void ConDisplayPages(IConsole::IResult *pResult, void *pUserData);
	static void ConDisplayProfile(IConsole::IResult *pResult, void *pUserData);
	static void ConDisplayTopBlockpoints(IConsole::IResult *pResult, void *pUserData);
	static void ConDisplayTopClans(IConsole::IResult *pResult, void *pUserData);
	static void ConDisplayTopKillStreak(IConsole::IResult *pResult, void *pUserData);
	static void ConDisplayTopLevel(IConsole::IResult *pResult, void *pUserData);
	static void ConExp(IConsole::IResult *pResult, void *pUserData);
	static void ConGetCid(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveBlockpoints(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveBlockpointsRequest(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveExperience(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveLevel(IConsole::IResult *pResult, void *pUserData);
	static void ConGivePages(IConsole::IResult *pResult, void *pUserData);
	static void ConGivePassive(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveWeaponkits(IConsole::IResult *pResult, void *pUserData);
	static void ConIpBanClear(IConsole::IResult *pResult, void *pUserData);
	static void ConIpBans(IConsole::IResult *pResult, void *pUserData);
	static void ConJoinEvent(IConsole::IResult *pResult, void *pUserData);
	static void ConKnockout(IConsole::IResult *pResult, void *pUserData);
	static void ConLeaveEvent(IConsole::IResult *pResult, void *pUserData);
	static void ConListOutstandingInvites(IConsole::IResult *pResult, void *pUserData);
	static void ConLogin(IConsole::IResult *pResult, void *pUserData);
	static void ConPassive(IConsole::IResult *pResult, void *pUserData);
	static void ConPassiveRemover(IConsole::IResult *pResult, void *pUserData);
	static void ConRegister(IConsole::IResult *pResult, void *pUserData);
	static void ConSendSound(IConsole::IResult *pResult, void *pUserData);
	static void ConSetBlockpoints(IConsole::IResult *pResult, void *pUserData);
	static void ConSetExperience(IConsole::IResult *pResult, void *pUserData);
	static void ConSetGunDesignCosmetic(IConsole::IResult *pResult, void *pUserData);
	static void ConSetKnockoutCosmetic(IConsole::IResult *pResult, void *pUserData);
	static void ConSetLevel(IConsole::IResult *pResult, void *pUserData);
	static void ConSetPages(IConsole::IResult *pResult, void *pUserData);
	static void ConSetPassive(IConsole::IResult *pResult, void *pUserData);
	static void ConSetSkinManiCosmetic(IConsole::IResult *pResult, void *pUserData);
	static void ConSetSpecialCosmetic(IConsole::IResult *pResult, void *pUserData);
	static void ConSetVip(IConsole::IResult *pResult, void *pUserData);
	static void ConSetVipAccount(IConsole::IResult *pResult, void *pUserData);
	static void ConSetWeaponkits(IConsole::IResult *pResult, void *pUserData);
	static void ConSetWeaponkitsAdmin(IConsole::IResult *pResult, void *pUserData);
	static void ConShopDecline(IConsole::IResult *pResult, void *pUserData);
	static void ConShopPurchase(IConsole::IResult *pResult, void *pUserData);
	static void ConStatusAccounts(IConsole::IResult *pResult, void *pUserData);
	static void ConTelekinesis(IConsole::IResult *pResult, void *pUserData);
	static void ConWeaponKit(IConsole::IResult *pResult, void *pUserData);
	static void ConWhoisAccount(IConsole::IResult *pResult, void *pUserData);
	static void ConWhoisId(IConsole::IResult *pResult, void *pUserData);
	static void ConWhoisIp(IConsole::IResult *pResult, void *pUserData);
	static void ConWhoisName(IConsole::IResult *pResult, void *pUserData);
	static void ConWhoisPurge(IConsole::IResult *pResult, void *pUserData);

private:
	CGameContext *m_pGameServer;
	IHttp *m_pHttp = nullptr;

	CAccounts *m_pAccounts = nullptr;
	CClanManager *m_pClans = nullptr;
	CWhoIs *m_pWhoIs = nullptr;

	CBlockTracker m_BlockTracker;
	CZoneManager m_ZoneManager;
	CShopPreview m_ShopPreview;
	CCosmeticsHandler m_CosmeticsHandler;
	CAnimationHandler m_Animations;
};

#endif // BLOCK_CONTEXT_H
