#include <engine/server.h>
#include <engine/shared/config.h>
#include <generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "ball.h"
#include <blockworlds/bw_context.h>

CBall::CBall(CGameWorld *pGameWorld, vec2 Pos, int Owner) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, true)
{
	m_Owner = Owner;
	m_Pos = Pos;

	m_IsRotating = true;

	m_LaserLifeSpan = Server()->TickSpeed();
	m_LaserDirAngle = 0;
	m_LaserInputDir = 0;

	m_TableDirV[0] = 5;
	m_TableDirV[1] = 12;
	m_TableDirV[2] = -12;
	m_TableDirV[3] = -5;

	for(int i = 0; i < 2; i++)
		m_aIds[i] = Server()->SnapNewId().value_or(-1);

	GameWorld()->InsertEntity(this);
}

void CBall::Reset()
{
	m_MarkedForDestroy = true;
	for(int i = 0; i < 2; i++)
		Server()->SnapFreeId(m_aIds[i]);
}

void CBall::Tick()
{
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	// If owner character exists, follow it. Otherwise keep last position and remain alive
	if(pOwnerChar && pOwnerChar->IsAlive())
	{
		m_Pos.x = pOwnerChar->m_Pos.x + 70 * sin(m_LaserDirAngle * pi / 180.0f);
		m_Pos.y = pOwnerChar->m_Pos.y + 70 * cos(m_LaserDirAngle * pi / 180.0f);
	}

	m_RotateDelay--;

	if(m_RotateDelay <= 0)
	{
		m_IsRotating ^= true;

		int DirSelect = rand() % 2;
		m_LaserInputDir = rand() % (m_TableDirV[DirSelect] - m_TableDirV[DirSelect] + 1) + m_TableDirV[DirSelect];
		m_RotateDelay = m_IsRotating ? Server()->TickSpeed() + (rand() % (7 - 3 + 1) + 3) : Server()->TickSpeed() + (rand() % (20 - 5 + 1) + 5);
	}

	if(m_IsRotating)
		m_LaserDirAngle += m_LaserInputDir;

	// If we have an owner character, position updated above. Otherwise keep last m_Pos
	if(pOwnerChar && pOwnerChar->IsAlive())
	{
		m_Pos2.x = m_Pos.x + 20 * sin(Server()->Tick() * 13 * pi / 180.0f);
		m_Pos2.y = m_Pos.y + 20 * cos(Server()->Tick() * 13 * pi / 180.0f);
	}

	m_Pos2.x = m_Pos.x + 20 * sin(Server()->Tick() * 13 * pi / 180.0f);
	m_Pos2.y = m_Pos.y + 20 * cos(Server()->Tick() * 13 * pi / 180.0f);
}

void CBall::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	// hide cosmetics for snapping client
	if(SnappingClient != SERVER_DEMO_CLIENT && m_Owner != SnappingClient)
	{
		CPlayer *pSnap = GameServer()->Bw().GetPlayer(SnappingClient);
		if(pSnap && pSnap->Bw().m_HideCosmetics)
			return;
	}

	CClientMask TeamMask = CClientMask().set();
	// derive visibility from player team when possible so effect persists while player is connected
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

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	GameServer()->Bw().SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_aIds[0], m_Pos, m_Pos, Server()->Tick(), m_Owner, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);

	CNetObj_DDNetProjectile Proj = {};
	Proj.m_X = round_to_int(m_Pos2.x * 100.0f);
	Proj.m_Y = round_to_int(m_Pos2.y * 100.0f);
	Proj.m_Type = WEAPON_HAMMER;
	Proj.m_Owner = m_Owner;
	Proj.m_StartTick = 0;
	Proj.m_VelX = 0;
	Proj.m_VelY = 0;
	Server()->SnapNewItem(m_aIds[1], Proj);
}
