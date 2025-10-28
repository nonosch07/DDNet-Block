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

CEventComponent::~CEventComponent()
{
	// Clear saved tees and weapons; unique_ptr will free saved tees automatically
	m_pSavedPlayers.clear();
	m_SavedWeapons.clear();
}

void CEventComponent::SetState(CEventComponent::EEventState NewState)
{
	EEventState OldState;
	std::function<void(EEventState, EEventState)> cb;
	{
		// protect state change and copy callback while holding lock on derived classes if they expose mutex
		// CEventComponent doesn't have its own mutex; derived classes may override behavior. We simply update state and call callback.
		OldState = m_State;
		if(OldState == NewState)
			return;
		m_State = NewState;
		cb = m_pfnOnStateChange;
	}

	if(cb)
		cb(OldState, NewState);
}

// Delegate position/weapons/hook helpers to centralized inline helpers
#include "event_helpers.h"

void CEventComponent::SavePosition(int ClientId)
{
	SavePositionHelper(GameServer(), m_pSavedPlayers, ClientId);
}

void CEventComponent::LoadPosition(int ClientId)
{
	LoadPositionHelper(GameServer(), m_pSavedPlayers, ClientId);
}

void CEventComponent::SaveWeapons(int ClientId)
{
	SaveWeaponsHelper(GameServer(), m_SavedWeapons, ClientId);
}

void CEventComponent::LoadWeapons(int ClientId)
{
	LoadWeaponsHelper(GameServer(), m_SavedWeapons, ClientId);
}

int CEventComponent::PlayerHookedGroundFor(int ClientId) const
{
	return PlayerHookedGroundForHelper(GameServer(), ClientId);
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
