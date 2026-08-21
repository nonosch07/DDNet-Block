// Halo special - lasers positioned above the player's head
#ifndef BLOCKWORLDS_COSMETICS_SPECIALS_HALO_H
#define BLOCKWORLDS_COSMETICS_SPECIALS_HALO_H

#include <game/server/entity.h>

class CHalo : public CEntity
{
public:
	CHalo(CGameWorld *pGameWorld, int Owner);
	virtual void Reset() override;
	virtual void Tick() override;
	virtual void Snap(int SnappingClient) override;

private:
	int m_Owner;
	int m_aIds[6];
};

#endif // BLOCKWORLDS_COSMETICS_SPECIALS_HALO_H
