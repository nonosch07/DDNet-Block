#ifndef BLOCK_ZONES_REDIRECTZONE_H
#define BLOCK_ZONES_REDIRECTZONE_H

#include "zone.h"

#include <engine/shared/protocol.h>

class CRedirectZone final : public IZone
{
	int m_Port;
	bool m_aWasInZone[MAX_CLIENTS];
	int m_aLastRedirectTick[MAX_CLIENTS];

public:
	CRedirectZone(class CGameContext *pGameServer, int Port);

	int Port() const { return m_Port; }

	void Tick() override;
	void Snap(int ClientID) override { (void)ClientID; }
};

#endif // BLOCK_ZONES_REDIRECTZONE_H
