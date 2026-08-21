#ifndef BLOCKWORLDS_COSMETICS_SPECIALS_FLAG_H
#define BLOCKWORLDS_COSMETICS_SPECIALS_FLAG_H

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
