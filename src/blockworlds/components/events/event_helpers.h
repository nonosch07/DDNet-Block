#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_HELPERS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_HELPERS_H

#include <engine/shared/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <blockworlds/bw_base.h>
#include <blockworlds/bw_context.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

inline void SavePositionHelper(CGameContext *pGameServer, std::map<int, std::unique_ptr<class CSaveTee>> &MPSavedPlayers, int ClientId)
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
	MPSavedPlayers.insert_or_assign(ClientId, std::move(pSavedTee));
}

inline void LoadPositionHelper(CGameContext *pGameServer, std::map<int, std::unique_ptr<class CSaveTee>> &MPSavedPlayers, int ClientId)
{
	auto It = MPSavedPlayers.find(ClientId);
	CPlayer *pPlayer = pGameServer->Bw().GetPlayer(ClientId);

	if(It != MPSavedPlayers.end())
	{
		if(!pPlayer)
		{
			MPSavedPlayers.erase(It);
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
			It->second->Load(pChar, TEAM_FLOCK);
			pChar->ResetVelocity();
		}

		// erase entry (unique_ptr will free)
		MPSavedPlayers.erase(It);
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

inline void SaveWeaponsHelper(CGameContext *pGameServer, std::map<int, std::array<CCharacterCore::CWeaponStat, NUM_WEAPONS>> &MSavedWeapons, int ClientId)
{
	if(const auto It = MSavedWeapons.find(ClientId); It != MSavedWeapons.end())
	{
		MSavedWeapons.erase(It);
	}

	auto *const Character = pGameServer->GetPlayerChar(ClientId);
	if(!Character)
		return;

	std::array<CCharacterCore::CWeaponStat, NUM_WEAPONS> SavedWeapons;
	mem_copy(SavedWeapons.data(), Character->Core()->m_aWeapons, sizeof(CCharacterCore::CWeaponStat) * NUM_WEAPONS);
	MSavedWeapons.emplace(ClientId, SavedWeapons);

	mem_zero(&Character->Bw().Core().m_aWeapons, sizeof(CCharacterCore::CWeaponStat) * NUM_WEAPONS);
	Character->GiveWeapon(WEAPON_HAMMER);
	Character->GiveWeapon(WEAPON_GUN);
}

inline void LoadWeaponsHelper(CGameContext *pGameServer, std::map<int, std::array<CCharacterCore::CWeaponStat, NUM_WEAPONS>> &MSavedWeapons, int ClientId)
{
	const auto It = MSavedWeapons.find(ClientId);
	if(It == MSavedWeapons.end())
	{
		return;
	}

	auto *Character = pGameServer->GetPlayerChar(ClientId);
	if(!Character)
		return;

	mem_copy(&Character->Bw().Core().m_aWeapons, &It->second, sizeof(CCharacterCore::CWeaponStat) * NUM_WEAPONS);
	MSavedWeapons.erase(It);
}

inline int PlayerHookedGroundForHelper(CGameContext *pGameServer, int ClientId)
{
	auto *pChar = pGameServer->GetPlayerChar(ClientId);
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
	const int Mins = Secs / 60;
	const int RemSecs = Secs % 60;
	if(Mins > 0 && RemSecs > 0)
		str_format(pBuf, BufSize, "%d %s %d %s",
			Mins, Mins == 1 ? "min" : "mins",
			RemSecs, RemSecs == 1 ? "sec" : "secs");
	else if(Mins > 0)
		str_format(pBuf, BufSize, "%d %s", Mins, Mins == 1 ? "min" : "mins");
	else
		str_format(pBuf, BufSize, "%d %s", RemSecs, RemSecs == 1 ? "sec" : "secs");
}

inline vec2 RandomSpawnPos(const std::vector<vec2> &Positions, std::set<int> &UsedIndices)
{
	if(Positions.empty())
		return {0, 0};

	// build the list of positions not yet used this cycle
	std::vector<int> Available;
	for(int i = 0; i < (int)Positions.size(); ++i)
		if(!UsedIndices.contains(i))
			Available.push_back(i);

	// full cycle exhausted -> start a fresh cycle
	if(Available.empty())
	{
		UsedIndices.clear();
		for(int i = 0; i < (int)Positions.size(); ++i)
			Available.push_back(i);
	}

	int Idx = Available[secure_rand_below((int)Available.size())];
	UsedIndices.insert(Idx);
	return Positions[Idx];
}

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_HELPERS_H
