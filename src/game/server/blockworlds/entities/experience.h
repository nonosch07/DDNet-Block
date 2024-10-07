#ifndef GAME_SERVER_BLOCKWORLDS_ENTITIES_EXPERIENCE_H
#define GAME_SERVER_BLOCKWORLDS_ENTITIES_EXPERIENCE_H
#include <game/server/entity.h>

class CAccountManager;

class CExperience : public CEntity
{
public:
	CExperience(CGameWorld *pGameWorld, vec2 Pos, int Amount, int TargetID);

	virtual void Reset() override;
	virtual void Tick() override;
	virtual void Snap(int SnappingClient) override;

private:
	int m_Amount;
	int m_TargetID;
};

#endif