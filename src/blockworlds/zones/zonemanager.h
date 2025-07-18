#ifndef BLOCKWORLDS_ZONES_ZONEMANAGER_H
#define BLOCKWORLDS_ZONES_ZONEMANAGER_H

class IZone;
class CGameContext;

enum
{
	ZONE_NOCOLL,
	ZONE_PASSIVE,
	ZONE_SPAWN,
	NUM_ZONES
};

class CZoneManager
{
	CGameContext *m_pGameServer;
	IZone *m_aZones[NUM_ZONES];

	CGameContext *GameServer() const { return m_pGameServer; }

public:
	CZoneManager();

	void Init(CGameContext *pGameServer);
	IZone *GetZone(int Type);

	void Tick();
	void Snap(int ClientID);
};

#endif // BLOCKWORLDS_ZONES_ZONEMANAGER_H
