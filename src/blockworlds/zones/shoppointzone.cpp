#include "shoppointzone.h"

#include <blockworlds/shop/storemanager.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CShopPointZone::CShopPointZone(CGameContext *pGameServer, const vec2 &Pos, float Radius, int Category, int Item) :
	IZone(pGameServer, -1), m_Pos(Pos), m_Radius(Radius), m_Category(Category), m_Item(Item)
{
	mem_zero(m_aWasInZone, sizeof(m_aWasInZone));
}

void CShopPointZone::Tick()
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer)
			continue;
		CCharacter *pChar = pPlayer->GetCharacter();
		if(!pChar)
			continue;

		bool InZone = distance(pChar->m_Pos, m_Pos) <= m_Radius;
		bool WasInZone = m_aWasInZone[i];

		if(InZone && !WasInZone)
		{
			int CurrentTick = GameServer()->Server()->Tick();
			if(pChar->m_PendingPurchase != nullptr ||
				CurrentTick < pChar->m_LastShopTick + (GameServer()->Server()->TickSpeed() * 2))
			{
				// debounce / already pending
			}
			else
			{
				pChar->m_LastShopTick = CurrentTick;
				new CShop(GameServer(), pPlayer, m_Category, m_Item, 15);
			}
		}

		m_aWasInZone[i] = InZone;
	}
}
