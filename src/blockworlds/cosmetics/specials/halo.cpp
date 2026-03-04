#include "halo.h"

#include <engine/server.h>
#include <game/generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CHalo::CHalo(CGameWorld *pGameWorld, int Owner) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Owner = Owner;
	for(int i = 0; i < 6; ++i)
		m_aIds[i] = Server()->SnapNewId();
	GameWorld()->InsertEntity(this);
}

void CHalo::Reset()
{
	m_MarkedForDestroy = true;
	for(int i = 0; i < 6; ++i)
		Server()->SnapFreeId(m_aIds[i]);
}

void CHalo::Tick()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(pOwner && pOwner->IsAlive())
		m_Pos = pOwner->m_Pos;
}

void CHalo::Snap(int SnappingClient)
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

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	for(int i = 0; i < 6; ++i)
	{
		float ang = i * (2 * pi / 6.0f) + Server()->Tick() / 100.0f;
		int dx = (int)(40 * cosf(ang));
		int dy = -60 + (int)(8 * sinf(Server()->Tick() / 10.0f + i));

		vec2 Pos = m_Pos + vec2(dx, dy);
		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp), m_aIds[i], Pos, Pos, Server()->Tick(), m_Owner, 0, -1, -1);
	}
}
