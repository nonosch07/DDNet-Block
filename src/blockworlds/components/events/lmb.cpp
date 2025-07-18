#include "lmb.h"

#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <blockworlds/components/core/component_registry.h>

CLastManBlockingEvent::CLastManBlockingEvent(CGameContext *pGameContext) :
	CEventComponent(pGameContext), m_SpawnOffset(0), m_DDRaceTeam(-1), m_Winner(-1), m_FinishingReason(NATURAL)
{
	m_ActiveEndTick = -1;
	m_RegistrationEndTick = -1;

	m_SpawnPositions.clear();
	int Found = CGameContext::GetTilePositions(TILE_BW_LMB_START_POS, GameServer(), m_SpawnPositions);
	if(Found == 0)
	{
		EmergencyShutdown("Map has no LMB start tiles");
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
				Candidates().size() < 2 ? "Not enough candidates!" : "",
				"                                                                                     "
				"                                                                                     "
				"                                                                                     ");
		}
	}
	else if(GetState() == CEventComponent::EEventState::Active)
	{
		if(Server()->Tick() % Config()->m_SvLMBBroadcastRate == 0)
			for(const auto &item : Participants())
				GameServer()->SendBroadcast(item, "Participants left: %" PRIzu "\n"
								  "Time left: %d seconds\n"
								  "%s",
					Participants().size(),
					(int)((m_ActiveEndTick - Server()->Tick()) / Server()->TickSpeed()),
					"                                                                                     "
					"                                                                                     "
					"                                                                                     ");
		if(CheckEndCondition())
			FinishEvent(NATURAL);
	}
}

void CLastManBlockingEvent::OnSnapClientInfo(int ClientId, int SnappingClient, struct CNetObj_ClientInfo *pClientInfo)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(!Server()->ClientAuthed(ClientId) && IsParticipant(SnappingClient))
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

	if(IsParticipant(ClientId) && IsParticipant(SnappingClient))
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

	if(m_Candidates.size() < 2)
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
	Teams.SetTeamLock(m_DDRaceTeam, false);
	Teams.SetTeamInvitesOpen(m_DDRaceTeam, false);

	m_SpawnOffset = 0;
	m_pSavedPlayers.clear();
	for(const auto &item : m_Participants)
	{
		Join(item);
		m_SpawnOffset++;
	}

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
			// lame
			GameServer()->SendChatTarget(-1, "'%s' has won the %s", Server()->ClientName(m_Winner), GetEventName());
			GameServer()->SendBroadcast(-1, "'%s' has won the %s", Server()->ClientName(m_Winner), GetEventName());

			GameServer()->GetPlayer(m_Winner)->AddExpMultiplier(Config()->m_SvLMBWinnerExpMultiplier, Config()->m_SvLMBWinnerExpMultiplierDuration);
			GameServer()->SendChatTarget(m_Winner, "%d%% experience bonus enabled for %d minutes!", Config()->m_SvLMBWinnerExpMultiplier, Config()->m_SvLMBWinnerExpMultiplierDuration);
		}
		else
		{
			const char *pReason = m_ActiveEndTick <= Server()->Tick() ? "Timelimit" :
										"Tie";

			GameServer()->SendChatTarget(-1, "No one has won the %s (%s)", GetEventName(), pReason);
			GameServer()->SendBroadcast(-1, "No one has won the %s (%s)", GetEventName(), pReason);
		}
	}
	else if(m_FinishingReason == NOT_ENOUGH_CANDIDATES)
	{
		GameServer()->SendChatTarget(-1, "Not enough candidates joined %s", GetEventName());
		GameServer()->SendBroadcast(-1, "Not enough candidates joined %s", GetEventName());
	}
	else if(m_FinishingReason == EMERGENCY)
	{
		GameServer()->SendChatTarget(-1, "%s finished prematurely", GetEventName());
		GameServer()->SendBroadcast(-1, "%s finished prematurely", GetEventName());
	}

	auto RemainingParticipants = m_Participants;
	for(const auto &ClientId : RemainingParticipants)
		Leave(ClientId);

	if(m_DDRaceTeam != -1)
		GameServer()->m_pController->Teams().ResetRoundState(m_DDRaceTeam);

	SetState(CEventComponent::EEventState::Finished);
}

void CLastManBlockingEvent::ForceNextStage()
{
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

bool CLastManBlockingEvent::CanPlayerRegister(int ClientId) const
{
	if(!CheckClientId(ClientId))
		return false;
	if(GetState() != CEventComponent::EEventState::Registration)
		return false;
	if(std::find(Candidates().begin(), Candidates().end(), ClientId) != Candidates().end())
		return false;
	return true;
}

bool CLastManBlockingEvent::Register(int ClientId)
{
	if(!CanPlayerRegister(ClientId))
		return false;
	m_Candidates.push_back(ClientId);
	GameServer()->SendChatTarget(ClientId, "You successfully joined %s!", GetEventName());
	return true;
}
bool CLastManBlockingEvent::DeRegister(int ClientId)
{
	auto ClientIdIt = std::find(Participants().begin(), Participants().end(), ClientId);
	if(ClientIdIt == Participants().end())
		return false;
	m_Candidates.erase(ClientIdIt);
	GameServer()->SendChatTarget(ClientId, "You successfully left %s.", GetEventName());
	return true;
}

bool CLastManBlockingEvent::Join(int ClientId)
{
	for(const auto &item : m_Participants)
		SavePosition(item);

	auto *pChar = GameServer()->GetPlayerChar(ClientId);
	GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);
	pChar->ResetVelocity();
	pChar->Freeze(Config()->m_SvLMBInitialFreezeTime);
	GameServer()->Teleport(pChar, m_SpawnPositions[m_SpawnOffset % m_SpawnPositions.size()]);

	GameServer()->SendBroadcast(ClientId, " ");
	return true;
}
bool CLastManBlockingEvent::Leave(int ClientId)
{
	auto ClientIdIt = std::find(Participants().begin(), Participants().end(), ClientId);
	if(ClientIdIt == Participants().end())
		return false;
	m_Participants.erase(ClientIdIt);
	LoadPosition(ClientId);

	return true;
}

void CLastManBlockingEvent::EmergencyShutdown(const char *pMsg)
{
	CEventComponent::EmergencyShutdown(pMsg);
	if(GetState() != CEventComponent::EEventState::Finished)
		FinishEvent(EMERGENCY);
}
