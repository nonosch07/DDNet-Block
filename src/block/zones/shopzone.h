#ifndef BLOCK_ZONES_SHOPZONE_H
#define BLOCK_ZONES_SHOPZONE_H

#include "zone.h"

#include <engine/shared/protocol.h>

class CShopZone final : public IZone
{
	int m_Category;
	int m_Item;
	int m_aWasInZone[MAX_CLIENTS];
	int m_aLastShopTick[MAX_CLIENTS];

public:
	CShopZone(class CGameContext *pGameServer, int Category, int Item);
	void Tick() override;
	void Snap(int ClientID) override { (void)ClientID; }
};

#endif // BLOCK_ZONES_SHOPZONE_H
