
#include "flag.h"

#include <game/generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/teams.h>

CFlag::CFlag(CGameWorld *pGameWorld, int Owner, int Team) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_FLAG)
{
	m_Owner = Owner;
	m_Team = Team;

	GameWorld()->InsertEntity(this);
}

void CFlag::Tick()
{
	if(!Server()->ClientIngame(m_Owner))
	{
		m_MarkedForDestroy = true;
		return;
	}
	CCharacter *pChar = GameServer()->GetPlayerChar(m_Owner);
	if(pChar && pChar->IsAlive())
	{
		m_Pos = pChar->m_Pos;
	}
}

void CFlag::Reset()
{
	m_MarkedForDestroy = true;
}

void CFlag::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	// hide cosmetics for snapping client
	if(SnappingClient != SERVER_DEMO_CLIENT && m_Owner != SnappingClient)
	{
		CPlayer *pSnap = GameServer()->GetPlayer(SnappingClient);
		if(pSnap && pSnap->m_HideCosmetics)
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

	CNetObj_Flag *pFlag = (CNetObj_Flag *)Server()->SnapNewItem(NETOBJTYPE_FLAG, GetId(), sizeof(CNetObj_Flag));
	if(!pFlag)
		return;

	pFlag->m_X = (int)m_Pos.x;
	pFlag->m_Y = (int)m_Pos.y;
	pFlag->m_Team = m_Team;
}
