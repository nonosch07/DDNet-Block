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
		if(!pPlayer)
		{
			delete it->second;
			m_pSavedPlayers.erase(it);
			return;
		}

		CCharacter *pChar = GameServer()->GetPlayerChar(ClientId);
		if(!pChar)
		{
			pChar = pPlayer->ForceSpawn(vec2(0, 0), false);
		}

		if(pChar)
		{
			it->second->Load(pChar, TEAM_FLOCK, false);
			pChar->ResetVelocity();
		}

		// free saved tee and erase entry regardless of success to avoid leaks
		delete it->second;
		m_pSavedPlayers.erase(it);
		return;
	}

	if(pPlayer)
	{
		CCharacter *pChar = GameServer()->GetPlayerChar(ClientId);
		if(pChar && pChar->IsAlive())
		{
			pChar->Die(-1, WEAPON_WORLD);
		}
	}
}

void CEventComponent::SaveWeapons(int ClientId) {
	if (const auto it = m_SavedWeapons.find(ClientId); it != m_SavedWeapons.end()) {
		m_SavedWeapons.erase(it);
	}

	const auto Character = GameServer()->GetPlayerChar(ClientId);
	if (!Character)
		return;

	m_SavedWeapons.emplace(ClientId, *Character->Core()->m_aWeapons);

	mem_zero(&Character->Core()->m_aWeapons, sizeof(CCharacterCore::WeaponStat)*NUM_WEAPONS);
	Character->GiveWeapon(WEAPON_HAMMER);
	Character->GiveWeapon(WEAPON_GUN);
}

void CEventComponent::LoadWeapons(int ClientId) {
	const auto it = m_SavedWeapons.find(ClientId);
	if (it == m_SavedWeapons.end()) {
		return;
	}

	auto Character = GameServer()->GetPlayerChar(ClientId);
	if (!Character)
		return;

	mem_copy(&Character->Core()->m_aWeapons, &it->second, sizeof(CCharacterCore::WeaponStat)*NUM_WEAPONS);
	m_SavedWeapons.erase(it);
}

int CEventComponent::PlayerHookedGroundFor(bool ClientId) const {
	auto pChar = GameServer()->GetPlayerChar(ClientId);
	if (!pChar)
		return 0;

	bool HookingGround = pChar->Core()->m_HookState == HOOK_GRABBED && pChar->Core()->HookedPlayer() == -1;

	if (HookingGround)
		return pChar->Core()->m_HookTick;
	return 0;
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
