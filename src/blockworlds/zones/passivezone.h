#ifndef BLOCKWORLDS_ZONES_PASSIVEZONE_H
#define BLOCKWORLDS_ZONES_PASSIVEZONE_H

#include "zone.h"

class CCharacter;


class CPassiveZone final : public IZone
{
	int m_aProtectionTicks[MAX_CLIENTS];
	int m_aFreezedTicks[MAX_CLIENTS];
	bool m_ProtectionUsed[MAX_CLIENTS];
	bool m_aWasInZone[MAX_CLIENTS];

public:
	CPassiveZone(class CGameContext *pGameServer);

	void Tick() override;
	void Snap(int ClientID) override;
	void OnCharacterDeath(CCharacter *pCharacter);

private:
	void HandleProtection(int ClientID, CPlayer *pPlayer, CCharacter *pChar, bool InZone, bool WasInZone);
};

#endif // BLOCKWORLDS_ZONES_PASSIVEZONE_H
