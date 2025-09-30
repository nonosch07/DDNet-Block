#ifndef BLOCKWORLDS_ZONES_SHOPPOINTZONE_H
#define BLOCKWORLDS_ZONES_SHOPPOINTZONE_H

#include "zone.h"
#include <engine/shared/protocol.h>

class CShopPointZone final : public IZone
{
	vec2 m_Pos;
	float m_Radius;
	int m_Category;
	int m_Item;
	bool m_aWasInZone[MAX_CLIENTS];

public:
	CShopPointZone(CGameContext *pGameServer, const vec2 &Pos, float Radius, int Category, int Item);
	void Tick() override;
	void Snap(int ClientID) override { (void)ClientID; }
	int Category() const { return m_Category; }
	int Item() const { return m_Item; }
};

#endif // BLOCKWORLDS_ZONES_SHOPPOINTZONE_H
