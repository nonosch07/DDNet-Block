/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef BLOCKWORLDS_COSMETICS_SPECIALS_CROWN_H
#define BLOCKWORLDS_COSMETICS_SPECIALS_CROWN_H

#include <game/server/entity.h>

class CCrown : public CEntity
{
public:
	CCrown(CGameWorld *pGameWorld, int Owner);
	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	int m_aIds[4];
	int m_Owner;
};

#endif // BLOCKWORLDS_COSMETICS_SPECIALS_CROWN_H
