#include "experience.h"
#include <game/server/blockworlds/accounts.h>

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
		GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH, -1);
		CPlayer *pPlayer = pChr->GetPlayer();

		if(pPlayer->IsLoggedIn())
		{
			pPlayer->SetPlayerExperience(pPlayer->GetPlayerExperience() + g_Config.m_SvBlockExperience);
			pPlayer->SetPlayerKills(pPlayer->GetPlayerKills() + 1);
			pPlayer->SetPlayerBlockpoints(pPlayer->GetPlayerBlockpoints() + 1);
			// if (GameServer()->DataHandler()->Accounts()->GetPlayerClanID(m_TargetID)){ GameServer()->DataHandler()->Clans()->AddClanExp(GameServer()->DataHandler()->Accounts()->GetPlayerClanID(m_TargetID), g_Config.m_SvBlockExperience);}
		}
		else
		{
			if(pPlayer->m_LastExpAccountAlert + Server()->TickSpeed() * 300 < Server()->Tick())
			{
				GameServer()->SendChatTarget(m_TargetID, "Login/Register an account to receive your experience points");
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
