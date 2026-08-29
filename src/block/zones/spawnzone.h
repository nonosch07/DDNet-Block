#ifndef BLOCK_ZONES_SPAWNZONE_H
#define BLOCK_ZONES_SPAWNZONE_H

#include "zone.h"

#include <engine/shared/protocol.h> // MAX_CLIENTS

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

#endif // BLOCK_ZONES_SPAWNZONE_H
