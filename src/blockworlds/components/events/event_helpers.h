#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_HELPERS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_HELPERS_H

#include <array>
#include <map>

#include <engine/shared/protocol.h>
#include <game/teamscore.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>

inline void SavePositionHelper(CGameContext *pGameServer, std::map<int, std::unique_ptr<class CSaveTee>> &m_pSavedPlayers, int ClientId)
{
	auto pSavedTee = std::make_unique<CSaveTee>();
	auto *pChar = pGameServer->GetPlayerChar(ClientId);
	if(pChar)
		pSavedTee->Save(pChar, false);

	m_pSavedPlayers.insert_or_assign(ClientId, std::move(pSavedTee));
}

inline void LoadPositionHelper(CGameContext *pGameServer, std::map<int, std::unique_ptr<class CSaveTee>> &m_pSavedPlayers, int ClientId)
{
	auto it = m_pSavedPlayers.find(ClientId);
	CPlayer *pPlayer = pGameServer->GetPlayer(ClientId);

	if(it != m_pSavedPlayers.end())
	{
		if(!pPlayer)
		{
			m_pSavedPlayers.erase(it);
			return;
		}

		CCharacter *pChar = pGameServer->GetPlayerChar(ClientId);
		if(!pChar)
		{
			pChar = pPlayer->ForceSpawn(vec2(0, 0), false);
		}

		if(pChar)
		{
			it->second->Load(pChar, TEAM_FLOCK, false);
			pChar->ResetVelocity();
		}

		// erase entry (unique_ptr will free)
		m_pSavedPlayers.erase(it);
		return;
	}

	if(pPlayer)
	{
		CCharacter *pChar = pGameServer->GetPlayerChar(ClientId);
		if(pChar && pChar->IsAlive())
		{
			pChar->Die(-1, WEAPON_WORLD);
		}
	}
}

inline void SaveWeaponsHelper(CGameContext *pGameServer, std::map<int, std::array<CCharacterCore::WeaponStat, NUM_WEAPONS>> &m_SavedWeapons, int ClientId)
{
	if(const auto it = m_SavedWeapons.find(ClientId); it != m_SavedWeapons.end())
	{
		m_SavedWeapons.erase(it);
	}

	const auto Character = pGameServer->GetPlayerChar(ClientId);
	if(!Character)
		return;

	std::array<CCharacterCore::WeaponStat, NUM_WEAPONS> savedWeapons;
	mem_copy(savedWeapons.data(), Character->Core()->m_aWeapons, sizeof(CCharacterCore::WeaponStat) * NUM_WEAPONS);
	m_SavedWeapons.emplace(ClientId, savedWeapons);

	mem_zero(&Character->Core()->m_aWeapons, sizeof(CCharacterCore::WeaponStat) * NUM_WEAPONS);
	Character->GiveWeapon(WEAPON_HAMMER);
	Character->GiveWeapon(WEAPON_GUN);
}

inline void LoadWeaponsHelper(CGameContext *pGameServer, std::map<int, std::array<CCharacterCore::WeaponStat, NUM_WEAPONS>> &m_SavedWeapons, int ClientId)
{
	const auto it = m_SavedWeapons.find(ClientId);
	if(it == m_SavedWeapons.end())
	{
		return;
	}

	auto Character = pGameServer->GetPlayerChar(ClientId);
	if(!Character)
		return;

	mem_copy(&Character->Core()->m_aWeapons, &it->second, sizeof(CCharacterCore::WeaponStat) * NUM_WEAPONS);
	m_SavedWeapons.erase(it);
}

inline int PlayerHookedGroundForHelper(CGameContext *pGameServer, int ClientId)
{
	auto pChar = pGameServer->GetPlayerChar(ClientId);
	if(!pChar)
		return 0;

	bool HookingGround = pChar->Core()->m_HookState == HOOK_GRABBED && pChar->Core()->HookedPlayer() == -1;

	if(HookingGround)
		return pChar->Core()->m_HookTick;
	return 0;
}

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_HELPERS_H
