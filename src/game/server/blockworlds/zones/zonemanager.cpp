#include <base/system.h>

#include <engine/map.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include "passivezone.h"
#include "nocollisionzone.h"
#include "spawnzone.h"

#include "zone.h"
#include "zonemanager.h"

inline void IntsToStr(const int *pInts, int Num, char *pStr)
{
	while(Num)
	{
		pStr[0] = (((*pInts) >> 24) & 0xff) - 128;
		pStr[1] = (((*pInts) >> 16) & 0xff) - 128;
		pStr[2] = (((*pInts) >> 8) & 0xff) - 128;
		pStr[3] = ((*pInts) & 0xff) - 128;
		pStr += 4;
		pInts++;
		Num--;
	}

#if defined(__GNUC__) && __GNUC__ >= 7
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow" // false positive
#endif
	// null terminate
	pStr[-1] = 0;
#if defined(__GNUC__) && __GNUC__ >= 7
#pragma GCC diagnostic pop
#endif
}

CZoneManager::CZoneManager()
{
	m_pGameServer = nullptr;
	mem_zero(m_aZones, sizeof(m_aZones));
}

void CZoneManager::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;

	int GroupsStart, LayersStart, GroupsNum, LayersNum;
	IMap *pMap = GameServer()->Layers()->Map();

	pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	for(int g = 0; g < GroupsNum; g++)
	{
		auto *pGroup = static_cast<CMapItemGroup *>(pMap->GetItem(GroupsStart + g));

		char aGroupName[12];
		IntsToStr(pGroup->m_aName, 3, aGroupName);

		if(str_comp_nocase(aGroupName, "game_zones") == 0)
		{
			for(int l = 0; l < pGroup->m_NumLayers; l++)
			{
				auto *pLayer = static_cast<CMapItemLayer *>(pMap->GetItem(LayersStart + pGroup->m_StartLayer + l));

				if(pLayer->m_Type == LAYERTYPE_QUADS)
				{
					CMapItemLayerQuads *pQuads = reinterpret_cast<CMapItemLayerQuads *>(pLayer);

					char aName[12];
					IntsToStr(pQuads->m_aName, 3, aName);

					if(!m_aZones[ZONE_PASSIVE] && str_comp_nocase(aName, "passive") == 0)
					{
						CPassiveZone *pProtectionZone = new CPassiveZone(GameServer());
						pProtectionZone->Init(pQuads);

						m_aZones[ZONE_PASSIVE] = static_cast<IZone *>(pProtectionZone);

						dbg_msg("zones", "loaded passive zone with %d quads", pQuads->m_NumQuads);
					}
					else if(!m_aZones[ZONE_NOCOLL] && str_comp_nocase(aName, "no_coll") == 0)
					{
						CNoCollisionZone *pNoCollZone = new CNoCollisionZone(GameServer());
						pNoCollZone->Init(pQuads);

						m_aZones[ZONE_NOCOLL] = static_cast<IZone *>(pNoCollZone);

						dbg_msg("zones", "loaded no-collision zone with %d quads", pQuads->m_NumQuads);
					}
					else if(!m_aZones[ZONE_SPAWN] && str_comp_nocase(aName, "spawn") == 0)
					{
						CSpawnZone *pSpawnZone = new CSpawnZone(GameServer());
						pSpawnZone->Init(pQuads);

						m_aZones[ZONE_SPAWN] = static_cast<IZone *>(pSpawnZone);

						dbg_msg("zones", "loaded spawn zone with %d quads", pQuads->m_NumQuads);
					}
				}
			}
		}
	}
}

void CZoneManager::Tick()
{
	for(auto *pZone : m_aZones)
	{
		if(pZone && pZone->IsEnabled())
			pZone->Tick();
	}
}

void CZoneManager::Snap(int ClientID)
{
	for(auto *pZone : m_aZones)
	{
		if(pZone && pZone->IsEnabled())
			pZone->Snap(ClientID);
	}
}

IZone *CZoneManager::GetZone(int ZoneType)
{
	if(ZoneType >= 0 && ZoneType < NUM_ZONES)
		return m_aZones[ZoneType];
	return nullptr;
}