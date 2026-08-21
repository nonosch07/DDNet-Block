#include "event.h"
#include "game/teamscore.h"

#include <algorithm>
#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <blockworlds/bw_context.h>

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

	if(NewState == EEventState::Active)
	{
		for(int ClientId : m_Participants)
			SaveAndClearCosmetics(ClientId);
	}
	else if(NewState == EEventState::Ending)
	{
		auto Saved = m_SavedCosmetics;
		for(auto &[ClientId, _] : Saved)
			RestoreCosmetics(ClientId);
	}
}

void CEventComponent::SaveAndClearCosmetics(int ClientId)
{
	if(m_SavedCosmetics.count(ClientId) > 0)
		return;

	CPlayer *pPlayer = GameServer()->Bw().GetPlayer(ClientId);
	if(!pPlayer)
		return;

	SCosmeticsSnapshot Snap;
	Snap.m_Special = pPlayer->Bw().GetCurrentSpecial();
	Snap.m_SkinMani = pPlayer->Bw().GetSkinMani();
	Snap.m_GunDesign = pPlayer->Bw().GetGunDesign();
	Snap.m_Knockout = pPlayer->Bw().GetKnockout();
	Snap.m_FlagExpireTick = pPlayer->Bw().GetFlagExpireTick();
	m_SavedCosmetics[ClientId] = Snap;

	pPlayer->Bw().DisableCosmeticsForEvent();
}

void CEventComponent::RestoreCosmetics(int ClientId)
{
	auto It = m_SavedCosmetics.find(ClientId);
	if(It == m_SavedCosmetics.end())
		return;

	const SCosmeticsSnapshot &Snap = It->second;
	CPlayer *pPlayer = GameServer()->Bw().GetPlayer(ClientId);
	if(pPlayer)
	{
		pPlayer->Bw().SetSkinMani(Snap.m_SkinMani);
		pPlayer->Bw().SetGunDesign(Snap.m_GunDesign);
		pPlayer->Bw().SetKnockout(Snap.m_Knockout);
		if(Snap.m_Special >= 0)
			pPlayer->Bw().ToggleSpecial(Snap.m_Special);
		if(Snap.m_FlagExpireTick > Server()->Tick())
		{
			int RemainingTicks = Snap.m_FlagExpireTick - Server()->Tick();
			int RemainingMinutes = RemainingTicks / Server()->TickSpeed() / 60;
			if(RemainingMinutes > 0)
				pPlayer->Bw().GiveFlag(RemainingMinutes);
		}
	}

	m_SavedCosmetics.erase(It);
}

void CEventComponent::OnPlayerDropping(int ClientId)
{
	RestoreCosmetics(ClientId);
	OnEventPlayerDropping(ClientId);
}

// Delegate position/weapons/hook helpers to centralized inline helpers
#include "event_helpers.h"

void CEventComponent::SavePosition(int ClientId)
{
	SavePositionHelper(GameServer(), m_pSavedPlayers, ClientId);
}

void CEventComponent::LoadPosition(int ClientId)
{
	m_DeferredLoadQueue.push_back(ClientId);
}

void CEventComponent::SaveWeapons(int ClientId)
{
	SaveWeaponsHelper(GameServer(), m_SavedWeapons, ClientId);
}

void CEventComponent::LoadWeapons(int ClientId)
{
	m_DeferredWeaponsQueue.push_back(ClientId);
}

void CEventComponent::OnTick()
{
	#ifdef CONF_DEBUG
	if(g_Config.m_SvEventsTestMode && m_State == EEventState::Registration)
	{
#ifdef CONF_DEBUG
		const int NeedDummies = GetMinCandidates();

		if(g_Config.m_DbgDummies < NeedDummies)
			g_Config.m_DbgDummies = NeedDummies;

		// autorregister (they occupy the top client IDs)
		const int MaxClients = Server()->MaxClients();
		for(int i = MaxClients - g_Config.m_DbgDummies; i < MaxClients; ++i)
		{
			if(GameServer()->Bw().GetPlayer(i) && std::find(m_Candidates.begin(), m_Candidates.end(), i) == m_Candidates.end())
				Register(i);
		}
#endif
	}
	#endif
	// process both queues independently and requeue items that could not be completed yet
	// this makes restoration robust whenn a player has no character this tick (e.g., in spec, paused, timing issues)

	if(!m_DeferredLoadQueue.empty())
	{
		std::vector<int> Queue = std::move(m_DeferredLoadQueue);
		m_DeferredLoadQueue.clear();

		for(int ClientId : Queue)
		{
			// first atttempt
			LoadPositionHelper(GameServer(), m_pSavedPlayers, ClientId);

			// if the saved entry still exists after attempting to load, it means we couldn't apply it yet (no character spawned)
			// requeue for the next tick
			if(m_pSavedPlayers.find(ClientId) != m_pSavedPlayers.end())
			{
				m_DeferredLoadQueue.push_back(ClientId);
			}
		}
	}

	if(!m_DeferredWeaponsQueue.empty())
	{
		std::vector<int> WeaponQueue = std::move(m_DeferredWeaponsQueue);
		m_DeferredWeaponsQueue.clear();

		for(int ClientId : WeaponQueue)
		{
			if(GameServer()->Bw().GetPlayer(ClientId) == nullptr)
			{
				m_SavedWeapons.erase(ClientId);
				continue;
			}

			LoadWeaponsHelper(GameServer(), m_SavedWeapons, ClientId);

			if(m_SavedWeapons.find(ClientId) != m_SavedWeapons.end())
			{
				m_DeferredWeaponsQueue.push_back(ClientId);
			}
		}
	}
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
