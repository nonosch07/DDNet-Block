// Halo special - lasers positioned above the player's head
#ifndef BLOCK_COSMETICS_SPECIALS_HALO_H
#define BLOCK_COSMETICS_SPECIALS_HALO_H

#include <game/server/entity.h>

class CHalo : public CEntity
{
public:
	CHalo(CGameWorld *pGameWorld, int Owner);
	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	int m_Owner;
	int m_aIds[6];
};

#endif // BLOCK_COSMETICS_SPECIALS_HALO_H
