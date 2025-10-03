#ifndef GAME_SERVER_BLOCKWORLDS_ENTITIES_BALL_H
#define GAME_SERVER_BLOCKWORLDS_ENTITIES_BALL_H

#include <game/server/entity.h>
class CBall : public CEntity
{
public:
	CBall(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	virtual void Reset() override;
	virtual void Tick() override;
	virtual void Snap(int SnappingClient) override;
	inline vec2 GetPos() const { return m_Pos; }

private:
	int m_Owner;
	int m_aIDs[2];

	int m_LaserLifeSpan;
	int m_LaserDirAngle;
	int m_LaserInputDir;
	bool m_IsRotating;
	int m_RotateDelay;

	vec2 m_Pos2;

	int m_TableDirV[4];
};

#endif // GAME_SERVER_BLOCKWORLDS_COSMETICS_BALL_H
