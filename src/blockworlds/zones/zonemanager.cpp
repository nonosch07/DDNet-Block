#include <base/system.h>

#include <engine/map.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include "nocollisionzone.h"
#include "passivezone.h"
#include "shoppointzone.h"
#include "shopzone.h"
#include "spawnzone.h"
#include <blockworlds/shop/storemanager.h>

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

						dbg_msg("zones", "loaded no-collision zone with %d quads", pQuads->m_NumQuads);
					}
					else if(!m_aZones[ZONE_SPAWN] && str_comp_nocase(aName, "spawn") == 0)
					{
						CSpawnZone *pSpawnZone = new CSpawnZone(GameServer());
						pSpawnZone->Init(pQuads);

						m_aZones[ZONE_SPAWN] = static_cast<IZone *>(pSpawnZone);

						dbg_msg("zones", "loaded spawn zone with %d quads", pQuads->m_NumQuads);
					}
					else if(str_comp_nocase_num(aName, "shop", 4) == 0)
					{
						// aName formats supported:
						// shop
						// shop_<category_number>[_<item_number>]
						// shop_<category_name>[_<item_number>]
						int Category = CShop::CATEGORY_SKINMANI;
						int Item = 0;
						char aBuf[64];
						str_copy(aBuf, aName, sizeof(aBuf));
						// tokenize by '_'
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
						dbg_msg("zones", "loaded shop zone '%s' cat=%d item=%d with %d quads", aName, Category, Item, pQuads->m_NumQuads);
					}
				}
			}
		}

		if(GameServer()->Cosmetics())
		{
			// Skinmani
			for(int i = 0; i < GameServer()->Cosmetics()->NUM_SKINMANIS; ++i)
			{
				if(i < (int)covered_skin.size() && covered_skin[i])
					continue; // already covered by a quad shop
				int Price = 0, Level = 0;
				vec2 PreviewPos;
				if(GameServer()->Cosmetics()->ShopInfoSkinmani(i, Price, Level, PreviewPos))
				{
					CShopPointZone *p = new CShopPointZone(GameServer(), PreviewPos, 32.0f, CShop::CATEGORY_SKINMANI, i);
					m_vExtraZones.push_back(p);
					dbg_msg("zones", "created skinmani shop point zone for item %d at %.1f,%.1f", i, PreviewPos.x, PreviewPos.y);
				}
			}

			// Gundesigns
			for(int i = 0; i < GameServer()->Cosmetics()->NUM_GUNDESIGNS; ++i)
			{
				if(i < (int)covered_gun.size() && covered_gun[i])
					continue;
				int Price = 0, Level = 0;
				vec2 PreviewPos;
				if(GameServer()->Cosmetics()->ShopInfoGundesign(i, Price, Level, PreviewPos))
				{
					CShopPointZone *p = new CShopPointZone(GameServer(), PreviewPos, 32.0f, CShop::CATEGORY_GUNDESIGN, i);
					m_vExtraZones.push_back(p);
					dbg_msg("zones", "created gundesign shop point zone for item %d at %.1f,%.1f", i, PreviewPos.x, PreviewPos.y);
				}
			}

			// Knockouts
			for(int i = 0; i < GameServer()->Cosmetics()->NUM_KNOCKOUTS; ++i)
			{
				if(i < (int)covered_ko.size() && covered_ko[i])
					continue;
				int Price = 0, Level = 0;
				vec2 PreviewPos;
				if(GameServer()->Cosmetics()->ShopInfoKnockout(i, Price, Level, PreviewPos))
				{
					CShopPointZone *p = new CShopPointZone(GameServer(), PreviewPos, 32.0f, CShop::CATEGORY_KNOCKOUT, i);
					m_vExtraZones.push_back(p);
					dbg_msg("zones", "created knockout shop point zone for item %d at %.1f,%.1f", i, PreviewPos.x, PreviewPos.y);
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
