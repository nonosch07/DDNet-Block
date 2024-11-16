#include "preview.h"
#include <game/server/gamecontext.h>

CShopPreview::CShopPreview() :
	m_pGameContext(nullptr), m_pCosmeticsHandler(nullptr), m_pPlayer(nullptr), m_LastUpdateTime(0)
{
}

CShopPreview::CShopPreview(CGameContext *pGameContext) :
	CShopPreview() // delegate to the default constructor - dont understand ddnet code but its needed smh
{
	m_pGameContext = pGameContext;
}

void CShopPreview::Init(CGameContext *pGameServer)
{
	m_pGameContext = pGameServer;
	DisplayCosmetics();
}

void CShopPreview::Tick()
{
	if(GameServer()->Server()->Tick() - m_LastUpdateTime >= GameServer()->Server()->TickSpeed() * 3)
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
}

void CShopPreview::DisplayGundesign()
{
	for(int i = 0; i < GameServer()->Cosmetics()->NUM_GUNDESIGNS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;

		if(m_pGameContext->Cosmetics()->ShopInfoGundesign(i, Price, Level, PreviewPos))
		{
			GameServer()->Cosmetics()->DoGundesignRaw(PreviewPos, i, vec2(1, 0));
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

void CShopPreview::DisplayPricesAndLevels()
{
	char aBuf[128];

	for(int i = 0; i < GameServer()->Cosmetics()->NUM_GUNDESIGNS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;

		if(m_pGameContext->Cosmetics()->ShopInfoGundesign(i, Price, Level, PreviewPos))
		{
			if((GameServer()->Server()->Tick() / GameServer()->Server()->TickSpeed() / 3) % 2 == 0)
				str_format(aBuf, sizeof(aBuf), "BP: %d", Price);
			else
				str_format(aBuf, sizeof(aBuf), "LVL: %d", Level);

			GameServer()->Animations()->Laserwrite(aBuf, PreviewPos - vec2(0, -100.0f), 6.0f, GameServer()->Server()->TickSpeed() * 3, false);
		}
	}

	for(int i = 0; i < GameServer()->Cosmetics()->NUM_KNOCKOUTS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;

		if(m_pGameContext->Cosmetics()->ShopInfoKnockout(i, Price, Level, PreviewPos))
		{
			if((GameServer()->Server()->Tick() / GameServer()->Server()->TickSpeed() / 3) % 2 == 0)
				str_format(aBuf, sizeof(aBuf), "BP: %d", Price);
			else
				str_format(aBuf, sizeof(aBuf), "LVL: %d", Level);

			GameServer()->Animations()->Laserwrite(aBuf, PreviewPos - vec2(0, -100.0f), 6.0f, GameServer()->Server()->TickSpeed() * 3, true);
		}
	}

	for(int i = 0; i < GameServer()->Cosmetics()->NUM_SKINMANIS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;

		if(m_pGameContext->Cosmetics()->ShopInfoSkinmani(i, Price, Level, PreviewPos))
		{
			if((GameServer()->Server()->Tick() / GameServer()->Server()->TickSpeed() / 3) % 2 == 0)
				str_format(aBuf, sizeof(aBuf), "BP: %d", Price);
			else
				str_format(aBuf, sizeof(aBuf), "LVL: %d", Level);

			GameServer()->Animations()->Laserwrite(aBuf, PreviewPos - vec2(0, -100.0f), 6.0f, GameServer()->Server()->TickSpeed() * 3, true);
		}
	}
}
