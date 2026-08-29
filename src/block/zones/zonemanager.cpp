#include "zonemanager.h"

#include "1on1arenazone.h"
#include "1on1prepzone.h"
#include "movingeffectzone.h"
#include "nocollisionzone.h"
#include "noexpzone.h"
#include "passivezone.h"
#include "redirectzone.h"
#include "shoppointzone.h"
#include "shopzone.h"
#include "spawnzone.h"
#include "zone.h"

#include <engine/map.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include <block/base.h>
#include <block/context.h>
#include <block/shop/storemanager.h>

#include <array>

static inline void IntsToStr(const int *pInts, int Num, char *pStr)
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
	for(auto *pZone : m_vExtraZones)
		delete pZone;
	m_vExtraZones.clear();

	for(auto *&pZone : m_aZones)
	{
		delete pZone;
		pZone = nullptr;
	}
	m_V1on1Arenas.clear(); // borrowed pointers into m_vExtraZones
}

void CZoneManager::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;

	int GroupsStart, LayersStart, GroupsNum, LayersNum;
	IMap *pMap = GameServer()->Layers()->Map();

	pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	// prepare covered flags; we'll fill them as we detect quad-based shops
	std::vector<bool> CoveredSkin;
	std::vector<bool> CoveredGun;
	std::vector<bool> CoveredKo;

	if(GameServer()->Block().Cosmetics())
	{
		CoveredSkin.assign(CCosmeticsHandler::NUM_SKINMANIS, false);
		CoveredGun.assign(CCosmeticsHandler::NUM_GUNDESIGNS, false);
		CoveredKo.assign(CCosmeticsHandler::NUM_KNOCKOUTS, false);
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
						// dbg_msg("zones", "loaded passive zone with %d quads", pQuads->m_NumQuads);
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
					else if(str_comp_nocase(aName, "1on1_prep") == 0)
					{
						// only the first prep layer becomes the prep zone, but a further one
						// must not fall through to the arena branch below either, or players
						// would spawn in the preparation area during a match
						if(!m_aZones[ZONE_1ON1_PREP])
						{
							C1on1PrepZone *pPrepZone = new C1on1PrepZone(GameServer());
							pPrepZone->Init(pQuads);
							m_aZones[ZONE_1ON1_PREP] = static_cast<IZone *>(pPrepZone);
							// dbg_msg("zones", "loaded 1on1 prep zone with %d quads", pQuads->m_NumQuads);
						}
					}
					else if(str_comp_nocase_num(aName, "1on1_", 5) == 0)
					{
						// any "1on1_*" layer that is not "1on1_prep" becomes a match arena
						int ArenaIdx = (int)m_V1on1Arenas.size();
						C1on1ArenaZone *pArenaZone = new C1on1ArenaZone(GameServer(), aName, ArenaIdx);
						pArenaZone->Init(pQuads);
						m_vExtraZones.push_back(pArenaZone); // ownership for deletion
						m_V1on1Arenas.push_back(pArenaZone); // indexed access (borrowed)
						// dbg_msg("zones", "loaded 1on1 arena zone '%s' (idx=%d) with %d quads", aName, arenaIdx, pQuads->m_NumQuads);
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
								// dbg_msg("zones", "loaded redirect zone '%s' -> port %d with %d quads", aName, Port, pQuads->m_NumQuads);
							}
							else
							{
								// dbg_msg("zones", "redirect zone '%s' has invalid port %d (must be 1024-65535)", aName, Port);
							}
						}
						else
						{
							// dbg_msg("zones", "redirect zone '%s' has no valid port number", aName);
						}
					}
					else if(str_comp_nocase(aName, "mv_freeze") == 0)
					{
						CMovingEffectZone *pZone = new CMovingEffectZone(GameServer(), MOVINGEFFECT_FREEZE);
						pZone->InitMoving(pQuads);
						m_vExtraZones.push_back(pZone);
						// dbg_msg("zones", "loaded moving freeze zone with %d quads", pQuads->m_NumQuads);
					}
					else if(str_comp_nocase(aName, "mv_grabme") == 0)
					{
						CMovingEffectZone *pZone = new CMovingEffectZone(GameServer(), MOVINGEFFECT_GRABME);
						pZone->InitMoving(pQuads);
						m_vExtraZones.push_back(pZone);
						// dbg_msg("zones", "loaded moving grabme zone with %d quads", pQuads->m_NumQuads);
					}
					else if(str_comp_nocase(aName, "mv_hook") == 0)
					{
						CMovingEffectZone *pZone = new CMovingEffectZone(GameServer(), MOVINGEFFECT_HOOKABLE);
						pZone->InitMoving(pQuads);
						m_vExtraZones.push_back(pZone);
						// dbg_msg("zones", "loaded moving hookable zone with %d quads", pQuads->m_NumQuads);
					}
					else if(str_comp_nocase_num(aName, "shop", 4) == 0)
					{
						int Category = CShop::CATEGORY_SKINMANI;
						int Item = 0;
						char aBuf[64];
						str_copy(aBuf, aName, sizeof(aBuf));
						char *Tokens[4] = {nullptr};
						int t = 0;
						char *Tok = strtok(aBuf, "_");
						while(Tok && t < 4)
						{
							Tokens[t++] = Tok;
							Tok = strtok(nullptr, "_");
						}

						// tokens[0] == "shop"
						if(t >= 2 && Tokens[1])
						{
							if(isdigit(Tokens[1][0]))
							{
								Category = atoi(Tokens[1]);
							}
							else
							{
								if(str_comp_nocase(Tokens[1], "skin") == 0 || str_comp_nocase(Tokens[1], "skinmani") == 0)
									Category = CShop::CATEGORY_SKINMANI;
								else if(str_comp_nocase(Tokens[1], "gundesign") == 0 || str_comp_nocase(Tokens[1], "gun") == 0)
									Category = CShop::CATEGORY_GUNDESIGN;
								else if(str_comp_nocase(Tokens[1], "knockout") == 0 || str_comp_nocase(Tokens[1], "ko") == 0)
									Category = CShop::CATEGORY_KNOCKOUT;
								else if(str_comp_nocase(Tokens[1], "kit") == 0)
								{
									Category = CShop::CATEGORY_UTILITY;
									Item = CCosmeticsHandler::UTILITY_WEAPONKIT;
								}
								else if(str_comp_nocase(Tokens[1], "page") == 0)
								{
									Category = CShop::CATEGORY_UTILITY;
									Item = CCosmeticsHandler::UTILITY_DEATHNOTE_PAGE;
								}
								else if(str_comp_nocase(Tokens[1], "vip") == 0)
								{
									Category = CShop::CATEGORY_UTILITY;
									Item = CCosmeticsHandler::UTILITY_VIP_WEEK;
								}
								else
								{
									Category = atoi(Tokens[1]);
								}
							}
						}
						if(t >= 3 && Tokens[2] && isdigit(Tokens[2][0]))
							Item = atoi(Tokens[2]);
						CShopZone *pShopZone = new CShopZone(GameServer(), Category, Item);
						pShopZone->Init(pQuads);
						m_vExtraZones.push_back(pShopZone);
						// mark covered
						if(GameServer()->Block().Cosmetics())
						{
							if(Category == CShop::CATEGORY_SKINMANI && Item >= 0 && Item < (int)CoveredSkin.size())
								CoveredSkin[Item] = true;
							else if(Category == CShop::CATEGORY_GUNDESIGN && Item >= 0 && Item < (int)CoveredGun.size())
								CoveredGun[Item] = true;
							else if(Category == CShop::CATEGORY_KNOCKOUT && Item >= 0 && Item < (int)CoveredKo.size())
								CoveredKo[Item] = true;
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
		auto Positions = static_cast<C1on1PrepZone *>(pPrep)->GetSpawnPositions();
		if(!Positions.empty())
			return Positions;
	}
	// fallback: use positions from the first arena
	if(!m_V1on1Arenas.empty())
		return m_V1on1Arenas[0]->GetSpawnPositions();
	return {};
}

const char *CZoneManager::Get1on1ArenaName(int Idx) const
{
	if(Idx >= 0 && Idx < (int)m_V1on1Arenas.size())
		return m_V1on1Arenas[Idx]->GetDisplayName();
	return "Unknown";
}

std::vector<vec2> CZoneManager::Get1on1ArenaPositions(int Idx) const
{
	if(Idx >= 0 && Idx < (int)m_V1on1Arenas.size())
		return m_V1on1Arenas[Idx]->GetSpawnPositions();
	// -1 or out of range: pool positions from all arenas
	std::vector<vec2> All;
	for(auto *pArena : m_V1on1Arenas)
	{
		auto Pos = pArena->GetSpawnPositions();
		All.insert(All.end(), Pos.begin(), Pos.end());
	}
	return All;
}

std::vector<vec2> CZoneManager::GetNamedQuadCenters(const char *pName) const
{
	std::vector<vec2> Centers;
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
							vec2 Center = {0, 0};
							for(int i = 0; i < 4; ++i)
								Center += vec2{fx2f(Q.m_aPoints[i].x), fx2f(Q.m_aPoints[i].y)};
							Center /= 4.0f;
							Centers.push_back(Center);
						}
						// Continue searching to support multiple layers with the same name
					}
				}
			}
		}
	}
	return Centers;
}

std::vector<std::array<vec2, 4>> CZoneManager::GetNamedQuads(const char *pName) const
{
	std::vector<std::array<vec2, 4>> Quads;
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
							std::array<vec2, 4> Corners;
							for(int i = 0; i < 4; ++i)
								Corners[i] = vec2{fx2f(Q.m_aPoints[i].x), fx2f(Q.m_aPoints[i].y)};
							Quads.push_back(Corners);
						}
						// Continue searching to support multiple layers with the same name
					}
				}
			}
		}
	}
	return Quads;
}
