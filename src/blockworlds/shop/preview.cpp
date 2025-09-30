#include "preview.h"
#include "engine/server.h"
#include "game/server/entities/character.h"
#include "game/server/entities/pickup.h"
#include "game/server/entity.h"
#include "game/server/player.h"
#include "npcmanager.h"
#include <game/server/gamecontext.h>

#include <cmath> // for fabs

CShopPreview::CShopPreview() :
	m_pGameContext(nullptr),
	m_pCosmeticsHandler(nullptr),
	m_pPlayer(nullptr),
	m_LastUpdateTime(0)
{
	m_pNpcManager = new CNpcManager();
}

CShopPreview::CShopPreview(CGameContext *pGameContext) :
	CShopPreview() // delegate to the default constructor - dont understand ddnet code but its needed smh
{
	m_pGameContext = pGameContext;
	m_pNpcManager->Init(pGameContext);
}

CShopPreview::~CShopPreview()
{
	if(m_pNpcManager)
	{
		m_pNpcManager->RemoveAll();
		delete m_pNpcManager;
		m_pNpcManager = nullptr;
	}
}

void CShopPreview::Init(CGameContext *pGameServer)
{
	m_pGameContext = pGameServer;
	if(m_pNpcManager)
		m_pNpcManager->Init(pGameServer);
}

void CShopPreview::Tick()
{
	const int TickInterval = GameServer()->Server()->TickSpeed() * 3;
	if(GameServer()->Server()->Tick() - m_LastUpdateTime >= TickInterval)
	{
		m_LastUpdateTime = GameServer()->Server()->Tick();
		DisplayCosmetics();
		DisplayPricesAndLevels();
	}
}

void CShopPreview::DisplayCosmetics()
{
	DisplayGundesign();
	DisplayKnockouts();
	DisplaySkinManipulations();
}

void CShopPreview::DisplayGundesign()
{
	static std::vector<CPickup *> s_Pickups(GameServer()->Cosmetics()->NUM_GUNDESIGNS, nullptr);

	for(int i = 0; i < GameServer()->Cosmetics()->NUM_GUNDESIGNS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;

		if(m_pGameContext->Cosmetics()->ShopInfoGundesign(i, Price, Level, PreviewPos))
		{
			GameServer()->Cosmetics()->DoGundesignRaw(PreviewPos, i, vec2(1, 0));
		}

		if(s_Pickups[i] == nullptr)
		{
			if(i == GameServer()->Cosmetics()->GUNDESIGN_ARMOR)
			{
				s_Pickups[i] = new CPickup(&GameServer()->m_World, 1, 1, 1, 0);
				s_Pickups[i]->m_Pos = PreviewPos;
			}
			else if(i == GameServer()->Cosmetics()->GUNDESIGN_HEART)
			{
				s_Pickups[i] = new CPickup(&GameServer()->m_World, 0, 0, 1, 0);
				s_Pickups[i]->m_Pos = PreviewPos;
			}
			else if(i == GameServer()->Cosmetics()->GUNDESIGN_BLINKING)
			{
				s_Pickups[i] = new CPickup(&GameServer()->m_World, CGameWorld::ENTTYPE_PICKUP, 1, 1, 0);
				s_Pickups[i]->m_Pos = PreviewPos;
			}
		}
	}
}

void CShopPreview::DisplayKnockouts()
{
	for(int i = 0; i < GameServer()->Cosmetics()->NUM_KNOCKOUTS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;

		if(m_pGameContext->Cosmetics()->ShopInfoKnockout(i, Price, Level, PreviewPos))
		{
			GameServer()->Cosmetics()->DoKnockoutEffectRaw(PreviewPos, i);
		}
	}
}

void CShopPreview::DisplaySkinManipulations()
{
	int Num = GameServer()->Cosmetics()->NUM_SKINMANIS;
	if(m_pNpcManager)
		m_pNpcManager->Resize(Num);

	for(int i = 0; i < Num; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;

		if(!m_pGameContext->Cosmetics()->ShopInfoSkinmani(i, Price, Level, PreviewPos))
			continue;

		if(m_pNpcManager)
			m_pNpcManager->EnsureNpcAndApplySkinmani(i, PreviewPos, CCosmeticsHandler::ms_SkinmaniNames[i]);
	}
}

void CShopPreview::DisplayPriceLevel(const vec2 &PreviewPos, int Price, int Level)
{
	char aBuf[128];
	if((GameServer()->Server()->Tick() / (GameServer()->Server()->TickSpeed() * 3)) % 2 == 0)
		str_format(aBuf, sizeof(aBuf), "BP: %d", Price);
	else
		str_format(aBuf, sizeof(aBuf), "LVL: %d", Level);

	GameServer()->Animations()->Laserwrite(aBuf, PreviewPos - vec2(0, -100.0f), 5.0f, GameServer()->Server()->TickSpeed() * 3, true);
}

void CShopPreview::DisplayPricesAndLevels()
{
	for(int i = 0; i < GameServer()->Cosmetics()->NUM_GUNDESIGNS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(m_pGameContext->Cosmetics()->ShopInfoGundesign(i, Price, Level, PreviewPos))
		{
			DisplayPriceLevel(PreviewPos, Price, Level);
		}
	}

	for(int i = 0; i < GameServer()->Cosmetics()->NUM_KNOCKOUTS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(m_pGameContext->Cosmetics()->ShopInfoKnockout(i, Price, Level, PreviewPos))
		{
			DisplayPriceLevel(PreviewPos, Price, Level);
		}
	}

	for(int i = 0; i < GameServer()->Cosmetics()->NUM_SKINMANIS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(m_pGameContext->Cosmetics()->ShopInfoSkinmani(i, Price, Level, PreviewPos))
		{
			DisplayPriceLevel(PreviewPos, Price, Level);
		}
	}
}
