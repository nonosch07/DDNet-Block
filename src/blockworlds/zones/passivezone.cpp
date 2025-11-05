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

		if(InZone && !WasInZone && pChar->Core()->m_Passive)
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

		if(InZone && pChar->Core()->m_Passive)
		{
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

		// track how many ticks you've been frozen for (in a row)
		if(InZone && pChar->m_FreezeTime)
			m_aFreezedTicks[i]++;
		else
			m_aFreezedTicks[i] = 0;

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
	// nothing for now :)
	return;
}

void CPassiveZone::OnCharacterDeath(CCharacter *pCharacter)
{
	int ClientID = pCharacter->GetPlayer()->GetCid();
	m_aProtectionTicks[ClientID] = 0;
	m_aFreezedTicks[ClientID] = 0;
	m_ProtectionUsed[ClientID] = false; // reset per life
	m_aWasInZone[ClientID] = false;
	pCharacter->Core()->m_Passive = false;
}

void CPassiveZone::HandleProtection(int ClientID, CPlayer *pPlayer, CCharacter *pChar, bool InZone, bool WasInZone)
{
	const int TickSpeed = GameServer()->Server()->TickSpeed();

	const int GraceTicks = 3 * TickSpeed;
	const int MaxFreezeInsideTicks = 4 * TickSpeed;

	bool HasLocal = (pPlayer->m_LocalPassiveDuration > 0);
	bool HasAccount = (pPlayer->IsLoggedIn() && pPlayer->GetPlayerPassive() > 0);
	bool Eligible = (HasLocal || HasAccount) && pPlayer->IsUsingPassiveProtection();

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
		// if still frozen, start grace period, do not unprotect instantly
		if(pChar->Core()->m_Passive && m_aFreezedTicks[ClientID] > 0)
		{
			if(m_aProtectionTicks[ClientID] == 0)
				m_aProtectionTicks[ClientID] = GraceTicks;
			// dont unprotect yet
			return;
		}
		return;
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

		// only give protection once per life, but only if passive is enabled before entering
		static bool s_PassiveEnabledOnEntry[MAX_CLIENTS] = {false};
		// Only allow enabling if the user has opted in via IsUsingPassiveProtection.
		bool passiveAllowed = (pPlayer->IsLoggedIn() && pPlayer->GetPlayerPassive() > 0 && pPlayer->IsUsingPassiveProtection()) || (!pPlayer->IsLoggedIn() && pPlayer->m_LocalPassiveDuration > 0 && pPlayer->IsUsingPassiveProtection());
		if(WasInZone != InZone) // zone entry event
			s_PassiveEnabledOnEntry[ClientID] = passiveAllowed;
		if(!m_ProtectionUsed[ClientID] && s_PassiveEnabledOnEntry[ClientID])
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
		bool fullyUnfrozen = pChar->m_FreezeTime == 0 && !pChar->Core()->m_DeepFrozen && !pChar->Core()->m_LiveFrozen;
		if(fullyUnfrozen && m_aProtectionTicks[ClientID] == 0)
		{
			m_aProtectionTicks[ClientID] = 0;
			pChar->Core()->m_Passive = false;
		}
	}
}
