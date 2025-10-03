#ifndef GAME_SERVER_BLOCKWORLDS_ENTITIES_FLAG_H
#define GAME_SERVER_BLOCKWORLDS_ENTITIES_FLAG_H

#include <game/server/entity.h>

class CFlag : public CEntity
{
	int m_Team;
	int m_Owner;

public:
	CFlag(CGameWorld *pGameWorld, int Owner, int Team);

	virtual void Reset() override;
	virtual void Snap(int SnappingClient) override;
	virtual void Tick() override;
};

#endif
