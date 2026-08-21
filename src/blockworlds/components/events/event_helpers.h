#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_HELPERS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_HELPERS_H

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

#include <blockworlds/bw_base.h>
#include <engine/shared/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/teamscore.h>
#include <blockworlds/bw_context.h>

inline void SavePositionHelper(CGameContext *pGameServer, std::map<int, std::unique_ptr<class CSaveTee>> &m_pSavedPlayers, int ClientId)
{
	auto *pChar = pGameServer->GetPlayerChar(ClientId);
	if(!pChar)
	{
		// Spectators and dead players have nothing to save. Storing an unsaved CSaveTee
		// would make LoadPositionHelper load its uninitialized members into a character
		// later, and m_HookedPlayer and m_ActiveWeapon are used as array indices without
		// bounds checks. Any earlier save is kept, it is still the position to restore.
		return;
	}

	auto pSavedTee = std::make_unique<CSaveTee>();
	pSavedTee->Save(pChar, false);
	m_pSavedPlayers.insert_or_assign(ClientId, std::move(pSavedTee));
}

inline void LoadPositionHelper(CGameContext *pGameServer, std::map<int, std::unique_ptr<class CSaveTee>> &m_pSavedPlayers, int ClientId)
{
	auto it = m_pSavedPlayers.find(ClientId);
	CPlayer *pPlayer = pGameServer->Bw().GetPlayer(ClientId);

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
			// Restoring a saved tee must not look like a real spawn to the events.
			pGameServer->Bw().m_SuppressSpawnEvent = true;
			pChar = pPlayer->ForceSpawn(vec2(0, 0));
			pGameServer->Bw().m_SuppressSpawnEvent = false;
		}

		if(pChar)
		{
			it->second->Load(pChar, TEAM_FLOCK);
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

inline void SaveWeaponsHelper(CGameContext *pGameServer, std::map<int, std::array<CCharacterCore::CWeaponStat, NUM_WEAPONS>> &m_SavedWeapons, int ClientId)
{
	if(const auto it = m_SavedWeapons.find(ClientId); it != m_SavedWeapons.end())
	{
		m_SavedWeapons.erase(it);
	}

	const auto Character = pGameServer->GetPlayerChar(ClientId);
	if(!Character)
		return;

	std::array<CCharacterCore::CWeaponStat, NUM_WEAPONS> savedWeapons;
	mem_copy(savedWeapons.data(), Character->Core()->m_aWeapons, sizeof(CCharacterCore::CWeaponStat) * NUM_WEAPONS);
	m_SavedWeapons.emplace(ClientId, savedWeapons);

	mem_zero(&Character->Bw().Core().m_aWeapons, sizeof(CCharacterCore::CWeaponStat) * NUM_WEAPONS);
	Character->GiveWeapon(WEAPON_HAMMER);
	Character->GiveWeapon(WEAPON_GUN);
}

inline void LoadWeaponsHelper(CGameContext *pGameServer, std::map<int, std::array<CCharacterCore::CWeaponStat, NUM_WEAPONS>> &m_SavedWeapons, int ClientId)
{
	const auto it = m_SavedWeapons.find(ClientId);
	if(it == m_SavedWeapons.end())
	{
		return;
	}

	auto Character = pGameServer->GetPlayerChar(ClientId);
	if(!Character)
		return;

	mem_copy(&Character->Bw().Core().m_aWeapons, &it->second, sizeof(CCharacterCore::CWeaponStat) * NUM_WEAPONS);
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

inline void FormatTimeLeft(char *pBuf, int BufSize, int Secs)
{
	if(Secs <= 0)
	{
		str_copy(pBuf, "0 secs", BufSize);
		return;
	}
	const int mins = Secs / 60;
	const int secs = Secs % 60;
	if(mins > 0 && secs > 0)
		str_format(pBuf, BufSize, "%d %s %d %s",
			mins, mins == 1 ? "min" : "mins",
			secs, secs == 1 ? "sec" : "secs");
	else if(mins > 0)
		str_format(pBuf, BufSize, "%d %s", mins, mins == 1 ? "min" : "mins");
	else
		str_format(pBuf, BufSize, "%d %s", secs, secs == 1 ? "sec" : "secs");
}

inline vec2 RandomSpawnPos(const std::vector<vec2> &positions, std::set<int> &usedIndices)
{
	if(positions.empty())
		return {0, 0};

	// build the list of positions not yet used this cycle
	std::vector<int> available;
	for(int i = 0; i < (int)positions.size(); ++i)
		if(!usedIndices.count(i))
			available.push_back(i);

	// full cycle exhausted -> start a fresh cycle
	if(available.empty())
	{
		usedIndices.clear();
		for(int i = 0; i < (int)positions.size(); ++i)
			available.push_back(i);
	}

	int idx = available[secure_rand_below((int)available.size())];
	usedIndices.insert(idx);
	return positions[idx];
}

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_HELPERS_H
