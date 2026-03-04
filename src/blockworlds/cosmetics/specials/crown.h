/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_BLOCKWORLDS_ENTITIES_CROWN_H
#define GAME_SERVER_BLOCKWORLDS_ENTITIES_CROWN_H

#include <game/server/entity.h>

class CCrown : public CEntity
{
public:
	CCrown(CGameWorld *pGameWorld, int Owner);
	virtual void Reset() override;
	virtual void Tick() override;
	virtual void Snap(int SnappingClient) override;

private:
	int m_aIds[4];
	int m_Owner;
};

#endif // GAME_SERVER_BLOCKWORLDS_ENTITIES_CROWN_H
