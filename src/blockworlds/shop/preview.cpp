#include "preview.h"
#include "engine/server.h"
#include "game/server/entities/character.h"
#include "game/server/entities/pickup.h"
#include "game/server/entity.h"
#include "game/server/player.h"
#include <game/server/gamecontext.h>

#include <cmath> // for fabs

CShopPreview::CShopPreview() :
	m_pGameContext(nullptr),
	m_pCosmeticsHandler(nullptr),
	m_pPlayer(nullptr),
	m_LastUpdateTime(0)
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
	return;
	static std::vector<int> s_FakePlayerIDs(GameServer()->Cosmetics()->NUM_SKINMANIS, -1);
	static std::vector<bool> s_ToggledCosmetics(GameServer()->Cosmetics()->NUM_SKINMANIS, false);
	static std::vector<bool> s_Teleported(GameServer()->Cosmetics()->NUM_SKINMANIS, false);
	static std::vector<int> s_ConnectionTick(GameServer()->Cosmetics()->NUM_SKINMANIS, -1);

	for(int i = 0; i < GameServer()->Cosmetics()->NUM_SKINMANIS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;

		if(!m_pGameContext->Cosmetics()->ShopInfoSkinmani(i, Price, Level, PreviewPos))
			continue;

		if(s_FakePlayerIDs[i] == -1)
		{
			int DummyID = GameServer()->GetNextClientID();
			s_FakePlayerIDs[i] = DummyID;
			CPlayer *pFakePlayer = new(DummyID) CPlayer(GameServer(), GameServer()->m_NextUniqueClientId, DummyID, TEAM_RED);
			GameServer()->m_NextUniqueClientId++;
			GameServer()->m_apPlayers[DummyID] = pFakePlayer;

			GameServer()->OnClientConnected(DummyID, 0);
			GameServer()->Server()->BotJoin(DummyID, "");
			pFakePlayer->m_IsNpc = true;
			pFakePlayer->SetAfk(true);

			s_ConnectionTick[i] = GameServer()->Server()->Tick();
		}

		CPlayer *pPlayer = GameServer()->GetPlayer(s_FakePlayerIDs[i]);
		if(!pPlayer)
			continue;

		if(!s_ToggledCosmetics[i])
		{
			GameServer()->Cosmetics()->ToggleSkinmani(s_FakePlayerIDs[i], CCosmeticsHandler::ms_SkinmaniNames[i]);
			s_ToggledCosmetics[i] = true;
		}

		CCharacter *pChr = pPlayer->GetCharacter();
		if(pChr && !s_Teleported[i])
		{
			if(GameServer()->Server()->Tick() - s_ConnectionTick[i] >= GameServer()->Server()->TickSpeed() * 3)
			{
				GameServer()->Teleport(pChr, PreviewPos);
				s_Teleported[i] = true;
			}
		}
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
