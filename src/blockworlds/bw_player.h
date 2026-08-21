#ifndef BLOCKWORLDS_BW_PLAYER_H
#define BLOCKWORLDS_BW_PLAYER_H

#include <game/server/teeinfo.h>

#include <blockworlds/accounts.h>
#include <blockworlds/bw_base.h>
#include <blockworlds/clans.h>
#include <blockworlds/cosmetics/cosmetics.h>

#include <cstdarg>
#include <map>
#include <memory>
#include <optional>
#include <queue>

class CCharacter;
class CEntity;
class CGameContext;
class CPlayer;
class CShop;
class IServer;

static constexpr const char *BROADCAST_PADDING = "\n"
						 "                                                                                     "
						 "                                                                                     "
						 "                                                                                     ";

// All Blockworlds per-player state.
//
// This used to be ~460 lines spliced into upstream's CPlayer. It lives here
// instead, and CPlayer carries a single CBwPlayer member plus a Bw() accessor,
// so upstream's player.h stays essentially untouched across merges.
//
// Everything that needs the owning player goes through m_pPlayer; the small
// forwarders below exist so the moved code reads exactly as it did inside
// CPlayer.
class CBwPlayer
{
public:
	void Init(CPlayer *pPlayer) { m_pPlayer = pPlayer; }

	CPlayer *Player() const { return m_pPlayer; }
	CGameContext *GameServer() const;
	IServer *Server() const;
	int GetCid() const;
	CCharacter *GetCharacter() const;

private:
	CPlayer *m_pPlayer = nullptr;

public:
	// The join broadcast is owed to chat but not sent yet.
	bool m_JoinMsgPending = false;
	// 0.7 clients print the join themselves from Sv_ClientInfo, which was sent
	// silently because the join was withheld, so they need the chat message too.
	bool m_JoinMsgSilentForSixup = false;
	// Entry checks (VPN detection) have not cleared this client yet. While this is
	// set both the join and the leave broadcast are withheld, so a client that ends
	// up being banned never shows up in chat at all.
	bool m_EntryChecksPending = false;

	// last LMB vote call (tick)
	int64_t m_LastLMBVoteCall = 0;

	// temporary special expiration tick (server ticks, 50 ticks = 1 second)
	int m_SpecialExpireTick;
	// flag entity for programmatic flag reward and its expiration
	CEntity *m_pFlagEntity;
	int m_FlagExpireTick;

	// Blockworlds

	// Per-tick upkeep: async SQL results, deferred login save, telekinesis,
	// clan autosave and expiring EXP modifiers / specials / flags.
	void Tick();
	// Resets the BW half of a player, from CPlayer::Reset().
	void Reset();
	void OnDisconnect();

	void BWProcessAccountsResult(CAccountResult &Result);
	void BWProcessClansResult(CClanResult &Result);
	void BWProcessAdminCommandResult(CAdminCommandResult &Result);
	std::queue<std::shared_ptr<CAccountResult>> m_AccountQueryResult;
	std::queue<std::shared_ptr<CClanResult>> m_ClanQueryResult;
	std::queue<std::shared_ptr<CAdminCommandResult>> m_AdminCommandQueryResult;

	void OnPlayerLogin();
	void OnPlayerLogout();

	// clear cosmetics/effects (specials, flag, skin/gundesign, knockouts) without logging out account
	void ClearCosmetics();
	// disable only the active cosmetics (special entity + flag + visual slots) without resetting ownership
	void DisableCosmeticsForEvent();
	int GetFlagExpireTick() const { return m_FlagExpireTick; }

	// give a temporary special (spawns and auto-removes after Duration minutes)
	void GiveTemporarySpecial(int SpecialIndex, int DurationMinutes);

	// grant the flag effect (visual + optional exp) for a duration in minutes
	void GiveFlag(int DurationMinutes);
	void OnPlayerSave(bool Logout);

	//helper functions:
	int GetAccId() const { return m_Account.m_Id; }
	bool IsLoggedIn() const { return GetAccId() != 0; }

	// Setters
	void SetPlayerId(int Id) { m_Account.m_Id = Id; }
	void SetPlayerName(const char *Name) { str_copy(m_Account.m_aName, Name, sizeof(m_Account.m_aName)); }
	void SetPlayerPassword(const char *Password) { str_copy(m_Account.m_aPassword, Password, sizeof(m_Account.m_aPassword)); }
	void SetPlayerAddress(const char *Address) { str_copy(m_Account.m_aAddress, Address, sizeof(m_Account.m_aAddress)); }
	void SetPlayerVip(int Vip)
	{
		m_Account.m_Vip = Vip;
		m_Account.m_DirtyInventory = true;
	}
	void SetPlayerPages(int Pages)
	{
		m_Account.m_Pages = Pages;
		m_Account.m_DirtyInventory = true;
	}
	void SetPlayerPassiveRemovers(int Count)
	{
		m_Account.m_PassiveRemovers = Count;
		m_Account.m_DirtyInventory = true;
	}
	void SetPlayerLevel(int Level)
	{
		m_Account.m_Level = Level;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerExperience(int Experience)
	{
		m_Account.m_Experience = Experience;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerWeaponkits(int Weaponkits)
	{
		m_Account.m_Weaponkits = Weaponkits;
		m_Account.m_DirtyInventory = true;
	}
	// void SetPlayerClan(const char *ClanName) {}
	void SetPlayerRanking(int Ranking)
	{
		m_Account.m_Ranking = Ranking;
		m_Account.m_DirtyProgress = true;
	}
	void SetClanId(int ClanId)
	{
		m_Account.m_ClanId = ClanId;
		m_Account.m_DirtyProgress = true;
	}
	void SetAuthLevel(ClanAuthLevel Level)
	{
		m_Account.m_AuthLevel = Level;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerBlockpoints(int Blockpoints)
	{
		int Delta = Blockpoints - m_Account.m_Blockpoints;
		if(Delta > 0)
			m_SessionBlockpoints += Delta;
		m_Account.m_Blockpoints = Blockpoints;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerKnockouts(int Index, char Value)
	{
		m_Account.m_aKnockouts[Index] = Value;
		m_Account.m_DirtyInventory = true;
	}
	void SetPlayerGundesign(int Index, char Value)
	{
		m_Account.m_aGundesign[Index] = Value;
		m_Account.m_DirtyInventory = true;
	}
	void SetPlayerSkinmani(int Index, char Value)
	{
		m_Account.m_aSkinmani[Index] = Value;
		m_Account.m_DirtyInventory = true;
	}
	void SetPlayerPassive(int PassiveDuration)
	{
		m_Account.m_Passive = PassiveDuration;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerRegisterDate(const char *RegisterDate) { str_copy(m_Account.m_RegisterDate, RegisterDate, sizeof(m_Account.m_RegisterDate)); }
	void SetPlayerRankedGames(int RankedGames)
	{
		m_Account.m_RankedGames = RankedGames;
		m_Account.m_DirtyRanked = true;
	}
	void SetPlayerRankedKills(int RankedKills)
	{
		m_Account.m_RankedKills = RankedKills;
		m_Account.m_DirtyRanked = true;
	}
	void SetPlayerRankedDeaths(int RankedDeaths)
	{
		m_Account.m_RankedDeaths = RankedDeaths;
		m_Account.m_DirtyRanked = true;
	}
	void SetPlayerRankedWins(int RankedWins)
	{
		m_Account.m_RankedWins = RankedWins;
		m_Account.m_DirtyRanked = true;
	}
	void SetPlayerKills(int Kills)
	{
		int Delta = Kills - m_Account.m_Kills;
		if(Delta > 0)
			m_SessionKills += Delta;
		m_Account.m_Kills = Kills;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerDeaths(int Deaths)
	{
		int Delta = Deaths - m_Account.m_Deaths;
		if(Delta > 0)
			m_SessionDeaths += Delta;
		m_Account.m_Deaths = Deaths;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerTourneyWin(int TourneyWin)
	{
		m_Account.m_TourneyWin = TourneyWin;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerPlaytime(long long Playtime)
	{
		m_Account.m_Playtime = Playtime;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerKillstreak(int Killstreak)
	{
		if(Killstreak > m_SessionBestKillstreak)
			m_SessionBestKillstreak = Killstreak;
		m_Account.m_Killstreak = Killstreak;
		m_Account.m_DirtyProgress = true;
	}
	void SetWeeklyDay(int Day)
	{
		m_Account.m_WeeklyDay = Day;
		m_Account.m_DirtyProgress = true;
	}
	void SetWeeklyLastClaim(int Date)
	{
		m_Account.m_WeeklyLastClaim = Date;
		m_Account.m_DirtyProgress = true;
	}
	void SetWeeklyExpBoostUntil(long long Until)
	{
		m_Account.m_WeeklyExpBoostUntil = Until;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerTourneyWins(int TourneyWins)
	{
		m_Account.m_TourneyWin = TourneyWins;
		m_Account.m_DirtyProgress = true;
	}
	void SetPlayerLastName(const char *LastName)
	{
		str_copy(m_Account.m_aLastName, LastName, sizeof(m_Account.m_aLastName));
		m_Account.m_DirtyCore = true;
	}
	void SetPlayerLastSkin(const char *LastSkin)
	{
		str_copy(m_Account.m_aLastSkin, LastSkin, sizeof(m_Account.m_aLastSkin));
		m_Account.m_DirtyCore = true;
	}
	void SetPlayerLastBodyColor(int LastBodyColor)
	{
		m_Account.m_LastBodyColor = LastBodyColor;
		m_Account.m_DirtyCore = true;
	}
	void SetPlayerLastFeetColor(int LastFeetColor)
	{
		m_Account.m_LastFeetColor = LastFeetColor;
		m_Account.m_DirtyCore = true;
	}

	// Getters
	int GetPlayerId() { return m_Account.m_Id; }
	const char *GetPlayerName() { return m_Account.m_aName; }
	const char *GetPlayerPassword() { return m_Account.m_aPassword; }
	const char *GetPlayerAddress() { return m_Account.m_aAddress; }
	int GetPlayerVip() { return m_Account.m_Vip; }
	int GetPlayerPages() { return m_Account.m_Pages; }
	int GetPlayerPassiveRemovers() { return m_Account.m_PassiveRemovers; }
	int GetPlayerLevel() { return m_Account.m_Level; }
	int GetClanLevel();
	int GetPlayerExperience() { return m_Account.m_Experience; }
	int GetClanExperience();
	int GetPlayerWeaponkits() { return m_Account.m_Weaponkits; }
	int GetPlayerRanking() { return m_Account.m_Ranking; }
	int GetClanId() { return m_Account.m_ClanId; }
	ClanAuthLevel GetAuthLevel() { return m_Account.m_AuthLevel; }
	int GetPlayerBlockpoints() { return m_Account.m_Blockpoints; }
	const char *GetPlayerKnockouts() { return m_Account.m_aKnockouts; }
	const char *GetPlayerGundesign() { return m_Account.m_aGundesign; }
	const char *GetPlayerSkinmani() { return m_Account.m_aSkinmani; }

	int GetPlayerPassive() { return m_Account.m_Passive; }
	const char *GetPlayerRegisterDate() { return m_Account.m_RegisterDate; }
	int GetPlayerRankedGames() { return m_Account.m_RankedGames; }
	int GetPlayerRankedKills() { return m_Account.m_RankedKills; }
	int GetPlayerRankedDeaths() { return m_Account.m_RankedDeaths; }
	int GetPlayerRankedWins() { return m_Account.m_RankedWins; }
	int GetPlayerKills() { return m_Account.m_Kills; }
	int GetPlayerDeaths() { return m_Account.m_Deaths; }
	int GetPlayerTourneyWin() { return m_Account.m_TourneyWin; }
	long long GetPlayerPlaytime() { return m_Account.m_Playtime; }
	int GetPlayerKillstreak() { return m_Account.m_Killstreak; }
	int GetWeeklyDay() { return m_Account.m_WeeklyDay; }
	int GetWeeklyLastClaim() { return m_Account.m_WeeklyLastClaim; }
	long long GetWeeklyExpBoostUntil() { return m_Account.m_WeeklyExpBoostUntil; }
	const char *GetPlayerLastName() { return m_Account.m_aLastName; }
	const char *GetPlayerLastSkin() { return m_Account.m_aLastSkin; }
	int GetPlayerLastBodyColor() { return m_Account.m_LastBodyColor; }
	int GetPlayerLastFeetColor() { return m_Account.m_LastFeetColor; }

	CAccountData m_Account;

	// Upstream removed CPlayer::m_Score in favour of
	// IGameController::SnapPlayerScore; BW keeps its own copy for the value it
	// reports through Server()->SetClientScore().
	std::optional<int> m_Score;

	// timestamp (server tick) of last clan invite sent (for basic cooldown)
	int64_t m_LastClanInviteTick = 0;

	bool m_IsNpc = false;

	bool m_PendingLoginCoreSave = false;
	int64_t m_PendingLoginSaveTick = 0;

	int64_t m_LastDeathnote;
	int64_t m_LastExpAccountAlert;
	int64_t m_ClanSaveCooldown;

private:
	std::map<int /* modifier (percent) */, int /* end tick */> m_ExpModifiers;
	float m_CurrentExpMultiplier;
	void CalculateExpMultiplier();

public:
	void AddPlayerExp(int Amount, bool ApplyMultiplier = true);
	void ProcessWeeklyReward();
	float GetExpMultiplier() const { return m_CurrentExpMultiplier; }
	void AddExpMultiplier(int ModifierPercent, int Duration);

	//events
	// used for 1on1, default to true
	bool m_allowDeath;
	int sent1on1InviteTo;
	// last tick when the player sent a 1on1 invite (anti-spam)
	int64_t m_Last1on1InviteTick;
	// last tick when player created a blockpoint transfer offer
	int64_t m_LastBpTransferOfferTick = 0;
	// last tick when the player executed /register (anti-zombie-account spam)
	int64_t m_LastRegisterTick;
	bool m_HideInfo = false;
	bool m_ShowLevel = true;
	bool m_EventWinner = false;
	int m_EventWTick = -1;
	bool m_IsDummy = false;
	bool m_HideInfoInScoreboard;

	// Scoring mode: 0=level (default), 1=blockpoints
	int m_ScoreDisplayMode = 0;

	// Hide other players' cosmetics
	bool m_HideCosmetics = false;

	// --- Live session stats (reset on disconnect, not saved to DB) ---
	int m_SessionKills = 0;
	int m_SessionDeaths = 0;
	int m_SessionExpGained = 0;
	int m_SessionBestKillstreak = 0;
	int m_SessionBlockpoints = 0;

	// Race finish EXP cooldown (tick of last awarded race EXP)
	int m_LastRaceFinishExpTick = 0;
	int m_RaceFinishExpCount = 0; // finishes awarded this session

	// 0 = not in TDM, 1 = inside TDM, 2 has sent an invite (only leaders can), 3 received leader invite, 4 received clan user invite, 5 accepted & waiting
	int s_TDM;
	int s_TDM_team;
	int s_TDM_start;
	int TDM_invited_by;
	CTeeInfo m_OldTeeInfos;
	bool m_spectateTDM = false;
	//cosmetics
private:
	int m_CurrentKnockout = -1;
	int m_CurrentGunDesign = -1;
	int m_CurrentSkinMani = -1;

public:
	void SetKnockout(int Knockout) { m_CurrentKnockout = Knockout; }
	void SetGunDesign(int GunDesign) { m_CurrentGunDesign = GunDesign; }
	void SetSkinMani(int SkinMani) { m_CurrentSkinMani = SkinMani; }

	int GetKnockout() const { return m_CurrentKnockout; }
	int GetGunDesign() const { return m_CurrentGunDesign; }
	int GetSkinMani() const { return m_CurrentSkinMani; }

	int ToggleKnockout(int Knockout)
	{
		m_CurrentKnockout = m_CurrentKnockout == Knockout ? -1 : Knockout;
		return m_CurrentKnockout;
	}

	int ToggleGunDesign(int GunDesign)
	{
		m_CurrentGunDesign = m_CurrentGunDesign == GunDesign ? -1 : GunDesign;
		return m_CurrentGunDesign;
	}

	int ToggleSkinMani(int SkinMani)
	{
		m_CurrentSkinMani = m_CurrentSkinMani == SkinMani ? -1 : SkinMani;
		return m_CurrentSkinMani;
	}

	int m_LocalPassiveDuration = 0;
	int m_PassiveRaceCooldown = 0; // seconds remaining before player can redo the passive race
	bool m_PassivePendingGrant = false; // grant passive when cooldown expires
	int m_PassiveRemoverUseCooldown = 0; // seconds remaining before player can use a passive remover again

	// random cosmetic tile (temporary, not account-bound)
	int m_RandomCosmeticDuration = 0; // seconds remaining
	int m_RandomCosmeticSkinmani = -1;
	int m_RandomCosmeticKnockout = -1;
	int m_RandomCosmeticGundesign = -1;

	// banhammer (one-time use, set via rcon for now)
	bool m_BanhammerActive = false;

	// specials
	int m_CurrentSpecial = -1;
	class CEntity *m_pSpecialEntity = nullptr;
	const char *GetPlayerSpecials();
	int GetCurrentSpecial() const { return m_CurrentSpecial; }
	bool ToggleSpecial(int SpecialIndex);
	char m_aSpecialsOwned[CCosmeticsHandler::NUM_SPECIALS + 1];
	bool m_UsePassiveProtection = true;
	bool m_PassivePendingEnable = false;

	// Inline leaderboard capture for vote menu rendering
	bool m_CaptureTopToMenu = false; // when true, next TOP_MESSAGES/DIRECT top list goes into buffer
	int m_CaptureTopCategory = -1; // 0=Level,1=Blockpoints,2=Killstreaks,3=Clans
	int m_TopMessagesCount = 0;
	static constexpr int TOP_MAX_LINES = 15;
	static constexpr int TOP_MAX_LINE_LEN = 96;
	char m_aTopMessages[TOP_MAX_LINES][TOP_MAX_LINE_LEN];

	void TogglePassive()
	{
		if(m_UsePassiveProtection)
		{
			m_UsePassiveProtection = false;
			m_PassivePendingEnable = false;
			return;
		}
		if(GetCharacter())
		{
			if(m_PassivePendingEnable)
				m_PassivePendingEnable = false;
			else
				m_PassivePendingEnable = true; // will activate after death
			return;
		}
		m_UsePassiveProtection = true;
		m_PassivePendingEnable = false;
	}

	bool IsUsingPassiveProtection() const { return m_UsePassiveProtection; }
	bool IsPassivePendingEnable() const { return m_PassivePendingEnable; }
	void PromotePassiveIfPending()
	{
		if(m_PassivePendingEnable)
		{
			m_UsePassiveProtection = true;
			m_PassivePendingEnable = false;
		}
	}

	// Telekinesis (admin feature)
	bool m_TelekinesisEnabled = false; // whether this admin has telekinesis active
	int m_TelekinesisTarget = -1; // client ID of the player being held

	// Broadcasts
	class CBroadcastData
	{
	public:
		char m_aMessage[1024];
		int64_t m_SentTick;
	} m_BroadcastData;

	void SendBroadcastImp(const char *pMessage);
	[[gnu::format(printf, 2, 3)]] void SendBroadcast(const char *pFmt, ...)
	{
		char aBuf[1024];
		va_list args;
		va_start(args, pFmt);
		str_format_v(aBuf, sizeof(aBuf), pFmt, args);
		va_end(args);
		SendBroadcastImp(aBuf);
	}
	[[gnu::format(printf, 2, 3)]] void SendBroadcastAlignedLeft(const char *pFmt, ...)
	{
		char aBuf[1024];
		va_list args;
		va_start(args, pFmt);
		str_format_v(aBuf, sizeof(aBuf), pFmt, args);
		va_end(args);
		str_append(aBuf, BROADCAST_PADDING, sizeof(aBuf));
		SendBroadcastImp(aBuf);
	}
};

#endif // BLOCKWORLDS_BW_PLAYER_H
