#ifndef BLOCK_COSMETICS_SPECIALS_EPICCIRCLE_H
#define BLOCK_COSMETICS_SPECIALS_EPICCIRCLE_H

#include <game/server/entity.h>
class CEpicCircle : public CEntity
{
	enum
	{
		MAX_PARTICLES = 9
	};

public:
	CEpicCircle(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	int m_Owner;
	int m_aIDs[MAX_PARTICLES];

	vec2 m_RotatePos[MAX_PARTICLES];
};

#endif // BLOCK_COSMETICS_SPECIALS_EPICCIRCLE_H
