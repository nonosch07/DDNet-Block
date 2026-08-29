#ifndef BLOCK_ZONES_1ON1PREPZONE_H
#define BLOCK_ZONES_1ON1PREPZONE_H

#include "zone.h"

#include <base/vmath.h>

#include <vector>

class C1on1PrepZone final : public IZone
{
public:
	explicit C1on1PrepZone(CGameContext *pGameServer);

	void Tick() override {}
	void Snap(int ClientID) override {}

	// Returns the center position of every quad (i.e. all valid spawn points).
	std::vector<vec2> GetSpawnPositions() const { return GetCenters(); }
};

#endif // BLOCK_ZONES_1ON1PREPZONE_H
