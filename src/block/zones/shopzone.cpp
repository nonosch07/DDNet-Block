#include "shopzone.h"

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <block/shop/storemanager.h>

CShopZone::CShopZone(CGameContext *pGameServer, int Category, int Item) :
	IZone(pGameServer, -1), m_Category(Category), m_Item(Item)
{
	mem_zero(m_aWasInZone, sizeof(m_aWasInZone));
	for(int &i : m_aLastShopTick)
		i = 0;
}

void CShopZone::Tick()
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer)
			continue;
		CCharacter *pChar = pPlayer->GetCharacter();
		if(!pChar)
			continue;

		bool InZone = IsInZone(pChar->m_Pos);
		bool WasInZone = m_aWasInZone[i];

		if(InZone && !WasInZone)
		{
			int CurrentTick = GameServer()->Server()->Tick();
			if(pChar->Block().m_PendingPurchase != nullptr ||
				CurrentTick < pChar->Block().m_LastShopTick + (GameServer()->Server()->TickSpeed() * 2))
			{
				// debounce / already pending
			}
			else
			{
				pChar->Block().m_LastShopTick = CurrentTick;
				new CShop(GameServer(), pPlayer, m_Category, m_Item, 15);
			}
		}

		m_aWasInZone[i] = InZone;
	}
}
