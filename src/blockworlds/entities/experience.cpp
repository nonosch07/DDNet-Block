#include "experience.h"
#include <algorithm>
#include <blockworlds/accounts.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/teams.h>

#include <base/vmath.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>

#include <game/server/player.h>

CExperience::CExperience(CGameWorld *pGameWorld, vec2 Pos, int Amount, int TargetID) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_Pos = Pos;
	m_Amount = Amount;
	m_TargetID = TargetID;

	GameWorld()->InsertEntity(this);
}

void CExperience::Reset()
{
	m_MarkedForDestroy = true;
}

void CExperience::Tick()
{
	CCharacter *pChr = GameServer()->GetPlayerChar(m_TargetID);

	if(pChr == nullptr)
	{
		m_MarkedForDestroy = true;
		return;
	}

	float Distance = distance(m_Pos, pChr->m_Pos);

	if(Distance < 24.0f)
	{
		if(!pChr->IsAlive())
		{
			m_MarkedForDestroy = true;
			return;
		}

		GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH, -1);
		CPlayer *pPlayer = pChr->GetPlayer();

		IZone *pNoExpZone = nullptr;
		IZone *pSpawnZone = nullptr;
		if(GameServer()->ZoneManager())
		{
			pNoExpZone = GameServer()->ZoneManager()->GetZone(ZONE_NOEXP);
			pSpawnZone = GameServer()->ZoneManager()->GetZone(ZONE_SPAWN);
		}
		bool InNoExpZone = pNoExpZone && pNoExpZone->IsInZone(pChr->m_Pos);
		bool InSpawnZone = pSpawnZone && pSpawnZone->IsInZone(pChr->m_Pos);

		if(pPlayer->IsLoggedIn() && !InNoExpZone && !InSpawnZone)
		{
			int Amount = m_Amount; // dynamic amount
			pPlayer->AddPlayerExp(Amount);

			// Don't count account-level kills during active server events
			bool InEvent = false;
			if(auto pEvents = g_ComponentRegistry.Get<CEvents>(); pEvents && pEvents->GetActiveEvent())
			{
				auto pActiveEvent = pEvents->GetActiveEvent();
				if(pActiveEvent->GetState() == CEventComponent::EEventState::Active || pActiveEvent->GetState() == CEventComponent::EEventState::Preparation)
				{
					const auto &Participants = pActiveEvent->Participants();
					if(std::find(Participants.begin(), Participants.end(), pPlayer->GetCid()) != Participants.end())
						InEvent = true;
				}
			}
			if(!InEvent)
				pPlayer->SetPlayerKills(pPlayer->GetPlayerKills() + 1);

			pPlayer->SetPlayerBlockpoints(pPlayer->GetPlayerBlockpoints() + 1);

			if(pPlayer->GetClanId())
				GameServer()->Clans()->AddClanExp(pPlayer->GetClanId(), Amount);
		}
		else
		{
			if((pPlayer->m_LastExpAccountAlert == 0 && Server()->Tick() <= Server()->TickSpeed() * 300) || // server is very young
				pPlayer->m_LastExpAccountAlert + Server()->TickSpeed() * 300 < Server()->Tick())
			{
				if(!pPlayer->IsLoggedIn())
					GameServer()->SendChatTarget(m_TargetID, "Login/Register an account to receive your experience points");
				else if(InSpawnZone)
					GameServer()->SendChatTarget(m_TargetID, "You don't receive EXP in spawn zone");
				else if(InNoExpZone)
					GameServer()->SendChatTarget(m_TargetID, "You don't receive EXP in this area");
				pPlayer->m_LastExpAccountAlert = Server()->Tick();
			}
		}

		m_MarkedForDestroy = true;
		return;
	}

	vec2 Direction = normalize(pChr->m_Pos - m_Pos);
	float Speed = Distance * 0.035f + 11.0f;
	m_Pos += Direction * Speed;
}
// circular effect
// void CExperience::Tick()
// {
//     CCharacter *pChr = GameServer()->GetPlayerChar(m_TargetID);

//     // If the character is invalid, destroy the particle
//     if(pChr == nullptr)
//     {
//         m_MarkedForDestroy = true;
//         return;
//     }

//     float Distance = distance(m_Pos, pChr->m_Pos);
//     if(Distance < 24.0f)
//     {
//         GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH, -1);
//         CPlayer *pPlayer = pChr->GetPlayer();

//         if(pPlayer->IsLoggedIn())
//         {
//             pPlayer->SetPlayerExperience(pPlayer->GetPlayerExperience() + g_Config.m_SvBlockExperience);
//             pPlayer->SetPlayerKills(pPlayer->GetPlayerKills() + 1);
//             pPlayer->SetPlayerBlockpoints(pPlayer->GetPlayerBlockpoints() + 1);
//         }
//         else
//         {
//             if(pPlayer->m_LastExpAccountAlert + Server()->TickSpeed() * 300 < Server()->Tick())
//             {
//                 GameServer()->SendChatTarget(m_TargetID, "Login/Register an account to receive your experience points");
//                 pPlayer->m_LastExpAccountAlert = Server()->Tick();
//             }
//         }

//         m_MarkedForDestroy = true;
//         return;
//     }

//     vec2 Direction = normalize(pChr->m_Pos - m_Pos);
//     float Speed = Distance * 0.03f + 9.0f;

//     static float Time = 0.0f;
//     Time += 0.1f;

//     float SpiralRadius = 5.0f;
//     vec2 SpiralOffset = vec2(
//         SpiralRadius * sin(Time * 5),
//         SpiralRadius * cos(Time * 5)
//     );

//     m_Pos += (Direction * Speed) + SpiralOffset * (Distance / 100.0f);

// }

void CExperience::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient, m_Pos))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetId(), sizeof(CNetObj_Projectile)));
	if(pProj == 0x0)
		return;

	pProj->m_X = (int)m_Pos.x;
	pProj->m_Y = (int)m_Pos.y;
	pProj->m_VelX = 0;
	pProj->m_VelY = 0;
	pProj->m_StartTick = Server()->Tick() - 1;
	pProj->m_Type = WEAPON_HAMMER;
}
