#ifndef GAME_SERVER_SPAWNZONE_H
#define GAME_SERVER_SPAWNZONE_H

#include "zone.h"

class CCharacter;

class CSpawnZone final : public IZone
{
	int m_aSpawnTicks[MAX_CLIENTS];
	int m_aSnapIds[MAX_CLIENTS];
	bool m_aIsInSpawnZone[MAX_CLIENTS];

public:
	CSpawnZone(class CGameContext *pGameServer);

	void Tick() override;
	void Snap(int ClientID) override;

	void OnCharacterDeath(CCharacter *pCharacter);

private:
	void KeepInSpawn(int ClientID, int Ticks);
};

#endif // GAME_SERVER_SPAWNZONE_H