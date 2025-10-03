/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "crown.h"

CCrown::CCrown(CGameWorld *pGameWorld, int Owner) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Owner = Owner;

	for(int i = 0; i < 4; i++)
		m_IDs[i] = Server()->SnapNewId();

	GameWorld()->InsertEntity(this);
}

void CCrown::Reset()
{
	m_MarkedForDestroy = true;

	for(int i = 0; i < 4; i++)
		Server()->SnapFreeId(m_IDs[i]);
}

void CCrown::Tick()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	// Keep crown alive across death; follow owner when character exists
	if(pOwner && pOwner->IsAlive())
		m_Pos = pOwner->m_Pos;
}

void CCrown::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
	{
		return;
	}

	CClientMask TeamMask = CClientMask().set();
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

	CNetObj_Laser *pObj[4];

	for(int i = 0; i < 4; i++)
	{
		vec2 From = vec2(0, 0);
		vec2 To = vec2(0, 0);

		switch(i)
		{
		case 0:
			From.x = -20;
			From.y = -20;
			To.y = -45;
			break;
		case 1:
			From.x = +20;
			From.y = -20;
			To.y = -45;
			break;
		case 2:
			To.x = -25;
			To.y = -35;
			break;
		case 3:
			To.x = +25;
			To.y = -35;
			break;
		}

		pObj[i] = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[i], sizeof(CNetObj_Laser)));

		if(!pObj[i])
			return;

		pObj[i]->m_X = (int)m_Pos.x + To.x;
		pObj[i]->m_Y = (int)m_Pos.y + To.y;
		pObj[i]->m_FromX = (int)m_Pos.x + From.x;
		pObj[i]->m_FromY = (int)m_Pos.y + From.y;
		pObj[i]->m_StartTick = Server()->Tick();
	}
}
