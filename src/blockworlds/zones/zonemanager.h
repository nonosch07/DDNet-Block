#ifndef BLOCKWORLDS_ZONES_ZONEMANAGER_H
#define BLOCKWORLDS_ZONES_ZONEMANAGER_H

#include "1on1arenazone.h"
#include "1on1prepzone.h"
#include "zone.h"

#include <base/vmath.h>

#include <array>
#include <string>
#include <vector>

class CGameContext;

enum
{
	ZONE_NOCOLL,
	ZONE_PASSIVE,
	ZONE_SPAWN,
	ZONE_NOEXP,
	ZONE_1ON1_PREP, // prep/config-phase spawn area
	NUM_ZONES
};

class CZoneManager
{
	CGameContext *m_pGameServer;
	IZone *m_aZones[NUM_ZONES];
	std::vector<IZone *> m_vExtraZones; // dynamic zones (shops, custom zones)
	std::vector<C1on1ArenaZone *> m_V1on1Arenas; // 1on1 selectable match arenas

	CGameContext *GameServer() const { return m_pGameServer; }

public:
	CZoneManager();
	~CZoneManager();

	void Init(CGameContext *pGameServer);
	IZone *GetZone(int Type);

	std::vector<vec2> Get1on1PrepPositions() const;

	int Get1on1ArenaCount() const { return (int)m_V1on1Arenas.size(); }

	const char *Get1on1ArenaName(int Idx) const;

	std::vector<vec2> Get1on1ArenaPositions(int Idx) const;

	// return centers of quads from named quad layers (e.g. "tdm_red", "tdm_blue")
	std::vector<vec2> GetNamedQuadCenters(const char *pName) const;

	// return full quad corner coordinates for a named quad layer
	// each quad is represented as std::array<vec2, 4> (clockwise order as in map data)
	std::vector<std::array<vec2, 4>> GetNamedQuads(const char *pName) const;

	void Tick();
	void Snap(int ClientID);
};

#endif // BLOCKWORLDS_ZONES_ZONEMANAGER_H
