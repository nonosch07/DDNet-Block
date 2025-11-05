#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "nocollisionzone.h"
#include "zonemanager.h"

CNoCollisionZone::CNoCollisionZone(CGameContext *pGameServer) :
	IZone(pGameServer, ZONE_NOCOLL)
{
	mem_zero(m_aWasInZone, sizeof(m_aWasInZone));
}

void CNoCollisionZone::Tick()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer)
			continue;

		CCharacter *pChar = pPlayer->GetCharacter();
		if(!pChar)
			continue;

		bool InZone = IsInZone(pChar->m_Pos);
		bool WasInZone = m_aWasInZone[i];

		if(InZone)
		{
			Protect(i, GameServer()->Server()->TickSpeed());
			if(!WasInZone)
			{
				if(pChar->Core()->HookedPlayer() != -1)
				{
					pChar->ReleaseHook();
				}
				if(pChar->m_HookedBy >= 0 && pChar->m_HookedBy < MAX_CLIENTS)
				{
					if(CCharacter *pHooker = GameServer()->GetPlayerChar(pChar->m_HookedBy))
					{
						if(pHooker->Core()->HookedPlayer() == pChar->GetPlayer()->GetCid())
							pHooker->ReleaseHook();
					}
				}
			}

			const int myId = pChar->GetPlayer()->GetCid();
			for(int h = 0; h < MAX_CLIENTS; ++h)
			{
				if(h == i)
					continue;
				CCharacter *pHooker = GameServer()->GetPlayerChar(h);
				if(!pHooker)
					continue;
				if(pHooker->Core()->HookedPlayer() == myId)
				{
					pHooker->ReleaseHook();
				}
			}
		}
		else
		{
			Unprotect(i);
		}
		m_aWasInZone[i] = InZone;
	}
}

void CNoCollisionZone::Snap(int ClientID)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];

		if(!pPlayer)
			continue;

		CCharacter *pChar = pPlayer->GetCharacter();

		if(!pChar)
			continue;

		int ID = i;

		if(!GameServer()->Server()->Translate(ID, ClientID))
			continue;

		if(!pChar->CanSnapCharacter(ClientID))
			continue;

		if(!pChar->IsSnappingCharacterInView(ClientID))
			continue;
	}
}

void CNoCollisionZone::OnCharacterDeath(CCharacter *pCharacter)
{
	pCharacter->Core()->m_Protected = false;
}

void CNoCollisionZone::Protect(int ClientID, int Ticks)
{
	CCharacter *pChar = GameServer()->GetPlayerChar(ClientID);
	if(!pChar)
		return;

	pChar->Core()->m_Protected = true;
}

void CNoCollisionZone::Unprotect(int ClientID)
{
	CCharacter *pChar = GameServer()->GetPlayerChar(ClientID);
	if(!pChar)
		return;

	pChar->Core()->m_Protected = false;
}
