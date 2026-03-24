#include <base/system.h>

#include <engine/map.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include "1on1arenazone.h"
#include "1on1prepzone.h"
#include "nocollisionzone.h"
#include "noexpzone.h"
#include "passivezone.h"
#include "redirectzone.h"
#include "shoppointzone.h"
#include "shopzone.h"
#include "spawnzone.h"
#include <blockworlds/shop/storemanager.h>

#include "zone.h"
#include "zonemanager.h"
#include <array>

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

CZoneManager::~CZoneManager()
{
	for(auto z : m_vExtraZones)
		delete z;
}

void CZoneManager::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;

	int GroupsStart, LayersStart, GroupsNum, LayersNum;
	IMap *pMap = GameServer()->Layers()->Map();

	pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	// prepare covered flags; we'll fill them as we detect quad-based shops
	std::vector<bool> covered_skin;
	std::vector<bool> covered_gun;
	std::vector<bool> covered_ko;

	if(GameServer()->Cosmetics())
	{
		covered_skin.assign(GameServer()->Cosmetics()->NUM_SKINMANIS, false);
		covered_gun.assign(GameServer()->Cosmetics()->NUM_GUNDESIGNS, false);
		covered_ko.assign(GameServer()->Cosmetics()->NUM_KNOCKOUTS, false);
	}

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
					}
					else if(!m_aZones[ZONE_SPAWN] && str_comp_nocase(aName, "spawn") == 0)
					{
						CSpawnZone *pSpawnZone = new CSpawnZone(GameServer());
						pSpawnZone->Init(pQuads);
						m_aZones[ZONE_SPAWN] = static_cast<IZone *>(pSpawnZone);
					}
					else if(!m_aZones[ZONE_NOEXP] && (str_comp_nocase(aName, "no_exp") == 0))
					{
						CNoExpZone *pNoExpZone = new CNoExpZone(GameServer());
						pNoExpZone->Init(pQuads);
						m_aZones[ZONE_NOEXP] = static_cast<IZone *>(pNoExpZone);
					}
					else if(!m_aZones[ZONE_1ON1_PREP] && str_comp_nocase(aName, "1on1_prep") == 0)
					{
						C1on1PrepZone *pPrepZone = new C1on1PrepZone(GameServer());
						pPrepZone->Init(pQuads);
						m_aZones[ZONE_1ON1_PREP] = static_cast<IZone *>(pPrepZone);
						dbg_msg("zones", "loaded 1on1 prep zone with %d quads", pQuads->m_NumQuads);
					}
					else if(str_comp_nocase_num(aName, "1on1_", 5) == 0)
					{
						// any "1on1_*" layer that is not "1on1_prep" becomes a match arena
						int arenaIdx = (int)m_v1on1Arenas.size();
						C1on1ArenaZone *pArenaZone = new C1on1ArenaZone(GameServer(), aName, arenaIdx);
						pArenaZone->Init(pQuads);
						m_vExtraZones.push_back(pArenaZone); // ownership for deletion
						m_v1on1Arenas.push_back(pArenaZone); // indexed access (borrowed)
						dbg_msg("zones", "loaded 1on1 arena zone '%s' (idx=%d) with %d quads", aName, arenaIdx, pQuads->m_NumQuads);
					}
					else if(str_comp_nocase_num(aName, "redi_", 5) == 0)
					{
						const char *pPortStr = aName + 5;
						if(pPortStr[0] != '\0' && isdigit(pPortStr[0]))
						{
							int Port = str_toint(pPortStr);
							if(Port >= 1024 && Port <= 65535)
							{
								CRedirectZone *pRedirectZone = new CRedirectZone(GameServer(), Port);
								pRedirectZone->Init(pQuads);
								m_vExtraZones.push_back(pRedirectZone);
								dbg_msg("zones", "loaded redirect zone '%s' -> port %d with %d quads", aName, Port, pQuads->m_NumQuads);
							}
							else
							{
								dbg_msg("zones", "redirect zone '%s' has invalid port %d (must be 1024-65535)", aName, Port);
							}
						}
						else
						{
							dbg_msg("zones", "redirect zone '%s' has no valid port number", aName);
						}
					}
					else if(str_comp_nocase_num(aName, "shop", 4) == 0)
					{
						int Category = CShop::CATEGORY_SKINMANI;
						int Item = 0;
						char aBuf[64];
						str_copy(aBuf, aName, sizeof(aBuf));
						char *tokens[4] = {0};
						int t = 0;
						char *tok = strtok(aBuf, "_");
						while(tok && t < 4)
						{
							tokens[t++] = tok;
							tok = strtok(nullptr, "_");
						}

						// tokens[0] == "shop"
						if(t >= 2 && tokens[1])
						{
							if(isdigit(tokens[1][0]))
							{
								Category = atoi(tokens[1]);
							}
							else
							{
								if(str_comp_nocase(tokens[1], "skin") == 0 || str_comp_nocase(tokens[1], "skinmani") == 0)
									Category = CShop::CATEGORY_SKINMANI;
								else if(str_comp_nocase(tokens[1], "gundesign") == 0 || str_comp_nocase(tokens[1], "gun") == 0)
									Category = CShop::CATEGORY_GUNDESIGN;
								else if(str_comp_nocase(tokens[1], "knockout") == 0 || str_comp_nocase(tokens[1], "ko") == 0)
									Category = CShop::CATEGORY_KNOCKOUT;
								else if(str_comp_nocase(tokens[1], "kit") == 0)
								{
									Category = CShop::CATEGORY_UTILITY;
									Item = 0; // weaponkit item
								}
								else if(str_comp_nocase(tokens[1], "page") == 0)
								{
									Category = CShop::CATEGORY_UTILITY;
									Item = 1; // deathnote page item
								}
								else
								{
									Category = atoi(tokens[1]);
								}
							}
						}
						if(t >= 3 && tokens[2] && isdigit(tokens[2][0]))
							Item = atoi(tokens[2]);
						CShopZone *pShopZone = new CShopZone(GameServer(), Category, Item);
						pShopZone->Init(pQuads);
						m_vExtraZones.push_back(pShopZone);
						// mark covered
						if(GameServer()->Cosmetics())
						{
							if(Category == CShop::CATEGORY_SKINMANI && Item >= 0 && Item < (int)covered_skin.size())
								covered_skin[Item] = true;
							else if(Category == CShop::CATEGORY_GUNDESIGN && Item >= 0 && Item < (int)covered_gun.size())
								covered_gun[Item] = true;
							else if(Category == CShop::CATEGORY_KNOCKOUT && Item >= 0 && Item < (int)covered_ko.size())
								covered_ko[Item] = true;
						}
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
	for(auto *pZone : m_vExtraZones)
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
	for(auto *pZone : m_vExtraZones)
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

std::vector<vec2> CZoneManager::Get1on1PrepPositions() const
{
	auto *pPrep = m_aZones[ZONE_1ON1_PREP];
	if(pPrep && pPrep->IsEnabled())
	{
		auto positions = static_cast<C1on1PrepZone *>(pPrep)->GetSpawnPositions();
		if(!positions.empty())
			return positions;
	}
	// fallback: use positions from the first arena
	if(!m_v1on1Arenas.empty())
		return m_v1on1Arenas[0]->GetSpawnPositions();
	return {};
}

const char *CZoneManager::Get1on1ArenaName(int idx) const
{
	if(idx >= 0 && idx < (int)m_v1on1Arenas.size())
		return m_v1on1Arenas[idx]->GetDisplayName();
	return "Unknown";
}

std::vector<vec2> CZoneManager::Get1on1ArenaPositions(int idx) const
{
	if(idx >= 0 && idx < (int)m_v1on1Arenas.size())
		return m_v1on1Arenas[idx]->GetSpawnPositions();
	// -1 or out of range: pool positions from all arenas
	std::vector<vec2> all;
	for(auto *pArena : m_v1on1Arenas)
	{
		auto pos = pArena->GetSpawnPositions();
		all.insert(all.end(), pos.begin(), pos.end());
	}
	return all;
}

std::vector<vec2> CZoneManager::GetNamedQuadCenters(const char *pName) const
{
	std::vector<vec2> centers;
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

					if(str_comp_nocase(aName, pName) == 0)
					{
						auto *pQuadData = static_cast<CQuad *>(pMap->GetData(pQuads->m_Data));
						for(int q = 0; q < pQuads->m_NumQuads; q++)
						{
							CQuad &Q = pQuadData[q];
							// compute center average of four points
							vec2 center = {0, 0};
							for(int i = 0; i < 4; ++i)
								center += vec2{fx2f(Q.m_aPoints[i].x), fx2f(Q.m_aPoints[i].y)};
							center /= 4.0f;
							centers.push_back(center);
						}
						// Continue searching to support multiple layers with the same name
					}
				}
			}
		}
	}
	return centers;
}

std::vector<std::array<vec2, 4>> CZoneManager::GetNamedQuads(const char *pName) const
{
	std::vector<std::array<vec2, 4>> quads;
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

					if(str_comp_nocase(aName, pName) == 0)
					{
						auto *pQuadData = static_cast<CQuad *>(pMap->GetData(pQuads->m_Data));
						for(int q = 0; q < pQuads->m_NumQuads; q++)
						{
							CQuad &Q = pQuadData[q];
							std::array<vec2, 4> corners;
							for(int i = 0; i < 4; ++i)
								corners[i] = vec2{fx2f(Q.m_aPoints[i].x), fx2f(Q.m_aPoints[i].y)};
							quads.push_back(corners);
						}
						// Continue searching to support multiple layers with the same name
					}
				}
			}
		}
	}
	return quads;
}
