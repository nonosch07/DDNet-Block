#include "lmb.h"

#include <engine/shared/config.h>

#include <game/teamscore.h>
#include <game/mapitems.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/entities/character.h>
#include <game/server/player.h>

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/promises.h>

CLastManBlockingEvent::CLastManBlockingEvent(CGameContext *pGameContext) :
	CEventComponent(pGameContext), m_SpawnOffset(0), m_Timelimit(-1), m_DDRaceTeam(-1), m_Winner(-1), m_FinishingReason(NATURAL)
{
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

	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(CheckEndCondition())
		FinishEvent(NATURAL);
}

void CLastManBlockingEvent::OpenRegistration()
{
	m_Candidates.clear();
	SetState(CEventComponent::EEventState::Registration);
	auto pPromises = Registry()->Get<CPromises>();
	if(pPromises)
		pPromises->AddPromise(
			Server()->Tick() + Config()->m_SvLMBRegistrationTime * Server()->TickSpeed(),
			shared_from_this(),
			[](std::shared_ptr<void> pUserData){
				auto pThis = std::static_pointer_cast<CLastManBlockingEvent>(pUserData);
				pThis->CloseRegistration();
			});
	else
		EmergencyShutdown("Promises are not plugged in"); // TODO: dependencies
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
	Teams.SetTeamLock(m_DDRaceTeam, true);
	Teams.SetTeamInvitesOpen(m_DDRaceTeam, false);

	m_SpawnOffset = 0;
	m_pSavedPlayers.clear();
	for(const auto &item : m_Participants)
	{
		Join(item);
		m_SpawnOffset++;
	}

	SetState(CEventComponent::EEventState::Active);
	m_StartTick = Server()->Tick();
	m_Timelimit = m_StartTick + Config()->m_SvLMBActiveTime * Server()->TickSpeed();
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
			const char *pReason = m_Timelimit <= Server()->Tick() ? "Timelimit" :
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
	for(const auto& ClientId : RemainingParticipants)
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
	if(Server()->Tick() > m_StartTick + m_Timelimit)
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
	GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, TEAM_FLOCK);
	return true;
}

void CLastManBlockingEvent::EmergencyShutdown(const char *pMsg)
{
	CEventComponent::EmergencyShutdown(pMsg);
	if(GetState() != CEventComponent::EEventState::Finished)
		FinishEvent(EMERGENCY);
}
