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

#include <game/server/blockworlds/accounts.h>

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
	CCharacter *ForceSpawn(vec2 Pos); // required for loading savegames
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

	// Blockworlds

	void BWProcessScoreResult(CAccountResult &Result);
	void BWProcessAdminCommandResult(CAdminCommandResult &Result);
	std::shared_ptr<CAccountResult> m_AccountQueryResult;
	std::shared_ptr<CAdminCommandResult> m_AdminCommandQueryResult;

	void OnPlayerLogin();
	void OnPlayerLogout(int SetLoggedIn = 0);
	void OnPlayerSave(int SetLoggedIn);

	//helper functions:
	int GetAccId() { return m_Account.m_Id; }
	bool IsLoggedIn() { return GetAccId() != 0; }
	void SetAccId(int Id);

	// Setters
	void SetPlayerId(int Id) { m_Account.m_Id = Id; }
	void SetPlayerName(const char *Name) { strncpy(m_Account.m_aName, Name, sizeof(m_Account.m_aName) - 1); }
	void SetPlayerPassword(const char *Password) { strncpy(m_Account.m_aPassword, Password, sizeof(m_Account.m_aPassword) - 1); }
	void SetPlayerAddress(const char *Address) { strncpy(m_Account.m_aAddress, Address, sizeof(m_Account.m_aAddress) - 1); }
	void SetPlayerIsLoggedIn(int IsLoggedIn) { m_Account.m_IsLoggedIn = IsLoggedIn; }
	void SetPlayerVip(int Vip) { m_Account.m_Vip = Vip; }
	void SetPlayerPages(int Pages) { m_Account.m_Pages = Pages; }
	void SetPlayerLevel(int Level) { m_Account.m_Level = Level; }
	void SetPlayerExperience(int Experience) { m_Account.m_Experience = Experience; }
	void SetPlayerWeaponkits(int Weaponkits) { m_Account.m_Weaponkits = Weaponkits; }
	void SetPlayerClan(const char *ClanName)
	{
		std::strncpy(m_Account.m_aClan, ClanName, sizeof(m_Account.m_aClan) - 1);
		m_Account.m_aClan[sizeof(m_Account.m_aClan) - 1] = '\0';
	}
	void SetPlayerRanking(int Ranking) { m_Account.m_Ranking = Ranking; }
	void SetPlayerBlockpoints(int Blockpoints) { m_Account.m_Blockpoints = Blockpoints; }
	void SetPlayerKnockouts(const char *Knockouts) { strncpy(m_Account.m_aKnockouts, Knockouts, sizeof(m_Account.m_aKnockouts) - 1); }
	void SetPlayerGundesign(const char *Gundesign) { strncpy(m_Account.m_aGundesign, Gundesign, sizeof(m_Account.m_aGundesign) - 1); }
	void SetPlayerSkinmani(const char *Skinmani) { strncpy(m_Account.m_aSkinmani, Skinmani, sizeof(m_Account.m_aSkinmani) - 1); }
	void SetPlayerExtras(const char *Extras) { strncpy(m_Account.m_aExtras, Extras, sizeof(m_Account.m_aExtras) - 1); }
	void SetPlayerRegisterDate(const char *RegisterDate) { strncpy(m_Account.m_RegisterDate, RegisterDate, sizeof(m_Account.m_RegisterDate) - 1); }
	void SetPlayerRankedGames(int RankedGames) { m_Account.m_RankedGames = RankedGames; }
	void SetPlayerRankedKills(int RankedKills) { m_Account.m_RankedKills = RankedKills; }
	void SetPlayerRankedDeaths(int RankedDeaths) { m_Account.m_RankedDeaths = RankedDeaths; }
	void SetPlayerRankedWins(int RankedWins) { m_Account.m_RankedWins = RankedWins; }
	void SetPlayerKills(int Kills) { m_Account.m_Kills = Kills; }
	void SetPlayerDeaths(int Deaths) { m_Account.m_Deaths = Deaths; }
	void SetPlayerTourneyWin(int TourneyWin) { m_Account.m_TourneyWin = TourneyWin; }
	void SetPlayerPlaytime(long long Playtime) { m_Account.m_Playtime = Playtime; }
	void SetPlayerKillstreak(int Killstreak) { m_Account.m_Killstreak = Killstreak; }
	void SetPlayerLastName(const char *LastName) { strncpy(m_Account.m_aLastName, LastName, sizeof(m_Account.m_aLastName) - 1); }
	void SetPlayerLastSkin(const char *LastSkin) { strncpy(m_Account.m_aLastSkin, LastSkin, sizeof(m_Account.m_aLastSkin) - 1); }
	void SetPlayerLastBodyColor(int LastBodyColor) { m_Account.m_LastBodyColor = LastBodyColor; }
	void SetPlayerLastFeetColor(int LastFeetColor) { m_Account.m_LastFeetColor = LastFeetColor; }

	// Getters
	int GetPlayerId() { return m_Account.m_Id; }
	const char *GetPlayerName() { return m_Account.m_aName; }
	const char *GetPlayerPassword() { return m_Account.m_aPassword; }
	const char *GetPlayerAddress() { return m_Account.m_aAddress; }
	int GetPlayerIsLoggedIn() { return m_Account.m_IsLoggedIn; }
	int GetPlayerVip() { return m_Account.m_Vip; }
	int GetPlayerPages() { return m_Account.m_Pages; }
	int GetPlayerLevel() { return m_Account.m_Level; }
	int GetPlayerExperience() { return m_Account.m_Experience; }
	int GetPlayerWeaponkits() { return m_Account.m_Weaponkits; }
	int GetPlayerRanking() { return m_Account.m_Ranking; }
	const char *GetPlayerClan() { return m_Account.m_aClan; }
	int GetPlayerBlockpoints() { return m_Account.m_Blockpoints; }
	const char *GetPlayerKnockouts() { return m_Account.m_aKnockouts; }
	const char *GetPlayerGundesign() { return m_Account.m_aGundesign; }
	const char *GetPlayerSkinmani() { return m_Account.m_aSkinmani; }
	const char *GetPlayerExtras() { return m_Account.m_aExtras; }
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
	const char *GetPlayerLastName() { return m_Account.m_aLastName; }
	const char *GetPlayerLastSkin() { return m_Account.m_aLastSkin; }
	int GetPlayerLastBodyColor() { return m_Account.m_LastBodyColor; }
	int GetPlayerLastFeetColor() { return m_Account.m_LastFeetColor; }

	void AddPlayerExp(int Amount);

	CAccountData m_Account;

	bool m_allowDeath;
	int sent1on1InviteTo;
	bool m_HideInfo = false;
	bool m_ShowLevel = true;
	bool m_EventWinner = false;
	int m_EventWTick = -1;
	bool m_IsDummy = false;
	bool m_HideInfoInScoreboard;

	int64_t m_LastDeathnote;
	int64_t m_LastExpAccountAlert;

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
};

#endif
