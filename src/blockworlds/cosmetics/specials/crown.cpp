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
		m_aIds[i] = Server()->SnapNewId();
	std::sort(m_aIds, m_aIds + std::size(m_aIds));

	GameWorld()->InsertEntity(this);
}

void CCrown::Reset()
{
	m_MarkedForDestroy = true;

	for(int i = 0; i < 4; i++)
		Server()->SnapFreeId(m_aIds[i]);
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

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

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

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp), m_aIds[i], m_Pos + To, m_Pos + From, Server()->Tick(), m_Owner, 0, -1, -1, LASERFLAG_NO_PREDICT);
	}
}
