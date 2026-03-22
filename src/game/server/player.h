/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/alloc.h>
#include <game/server/save.h>

#include "teeinfo.h"

#include <memory>
#include <optional>
#include <queue>

#include <blockworlds/accounts.h>
#include <blockworlds/clans.h>
#include <blockworlds/cosmetics/cosmetics.h>

class CCharacter;
class CGameContext;
class IServer;
struct CNetObj_PlayerInput;
struct CScorePlayerResult;

class Accounts;
class CShop;

enum
{
	WEAPON_GAME = -3, // team switching etc
	WEAPON_SELF = -2, // console kill command
	WEAPON_WORLD = -1, // death tiles etc
};

static constexpr const char *BROADCAST_PADDING = "\n"
						 "                                                                                     "
						 "                                                                                     "
						 "                                                                                     ";

// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, uint32_t UniqueClientId, int ClientId, int Team);
	~CPlayer();

	void Reset();

	void TryRespawn();
	void Respawn(bool WeakHook = false); // with WeakHook == true the character will be spawned after all calls of Tick from other Players
	CCharacter *ForceSpawn(vec2 Pos, bool doEvent); // required for loading savegames
	void SetTeam(int Team, bool DoChatMsg = true);
	int GetTeam() const { return m_Team; }
	int GetCid() const { return m_ClientId; }
	uint32_t GetUniqueCid() const { return m_UniqueClientId; }
	int GetClientVersion() const;
	bool SetTimerType(int TimerType);

	void Tick();
	void PostTick();

	// will be called after all Tick and PostTick calls from other players
	void PostPostTick();
	void Snap(int SnappingClient);
	void FakeSnap();

	void OnDirectInput(CNetObj_PlayerInput *pNewInput);
	void OnPredictedInput(CNetObj_PlayerInput *pNewInput);
	void OnPredictedEarlyInput(CNetObj_PlayerInput *pNewInput);
	void OnDisconnect();

	void KillCharacter(int Weapon = WEAPON_GAME, bool SendKillMsg = true);
	CCharacter *GetCharacter();
	const CCharacter *GetCharacter() const;

	void SpectatePlayerName(const char *pName);

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;
	int m_TuneZone;
	int m_TuneZoneOld;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aCurLatency[MAX_CLIENTS];

	int m_SentSnaps = 0;

	// used for spectator mode
	int m_SpectatorId;

	bool m_IsReady;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int64_t m_LastLMBVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastEmoteGlobal;
	int m_LastKill;
	int m_aLastCommands[4];
	int m_LastCommandPos;
	int m_LastWhisperTo;
	int m_LastInvited;

	int m_SendVoteIndex;

	CTeeInfo m_TeeInfos;

	int m_DieTick;
	int m_PreviousDieTick;
	std::optional<int> m_Score;
	int m_JoinTick;
	bool m_ForceBalanced;
	int m_LastActionTick;
	int m_TeamChangeTick;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

private:
	const uint32_t m_UniqueClientId;
	CCharacter *m_pCharacter;
	int m_NumInputs;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	//
	bool m_Spawning;
	bool m_WeakHookSpawn;
	int m_ClientId;
	int m_Team;

	int m_Paused;
	int64_t m_ForcePauseTime;
	int64_t m_LastPause;
	bool m_Afk;

	int m_DefEmote;
	int m_OverrideEmote;
	int m_OverrideEmoteReset;
	bool m_Halloween;

public:
	enum
	{
		PAUSE_NONE = 0,
		PAUSE_PAUSED,
		PAUSE_SPEC
	};

	enum
	{
		TIMERTYPE_DEFAULT = -1,
		TIMERTYPE_GAMETIMER = 0,
		TIMERTYPE_BROADCAST,
		TIMERTYPE_GAMETIMER_AND_BROADCAST,
		TIMERTYPE_SIXUP,
		TIMERTYPE_NONE,
	};

	bool m_DND;
	bool m_Whispers;
	int64_t m_FirstVoteTick;
	char m_aTimeoutCode[64];

	void ProcessPause();
	int Pause(int State, bool Force);
	int ForcePause(int Time);
	int IsPaused() const;
	bool CanSpec() const;

	bool IsPlaying() const;
	int64_t m_Last_KickVote;
	int64_t m_Last_Team;
	int m_ShowOthers;
	bool m_ShowAll;
	vec2 m_ShowDistance;
	bool m_SpecTeam;
	bool m_NinjaJetpack;

	int m_ChatScore;

	bool m_Moderating;

	void UpdatePlaytime();
	void AfkTimer();
	void SetAfk(bool Afk);
	void SetInitialAfk(bool Afk);
	bool IsAfk() const { return m_Afk; }

	int64_t m_LastPlaytime;
	int64_t m_LastEyeEmote;
	int64_t m_LastBroadcast;
	bool m_LastBroadcastImportance;

	CNetObj_PlayerInput *m_pLastTarget;
	bool m_LastTargetInit;

	bool m_EyeEmoteEnabled;
	int m_TimerType;

	int GetDefaultEmote() const;
	void OverrideDefaultEmote(int Emote, int Tick);
	bool CanOverrideDefaultEmote() const;

	bool m_FirstPacket;
	int64_t m_LastSqlQuery;
	void ProcessScoreResult(CScorePlayerResult &Result);
	std::shared_ptr<CScorePlayerResult> m_ScoreQueryResult;
	std::shared_ptr<CScorePlayerResult> m_ScoreFinishResult;
	bool m_NotEligibleForFinish;
	int64_t m_EligibleForFinishCheck;
	bool m_VotedForPractice;
	int m_SwapTargetsClientId; //Client ID of the swap target for the given player
	bool m_BirthdayAnnounced;

	int m_RescueMode;

	CSaveTee m_LastTeleTee;
	// temporary special expiration tick (server ticks, 50 ticks = 1 second)
	int m_SpecialExpireTick;
	// flag entity for programmatic flag reward and its expiration
	CEntity *m_pFlagEntity;
	int m_FlagExpireTick;

	// Blockworlds

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
	int GetClanLevel()
	{
		CClansData tmp;
		if(m_Account.m_ClanId > 0 && GameServer()->Clans()->GetClanSnapshotById(m_Account.m_ClanId, tmp))
			return tmp.m_Level;
		return 0;
	}
	int GetPlayerExperience() { return m_Account.m_Experience; }
	int GetClanExperience()
	{
		CClansData tmp;
		if(m_Account.m_ClanId > 0 && GameServer()->Clans()->GetClanSnapshotById(m_Account.m_ClanId, tmp))
			return tmp.m_Experience;
		return 0;
	}
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
	void SendBroadcast(const char *pFmt, ...)
	{
		char aBuf[1024];
		va_list args;
		va_start(args, pFmt);
		str_format_v(aBuf, sizeof(aBuf), pFmt, args);
		va_end(args);
		SendBroadcastImp(aBuf);
	}
	void SendBroadcastAlignedLeft(const char *pFmt, ...)
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

#endif
