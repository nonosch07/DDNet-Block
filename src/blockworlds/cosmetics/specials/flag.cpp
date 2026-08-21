
#include "flag.h"

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/teams.h>

#include <blockworlds/bw_context.h>

CFlag::CFlag(CGameWorld *pGameWorld, int Owner, int Team) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_FLAG, true)
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

	if(!GetId().has_value())
		return;

	CNetObj_Flag Flag = {};
	Flag.m_X = (int)m_Pos.x;
	Flag.m_Y = (int)m_Pos.y;
	Flag.m_Team = m_Team;
	Server()->SnapNewItem(GetId().value(), Flag);
}
