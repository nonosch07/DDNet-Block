#ifndef GAME_SERVER_BASICZONE_H
#define GAME_SERVER_BASICZONE_H

#include <array>
#include <vector>

#include <base/vmath.h>

class CGameContext;
struct CMapItemLayerQuads;
struct CQuad;

class IZone
{
	CGameContext *m_pGameServer;
	std::vector<std::array<vec2, 4>> m_apQuads;

	int m_ZoneType;
	bool m_IsEnabled;

public:
	IZone(CGameContext *pGameServer, int Type);
	virtual ~IZone() = default;
	void Init(CMapItemLayerQuads *pQuadsLayer);

	int Type() const { return m_ZoneType; }
	virtual void Tick() = 0;
	virtual void Snap(int ClientID) = 0;

	bool IsInZone(vec2 Target) const;

	bool IsEnabled() const { return m_IsEnabled; }
	void Enable() { m_IsEnabled = true; }
	void Disable() { m_IsEnabled = false; }

	CGameContext *GameServer() const { return m_pGameServer; }
};

#endif // GAME_SERVER_BASICZONE_H
