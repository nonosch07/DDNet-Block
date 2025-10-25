#include "lmb.h"

#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/discord/webhook.h>

CLastManBlockingEvent::CLastManBlockingEvent(CGameContext *pGameContext) :
	CEventComponent(pGameContext), m_SpawnOffset(0), m_DDRaceTeam(-1), m_Winner(-1), m_FinishingReason(NATURAL)
{
	m_RegistrationEndTick = -1;
	m_ActiveStartTick = -1;
	m_ActiveEndTick = -1;

	m_SpawnPositions.clear();
	int Found = CGameContext::GetTilePositions(TILE_BW_EVENT_START_POS, GameServer(), m_SpawnPositions);
	if(Found == 0)
	{
		EmergencyShutdown("Map has no event start tiles");
		return;
	}
}

void CLastManBlockingEvent::OnTick()
{
	if(CEventComponent::EmergencyShutdown())
	{
		FinishEvent(EMERGENCY);
		return;
	}

	if(GetState() == CEventComponent::EEventState::Registration)
	{
		if(Server()->Tick() >= m_RegistrationEndTick)
		{
			CloseRegistration();
			return;
		}

		// TODO: broadcast manager
		if(Server()->Tick() % Config()->m_SvLMBBroadcastRate == 0)
		{
			GameServer()->SendBroadcast(-1, "%s is about to start!\n"
							"Register with /join\n"
							"Time left: %d seconds\n\n"
							"Candidates: %" PRIzu "\n\n"
							"%s\n"
							"%s",
				GetEventName(),
				(int)((m_RegistrationEndTick - Server()->Tick()) / Server()->TickSpeed()),
				Candidates().size(),
				(int)Candidates().size() < Config()->m_SvLMBMinimumCandidates ? "Not enough candidates!" : "",
				"                                                                                     "
				"                                                                                     "
				"                                                                                     ");
		}
	}
	else if(GetState() == CEventComponent::EEventState::Active)
	{
		if(Server()->Tick() % Config()->m_SvLMBBroadcastRate == 0)
			for(const auto &ClientId : Participants())
				GameServer()->SendBroadcast(ClientId, "Participants left: %" PRIzu "\n"
								      "Time left: %d seconds\n"
								      "%s",
					Participants().size(),
					(int)((m_ActiveEndTick - Server()->Tick()) / Server()->TickSpeed()),
					"                                                                                     "
					"                                                                                     "
					"                                                                                     ");

		if(Server()->Tick() > m_ActiveStartTick + Config()->m_SvLMBInitialFreezeTime * Server()->TickSpeed())
		{
			CheckFreezeTime();
		}

		for(int participant : Participants())
		{
			if(PlayerHookedGroundFor(participant) > Config()->m_SvGroundHookPenaltyDelay)
			{
				GameServer()->GetPlayerChar(participant)->FreezeForce(Config()->m_SvGroundHookPenalty);
			}
		}

		if(CheckEndCondition())
			FinishEvent(NATURAL);
	}
}

void CLastManBlockingEvent::OnSnapClientInfo(int ClientId, int SnappingClient, struct CNetObj_ClientInfo *pClientInfo)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(!Server()->ClientAuthed(SnappingClient) && IsParticipant(ClientId))
	{
		StrToInts(&pClientInfo->m_Name0, 4, " ");
		StrToInts(&pClientInfo->m_Clan0, 3, " ");
		StrToInts(&pClientInfo->m_Skin0, 6, "default");
		pClientInfo->m_Country = 0;
		pClientInfo->m_UseCustomColor = false;
		pClientInfo->m_ColorBody = 0;
		pClientInfo->m_ColorFeet = 0;
	}
}

void CLastManBlockingEvent::OnSnapPlayerInfo(int ClientId, int SnappingClient, struct CNetObj_PlayerInfo *pPlayerInfo)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(!Server()->ClientAuthed(SnappingClient) && IsParticipant(ClientId))
	{
		pPlayerInfo->m_Score = 0;
	}
}

void CLastManBlockingEvent::OpenRegistration()
{
	m_Candidates.clear();
	m_RegistrationEndTick = Server()->Tick() + Config()->m_SvLMBRegistrationTime * Server()->TickSpeed();

	SetState(CEventComponent::EEventState::Registration);
}
void CLastManBlockingEvent::CloseRegistration()
{
	SetState(CEventComponent::EEventState::Preparation);

	if((int)m_Candidates.size() < Config()->m_SvLMBMinimumCandidates)
	{
		FinishEvent(NOT_ENOUGH_CANDIDATES);
		return;
	}

	m_Participants = m_Candidates;
	m_Candidates.clear();
	StartEvent();
}
void CLastManBlockingEvent::StartEvent()
{
	auto Participants = m_Participants;
	for(const auto &ClientId : Participants)
	{
		if(!GameServer()->GetPlayerChar(ClientId))
			Leave(ClientId);
	}

	auto &Teams = GameServer()->m_pController->Teams();
	m_DDRaceTeam = Teams.GetFirstEmptyTeam();
	if(m_DDRaceTeam == -1)
	{
		EmergencyShutdown("No free team was found");
		return;
	}
	// mark this team as an event team so team-wide kills are suppressed
	Teams.SetTeamEvent(m_DDRaceTeam, true);
	Teams.SetTeamLock(m_DDRaceTeam, false);
	Teams.SetTeamInvitesOpen(m_DDRaceTeam, false);

	m_SpawnOffset = 0;
	m_pSavedPlayers.clear();
	m_SavedWeapons.clear();
	m_FrozenSince.clear();
	for(const auto &ClientId : m_Participants)
	{
		Join(ClientId);
		m_SpawnOffset++;
	}

	m_ActiveStartTick = Server()->Tick();
	m_ActiveEndTick = Server()->Tick() + Config()->m_SvLMBActiveTime * Server()->TickSpeed();

	SetState(CEventComponent::EEventState::Active);
}
void CLastManBlockingEvent::FinishEvent()
{
	SetState(CEventComponent::EEventState::Ending);
	if(m_FinishingReason == NATURAL)
	{
		if(m_Winner != -1)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "'%s' has won the %s", Server()->ClientName(m_Winner), GetEventName());
			GameServer()->SendChatTarget(-1, aBuf);
			GameServer()->SendBroadcast(-1, aBuf, false);

			CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
			if(Discord.IsConfigured(g_Config.m_SvDiscordWebhookUrlLmb[0] ? g_Config.m_SvDiscordWebhookUrlLmb : nullptr))
			{
				char aMsg[512];
				const char *pMap = Server()->GetMapName();
				str_format(aMsg, sizeof(aMsg), "LMB finished on %s: Winner **%s**", pMap ? pMap : "<map>", Server()->ClientName(m_Winner));
				CDiscordWebhook::SSendOptions Opt;
				Opt.m_pWebhookUrl = g_Config.m_SvDiscordWebhookUrlLmb[0] ? g_Config.m_SvDiscordWebhookUrlLmb : nullptr;
				Discord.Send(aMsg, Opt);
			}

			int BlockpointsReward = Config()->m_SvLMBBlockpointsReward;
			int PagesReward = Config()->m_SvLMBPagesReward;
			CPlayer *pWinner = GameServer()->GetPlayer(m_Winner);
			if(pWinner)
			{
				pWinner->SetPlayerBlockpoints(pWinner->GetPlayerBlockpoints() + BlockpointsReward);
				pWinner->SetPlayerPages(pWinner->GetPlayerPages() + PagesReward);
				pWinner->SetPlayerTourneyWins(pWinner->GetPlayerTourneyWin() + 1);
				str_format(aBuf, sizeof(aBuf), "You've received %d blockpoints and %d pages for winning!", BlockpointsReward, PagesReward);
				GameServer()->SendChatTarget(m_Winner, aBuf);
			}

			GameServer()->GetPlayer(m_Winner)->AddExpMultiplier(Config()->m_SvLMBWinnerExpMultiplier, Config()->m_SvLMBWinnerExpMultiplierDuration);
			str_format(aBuf, sizeof(aBuf), "%d%% experience bonus enabled for %d minutes!", Config()->m_SvLMBWinnerExpMultiplier, Config()->m_SvLMBWinnerExpMultiplierDuration);
			GameServer()->GetPlayer(m_Winner)->GiveFlag(Config()->m_SvLMBWinnerExpMultiplierDuration);
			GameServer()->SendChatTarget(m_Winner, aBuf);
		}
		else
		{
			const char *pReason = m_ActiveEndTick <= Server()->Tick() ? "Timelimit" : "Tie";
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "No one has won the %s (%s)", GetEventName(), pReason);
			GameServer()->SendChatTarget(-1, aBuf);
			GameServer()->SendBroadcast(-1, aBuf, false);

			CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
			if(Discord.IsConfigured(g_Config.m_SvDiscordWebhookUrlLmb[0] ? g_Config.m_SvDiscordWebhookUrlLmb : nullptr))
			{
				char aMsg[256];
				str_format(aMsg, sizeof(aMsg), "LMB finished: No winner (%s)", pReason);
				CDiscordWebhook::SSendOptions Opt;
				Opt.m_pWebhookUrl = g_Config.m_SvDiscordWebhookUrlLmb[0] ? g_Config.m_SvDiscordWebhookUrlLmb : nullptr;
				Discord.Send(aMsg, Opt);
			}
		}
	}
	else if(m_FinishingReason == NOT_ENOUGH_CANDIDATES)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Not enough candidates joined %s", GetEventName());
		GameServer()->SendChatTarget(-1, aBuf);
		GameServer()->SendBroadcast(-1, aBuf, false);

		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		if(Discord.IsConfigured(g_Config.m_SvDiscordWebhookUrlLmb[0] ? g_Config.m_SvDiscordWebhookUrlLmb : nullptr))
		{
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = g_Config.m_SvDiscordWebhookUrlLmb[0] ? g_Config.m_SvDiscordWebhookUrlLmb : nullptr;
			Discord.Send("LMB cancelled: Not enough candidates", Opt);
		}
	}
	else if(m_FinishingReason == EMERGENCY)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s finished prematurely", GetEventName());
		GameServer()->SendChatTarget(-1, aBuf);
		GameServer()->SendBroadcast(-1, aBuf, false);

		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		if(Discord.IsConfigured(g_Config.m_SvDiscordWebhookUrlLmb[0] ? g_Config.m_SvDiscordWebhookUrlLmb : nullptr))
		{
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = g_Config.m_SvDiscordWebhookUrlLmb[0] ? g_Config.m_SvDiscordWebhookUrlLmb : nullptr;
			Discord.Send("LMB finished prematurely (emergency)", Opt);
		}
	}

	auto RemainingParticipants = m_Participants;
	for(const auto &ClientId : RemainingParticipants)
		Leave(ClientId);

	if(m_DDRaceTeam != -1)
		GameServer()->m_pController->Teams().ResetRoundState(m_DDRaceTeam);

	if(m_DDRaceTeam != -1)
		GameServer()->m_pController->Teams().SetTeamEvent(m_DDRaceTeam, false);

	SetState(CEventComponent::EEventState::Finished);
}

void CLastManBlockingEvent::ForceNextStage()
{
	if(GetState() == CEventComponent::EEventState::Registration)
		CloseRegistration();
	else if(GetState() == CEventComponent::EEventState::Active)
		FinishEvent(NATURAL);
}

bool CLastManBlockingEvent::CheckEndCondition()
{
	if(Server()->Tick() > m_ActiveEndTick)
	{
		m_Winner = -1;
		return true;
	}
	if(m_Participants.size() == 1)
	{
		m_Winner = m_Participants[0];
		return true;
	}
	if(m_Participants.empty())
	{
		m_Winner = -1;
		return true;
	}
	return false;
}

bool CLastManBlockingEvent::Register(int ClientId)
{
	if(!CheckClientId(ClientId))
		return false;
	if(GetState() != CEventComponent::EEventState::Registration)
	{
		GameServer()->SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	// only logged-in players can join
	CPlayer *pPlayer = GameServer()->GetPlayer(ClientId);
	if(!pPlayer || !pPlayer->IsLoggedIn())
	{
		GameServer()->SendChatTarget(ClientId, "You must be logged in to join this event.");
		return false;
	}
	if(IsCandidate(ClientId))
	{
		GameServer()->SendChatTarget(ClientId, "You already registered to participate.");
		return false;
	}

	m_Candidates.push_back(ClientId);
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You successfully joined %s!", GetEventName());
		GameServer()->SendChatTarget(ClientId, aBuf);
	}
	return true;
}
bool CLastManBlockingEvent::DeRegister(int ClientId)
{
	if(GetState() != CEventComponent::EEventState::Registration)
	{
		GameServer()->SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	auto ClientIdIt = std::find(Candidates().begin(), Candidates().end(), ClientId);
	if(ClientIdIt == Candidates().end())
	{
		GameServer()->SendChatTarget(ClientId, "You aren't registered to participate.");
		return false;
	}

	m_Candidates.erase(ClientIdIt);
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You successfully left %s.", GetEventName());
		GameServer()->SendChatTarget(ClientId, aBuf);
	}
	return true;
}

bool CLastManBlockingEvent::Join(int ClientId)
{
	SaveWeapons(ClientId);
	SavePosition(ClientId);

	m_FrozenSince.emplace(ClientId, 0);
	auto *pChar = GameServer()->GetPlayerChar(ClientId);
	GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);
	pChar->ResetVelocity();
	pChar->FreezeForce(Config()->m_SvLMBInitialFreezeTime);
	GameServer()->Teleport(pChar, m_SpawnPositions[m_SpawnOffset % m_SpawnPositions.size()]);

	if(auto pPlayer = GameServer()->GetPlayer(ClientId))
	{
		// remove all cosmetics and prevent reactivation during the event via cosmetics module checks
		pPlayer->ClearCosmetics();
		pPlayer->SetSkinMani(-1);
		pPlayer->SetGunDesign(-1);
		pPlayer->SetKnockout(-1);
		if(pPlayer->GetCurrentSpecial() != -1)
			pPlayer->ToggleSpecial(pPlayer->GetCurrentSpecial());
	}

	GameServer()->SendBroadcast(" ", ClientId);
	return true;
}
bool CLastManBlockingEvent::Leave(int ClientId)
{
	auto ClientIdIt = std::find(Participants().begin(), Participants().end(), ClientId);
	if(ClientIdIt == Participants().end())
		return false;
	m_Participants.erase(ClientIdIt);
	LoadPosition(ClientId);
	LoadWeapons(ClientId);

	// restore specials state is intentionally left unchanged; players may re-enable after event
	static const char *s_randomStrings[] = {
		"noob!",
		"you're out!",
		"not even ReiTW sux that hard!",
		"better luck next time",
		"i died help",
		"RQ",
		"RIP",
	};
	const int NumMessages = sizeof(s_randomStrings) / sizeof(s_randomStrings[0]);
	int Index = (Server()->Tick() + ClientId) % NumMessages;
	GameServer()->SendChatTarget(ClientId, s_randomStrings[Index]);
	return true;
}

void CLastManBlockingEvent::EmergencyShutdown(const char *pMsg)
{
	CEventComponent::EmergencyShutdown(pMsg);
	if(GetState() != CEventComponent::EEventState::Finished)
		FinishEvent(EMERGENCY);
}

void CLastManBlockingEvent::OnCharacterSpawn(int ClientId, vec2 SpawnPos)
{
	if(GetState() == CEventComponent::EEventState::Active)
	{
		if(IsParticipant(ClientId))
		{
			Leave(ClientId);
			GameServer()->SendChatTarget(ClientId, "You was disqualified!");
			GameServer()->SendBroadcast("You was disqualified!", ClientId);
		}
	}
}
void CLastManBlockingEvent::OnPlayerDropping(int ClientId)
{
	if(GetState() == CEventComponent::EEventState::Active)
	{
		if(IsParticipant(ClientId))
		{
			Leave(ClientId);
		}
	}
	else if(GetState() == CEventComponent::EEventState::Registration)
	{
		if(IsCandidate(ClientId))
		{
			DeRegister(ClientId);
		}
	}
}

bool CLastManBlockingEvent::IsCandidate(int ClientId) const
{
	return std::find(m_Candidates.begin(), m_Candidates.end(), ClientId) != m_Candidates.end();
}
bool CLastManBlockingEvent::IsParticipant(int ClientId) const
{
	return std::find(m_Participants.begin(), m_Participants.end(), ClientId) != m_Participants.end();
}
void CLastManBlockingEvent::CheckFreezeTime()
{
	auto Participants = m_Participants;
	for(const auto &ClientId : Participants)
	{
		auto *pChar = GameServer()->GetPlayerChar(ClientId);
		if(!pChar || !pChar->IsAlive())
			continue;
		int FrozenSince = 0;
		{
			auto itfs = m_FrozenSince.find(ClientId);
			if(itfs != m_FrozenSince.end())
				FrozenSince = itfs->second;
		}
		bool WasFrozenTickBefore = FrozenSince != 0;
		bool IsFrozenNow = pChar->m_FreezeTime;
		if(IsFrozenNow && !WasFrozenTickBefore)
		{
			LogDebug("%d frozen", ClientId);
			SetFrozenSince(ClientId, Server()->Tick());
		}
		if(!IsFrozenNow && WasFrozenTickBefore)
		{
			LogDebug("%d stops being freezed: %d ", ClientId, Server()->Tick() - FrozenSince);
			SetFrozenSince(ClientId, 0);
		}

		// require a configurable continuous freeze duration (in ticks) to eliminate the player
		int RequiredFreezeTicks = Config()->m_SvLMBFreezeTimeout; // configured in ticks (50 ticks = 1 second)
		if(IsFrozenNow && FrozenSince != 0 && Server()->Tick() - FrozenSince >= RequiredFreezeTicks)
		{
			Leave(ClientId);
		}
	}
}
int CLastManBlockingEvent::GetFrozenSince(int ClientId) const
{
	auto it = m_FrozenSince.find(ClientId);
	if(it == m_FrozenSince.end())
		return 0;
	return it->second;
}
void CLastManBlockingEvent::SetFrozenSince(int ClientId, int Tick)
{
	m_FrozenSince[ClientId] = Tick;
}
