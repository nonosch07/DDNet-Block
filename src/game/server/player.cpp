/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "player.h"
#include "entities/character.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "score.h"

#include <base/system.h>

#include <cstdint>
#include <ctime>
#include <engine/antibot.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <memory>

#include <game/gamecore.h>
#include <game/teamscore.h>

#include <blockworlds/accounts.h>
#include <blockworlds/clans.h>
#include <blockworlds/discord/webhook.h>
#include <blockworlds/common.h>

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/cosmetics/cosmetics.h>
// include specific event header to query 1on1 scores
#include <blockworlds/components/oneonone_manager.h>
#include <game/mapitems.h>
// (no direct dependency on requests here; rename notice is broadcast directly)
#include <blockworlds/components/requests.h>

// specials
#include <blockworlds/cosmetics/specials/ball.h>
#include <blockworlds/cosmetics/specials/crown.h>
#include <blockworlds/cosmetics/specials/epiccircle.h>
#include <blockworlds/cosmetics/specials/flag.h>
#include <blockworlds/cosmetics/specials/halo.h>

MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

CPlayer::CPlayer(CGameContext *pGameServer, uint32_t UniqueClientId, int ClientId, int Team) :
	m_UniqueClientId(UniqueClientId)
{
	m_pGameServer = pGameServer;
	m_ClientId = ClientId;
	m_Team = GameServer()->m_pController->ClampTeam(Team);
	m_NumInputs = 0;
	Reset();
	GameServer()->Antibot()->OnPlayerInit(m_ClientId);
	GameServer()->m_pController->m_BlockTracker.StartTrackPlayer(ClientId);

	m_LastDeathnote = Server()->Tick();
	m_LastExpAccountAlert = 0;
	m_ClanSaveCooldown = 0;
}

CPlayer::~CPlayer()
{
	GameServer()->Antibot()->OnPlayerDestroy(m_ClientId);
	delete m_pLastTarget;
	delete m_pCharacter;
	m_pCharacter = 0;
}

void CPlayer::Reset()
{
	m_DieTick = Server()->Tick();
	m_PreviousDieTick = m_DieTick;
	m_JoinTick = Server()->Tick();
	delete m_pCharacter;
	m_pCharacter = 0;
	m_SpectatorId = SPEC_FREEVIEW;
	m_LastActionTick = Server()->Tick();
	m_TeamChangeTick = Server()->Tick();
	m_LastSetTeam = 0;
	m_LastInvited = 0;
	m_WeakHookSpawn = false;

	int *pIdMap = Server()->GetIdMap(m_ClientId);
	for(int i = 1; i < VANILLA_MAX_CLIENTS; i++)
	{
		pIdMap[i] = -1;
	}
	pIdMap[0] = m_ClientId;

	// DDRace

	m_LastCommandPos = 0;
	m_LastPlaytime = 0;
	m_ChatScore = 0;
	m_Moderating = false;
	m_EyeEmoteEnabled = true;
	if(Server()->IsSixup(m_ClientId))
		m_TimerType = TIMERTYPE_SIXUP;
	else
		m_TimerType = (g_Config.m_SvDefaultTimerType == TIMERTYPE_GAMETIMER || g_Config.m_SvDefaultTimerType == TIMERTYPE_GAMETIMER_AND_BROADCAST) ? TIMERTYPE_BROADCAST : g_Config.m_SvDefaultTimerType;

	m_DefEmote = EMOTE_NORMAL;
	m_Afk = true;
	m_LastWhisperTo = -1;
	m_LastSetSpectatorMode = 0;
	m_aTimeoutCode[0] = '\0';
	delete m_pLastTarget;
	m_pLastTarget = new CNetObj_PlayerInput({0});
	m_LastTargetInit = false;
	m_TuneZone = 0;
	m_TuneZoneOld = m_TuneZone;
	m_Halloween = false;
	m_FirstPacket = true;

	m_SendVoteIndex = -1;

	if(g_Config.m_Events)
	{
		const ETimeSeason Season = time_season();
		if(Season == SEASON_NEWYEAR)
		{
			m_DefEmote = EMOTE_HAPPY;
		}
		else if(Season == SEASON_HALLOWEEN)
		{
			m_DefEmote = EMOTE_ANGRY;
			m_Halloween = true;
		}
		else
		{
			m_DefEmote = EMOTE_NORMAL;
		}
	}
	m_OverrideEmoteReset = -1;

	GameServer()->Score()->PlayerData(m_ClientId)->Reset();

	m_Last_KickVote = 0;
	m_Last_Team = 0;
	m_LastLMBVoteCall = 0;
	m_ShowOthers = g_Config.m_SvShowOthersDefault;
	m_ShowAll = g_Config.m_SvShowAllDefault;
	m_ShowDistance = vec2(1200, 800);
	m_SpecTeam = false;
	m_NinjaJetpack = false;

	m_Paused = PAUSE_NONE;
	m_DND = false;
	m_Whispers = true;

	m_LastPause = 0;
	m_Score.reset();

	// Variable initialized:
	m_Last_Team = 0;
	m_LastSqlQuery = 0;
	m_ScoreQueryResult = nullptr;
	m_ScoreFinishResult = nullptr;

	int64_t Now = Server()->Tick();
	int64_t TickSpeed = Server()->TickSpeed();
	// If the player joins within ten seconds of the server becoming
	// non-empty, allow them to vote immediately. This allows players to
	// vote after map changes or when they join an empty server.
	//
	// Otherwise, block voting in the beginning after joining.
	if(Now > GameServer()->m_NonEmptySince + 10 * TickSpeed)
		m_FirstVoteTick = Now + g_Config.m_SvJoinVoteDelay * TickSpeed;
	else
		m_FirstVoteTick = Now;

	m_NotEligibleForFinish = false;
	m_EligibleForFinishCheck = 0;
	m_VotedForPractice = false;
	m_SwapTargetsClientId = -1;
	m_BirthdayAnnounced = false;
	m_RescueMode = RESCUEMODE_AUTO;

	GameServer()->m_pController->m_BlockTracker.StopTrackPlayer(m_ClientId);
	m_ExpModifiers.clear();
	CalculateExpMultiplier();
	m_SpecialExpireTick = 0;
	m_pFlagEntity = nullptr;
	m_FlagExpireTick = 0;

	// initialize owned specials default (VIP check will apply when player logs in)
	for(int i = 0; i < CCosmeticsHandler::NUM_SPECIALS; ++i)
		m_aSpecialsOwned[i] = '0';
	m_aSpecialsOwned[CCosmeticsHandler::NUM_SPECIALS] = '\0';
}

static int PlayerFlags_SixToSeven(int Flags)
{
	int Seven = 0;
	if(Flags & PLAYERFLAG_CHATTING)
		Seven |= protocol7::PLAYERFLAG_CHATTING;
	if(Flags & PLAYERFLAG_SCOREBOARD)
		Seven |= protocol7::PLAYERFLAG_SCOREBOARD;

	return Seven;
}

void CPlayer::Tick()
{
	if(m_ScoreQueryResult != nullptr && m_ScoreQueryResult->m_Completed && m_SentSnaps >= 3)
	{
		ProcessScoreResult(*m_ScoreQueryResult);
		m_ScoreQueryResult = nullptr;
	}
	if(m_ScoreFinishResult != nullptr && m_ScoreFinishResult->m_Completed)
	{
		ProcessScoreResult(*m_ScoreFinishResult);
		m_ScoreFinishResult = nullptr;
	}

	if(!m_AccountQueryResult.empty() && m_AccountQueryResult.front() && m_AccountQueryResult.front()->m_Completed)
	{
		BWProcessAccountsResult(*m_AccountQueryResult.front());
		m_AccountQueryResult.pop();
	}
	if(m_PendingLoginCoreSave && IsLoggedIn())
	{
		if(Server()->Tick() > m_PendingLoginSaveTick)
		{
			GameServer()->Accounts()->Save(GetCid(), &m_Account);
			dbg_msg("account", "login snapshot saved for AccountId=%d (deferred)", m_Account.m_Id);
			m_PendingLoginCoreSave = false;
		}
	}
	if(!m_AdminCommandQueryResult.empty() && m_AdminCommandQueryResult.front() && m_AdminCommandQueryResult.front()->m_Completed)
	{
		BWProcessAdminCommandResult(*m_AdminCommandQueryResult.front());
		m_AdminCommandQueryResult.pop();
	}
	if(!m_ClanQueryResult.empty() && m_ClanQueryResult.front() && m_ClanQueryResult.front()->m_Completed)
	{
		BWProcessClansResult(*m_ClanQueryResult.front());
		m_ClanQueryResult.pop();
	}

	if(!Server()->ClientIngame(m_ClientId))
		return;

	if(m_ChatScore > 0)
		m_ChatScore--;

	// Do not set client score here unconditionally: event components (e.g., 1on1, TDM)
	// manage player visible score and call Server()->SetClientScore when necessary.
	// Calling SetClientScore every tick here could overwrite event-updated values.

	if(m_Moderating && m_Afk)
	{
		m_Moderating = false;
		GameServer()->SendChatTarget(m_ClientId, "Active moderator mode disabled because you are afk.");

		if(!GameServer()->PlayerModerating())
			GameServer()->SendChat(-1, TEAM_ALL, "Server kick/spec votes are no longer actively moderated.");
	}

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientId, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = maximum(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = minimum(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick() % Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum / Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if(Server()->GetNetErrorString(m_ClientId)[0])
	{
		SetInitialAfk(true);

		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "'%s' would have timed out, but can use timeout protection now", Server()->ClientName(m_ClientId));
		GameServer()->SendChat(-1, TEAM_ALL, aBuf);
		Server()->ResetNetErrorString(m_ClientId);
	}

	if(!GameServer()->m_World.m_Paused)
	{
		int EarliestRespawnTick = m_PreviousDieTick + Server()->TickSpeed() * 3;
		int RespawnTick = maximum(m_DieTick, EarliestRespawnTick) + 2;
		if(!m_pCharacter && RespawnTick <= Server()->Tick())
			m_Spawning = true;

		if(m_pCharacter)
		{
			if(m_pCharacter->IsAlive())
			{
				ProcessPause();
				if(!m_Paused)
					m_ViewPos = m_pCharacter->m_Pos;
			}
			else if(!m_pCharacter->IsPaused())
			{
				delete m_pCharacter;
				m_pCharacter = 0;
			}
		}
		else if(m_Spawning && !m_WeakHookSpawn)
			TryRespawn();
	}
	else
	{
		++m_DieTick;
		++m_PreviousDieTick;
		++m_JoinTick;
		++m_LastActionTick;
		++m_TeamChangeTick;
	}

	m_TuneZoneOld = m_TuneZone; // determine needed tunings with viewpos
	int CurrentIndex = GameServer()->Collision()->GetMapIndex(m_ViewPos);
	m_TuneZone = GameServer()->Collision()->IsTune(CurrentIndex);

	if(m_TuneZone != m_TuneZoneOld) // don't send tunings all the time
	{
		GameServer()->SendTuningParams(m_ClientId, m_TuneZone);
	}

	if(m_OverrideEmoteReset >= 0 && m_OverrideEmoteReset <= Server()->Tick())
	{
		m_OverrideEmoteReset = -1;
	}

	if(m_Halloween && m_pCharacter && !m_pCharacter->IsPaused())
	{
		if(1200 - ((Server()->Tick() - m_pCharacter->GetLastAction()) % (1200)) < 5)
		{
			GameServer()->SendEmoticon(GetCid(), EMOTICON_GHOST, -1);
		}
	}

	if(m_ClanSaveCooldown + Server()->TickSpeed() * g_Config.m_SvClanSaveInterval < Server()->Tick())
	{
		if(IsLoggedIn() && GetClanId() > 0)
		{
			GameServer()->Clans()->SaveClan(GetCid(), GetClanId());
			m_ClanSaveCooldown = Server()->Tick();
		}
	}

	{
		bool RecalculationNeeded = false;
		auto it = m_ExpModifiers.begin();
		for(; it != m_ExpModifiers.end();)
		{
			if(it->second <= Server()->Tick())
			{
				it = m_ExpModifiers.erase(it);
				RecalculationNeeded = true;
			}
			else
			{
				++it;
			}
		}

		// expire temporary special if its time has come
		if(m_SpecialExpireTick && Server()->Tick() >= m_SpecialExpireTick)
		{
			if(m_pSpecialEntity)
			{
				GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
				m_pSpecialEntity = nullptr;
			}
			m_CurrentSpecial = -1;
			m_SpecialExpireTick = 0;
		}

		// expire flag reward if needed
		if(m_pFlagEntity && m_FlagExpireTick && Server()->Tick() >= m_FlagExpireTick)
		{
			GameServer()->m_World.RemoveEntity(m_pFlagEntity);
			m_pFlagEntity = nullptr;
			m_FlagExpireTick = 0;
		}
		if(RecalculationNeeded)
			CalculateExpMultiplier();
	}
}

void CPlayer::GiveFlag(int DurationMinutes)
{
	// remove existing flag if present
	if(m_pFlagEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pFlagEntity);
		m_pFlagEntity = nullptr;
		m_FlagExpireTick = 0;
	}

	// spawn new flag entity attached to player
	m_pFlagEntity = new CFlag(&GameServer()->m_World, GetCid(), 0);
	if(DurationMinutes > 0)
		m_FlagExpireTick = Server()->Tick() + DurationMinutes * 60 * Server()->TickSpeed();
}

void CPlayer::GiveTemporarySpecial(int SpecialIndex, int DurationMinutes)
{
	if(SpecialIndex < 0 || SpecialIndex >= CCosmeticsHandler::NUM_SPECIALS)
		return;

	if(m_CurrentSpecial != -1 && m_pSpecialEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
		m_pSpecialEntity = nullptr;
		m_CurrentSpecial = -1;
		m_SpecialExpireTick = 0;
	}

	if(SpecialIndex == CCosmeticsHandler::SPECIAL_BALL)
	{
		extern class CBall *CreateBall(CGameWorld *, vec2, int);
		m_pSpecialEntity = new CBall(&GameServer()->m_World, GetCharacter() ? GetCharacter()->m_Pos : m_ViewPos, GetCid());
	}
	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_CROWN)
	{
		m_pSpecialEntity = new CCrown(&GameServer()->m_World, GetCid());
	}
	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_EPICCIRCLE)
	{
		m_pSpecialEntity = new CEpicCircle(&GameServer()->m_World, GetCharacter() ? GetCharacter()->m_Pos : m_ViewPos, GetCid());
	}
	else
		return;

	m_CurrentSpecial = SpecialIndex;
	if(DurationMinutes > 0)
		m_SpecialExpireTick = Server()->Tick() + DurationMinutes * Server()->TickSpeed();
}

void CPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags & PLAYERFLAG_IN_MENU)
		m_aCurLatency[m_ClientId] = GameServer()->m_apPlayers[m_ClientId]->m_Latency.m_Min;

	if(m_PlayerFlags & PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aCurLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators
	if((m_Team == TEAM_SPECTATORS || m_Paused) && m_SpectatorId != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorId] && GameServer()->m_apPlayers[m_SpectatorId]->GetCharacter())
		m_ViewPos = GameServer()->m_apPlayers[m_SpectatorId]->GetCharacter()->m_Pos;
}

void CPlayer::PostPostTick()
{
	if(!Server()->ClientIngame(m_ClientId))
		return;

	if(!GameServer()->m_World.m_Paused && !m_pCharacter && m_Spawning && m_WeakHookSpawn)
		TryRespawn();
}

void CPlayer::Snap(int SnappingClient)
{
	if(!Server()->ClientIngame(m_ClientId))
		return;

	int id = m_ClientId;
	if(!Server()->Translate(id, SnappingClient))
		return;

	CNetObj_ClientInfo *pClientInfo = Server()->SnapNewItem<CNetObj_ClientInfo>(id);
	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientId));

	if(IsLoggedIn() && GetClanId() > 0)
	{
		const char *pClanName = "";
		CClansData tmpClan;
		if(GameServer()->Clans()->GetClanSnapshotById(GetClanId(), tmpClan))
			pClanName = tmpClan.m_ClanName;
		StrToInts(&pClientInfo->m_Clan0, 3, pClanName);
	}
	else
	{
		StrToInts(&pClientInfo->m_Clan0, 3, "");
	}

	pClientInfo->m_Country = Server()->ClientCountry(m_ClientId);
	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_aSkinName);
	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;

	CCharacter *pCharForRainbow = GameServer()->GetPlayerChar(m_ClientId);
	if(pCharForRainbow && pCharForRainbow->IsHookRainbowActive())
	{
		int64_t TickDef = Server()->Tick() - m_DieTick;
		float freq = 255.0f;

		float divider = pCharForRainbow->GetHookRainbowDivider();
		if(divider > 0.0f)
			freq *= divider;

		float h = (sinf(TickDef / freq) + 1.0f) / 2.0f;
		float s = 0.5f;
		float l = 0.5f;
		int color = ((int)(h * 255) << 16) + ((int)(s * 255) << 8) + (int)((l - 0.5f) * 255 * 2);

		pClientInfo->m_ColorBody = color;
		pClientInfo->m_ColorFeet = color;
		pClientInfo->m_UseCustomColor = 1;
	}

	if(GetSkinMani() != -1)
		GameServer()->Cosmetics()->SnapSkinmani(m_ClientId, m_DieTick, pClientInfo);

	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnSnapClientInfo(GetCid(), SnappingClient, pClientInfo);

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	int Latency = SnappingClient == SERVER_DEMO_CLIENT ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aCurLatency[m_ClientId];

	int Score;
	// This is the time sent to the player while ingame (do not confuse to the one reported to the master server).
	// Due to clients expecting this as a negative value, we have to make sure it's negative.
	// Special numbers:
	// -9999: means no time and isn't displayed in the scoreboard.
		if(m_Score.has_value())
		{
			bool treatedAsEventScore = false;
			// First check active 1on1 matches managed by the oneonone manager
			if(auto mgr = g_ComponentRegistry.Get<COneOnOneManager>(); mgr)
			{
				if(auto match = mgr->GetMatchForPlayer(GetCid()); match)
				{
					if(auto evScore = match->GetScoreOf(GetCid()); evScore.has_value())
					{
						Score = evScore.value();
						treatedAsEventScore = true;
					}
				}
			}

			// fallback to the global active event (other event types)
			if(!treatedAsEventScore)
			{
				if(auto eventsAccessor = g_ComponentRegistry.Get<CEvents>(); eventsAccessor)
				{
					if(auto active = eventsAccessor->GetActiveEvent())
					{
						if(auto evScore = active->GetScoreOf(GetCid()); evScore.has_value())
						{
							Score = evScore.value();
							treatedAsEventScore = true;
						}
					}
				}
			}

			if(!treatedAsEventScore)
			{
				// shift the time by a second if the player actually took 9999
				// seconds to finish the map.
				if(m_Score.value() == 9999)
					Score = -10000;
				else
					Score = -m_Score.value();
			}
		}
	else
	{
		Score = -9999;
	}

	// send 0 if times of others are not shown
	if(SnappingClient != m_ClientId && g_Config.m_SvHideScore)
		Score = -9999;

	bool bScoreSetFromEvent = false;
	// Prefer manager-based 1on1 matches for per-player scoring/participant checks
	if(auto mgr = g_ComponentRegistry.Get<COneOnOneManager>(); mgr)
	{
		if(auto match = mgr->GetMatchForPlayer(GetCid()); match)
		{
			auto parts = match->Participants();
			bool isParticipant = std::find(parts.begin(), parts.end(), GetCid()) != parts.end();

			if(auto evScore = match->GetScoreOf(GetCid()); evScore.has_value())
			{
				Score = evScore.value();
				m_Score = evScore;
				Server()->SetClientScore(m_ClientId, Score);
				bScoreSetFromEvent = true;
			}
			else if(isParticipant)
			{
				Score = 0;
				m_Score = 0;
				Server()->SetClientScore(m_ClientId, Score);
				bScoreSetFromEvent = true;
			}
		}
	}

	// fallback to global active event if no manager match was found
	if(!bScoreSetFromEvent)
	{
		if(auto eventsAccessor = g_ComponentRegistry.Get<CEvents>(); eventsAccessor)
		{
			if(auto active = eventsAccessor->GetActiveEvent())
			{
				const auto &parts = active->Participants();
				bool isParticipant = std::find(parts.begin(), parts.end(), GetCid()) != parts.end();

				if(auto evScore = active->GetScoreOf(GetCid()); evScore.has_value())
				{
					Score = evScore.value();
					m_Score = evScore;
					Server()->SetClientScore(m_ClientId, Score);
					bScoreSetFromEvent = true;
				}
				else if(isParticipant)
				{
					Score = 0;
					m_Score = 0;
					Server()->SetClientScore(m_ClientId, Score);
					bScoreSetFromEvent = true;
				}
			}
		}
	}

	if(!bScoreSetFromEvent)
	{
		if(IsLoggedIn())
		{
			m_Score = Score = GetPlayerLevel();
			Server()->SetClientScore(m_ClientId, Score);
		}
		else
		{
			m_Score = Score = 0;
			Server()->SetClientScore(m_ClientId, Score);
		}
	}

	if(!Server()->IsSixup(SnappingClient))
	{
		CNetObj_PlayerInfo *pPlayerInfo = Server()->SnapNewItem<CNetObj_PlayerInfo>(id);
		if(!pPlayerInfo)
			return;

		pPlayerInfo->m_Latency = Latency;
		pPlayerInfo->m_Score = Score;
		pPlayerInfo->m_Local = (int)(m_ClientId == SnappingClient && (m_Paused != PAUSE_PAUSED || SnappingClientVersion >= VERSION_DDNET_OLD));
		pPlayerInfo->m_ClientId = id;
		pPlayerInfo->m_Team = m_Team;
		if(SnappingClientVersion < VERSION_DDNET_INDEPENDENT_SPECTATORS_TEAM)
		{
			// In older versions the SPECTATORS TEAM was also used if the own player is in PAUSE_PAUSED or if any player is in PAUSE_SPEC.
			pPlayerInfo->m_Team = (m_Paused != PAUSE_PAUSED || m_ClientId != SnappingClient) && m_Paused < PAUSE_SPEC ? m_Team : TEAM_SPECTATORS;
		}

		for(const auto &Component : g_ComponentRegistry.Active())
			Component->OnSnapClientInfo(GetCid(), SnappingClient, pClientInfo);
	}
	else
	{
		protocol7::CNetObj_PlayerInfo *pPlayerInfo = Server()->SnapNewItem<protocol7::CNetObj_PlayerInfo>(id);
		if(!pPlayerInfo)
			return;

		pPlayerInfo->m_PlayerFlags = PlayerFlags_SixToSeven(m_PlayerFlags);
		if(SnappingClientVersion >= VERSION_DDRACE && (m_PlayerFlags & PLAYERFLAG_AIM))
			pPlayerInfo->m_PlayerFlags |= protocol7::PLAYERFLAG_AIM;
		if(g_Config.m_SvShowAuthedUsers && Server()->ClientAuthed(m_ClientId))
			pPlayerInfo->m_PlayerFlags |= protocol7::PLAYERFLAG_ADMIN;

		// Times are in milliseconds for 0.7
		pPlayerInfo->m_Score = m_Score.has_value() ? GameServer()->Score()->PlayerData(m_ClientId)->m_BestTime * 1000 : -1;
		pPlayerInfo->m_Latency = Latency;
	}

	if(m_ClientId == SnappingClient && (m_Team == TEAM_SPECTATORS || m_Paused))
	{
		if(!Server()->IsSixup(SnappingClient))
		{
			CNetObj_SpectatorInfo *pSpectatorInfo = Server()->SnapNewItem<CNetObj_SpectatorInfo>(m_ClientId);
			if(!pSpectatorInfo)
				return;

			pSpectatorInfo->m_SpectatorId = m_SpectatorId;
			pSpectatorInfo->m_X = m_ViewPos.x;
			pSpectatorInfo->m_Y = m_ViewPos.y;
		}
		else
		{
			protocol7::CNetObj_SpectatorInfo *pSpectatorInfo = Server()->SnapNewItem<protocol7::CNetObj_SpectatorInfo>(m_ClientId);
			if(!pSpectatorInfo)
				return;

			pSpectatorInfo->m_SpecMode = m_SpectatorId == SPEC_FREEVIEW ? protocol7::SPEC_FREEVIEW : protocol7::SPEC_PLAYER;
			pSpectatorInfo->m_SpectatorId = m_SpectatorId;
			pSpectatorInfo->m_X = m_ViewPos.x;
			pSpectatorInfo->m_Y = m_ViewPos.y;
		}
	}

	CNetObj_DDNetPlayer *pDDNetPlayer = Server()->SnapNewItem<CNetObj_DDNetPlayer>(id);
	if(!pDDNetPlayer)
		return;

	// for 0.6 clients expose auth level only when explicitly allowed - requested by painn
	// when disabled, force AUTHED_NO so clients won't highlight names so we hidden
	if(g_Config.m_SvShowAuthedUsers)
		pDDNetPlayer->m_AuthLevel = Server()->GetAuthedState(m_ClientId);
	else
		pDDNetPlayer->m_AuthLevel = AUTHED_NO;
	pDDNetPlayer->m_Flags = 0;
	if(m_Afk && !m_IsNpc)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_AFK;
	if(m_Paused == PAUSE_SPEC)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_SPEC;
	if(m_Paused == PAUSE_PAUSED)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_PAUSED;

	if(Server()->IsSixup(SnappingClient) && m_pCharacter && m_pCharacter->m_DDRaceState == DDRACE_STARTED &&
		GameServer()->m_apPlayers[SnappingClient]->m_TimerType == TIMERTYPE_SIXUP)
	{
		protocol7::CNetObj_PlayerInfoRace *pRaceInfo = Server()->SnapNewItem<protocol7::CNetObj_PlayerInfoRace>(id);
		if(!pRaceInfo)
			return;
		pRaceInfo->m_RaceStartTick = m_pCharacter->m_StartTime;
	}

	bool ShowSpec = m_pCharacter && m_pCharacter->IsPaused() && m_pCharacter->CanSnapCharacter(SnappingClient);

	if(SnappingClient != SERVER_DEMO_CLIENT)
	{
		CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];
		ShowSpec = ShowSpec && (GameServer()->GetDDRaceTeam(m_ClientId) == GameServer()->GetDDRaceTeam(SnappingClient) || pSnapPlayer->m_ShowOthers == SHOW_OTHERS_ON || (pSnapPlayer->GetTeam() == TEAM_SPECTATORS || pSnapPlayer->IsPaused()));
	}

	if(ShowSpec)
	{
		CNetObj_SpecChar *pSpecChar = Server()->SnapNewItem<CNetObj_SpecChar>(id);
		if(!pSpecChar)
			return;

		pSpecChar->m_X = m_pCharacter->Core()->m_Pos.x;
		pSpecChar->m_Y = m_pCharacter->Core()->m_Pos.y;
	}
}

void CPlayer::FakeSnap()
{
	m_SentSnaps++;
	if(GetClientVersion() >= VERSION_DDNET_OLD)
		return;

	if(Server()->IsSixup(m_ClientId))
		return;

	int FakeId = VANILLA_MAX_CLIENTS - 1;

	CNetObj_ClientInfo *pClientInfo = Server()->SnapNewItem<CNetObj_ClientInfo>(FakeId);

	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, " ");
	StrToInts(&pClientInfo->m_Clan0, 3, "");
	StrToInts(&pClientInfo->m_Skin0, 6, "default");

	if(m_Paused != PAUSE_PAUSED)
		return;

	CNetObj_PlayerInfo *pPlayerInfo = Server()->SnapNewItem<CNetObj_PlayerInfo>(FakeId);
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = m_Latency.m_Min;
	pPlayerInfo->m_Local = 1;
	pPlayerInfo->m_ClientId = FakeId;
	pPlayerInfo->m_Score = -9999;
	pPlayerInfo->m_Team = TEAM_SPECTATORS;

	CNetObj_SpectatorInfo *pSpectatorInfo = Server()->SnapNewItem<CNetObj_SpectatorInfo>(FakeId);
	if(!pSpectatorInfo)
		return;

	pSpectatorInfo->m_SpectatorId = m_SpectatorId;
	pSpectatorInfo->m_X = m_ViewPos.x;
	pSpectatorInfo->m_Y = m_ViewPos.y;
}

void CPlayer::OnDisconnect()
{
	ClearCosmetics();
	KillCharacter();
	m_Moderating = false;
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags & PLAYERFLAG_CHATTING) && (pNewInput->m_PlayerFlags & PLAYERFLAG_CHATTING))
		return;

	AfkTimer();

	m_NumInputs++;

	if(m_pCharacter && !m_Paused)
		m_pCharacter->OnPredictedInput(pNewInput);

	// Magic number when we can hope that client has successfully identified itself
	if(m_NumInputs == 20 && g_Config.m_SvClientSuggestion[0] != '\0' && GetClientVersion() <= VERSION_DDNET_OLD)
		GameServer()->SendBroadcast(g_Config.m_SvClientSuggestion, m_ClientId);
	else if(m_NumInputs == 200 && Server()->IsSixup(m_ClientId))
		GameServer()->SendBroadcast("This server uses an experimental translation from Teeworlds 0.7 to 0.6. Please report bugs on ddnet.org/discord", m_ClientId);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	Server()->SetClientFlags(m_ClientId, pNewInput->m_PlayerFlags);

	AfkTimer();

	if(((!m_pCharacter && m_Team == TEAM_SPECTATORS) || m_Paused) && m_SpectatorId == SPEC_FREEVIEW)
		m_ViewPos = vec2(pNewInput->m_TargetX, pNewInput->m_TargetY);

	// check for activity
	if(mem_comp(pNewInput, m_pLastTarget, sizeof(CNetObj_PlayerInput)))
	{
		mem_copy(m_pLastTarget, pNewInput, sizeof(CNetObj_PlayerInput));
		// Ignore the first direct input and keep the player afk as it is sent automatically
		if(m_LastTargetInit)
			UpdatePlaytime();
		m_LastActionTick = Server()->Tick();
		m_LastTargetInit = true;
	}
}

void CPlayer::OnPredictedEarlyInput(CNetObj_PlayerInput *pNewInput)
{
	if((pNewInput->m_PlayerFlags & PLAYERFLAG_IN_MENU && !(m_PlayerFlags & PLAYERFLAG_IN_MENU)))
	{
		// resend vote options everytime somebody enters the menu, so the votes dont get out of sync when connecting a dummy
		// However, if the player is participating in an active event, do not send vote options
		// to avoid distracting them
		if(auto events = g_ComponentRegistry.Get<CEvents>(); events)
		{
			auto active = events->GetActiveEvent();
			if(active)
			{
				const auto &parts = active->Participants();
				if(std::find(parts.begin(), parts.end(), GetCid()) != parts.end())
				{
					GameServer()->ClearVotes(GetCid());
					return;
				}
			}
		}

		GameServer()->ClearVotes(GetCid());
		GameServer()->ProgressVoteOptions(GetCid());
		GameServer()->SendCosmeticsVoteOptions(GetCid());
	}

	m_PlayerFlags = pNewInput->m_PlayerFlags;

	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && (pNewInput->m_Fire & 1))
		m_Spawning = true;

	// skip the input if chat is active
	if(m_PlayerFlags & PLAYERFLAG_CHATTING)
		return;

	if(m_pCharacter && !m_Paused)
		m_pCharacter->OnDirectInput(pNewInput);
}

int CPlayer::GetClientVersion() const
{
	return m_pGameServer->GetClientVersion(m_ClientId);
}

CCharacter *CPlayer::GetCharacter()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

const CCharacter *CPlayer::GetCharacter() const
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

void CPlayer::KillCharacter(int Weapon, bool SendKillMsg)
{
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientId, Weapon, SendKillMsg);

		delete m_pCharacter;
		m_pCharacter = 0;
	}
}

void CPlayer::Respawn(bool WeakHook)
{
	if(m_Team != TEAM_SPECTATORS)
	{
		m_WeakHookSpawn = WeakHook;
		m_Spawning = true;
	}
}

CCharacter *CPlayer::ForceSpawn(vec2 Pos, bool doEvent)
{
	// check for active 1on1 match via manager and override spawn position
	if(auto mgr = g_ComponentRegistry.Get<COneOnOneManager>(); mgr)
	{
		if(auto match = mgr->GetMatchForPlayer(GetCid()); match && match->GetState() == COneOnOneEvent::EEventState::Active)
		{
				auto parts = match->Participants();
			if(std::find(parts.begin(), parts.end(), GetCid()) != parts.end())
			{
				const auto &reservation = match->GetSpawnReservation();
				std::vector<vec2> spawnPositions;
				GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), spawnPositions);
				int idx = (GetCid() == parts[0]) ? reservation.pos1Idx : reservation.pos2Idx;
				if(idx >= 0 && idx < (int)spawnPositions.size())
					Pos = spawnPositions[idx];
			}
		}
	}
	m_Spawning = false;
	m_pCharacter = new(m_ClientId) CCharacter(&GameServer()->m_World, GameServer()->GetLastPlayerInput(m_ClientId));
	m_pCharacter->Spawn(this, Pos, doEvent);
	m_Team = 0;
	return m_pCharacter;
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	KillCharacter();

	m_Team = Team;
	m_LastSetTeam = Server()->Tick();
	m_LastActionTick = Server()->Tick();
	m_SpectatorId = SPEC_FREEVIEW;

	protocol7::CNetMsg_Sv_Team Msg;
	Msg.m_ClientId = m_ClientId;
	Msg.m_Team = m_Team;
	Msg.m_Silent = !DoChatMsg;
	Msg.m_CooldownTick = m_LastSetTeam + Server()->TickSpeed() * g_Config.m_SvTeamChangeDelay;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(pPlayer && pPlayer->m_SpectatorId == m_ClientId)
				pPlayer->m_SpectatorId = SPEC_FREEVIEW;
		}
	}

	Server()->ExpireServerInfo();
}

bool CPlayer::SetTimerType(int TimerType)
{
	if(TimerType == TIMERTYPE_DEFAULT)
	{
		if(Server()->IsSixup(m_ClientId))
			m_TimerType = TIMERTYPE_SIXUP;
		else
			SetTimerType(g_Config.m_SvDefaultTimerType);

		return true;
	}

	if(Server()->IsSixup(m_ClientId))
	{
		if(TimerType == TIMERTYPE_SIXUP || TimerType == TIMERTYPE_NONE)
		{
			m_TimerType = TimerType;
			return true;
		}
		else
			return false;
	}

	if(TimerType == TIMERTYPE_GAMETIMER)
	{
		if(GetClientVersion() >= VERSION_DDNET_GAMETICK)
			m_TimerType = TimerType;
		else
			return false;
	}
	else if(TimerType == TIMERTYPE_GAMETIMER_AND_BROADCAST)
	{
		if(GetClientVersion() >= VERSION_DDNET_GAMETICK)
			m_TimerType = TimerType;
		else
		{
			m_TimerType = TIMERTYPE_BROADCAST;
			return false;
		}
	}
	else
		m_TimerType = TimerType;

	return true;
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos;

	bool used1on1 = false;
	// check for active 1on1 match via manager and override spawn position if needed
	if(auto mgr = g_ComponentRegistry.Get<COneOnOneManager>(); mgr)
	{
		if(auto match = mgr->GetMatchForPlayer(GetCid()); match && match->GetState() == COneOnOneEvent::EEventState::Active)
		{
			auto parts = match->Participants();
			if(std::find(parts.begin(), parts.end(), GetCid()) != parts.end())
			{
				const auto &reservation = match->GetSpawnReservation();
				std::vector<vec2> spawnPositions;
				GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), spawnPositions);
				int idx = (GetCid() == parts[0]) ? reservation.pos1Idx : reservation.pos2Idx;
				if(idx >= 0 && idx < (int)spawnPositions.size())
				{
					SpawnPos = spawnPositions[idx];
					used1on1 = true;
				}
			}
		}
	}
	if(!used1on1)
	{
		if(!GameServer()->m_pController->CanSpawn(m_Team, &SpawnPos, GameServer()->GetDDRaceTeam(m_ClientId)))
			return;
	}

	m_WeakHookSpawn = false;
	m_Spawning = false;
	m_pCharacter = new(m_ClientId) CCharacter(&GameServer()->m_World, GameServer()->GetLastPlayerInput(m_ClientId));
	m_ViewPos = SpawnPos;
	m_pCharacter->Spawn(this, SpawnPos);
	GameServer()->CreatePlayerSpawn(SpawnPos, GameServer()->m_pController->GetMaskForPlayerWorldEvent(m_ClientId));

	if(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
		m_pCharacter->SetSolo(true);
}

void CPlayer::UpdatePlaytime()
{
	m_LastPlaytime = time_get();
}

void CPlayer::AfkTimer()
{
	SetAfk(g_Config.m_SvMaxAfkTime != 0 && m_LastPlaytime < time_get() - time_freq() * g_Config.m_SvMaxAfkTime);
}

void CPlayer::SetAfk(bool Afk)
{
	if(m_Afk != Afk)
	{
		Server()->ExpireServerInfo();
		m_Afk = Afk;
	}
}

void CPlayer::SetInitialAfk(bool Afk)
{
	if(g_Config.m_SvMaxAfkTime == 0)
	{
		SetAfk(false);
		return;
	}

	SetAfk(Afk);

	// Ensure that the AFK state is not reset again automatically
	if(Afk)
		m_LastPlaytime = time_get() - time_freq() * g_Config.m_SvMaxAfkTime - 1;
	else
		m_LastPlaytime = time_get();
}

int CPlayer::GetDefaultEmote() const
{
	if(m_OverrideEmoteReset >= 0)
		return m_OverrideEmote;

	return m_DefEmote;
}

void CPlayer::OverrideDefaultEmote(int Emote, int Tick)
{
	m_OverrideEmote = Emote;
	m_OverrideEmoteReset = Tick;
	m_LastEyeEmote = Server()->Tick();
}

bool CPlayer::CanOverrideDefaultEmote() const
{
	return m_LastEyeEmote == 0 || m_LastEyeEmote + (int64_t)g_Config.m_SvEyeEmoteChangeDelay * Server()->TickSpeed() < Server()->Tick();
}

bool CPlayer::CanSpec() const
{
	return m_pCharacter->IsGrounded() && m_pCharacter->m_Pos == m_pCharacter->m_PrevPos;
}

void CPlayer::ProcessPause()
{
	if(m_ForcePauseTime && m_ForcePauseTime < Server()->Tick())
	{
		m_ForcePauseTime = 0;
		Pause(PAUSE_NONE, true);
	}

	if(m_Paused == PAUSE_SPEC && !m_pCharacter->IsPaused() && CanSpec())
	{
		m_pCharacter->Pause(true);
		GameServer()->CreateDeath(m_pCharacter->m_Pos, m_ClientId, GameServer()->m_pController->GetMaskForPlayerWorldEvent(m_ClientId));
		GameServer()->CreateSound(m_pCharacter->m_Pos, SOUND_PLAYER_DIE, GameServer()->m_pController->GetMaskForPlayerWorldEvent(m_ClientId));
	}
}

int CPlayer::Pause(int State, bool Force)
{
	if(State < PAUSE_NONE || State > PAUSE_SPEC) // Invalid pause state passed
		return 0;

	if(!m_pCharacter)
		return 0;

	char aBuf[128];
	if(State != m_Paused)
	{
		// Get to wanted state
		switch(State)
		{
		case PAUSE_PAUSED:
		case PAUSE_NONE:
			if(m_pCharacter->IsPaused()) // First condition might be unnecessary
			{
				if(!Force && m_LastPause && m_LastPause + (int64_t)g_Config.m_SvSpecFrequency * Server()->TickSpeed() > Server()->Tick())
				{
					GameServer()->SendChatTarget(m_ClientId, "Can't /spec that quickly.");
					return m_Paused; // Do not update state. Do not collect $200
				}
				m_pCharacter->Pause(false);
				m_ViewPos = m_pCharacter->m_Pos;
				GameServer()->CreatePlayerSpawn(m_pCharacter->m_Pos, GameServer()->m_pController->GetMaskForPlayerWorldEvent(m_ClientId));
			}
			[[fallthrough]];
		case PAUSE_SPEC:
			if(g_Config.m_SvPauseMessages)
			{
				str_format(aBuf, sizeof(aBuf), (State > PAUSE_NONE) ? "'%s' speced" : "'%s' resumed", Server()->ClientName(m_ClientId));
				GameServer()->SendChat(-1, TEAM_ALL, aBuf);
			}
			break;
		}

		// Update state
		m_Paused = State;
		m_LastPause = Server()->Tick();

		// Sixup needs a teamchange
		protocol7::CNetMsg_Sv_Team Msg;
		Msg.m_ClientId = m_ClientId;
		Msg.m_CooldownTick = Server()->Tick();
		Msg.m_Silent = true;
		Msg.m_Team = m_Paused ? protocol7::TEAM_SPECTATORS : m_Team;

		GameServer()->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, m_ClientId);
	}

	return m_Paused;
}

int CPlayer::ForcePause(int Time)
{
	m_ForcePauseTime = Server()->Tick() + Server()->TickSpeed() * Time;

	if(g_Config.m_SvPauseMessages)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "'%s' was force-paused for %ds", Server()->ClientName(m_ClientId), Time);
		GameServer()->SendChat(-1, TEAM_ALL, aBuf);
	}

	return Pause(PAUSE_SPEC, true);
}

int CPlayer::IsPaused() const
{
	return m_ForcePauseTime ? m_ForcePauseTime : -1 * m_Paused;
}

bool CPlayer::IsPlaying() const
{
	return m_pCharacter && m_pCharacter->IsAlive();
}

void CPlayer::SpectatePlayerName(const char *pName)
{
	if(!pName)
		return;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i != m_ClientId && Server()->ClientIngame(i) && !str_comp(pName, Server()->ClientName(i)))
		{
			m_SpectatorId = i;
			return;
		}
	}
}

void CPlayer::ProcessScoreResult(CScorePlayerResult &Result)
{
	if(Result.m_Success) // SQL request was successful
	{
		switch(Result.m_MessageKind)
		{
		case CScorePlayerResult::DIRECT:
			for(auto &aMessage : Result.m_Data.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->SendChatTarget(m_ClientId, aMessage);
			}
			break;
		case CScorePlayerResult::ALL:
		{
			bool PrimaryMessage = true;
			for(auto &aMessage : Result.m_Data.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;

				if(GameServer()->ProcessSpamProtection(m_ClientId) && PrimaryMessage)
					break;

				GameServer()->SendChat(-1, TEAM_ALL, aMessage, -1);
				PrimaryMessage = false;
			}
			break;
		}
		case CScorePlayerResult::BROADCAST:
			if(Result.m_Data.m_aBroadcast[0] != 0)
				GameServer()->SendBroadcast(Result.m_Data.m_aBroadcast, -1);
			break;
		case CScorePlayerResult::MAP_VOTE:
			GameServer()->m_VoteType = CGameContext::VOTE_TYPE_OPTION;
			GameServer()->m_LastMapVote = time_get();

			char aCmd[256];
			str_format(aCmd, sizeof(aCmd),
				"sv_reset_file types/%s/flexreset.cfg; change_map \"%s\"",
				Result.m_Data.m_MapVote.m_aServer, Result.m_Data.m_MapVote.m_aMap);

			char aChatmsg[512];
			str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)",
				Server()->ClientName(m_ClientId), Result.m_Data.m_MapVote.m_aMap, "/map");

			GameServer()->CallVote(m_ClientId, Result.m_Data.m_MapVote.m_aMap, aCmd, "/map", aChatmsg);
			break;
		case CScorePlayerResult::PLAYER_INFO:
		{
			if(Result.m_Data.m_Info.m_Time.has_value())
			{
				GameServer()->Score()->PlayerData(m_ClientId)->Set(Result.m_Data.m_Info.m_Time.value(), Result.m_Data.m_Info.m_aTimeCp);
				m_Score = Result.m_Data.m_Info.m_Time;
			}
			Server()->ExpireServerInfo();
			int Birthday = Result.m_Data.m_Info.m_Birthday;
			if(Birthday != 0 && !m_BirthdayAnnounced && GetCharacter())
			{
				char aBuf[512];
				str_format(aBuf, sizeof(aBuf),
					"Happy DDNet birthday to %s for finishing their first map %d year%s ago!",
					Server()->ClientName(m_ClientId), Birthday, Birthday > 1 ? "s" : "");
				GameServer()->SendChat(-1, TEAM_ALL, aBuf, m_ClientId);
				str_format(aBuf, sizeof(aBuf),
					"Happy DDNet birthday, %s!\nYou have finished your first map exactly %d year%s ago!",
					Server()->ClientName(m_ClientId), Birthday, Birthday > 1 ? "s" : "");
				GameServer()->SendBroadcast(aBuf, m_ClientId);
				m_BirthdayAnnounced = true;

				GameServer()->CreateBirthdayEffect(GetCharacter()->m_Pos, GetCharacter()->TeamMask());
			}
			GameServer()->SendRecord(m_ClientId);
			break;
		}
		case CScorePlayerResult::PLAYER_TIMECP:
			GameServer()->Score()->PlayerData(m_ClientId)->SetBestTimeCp(Result.m_Data.m_Info.m_aTimeCp);
			char aBuf[128], aTime[32];
			str_time_float(Result.m_Data.m_Info.m_Time.value(), TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "Showing the checkpoint times for '%s' with a race time of %s", Result.m_Data.m_Info.m_aRequestedPlayer, aTime);
			GameServer()->SendChatTarget(m_ClientId, aBuf);
			break;
		}
	}
}

//Blockworlds

void CPlayer::BWProcessAccountsResult(CAccountResult &Result)
{
	if(Result.m_Success)
	{
		switch(Result.m_MessageKind)
		{
		case CAccountResult::DIRECT:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->SendChatTarget(m_ClientId, aMessage);
			}
			break;

		case CAccountResult::ALL:
		{
			bool PrimaryMessage = true;
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;

				if(GameServer()->ProcessSpamProtection(m_ClientId) && PrimaryMessage)
					break;

				GameServer()->SendChat(-1, TEAM_ALL, aMessage, -1);
				PrimaryMessage = false;
			}
			break;
		}

		case CAccountResult::TOP_MESSAGES:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;

				GameServer()->SendChatTarget(m_ClientId, aMessage);
			}
			break;

		case CAccountResult::BROADCAST:
			break;

		case CAccountResult::LOGGED_IN_ALREADY:
			if(str_comp(Result.m_aLoginServer, g_Config.m_SvServerId) == 0)
			{
				GameServer()->SendChatTarget(m_ClientId, "Account is already being used on this server.");
				break;
			}
			GameServer()->SendChatTarget(m_ClientId, "Account is already being used on other server.");
			break;

		case CAccountResult::LOGIN_WRONG_PASS:
			GameServer()->SendChatTarget(m_ClientId, "Wrong username or password.");
			break;

		case CAccountResult::LOGIN_INFO:
			m_Account = Result.m_Account;
			OnPlayerLogin(); // Call the login handler
			break;

		case CAccountResult::REGISTER:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					continue;
				GameServer()->SendChatTarget(m_ClientId, aMessage);
			}
			// GameServer()->RegisterBanCheck(m_ClientId);
			break;

		case CAccountResult::LOG_ONLY:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				dbg_msg("account", "%s", aMessage);
			}
			break;
		}
	}
}

void CPlayer::BWProcessClansResult(CClanResult &Result)
{
	CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
	const char *pLogsUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
	bool discordConfigured = Discord.IsConfigured(pLogsUrl);

	if(Result.m_Success || Result.m_aaMessages[0][0] != '\0')
	{
		// First, apply any deferred actions requested by the SQL worker thread
		switch(Result.m_Action)
		{
		case CClanResult::ACTION_UPDATE_PLAYER_BY_CLIENT:
			if(Result.m_ActionClientId >= 0 && Result.m_ActionClientId < MAX_CLIENTS)
			{
				CPlayer *pTarget = GameServer()->m_apPlayers[Result.m_ActionClientId];
				if(pTarget)
				{
					const char *pPlayerName = pTarget->GetPlayerName();

					pTarget->m_Account.m_ClanId = Result.m_ActionNewClanId;
					pTarget->m_Account.m_AuthLevel = static_cast<ClanAuthLevel>(Result.m_ActionNewAuthLevel);

					if(discordConfigured && Result.m_Success)
					{
						if(Result.m_ActionNewClanId > 0 && Result.m_ActionNewAuthLevel == static_cast<int>(ClanAuthLevel::LEADER))
						{
							std::string clanName = GameServer()->Clans()->GetClanNameCopy(Result.m_ActionNewClanId);
							char aMsg[512];
							str_format(aMsg, sizeof(aMsg), "[CLAN] Created: %s (cid=%d) created clan '%s' (id=%d)", pPlayerName, Result.m_ActionClientId, clanName.c_str(), Result.m_ActionNewClanId);
							CDiscordWebhook::SSendOptions Opt; Opt.m_pWebhookUrl = pLogsUrl; Discord.Send(aMsg, Opt);
						}
					}
				}
			}
			break;
		case CClanResult::ACTION_UPDATE_PLAYER_BY_NAME:
			if(Result.m_ActionPlayerName[0] != '\0')
			{
				// find target player by name to capture previous clan for logging
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					CPlayer *pTarget = GameServer()->m_apPlayers[i];
					if(pTarget && pTarget->IsLoggedIn() && str_comp(pTarget->m_Account.m_aName, Result.m_ActionPlayerName) == 0)
					{
						int prevClan = pTarget->GetClanId();
						const char *pPlayerName = pTarget->GetPlayerName();
						pTarget->m_Account.m_ClanId = Result.m_ActionNewClanId;
						pTarget->m_Account.m_AuthLevel = static_cast<ClanAuthLevel>(Result.m_ActionNewAuthLevel);

						if(discordConfigured && Result.m_Success)
						{
							if(Result.m_ActionNewClanId > 0 && prevClan == 0)
							{
								std::string clanName = GameServer()->Clans()->GetClanNameCopy(Result.m_ActionNewClanId);
								char aMsg[512];
								str_format(aMsg, sizeof(aMsg), "[CLAN] Joined: %s joined clan '%s' (id=%d)", pPlayerName, clanName.c_str(), Result.m_ActionNewClanId);
								CDiscordWebhook::SSendOptions Opt; Opt.m_pWebhookUrl = pLogsUrl; Discord.Send(aMsg, Opt);
							}
							else if(Result.m_ActionNewClanId == 0 && prevClan > 0)
							{
								std::string clanName = GameServer()->Clans()->GetClanNameCopy(prevClan);
								char aMsg[512];
								str_format(aMsg, sizeof(aMsg), "[CLAN] Removed: %s was removed/left clan '%s' (id=%d)", pPlayerName, clanName.c_str(), prevClan);
								CDiscordWebhook::SSendOptions Opt; Opt.m_pWebhookUrl = pLogsUrl; Discord.Send(aMsg, Opt);
							}
						}
						break;
					}
				}
			}
			break;
		case CClanResult::ACTION_RESET_CLAN_PLAYERS:
			if(Result.m_ActionResetClanId > 0)
			{
				// log clan deletion
				if(discordConfigured && Result.m_Success)
				{
					std::string clanName = GameServer()->Clans()->GetClanNameCopy(Result.m_ActionResetClanId);
					char aMsg[512];
					str_format(aMsg, sizeof(aMsg), "[CLAN] Deleted: Clan '%s' (id=%d) was deleted", clanName.c_str(), Result.m_ActionResetClanId);
					CDiscordWebhook::SSendOptions Opt; Opt.m_pWebhookUrl = pLogsUrl; Discord.Send(aMsg, Opt);
				}
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					CPlayer *pTarget = GameServer()->m_apPlayers[i];
					if(pTarget && pTarget->IsLoggedIn() && pTarget->m_Account.m_ClanId == Result.m_ActionResetClanId)
					{
						pTarget->m_Account.m_ClanId = 0;
						pTarget->m_Account.m_AuthLevel = ClanAuthLevel::NONE;
					}
				}
			}
			break;
		case CClanResult::ACTION_NOTIFY_CLAN_RENAME:
			if(GetClanId() > 0)
			{
				char aBuf[192];
				const char *pOld = Result.m_ActionOldClanName[0] ? Result.m_ActionOldClanName : "<old>";
				const char *pNew = Result.m_ActionNewClanName[0] ? Result.m_ActionNewClanName : "<new>";
				str_format(aBuf, sizeof(aBuf), "Clan renamed: '%s' -> '%s'", pOld, pNew);
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					CPlayer *pMember = GameServer()->m_apPlayers[i];
					if(pMember && pMember->IsLoggedIn() && pMember->GetClanId() == GetClanId())
					{
						GameServer()->SendChatTarget(i, aBuf);
					}
				}
				if(discordConfigured && Result.m_Success)
				{
					char aMsg[512];
					str_format(aMsg, sizeof(aMsg), "[CLAN] Renamed: '%s' -> '%s'", pOld, pNew);
					CDiscordWebhook::SSendOptions Opt; Opt.m_pWebhookUrl = pLogsUrl; Discord.Send(aMsg, Opt);
				}
			}
			break;
		default:
			break;
		}

		if(Result.m_Success && Result.m_ActionChargeClientId >= 0 && Result.m_ActionChargeAmount > 0)
		{
			int cid = Result.m_ActionChargeClientId;
			if(cid >= 0 && cid < MAX_CLIENTS)
			{
				CPlayer *pCharger = GameServer()->m_apPlayers[cid];
				if(pCharger && pCharger->IsLoggedIn())
				{
					int cost = Result.m_ActionChargeAmount;
					if(pCharger->GetPlayerBlockpoints() >= cost)
					{
						pCharger->SetPlayerBlockpoints(pCharger->GetPlayerBlockpoints() - cost);
						GameServer()->Accounts()->Save(cid, &pCharger->m_Account);
						char aBuf[128];
						if(cost == g_Config.m_SvClanRenamePrice)
							str_format(aBuf, sizeof(aBuf), "You paid %d blockpoints to rename the clan.", cost);
						else if(cost == g_Config.m_SvClanCreatePrice)
							str_format(aBuf, sizeof(aBuf), "You paid %d blockpoints to create the clan.", cost);
						else
							str_format(aBuf, sizeof(aBuf), "You paid %d blockpoints.", cost);
						GameServer()->SendChatTarget(cid, aBuf);
					}
					else
					{
						if(Result.m_ActionChargeAmount == g_Config.m_SvClanRenamePrice)
							GameServer()->SendChatTarget(cid, "Rename fee could not be charged due to insufficient blockpoints after rename.");
						else if(Result.m_ActionChargeAmount == g_Config.m_SvClanCreatePrice)
							GameServer()->SendChatTarget(cid, "Creation fee could not be charged due to insufficient blockpoints after creation.");
						else
							GameServer()->SendChatTarget(cid, "Fee could not be charged due to insufficient blockpoints.");
					}
				}
			}
		}

		switch(Result.m_MessageKind)
		{
		case CClanResult::DIRECT:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->SendChatTarget(m_ClientId, aMessage);
			}
			break;
		case CClanResult::ALL:
		case CClanResult::BROADCAST:
		case CClanResult::DELETE:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->SendChatTarget(m_ClientId, aMessage);
			}
			break;
		case CClanResult::CLAN:
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				CPlayer *pMember = GameServer()->m_apPlayers[i];
				if(pMember && pMember->IsLoggedIn() && pMember->GetClanId() == GetClanId())
				{
					for(auto &aMessage : Result.m_aaMessages)
					{
						if(aMessage[0] == 0)
							break;
						GameServer()->SendChatTarget(i, aMessage);
					}
				}
			}
			break;
		}
		}
	}
}

void CPlayer::BWProcessAdminCommandResult(CAdminCommandResult &Result)
{
	if(Result.m_Success) // SQL request was successful
	{
		switch(Result.m_MessageKind)
		{
		case CAdminCommandResult::FREEZE_ACC:
		{
			break;
		}
		case CAdminCommandResult::MODERATOR:
		{
			break;
		}
		case CAdminCommandResult::SUPER_MODERATOR:
		{
			break;
		}
		case CAdminCommandResult::SUPPORTER:
		{
			break;
		}
		case CAdminCommandResult::DIRECT:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->SendChatTarget(m_ClientId, aMessage);
			}
			break;
		case CAdminCommandResult::ALL:
		{
			bool PrimaryMessage = true;
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;

				if(GameServer()->ProcessSpamProtection(m_ClientId) && PrimaryMessage)
					break;

				GameServer()->SendChat(-1, TEAM_ALL, aMessage, -1);
				PrimaryMessage = false;
			}
			break;
		}
		case CAdminCommandResult::BROADCAST:
			// if(Result.m_aBroadcast[0] != 0)
			// 	GameServer()->SendBroadcast(Result.m_aBroadcast, -1);
			break;
		case CAdminCommandResult::LOG_ONLY:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				dbg_msg("account", "%s", aMessage);
			}
			break;
		}
	}
}

void CPlayer::OnPlayerLogin()
{
	// KillCharacter(); - no need to kill since we allow using /login only at spawn
	GameServer()->SendChatTarget(m_ClientId, "Login successfully");

	GameServer()->ClearVotes(GetCid());
	GameServer()->ProgressVoteOptions(GetCid());
	GameServer()->SendCosmeticsVoteOptions(GetCid());

	if(GetPlayerVip())
	{
		for(int i = 0; i < CCosmeticsHandler::NUM_SPECIALS; ++i)
		{
			m_aSpecialsOwned[i] = '1';
		}
	}

	if(m_Account.m_Id > 0)
	{
		bool Dirty = false;
		const char *pCurName = Server()->ClientName(GetCid());
		if(pCurName[0] && str_comp(pCurName, m_Account.m_aLastName) != 0)
		{
			str_copy(m_Account.m_aLastName, pCurName, sizeof(m_Account.m_aLastName));
			Dirty = true;
		}
		const char *pSkin = m_TeeInfos.m_aSkinName;
		if(pSkin[0] && str_comp(pSkin, m_Account.m_aLastSkin) != 0)
		{
			str_copy(m_Account.m_aLastSkin, pSkin, sizeof(m_Account.m_aLastSkin));
			Dirty = true;
		}
		if(m_TeeInfos.m_ColorBody != m_Account.m_LastBodyColor)
		{
			m_Account.m_LastBodyColor = m_TeeInfos.m_ColorBody;
			Dirty = true;
		}
		if(m_TeeInfos.m_ColorFeet != m_Account.m_LastFeetColor)
		{
			m_Account.m_LastFeetColor = m_TeeInfos.m_ColorFeet;
			Dirty = true;
		}
		char aIp[48] = {0};
		Server()->GetClientAddr(GetCid(), aIp, sizeof(aIp));
		if(aIp[0])
		{
			bool Invalid = false;
			for(int i = 0; aIp[i]; ++i)
			{
				unsigned char c = (unsigned char)aIp[i];
				if(c < 0x20 || c > 0x7E)
				{
					Invalid = true;
					break;
				}
			}
			if(!Invalid)
			{
				str_copy(m_Account.m_aAddress, aIp, sizeof(m_Account.m_aAddress));
				Dirty = true;
			}
		}
		if(Dirty)
		{
			m_Account.m_DirtyCore = true;
			m_PendingLoginCoreSave = true;
			m_PendingLoginSaveTick = Server()->Tick();
		}
	}
}

void CPlayer::OnPlayerSave(bool Logout)
{
	dbg_msg("account", "saving account '%s' CID=%d AccountId=%d Logout=%d", Server()->ClientName(GetCid()), GetCid(), m_Account.m_Id, Logout);

	if(!m_Account.m_Id)
		return;

	char aName[32];
	str_copy(aName, Server()->ClientName(m_ClientId), sizeof(aName));

	if(str_comp(aName, m_Account.m_aLastName) != 0)
	{
		str_format(m_Account.m_aLastName, sizeof(m_Account.m_aLastName), "%s", aName);
		m_Account.m_DirtyCore = true;
	}

	char aIp[48] = {0};
	Server()->GetClientAddr(GetCid(), aIp, sizeof(aIp));
	if(aIp[0])
	{
		bool Invalid = false;
		for(int i = 0; aIp[i]; ++i)
		{
			unsigned char c = (unsigned char)aIp[i];
			if(c < 0x20 || c > 0x7E)
			{
				Invalid = true;
				break;
			}
		}
		if(Invalid)
		{
			str_copy(aIp, "npc", sizeof(aIp)); // fallback safe token
		}
		if(str_comp(aIp, m_Account.m_aAddress) != 0)
		{
			str_copy(m_Account.m_aAddress, aIp, sizeof(m_Account.m_aAddress));
			m_Account.m_DirtyCore = true;
		}
	}

	int ColorFeet = GameServer()->m_apPlayers[m_ClientId]->m_TeeInfos.m_ColorFeet;

	if(ColorFeet != m_Account.m_LastFeetColor)
	{
		m_Account.m_LastFeetColor = ColorFeet;
		m_Account.m_DirtyCore = true;
	}

	int ColorBody = GameServer()->m_apPlayers[m_ClientId]->m_TeeInfos.m_ColorBody;

	if(ColorBody != m_Account.m_LastBodyColor)
	{
		m_Account.m_LastBodyColor = ColorBody;
		m_Account.m_DirtyCore = true;
	}

	const char *aSkinName = GameServer()->m_apPlayers[m_ClientId]->m_TeeInfos.m_aSkinName;

	if(str_comp(aSkinName, m_Account.m_aLastSkin) != 0)
	{
		str_format(m_Account.m_aLastSkin, sizeof(m_Account.m_aLastSkin), "%s", aSkinName);
		m_Account.m_DirtyCore = true;
	}

	GameServer()->Accounts()->Save(GetCid(), &m_Account);
	if(Logout)
		GameServer()->Accounts()->Logout(GetCid(), m_Account.m_Id);
}

void CPlayer::OnPlayerLogout()
{
	if(!IsLoggedIn())
		return;
	if(GameServer()->Accounts() && GameServer()->Accounts()->ShutdownFlushActive())
	{
		dbg_msg("account", "skip duplicate logout save for AccountId=%d during shutdown", GetAccId());
		m_Account = CAccountData();
		return;
	}

	GameServer()->ClearVotes(GetCid());
	GameServer()->ProgressVoteOptions(GetCid());
	OnPlayerSave(true);
	dbg_msg("account", "logging out AccountId=%d", GetAccId());

	if(GetClanId() > 0)
	{
		GameServer()->Clans()->SaveClan(GetCid(), GetClanId());
	}

	// clear cosmetics and save account
	ClearCosmetics();
	SetSkinMani(-1);
	SetGunDesign(-1);
	SetKnockout(-1);

	// clear account data
	m_Account = CAccountData();
}

void CPlayer::ClearCosmetics()
{
	// remove any active special entity
	if(m_pSpecialEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
		m_pSpecialEntity = nullptr;
	}
	m_CurrentSpecial = -1;
	m_SpecialExpireTick = 0;

	// remove any active flag entity
	if(m_pFlagEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pFlagEntity);
		m_pFlagEntity = nullptr;
	}
	m_FlagExpireTick = 0;

	// reset owned specials
	for(int i = 0; i < CCosmeticsHandler::NUM_SPECIALS; ++i)
		m_aSpecialsOwned[i] = '0';
	m_aSpecialsOwned[CCosmeticsHandler::NUM_SPECIALS] = '\0';

	// reset active cosmetics
	m_CurrentSkinMani = -1;
	m_CurrentGunDesign = -1;
	m_CurrentKnockout = -1;
}

const char *CPlayer::GetPlayerSpecials()
{
	return m_aSpecialsOwned;
}

const char *CPlayer::GetEffectiveKnockouts()
{
	// build a temporary buffer reflecting owned knockouts + VIP access
	static thread_local char aBuf[256];
	const char *p = m_Account.m_aKnockouts;
	int len = sizeof(m_Account.m_aKnockouts);
	for(int i = 0; i < len; ++i)
		aBuf[i] = p[i];
	aBuf[len - 1] = '\0';
	// if VIP, set VIP splash
	if(GetPlayerVip())
	{
		if((int)strlen(aBuf) > CCosmeticsHandler::KNOCKOUT_VIP_SPLASH)
			aBuf[CCosmeticsHandler::KNOCKOUT_VIP_SPLASH] = '1';
	}
	return aBuf;
}

const char *CPlayer::GetEffectiveGundesign()
{
	static thread_local char aBuf[256];
	const char *p = m_Account.m_aGundesign;
	int len = sizeof(m_Account.m_aGundesign);
	for(int i = 0; i < len; ++i)
		aBuf[i] = p[i];
	aBuf[len - 1] = '\0';
	if(GetPlayerVip())
	{
		if((int)strlen(aBuf) > CCosmeticsHandler::GUNDESIGN_VIP_STARGUN)
			aBuf[CCosmeticsHandler::GUNDESIGN_VIP_STARGUN] = '1';
	}
	return aBuf;
}

const char *CPlayer::GetEffectiveSkinmani()
{
	static thread_local char aBuf[256];
	const char *p = m_Account.m_aSkinmani;
	int len = sizeof(m_Account.m_aSkinmani);
	for(int i = 0; i < len; ++i)
		aBuf[i] = p[i];
	aBuf[len - 1] = '\0';
	if(GetPlayerVip())
	{
		if((int)strlen(aBuf) > CCosmeticsHandler::SKINMANI_VIP_RAINBOW)
			aBuf[CCosmeticsHandler::SKINMANI_VIP_RAINBOW] = '1';
		if((int)strlen(aBuf) > CCosmeticsHandler::SKINMANI_VIP_RAINBOW_EPI)
			aBuf[CCosmeticsHandler::SKINMANI_VIP_RAINBOW_EPI] = '1';
		if((int)strlen(aBuf) > CCosmeticsHandler::SKINMANI_VIP_HOOK_RAINBOW)
			aBuf[CCosmeticsHandler::SKINMANI_VIP_HOOK_RAINBOW] = '1';
	}
	return aBuf;
}

bool CPlayer::ToggleSpecial(int SpecialIndex)
{
	if(SpecialIndex < 0 || SpecialIndex >= CCosmeticsHandler::NUM_SPECIALS)
		return false;

	if(m_CurrentSpecial == SpecialIndex)
	{
		if(m_pSpecialEntity)
		{
			GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
			m_pSpecialEntity = nullptr;
		}
		m_CurrentSpecial = -1;
		return true;
	}

	if(m_CurrentSpecial != -1 && m_pSpecialEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
		m_pSpecialEntity = nullptr;
		m_CurrentSpecial = -1;
	}

	if(SpecialIndex == CCosmeticsHandler::SPECIAL_BALL)
	{
		extern class CBall *CreateBall(CGameWorld *, vec2, int);
		// fallback: use direct constructor if available
		m_pSpecialEntity = new CBall(&GameServer()->m_World, GetCharacter() ? GetCharacter()->m_Pos : m_ViewPos, GetCid());
	}
	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_CROWN)
	{
		m_pSpecialEntity = new CCrown(&GameServer()->m_World, GetCid());
	}
	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_EPICCIRCLE)
	{
		m_pSpecialEntity = new CEpicCircle(&GameServer()->m_World, GetCharacter() ? GetCharacter()->m_Pos : m_ViewPos, GetCid());
	}

	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_HALO)
	{
		m_pSpecialEntity = new CHalo(&GameServer()->m_World, GetCid());
	}
	else
		return false;

	m_CurrentSpecial = SpecialIndex;
	return true;
}

void CPlayer::AddPlayerExp(int Amount, bool ApplyMultiplier)
{
	// apply player-specific multipliers and optional weekend bonus
	float TotalMult = 1.0f;
	if(ApplyMultiplier)
		TotalMult *= GetExpMultiplier();
	if(g_Config.m_SvWeekendExpEnabled)
	{
		time_t t = time(nullptr);
		struct tm tmres;
		localtime_r(&t, &tmres);
		int wday = tmres.tm_wday; // 0=Sunday, 6=Saturday
		if(wday == 0 || wday == 6)
		{
			float weekendMult = (float)g_Config.m_SvWeekendExpMultiplier / 100.0f;
			if(weekendMult < 0.01f)
				weekendMult = 0.01f;
			TotalMult *= weekendMult;
		}
	}
	Amount = (int)((float)Amount * TotalMult);
	m_Account.m_Experience += Amount;

	if(GetPlayerExperience() >= NeededAccountExp(GetPlayerLevel()))
	{
		CPlayer *pPlayer = GameServer()->GetPlayer(m_ClientId);

		int ExcessiveExp = GetPlayerExperience() - NeededAccountExp(GetPlayerLevel());

		SetPlayerLevel(GetPlayerLevel() + 1);
		SetPlayerExperience(0);

		GameServer()->CreateSound(pPlayer->GetCharacter()->GetPos(), SOUND_CTF_CAPTURE, -1);
		pPlayer->GetCharacter()->SetEmote(EMOTE_HAPPY, Server()->Tick() + 2 * Server()->TickSpeed());

		// confetti effect
		if(pPlayer->GetCharacter())
		{
			CCharacter *pChar = pPlayer->GetCharacter();
			GameServer()->CreateFinishEffect(pChar->GetPos(), pChar->TeamMask());
		}

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[LevelUp+]: You are now level %d!", GetPlayerLevel());
		GameServer()->SendChatTarget(m_ClientId, aBuf);

		if(GetPlayerLevel() % 50 == 0)
		{
			SetPlayerBlockpoints(GetPlayerBlockpoints() + 300);

			str_copy(aBuf, "[LevelUp+]: You've received 300bp !", sizeof(aBuf));
			GameServer()->SendChatTarget(m_ClientId, aBuf);
		}

		AddPlayerExp(ExcessiveExp, false);

		OnPlayerSave(false);
	}
}

void CPlayer::AddExpMultiplier(int ModifierPercent, int Duration)
{
	auto it = m_ExpModifiers.find(ModifierPercent);
	if(it == m_ExpModifiers.end())
		m_ExpModifiers.emplace(ModifierPercent, Server()->Tick() + Duration * 60 * Server()->TickSpeed());
	else
		it->second += Duration * 60 * Server()->TickSpeed();

	CalculateExpMultiplier();
}
void CPlayer::CalculateExpMultiplier()
{
	if(m_ExpModifiers.empty())
	{
		m_CurrentExpMultiplier = 1.0f;
		return;
	}

	float Multiplier = 0.0f;
	switch(g_Config.m_SvBlockExperienceMultiplierStacking)
	{
	case HIGHEST:
	{
		int Highest = 0;
		for(const auto &ExpMultiplier : m_ExpModifiers)
			Highest = Highest > ExpMultiplier.first ? Highest : ExpMultiplier.first;
		m_CurrentExpMultiplier = (float)Highest / 100.0f;
		break;
	}
	case ADDITIVE:
	{
		Multiplier = 1.0f;
		for(const auto &ExpMultiplier : m_ExpModifiers)
			Multiplier += (ExpMultiplier.first - 100) / 100.0f;
		m_CurrentExpMultiplier = Multiplier;
		break;
	}
	case LOGARITHMIC:
	{
		Multiplier = 1.0f;
		for(const auto &ExpMultiplier : m_ExpModifiers)
			Multiplier *= (ExpMultiplier.first) / 100.0f;
		// Keep baseline at 1.0 and apply diminishing returns via logarithm
		m_CurrentExpMultiplier = 1.0f + log2f(Multiplier);
		if(m_CurrentExpMultiplier < 1.0f)
			m_CurrentExpMultiplier = 1.0f;
		break;
	}
	case MULTIPLICATIVE:
	{
		Multiplier = 1.0f;
		for(const auto &ExpMultiplier : m_ExpModifiers)
			Multiplier *= (ExpMultiplier.first) / 100.0f;
		m_CurrentExpMultiplier = Multiplier;
		break;
	}
	default:
		m_CurrentExpMultiplier = 1.0f;
		break;
	}
}

int CGameContext::GetNextClientID()
{
	int ClientID = -1;
	for(int i = 0; i < g_Config.m_SvMaxClients; i++)
	{
		if(m_apPlayers[i])
			continue;

		ClientID = i;
		break;
	}

	return ClientID;
}
