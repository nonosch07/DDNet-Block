/*
 *	by Rei
*/

#include "epiccircle.h"
#include <engine/server.h>
#include <engine/shared/config.h>
#include <generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <base/vmath.h>
#include <math.h>
#include <blockworlds/bw_context.h>

CEpicCircle::CEpicCircle(CGameWorld *pGameWorld, vec2 Pos, int Owner) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, true)
{
	m_Owner = Owner;
	m_Pos = Pos;

	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		m_aIDs[i] = Server()->SnapNewId().value_or(-1);
	}
	GameWorld()->InsertEntity(this);
}

void CEpicCircle::Reset()
{
	m_MarkedForDestroy = true;
	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		Server()->SnapFreeId(m_aIDs[i]);
	}
}

void CEpicCircle::Tick()
{
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(pOwnerChar && pOwnerChar->IsAlive())
		m_Pos = pOwnerChar->m_Pos;

	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		float rad = 16.0f * powf(sinf(Server()->Tick() / 30.0f), 3) * 1 + 50;
		float TurnFac = 0.025f;
		m_RotatePos[i].x = cosf(2 * pi * (i / (float)MAX_PARTICLES) + Server()->Tick() * TurnFac) * rad;
		m_RotatePos[i].y = sinf(2 * pi * (i / (float)MAX_PARTICLES) + Server()->Tick() * TurnFac) * rad;
	}
}

void CEpicCircle::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
	{
		return;
	}

	// hide cosmetics for snapping client
	if(SnappingClient != SERVER_DEMO_CLIENT && m_Owner != SnappingClient)
	{
		CPlayer *pSnap = GameServer()->Bw().GetPlayer(SnappingClient);
		if(pSnap && pSnap->Bw().m_HideCosmetics)
			return;
	}

	CClientMask TeamMask = CClientMask().set();
	if(m_Owner >= 0)
	{
		CPlayer *pOwnerPlayer = GameServer()->Bw().GetPlayer(m_Owner);
		if(pOwnerPlayer)
		{
			CCharacter *pOwnerChar = pOwnerPlayer->GetCharacter();
			if(pOwnerChar && pOwnerChar->IsAlive())
				TeamMask = pOwnerChar->TeamMask();
			else
				TeamMask = CClientMask().set();
		}
	}

	if(SnappingClient != SERVER_DEMO_CLIENT && m_Owner != -1 && !TeamMask.test(SnappingClient))
		return;

	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		CNetObj_Projectile Particle = {};
		Particle.m_X = m_Pos.x + m_RotatePos[i].x;
		Particle.m_Y = m_Pos.y + m_RotatePos[i].y;
		Particle.m_VelX = 4;
		Particle.m_VelY = 4;
		Particle.m_StartTick = Server()->Tick() - 4;
		Server()->SnapNewItem(m_aIDs[i], Particle);
	}
}
