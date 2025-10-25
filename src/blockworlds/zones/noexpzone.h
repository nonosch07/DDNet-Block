#ifndef BLOCKWORLDS_ZONES_NOEXPZONE_H
#define BLOCKWORLDS_ZONES_NOEXPZONE_H

#include "zone.h"
#include "zonemanager.h"

class CNoExpZone final : public IZone
{
public:
	CNoExpZone(class CGameContext *pGameServer) :
		IZone(pGameServer, ZONE_NOEXP) {}

	void Tick() override {}
	void Snap(int ClientID) override {}

	bool IsPlayerInNoExpZone(vec2 Pos) const { return IsInZone(Pos); }
};

#endif // BLOCKWORLDS_ZONES_NOEXPZONE_H
