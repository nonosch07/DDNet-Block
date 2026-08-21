#ifndef BLOCKWORLDS_ENTITIES_EXPERIENCE_H
#define BLOCKWORLDS_ENTITIES_EXPERIENCE_H
#include <game/server/entity.h>

class CExperience : public CEntity
{
public:
	CExperience(CGameWorld *pGameWorld, vec2 Pos, int Amount, int TargetID);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	int m_Amount;
	int m_TargetID;
};

#endif
