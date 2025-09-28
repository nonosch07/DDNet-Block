#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "passivezone.h"
#include "zonemanager.h"
#include <cmath>

CPassiveZone::CPassiveZone(CGameContext *pGameServer) :
	IZone(pGameServer, ZONE_PASSIVE)
{
	mem_zero(m_aProtectionTicks, sizeof(m_aProtectionTicks));
	mem_zero(m_aFreezedTicks, sizeof(m_aFreezedTicks));
	mem_zero(m_ProtectionUsed, sizeof(m_ProtectionUsed));
	mem_zero(m_aWasInZone, sizeof(m_aWasInZone));
}

void CPassiveZone::Tick()
{
	const int TickSpeed = GameServer()->Server()->TickSpeed();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer)
			continue;
		CCharacter *pChar = pPlayer->GetCharacter();
		if(!pChar)
			continue;

		// track how many ticks you've been frozen for (in a row)
		if(pChar->m_FreezeTime)
			m_aFreezedTicks[i]++;
		else
			m_aFreezedTicks[i] = 0;

		bool InZone = IsInZone(pChar->m_Pos);
		bool WasInZone = m_aWasInZone[i];

		// if the grace timer's running, tick it down
		// this is how many grace ticks you got left after leaving the zone while frozen
		if(m_aProtectionTicks[i] > 0)
		{
			m_aProtectionTicks[i]--;
			if(m_aProtectionTicks[i] == 0)
			{
				pChar->Core()->m_Passive = false;
			}
		}

		HandleProtection(i, pPlayer, pChar, InZone, WasInZone);

		// update wasInZone for next tick
		m_aWasInZone[i] = InZone;
	}
}

void CPassiveZone::Snap(int ClientID)
{
    CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
    if(!pPlayer)
        return;
    
    CCharacter *pChar = pPlayer->GetCharacter();
    if(!pChar)
        return;
    
    if(pChar->Core()->m_Passive)
    {
        vec2 PickupPos = pChar->m_Pos + vec2(0, -48.0f);
        
        GameServer()->SnapPickup(
            CSnapContext(pPlayer->GetClientVersion(), GameServer()->Server()->IsSixup(ClientID)), 
            1000,
            PickupPos, 
            POWERUP_ARMOR, 
            0, 
            0
        );
    }
}


void CPassiveZone::OnCharacterDeath(CCharacter *pCharacter)
{
	int ClientID = pCharacter->GetPlayer()->GetCid();
	m_aProtectionTicks[ClientID] = 0;
	m_aFreezedTicks[ClientID] = 0;
	m_ProtectionUsed[ClientID] = false; // reset per life
	m_aWasInZone[ClientID] = false;
	pCharacter->Core()->m_Passive = false;
	dbg_msg("passivezone", "OnCharacterDeath called for %d", ClientID);
}

void CPassiveZone::HandleProtection(int ClientID, CPlayer *pPlayer, CCharacter *pChar, bool InZone, bool WasInZone)
{
	const int TickSpeed = GameServer()->Server()->TickSpeed();

	const int GraceTicks = 3 * TickSpeed;
	const int MaxFreezeInsideTicks = 3 * TickSpeed;

	bool Eligible = ((pPlayer->IsLoggedIn() && pPlayer->GetPlayerPassive() > 0) || pPlayer->m_LocalPassiveDuration > 0);

	// if you're not eligible, clear protection
	if(!Eligible)
	{
		m_aProtectionTicks[ClientID] = 0;
		pChar->Core()->m_Passive = false;
		return;
	}

	// leaving the zone (was in, now out)
	if(WasInZone && !InZone)
	{
		if(pChar->Core()->m_Passive)
		{
			if(m_aFreezedTicks[ClientID] > 0)
			{
				if(m_aProtectionTicks[ClientID] == 0)
				{
					m_aProtectionTicks[ClientID] = GraceTicks;
				}
				return;
			}
			else
			{
				m_aProtectionTicks[ClientID] = 0;
				pChar->Core()->m_Passive = false;
				return;
			}
		}
	}

	// in the zone and eligible
	if(InZone)
	{
		// if you've been frozen inside the zone too long, drop your protection
		if(m_aFreezedTicks[ClientID] >= MaxFreezeInsideTicks)
		{
			if(pChar->Core()->m_Passive)
			{
				m_aProtectionTicks[ClientID] = 0;
				pChar->Core()->m_Passive = false;
			}
			return;
		}

		// only give protection once per life
		if(!m_ProtectionUsed[ClientID])
		{
			m_ProtectionUsed[ClientID] = true;
			pChar->Core()->m_Passive = true;
		}
		else
		{
			// fix:
			// don't re-enable passive on re-entering if you've already used your protection this life
			// if it's still active (you never left or lost it), keep it on
			// if it's off (you lost it earlier), don't set it back on
			// also reset the grace timer when you're in the zone
			if(pChar->Core()->m_Passive)
			{
				// still active, keep it on
			}
			// otherwise just chill and don't re-grant protection
			m_aProtectionTicks[ClientID] = 0;
		}
	}
	else
	{
		// not in zone:
		if(m_aFreezedTicks[ClientID] == 0 && m_aProtectionTicks[ClientID] == 0)
		{
			m_aProtectionTicks[ClientID] = 0;
			pChar->Core()->m_Passive = false;
		}
	}
}
