#include "preview.h"
#include "base/vmath.h"
#include "engine/shared/protocol.h"
#include "game/mapitems.h"
#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CShopPreview::CShopPreview(CGameContext *pGameContext) :
    m_pGameContext(pGameContext), m_LastUpdateTime(0)  // Initialize member variables
{
}

void CShopPreview::Init(CGameContext *pGameServer)
{
    m_pGameContext = pGameServer;
    DisplayCosmetics();
}

void CShopPreview::Tick()
{
    if (GameServer()->Server()->Tick() - m_LastUpdateTime >= GameServer()->Server()->TickSpeed() * 5) 
    {
        m_LastUpdateTime = GameServer()->Server()->Tick();
        DisplayCosmetics();
    }
}

void CShopPreview::DisplayCosmetics()
{
    DisplayGundesign();
    DisplayKnockouts();
    // DisplaySkinmanis(); // not needed atm
}

void CShopPreview::DisplaySkinmanis()
{
	/*
		for (int i = 0; i < GameServer()->Cosmetics()->NUM_SKINMANIS; i++)
		{
			int Price = 0, Level = 0;
			vec2 Position;

			if (m_pGameContext->Cosmetics()->ShopInfoSkinmani(i, Price, Level, Position))
			{
				int DummyID = GameServer()->GetNextClientID();
				if (DummyID < 0 || DummyID >= MAX_CLIENTS)
					continue;

				if (GameServer()->m_apPlayers[DummyID])
					continue;

				GameServer()->m_apPlayers[DummyID] = new(DummyID) CPlayer(GameServer(), GameServer()->NextUniqueClientId, DummyID, TEAM_RED);
				GameServer()->NextUniqueClientId += 1;
				GameServer()->OnClientConnected(DummyID, 0);
				GameServer()->Server()->BotJoin(DummyID, "");
				GameServer()->m_apPlayers[DummyID]->m_IsNpc = true;

				GameServer()->Cosmetics()->ToggleSkinmani(DummyID, GameServer()->Cosmetics()->ms_SkinmaniNames[i]);
			}
		}
	*/
}

void CShopPreview::DisplayGundesign()
{
    for (int i = 0; i < GameServer()->Cosmetics()->NUM_GUNDESIGNS; i++)
    {
        int Price = 0, Level = 0;
        vec2 Position;

        if (m_pGameContext->Cosmetics()->ShopInfoGundesign(i, Price, Level, Position))
                GameServer()->Cosmetics()->DoGundesignRaw(Position, i, vec2(-1, 0));
    }
}

void CShopPreview::DisplayKnockouts()
{
    for (int i = 0; i < GameServer()->Cosmetics()->NUM_KNOCKOUTS; i++)
    {
        int Price = 0, Level = 0;
        vec2 Position;

        if (m_pGameContext->Cosmetics()->ShopInfoKnockout(i, Price, Level, Position))
                GameServer()->Cosmetics()->DoKnockoutEffectRaw(Position, i);
    }
}
