#ifndef GAME_SERVER_NOCOLLZONE_H
#define GAME_SERVER_NOCOLLZONE_H

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

#endif // GAME_SERVER_NOCOLLZONE_H
