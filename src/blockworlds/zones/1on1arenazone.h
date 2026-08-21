#ifndef BLOCKWORLDS_ZONES_1ON1ARENAZONE_H
#define BLOCKWORLDS_ZONES_1ON1ARENAZONE_H

#include "zone.h"
#include <base/vmath.h>
#include <string>
#include <vector>

class C1on1ArenaZone final : public IZone
{
	std::string m_DisplayName;
	int m_ArenaIndex;

public:
	C1on1ArenaZone(CGameContext *pGameServer, const char *pLayerName, int arenaIndex);

	void Tick() override {}
	void Snap(int ClientID) override {}

	const char *GetDisplayName() const { return m_DisplayName.c_str(); }
	int GetArenaIndex() const { return m_ArenaIndex; }

	std::vector<vec2> GetSpawnPositions() const { return GetCenters(); }
};

#endif // BLOCKWORLDS_ZONES_1ON1ARENAZONE_H
