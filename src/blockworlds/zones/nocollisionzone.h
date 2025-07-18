#ifndef BLOCKWORLDS_ZONES_NOCOLLISIONZONE_H
#define BLOCKWORLDS_ZONES_NOCOLLISIONZONE_H

#include "zone.h"

class CCharacter;

class CNoCollisionZone final : public IZone
{
public:
	CNoCollisionZone(class CGameContext *pGameServer);

	void Tick() override;
	void Snap(int ClientID) override;
	void OnCharacterDeath(CCharacter *pCharacter);

private:
	void Protect(int ClientID, int Ticks);
	void Unprotect(int ClientID);
};

#endif // BLOCKWORLDS_ZONES_NOCOLLISIONZONE_H
