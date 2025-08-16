#ifndef BLOCKWORLDS_ZONES_PASSIVEZONE_H
#define BLOCKWORLDS_ZONES_PASSIVEZONE_H

#include "zone.h"

class CCharacter;

class CPassiveZone final : public IZone
{
	int m_aProtectionTicks[MAX_CLIENTS];
	int m_aFreezedTicks[MAX_CLIENTS];
	int m_aSnapIds[MAX_CLIENTS];
	bool m_aHasProtectInZone[MAX_CLIENTS];
	bool m_aTouchedTile[MAX_CLIENTS];
	bool m_ProtectionUsed[MAX_CLIENTS];

public:
	CPassiveZone(class CGameContext *pGameServer);

	void Tick() override;
	void Snap(int ClientID) override;

	void OnProtectionTile(int ClientID);
	void OnCharacterDeath(CCharacter *pCharacter);

private:
	void Protect(int ClientID, int Ticks);
	void Unprotect(int ClientID);
};

#endif // BLOCKWORLDS_ZONES_PASSIVEZONE_H
