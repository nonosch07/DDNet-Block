#ifndef BLOCK_COSMETICS_SPECIALS_BALL_H
#define BLOCK_COSMETICS_SPECIALS_BALL_H

#include <game/server/entity.h>
class CBall : public CEntity
{
public:
	CBall(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	vec2 GetPos() const { return m_Pos; }

private:
	int m_Owner;
	int m_aIds[2];

	int m_LaserLifeSpan;
	int m_LaserDirAngle;
	int m_LaserInputDir;
	bool m_IsRotating;
	int m_RotateDelay;

	vec2 m_Pos2;

	int m_TableDirV[4];
};

#endif // BLOCK_COSMETICS_SPECIALS_BALL_H
