#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "passivezone.h"
#include "zonemanager.h"

CPassiveZone::CPassiveZone(CGameContext *pGameServer) :
	IZone(pGameServer, ZONE_PASSIVE)
{
	mem_zero(m_aProtectionTicks, sizeof(m_aProtectionTicks));
	mem_zero(m_aFreezedTicks, sizeof(m_aFreezedTicks));
	mem_zero(m_aSnapIds, sizeof(m_aSnapIds));
	mem_zero(m_aHasProtectInZone, sizeof(m_aHasProtectInZone));
	mem_zero(m_aTouchedTile, sizeof(m_aTouchedTile));
	mem_zero(m_ProtectionUsed, sizeof(m_ProtectionUsed));
}

void CPassiveZone::Tick()
{
	return; // not implemented yet (with db)
	
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];

		if(!pPlayer)
			continue;

		CCharacter *pChar = pPlayer->GetCharacter();

		if(!pChar)
			continue;

		if(pChar->m_FreezeTime)
			m_aFreezedTicks[i]++;
		else
			m_aFreezedTicks[i] = 0;

		bool InZone = IsInZone(pChar->m_Pos);

		if(m_aHasProtectInZone[i])
		{
			if(InZone)
			{
				Protect(i, GameServer()->Server()->TickSpeed());

				if(m_aFreezedTicks[i] >= 3 * GameServer()->Server()->TickSpeed())
				{
					GameServer()->SendChatTarget(i, "You will lose protection in 3 sec.");
					Unprotect(i);
				}
			}
			else
			{
				GameServer()->SendChatTarget(i, "You are no longer protected!");
				Unprotect(i);
			}
		}
		else
		{
			if(!m_aProtectionTicks[i] && !m_aFreezedTicks[i] && InZone && !m_ProtectionUsed[i])
			{
				m_aHasProtectInZone[i] = true;
				m_ProtectionUsed[i] = true;
			}
		}

		if(m_aProtectionTicks[i] > 0)
			m_aProtectionTicks[i]--;

		pChar->Core()->m_Passive = (m_aProtectionTicks[i] > 0);
	}
}

void CPassiveZone::Snap(int ClientID)
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
			return;

		if(!pChar->CanSnapCharacter(ClientID))
			return;

		if(!pChar->IsSnappingCharacterInView(ClientID))
			return;

		/*
		if(pChar->Core()->m_Passive)
		{
			// afair 0 can be an id too, but you'll never run into situation
			// where some random zone would set first ever snap
			if(!m_aSnapIds[i])
				m_aSnapIds[i] = GameServer()->Server()->SnapNewID();

			int SnappingClientVersion = GameServer()->GetClientVersion(ClientID);
			bool Sixup = GameServer()->Server()->IsSixup(ClientID);

			GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Sixup), m_aSnapIds[i], vec2{pChar->m_Pos.x, pChar->m_Pos.y - 48.0f}, POWERUP_ARMOR, 0, 0);
		}
		else if(m_aSnapIds[i])
		{
			GameServer()->Server()->SnapFreeID(m_aSnapIds[i]);
			m_aSnapIds[i] = 0;
		}
		*/
	}
}

void CPassiveZone::OnProtectionTile(int ClientID)
{
	if(m_aTouchedTile[ClientID])
		return;

	m_aTouchedTile[ClientID] = true;
	Protect(ClientID, GameServer()->Server()->TickSpeed() * 60 * 60 * 2); // 2 hours
}

void CPassiveZone::OnCharacterDeath(CCharacter *pCharacter)
{
	int ClientID = pCharacter->GetPlayer()->GetCid();

	m_aProtectionTicks[ClientID] = 0;
	m_aFreezedTicks[ClientID] = 0;
	m_aSnapIds[ClientID] = 0;
	m_aHasProtectInZone[ClientID] = false;
	m_aTouchedTile[ClientID] = false;
	m_ProtectionUsed[ClientID] = false;

	// it's useless though but
	pCharacter->Core()->m_Passive = false;
}

void CPassiveZone::Protect(int ClientID, int Ticks)
{
	CCharacter *pChar = GameServer()->GetPlayerChar(ClientID);
	if(!pChar)
		return;

	pChar->Core()->m_Passive = true;
	m_aProtectionTicks[ClientID] = Ticks;
}

void CPassiveZone::Unprotect(int ClientID)
{
	CCharacter *pChar = GameServer()->GetPlayerChar(ClientID);
	if(!pChar)
		return;

	m_aHasProtectInZone[ClientID] = false;
	Protect(ClientID, 0); // if player isn't frozen, unprotect instantly, otherwise, if frozen for more than 3 sec, unprotect.
}
