#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "ball.h"

CBall::CBall(CGameWorld *pGameWorld, vec2 Pos, int Owner) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
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
		m_aIDs[i] = Server()->SnapNewId();

	GameWorld()->InsertEntity(this);
}

void CBall::Reset()
{
	m_MarkedForDestroy = true;
	for(int i = 0; i < 2; i++)
		Server()->SnapFreeId(m_aIDs[i]);
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

	CClientMask TeamMask = CClientMask().set();
	// derive visibility from player team when possible so effect persists while player is connected
	if(m_Owner >= 0)
	{
		CPlayer *pOwnerPlayer = GameServer()->GetPlayer(m_Owner);
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

	CNetObj_Laser *pObj;
	pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_aIDs[0], sizeof(CNetObj_Laser)));

	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_Pos.x;
	pObj->m_FromY = (int)m_Pos.y;
	pObj->m_StartTick = Server()->Tick();

	CNetObj_Projectile *pObj2 = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_aIDs[1], sizeof(CNetObj_Projectile)));
	if(!pObj2)
		return;

	pObj2->m_X = (int)m_Pos2.x;
	pObj2->m_Y = (int)m_Pos2.y;
	pObj2->m_StartTick = Server()->Tick();
}
