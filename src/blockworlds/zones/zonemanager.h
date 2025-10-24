#ifndef BLOCKWORLDS_ZONES_ZONEMANAGER_H
#define BLOCKWORLDS_ZONES_ZONEMANAGER_H

class IZone;
class CGameContext;

enum
{
	ZONE_NOCOLL,
	ZONE_PASSIVE,
	ZONE_SPAWN,
	ZONE_NOEXP,
	NUM_ZONES
};

class CZoneManager
{
	CGameContext *m_pGameServer;
	IZone *m_aZones[NUM_ZONES];
	std::vector<IZone *> m_vExtraZones; // dynamic zones (shops, custom zones)

	CGameContext *GameServer() const { return m_pGameServer; }

public:
	CZoneManager();
	~CZoneManager();

	void Init(CGameContext *pGameServer);
	IZone *GetZone(int Type);

	// return centers of quads from named quad layers (e.g. "tdm_red", "tdm_blue")
	std::vector<vec2> GetNamedQuadCenters(const char *pName) const;

	void Tick();
	void Snap(int ClientID);
};

#endif // BLOCKWORLDS_ZONES_ZONEMANAGER_H
