#include "event.h"
#include "game/teamscore.h"

#include <engine/shared/config.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CEventComponent::CEventComponent(CGameContext *pGameServer) :
	CComponent(pGameServer)
{
	m_State = EEventState::Created;

	m_EmergencyShutdown = false;
	m_EmergencyMessage[0] = '\0';
}

void CEventComponent::SetState(CEventComponent::EEventState NewState)
{
	if(m_State == NewState)
		return;

	EEventState OldState = m_State;
	m_State = NewState;

	if(m_pfnOnStateChange)
	{
		m_pfnOnStateChange(OldState, NewState);
	}
}

void CEventComponent::SavePosition(int ClientId)
{
	auto *pSavedTee = new CSaveTee();
	auto *pChar = GameServer()->GetPlayerChar(ClientId);
	if(pChar)
		pSavedTee->Save(pChar, false);

	m_pSavedPlayers.insert_or_assign(ClientId, pSavedTee);
}
void CEventComponent::LoadPosition(int ClientId)
{
	auto it = m_pSavedPlayers.find(ClientId);
	CPlayer *pPlayer = GameServer()->GetPlayer(ClientId);

	if(it != m_pSavedPlayers.end())
	{
		CCharacter *pChar = GameServer()->GetPlayerChar(ClientId);
		if(!pChar && pPlayer)
		{
			pChar = pPlayer->ForceSpawn(vec2(0, 0), false);
		}

		if(pChar)
		{
			it->second->Load(pChar, TEAM_FLOCK, false);
			pChar->ResetVelocity();
			// free saved tee and erase entry
			delete it->second;
			m_pSavedPlayers.erase(it);
		}
		return;
	}

	CCharacter *pChar = GameServer()->GetPlayerChar(ClientId);
	if(pChar && pChar->IsAlive())
	{
		pChar->Die(-1, WEAPON_WORLD);
	}
}
const char *CEventComponent::GetStateName() const
{
	return GetStateName(m_State);
}
const char *CEventComponent::GetStateName(CEventComponent::EEventState State)
{
	switch(State)
	{
	case EEventState::Created:
		return "created";
	case EEventState::Registration:
		return "registration";
	case EEventState::Preparation:
		return "preparation";
	case EEventState::Active:
		return "active";
	case EEventState::Ending:
		return "ending";
	case EEventState::Finished:
		return "finished";
	}
	return "";
}
