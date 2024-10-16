#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "nocollisionzone.h"
#include "zonemanager.h"

CNoCollisionZone::CNoCollisionZone(CGameContext *pGameServer)
    : IZone(pGameServer, ZONE_NOCOLL)
{
}

void CNoCollisionZone::Tick()
{
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        CPlayer *pPlayer = GameServer()->m_apPlayers[i];

        if(!pPlayer)
            continue;

        CCharacter *pChar = pPlayer->GetCharacter();

        if(!pChar)
            continue;

        bool InZone = IsInZone(pChar->m_Pos);

        if(InZone) { Protect(i, GameServer()->Server()->TickSpeed()); }
        else { Unprotect(i); }
    }
}

void CNoCollisionZone::Snap(int ClientID)
{
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        CPlayer *pPlayer = GameServer()->m_apPlayers[i];

        if(!pPlayer)
            continue;

        CCharacter *pChar = pPlayer->GetCharacter();

        if(!pChar)
            continue;

        int ID = i;

        if(!GameServer()->Server()->Translate(ID, ClientID))
            continue;

        if(!pChar->CanSnapCharacter(ClientID))
            continue;

        if(!pChar->IsSnappingCharacterInView(ClientID))
            continue;
    }
}

void CNoCollisionZone::OnCharacterDeath(CCharacter *pCharacter)
{
    int ClientID = pCharacter->GetPlayer()->GetCid();
    pCharacter->Core()->m_Protected = false;
}

void CNoCollisionZone::Protect(int ClientID, int Ticks)
{
    CCharacter *pChar = GameServer()->GetPlayerChar(ClientID);
    if(!pChar)
        return;

    pChar->Core()->m_Protected = true;
}

void CNoCollisionZone::Unprotect(int ClientID)
{
    CCharacter *pChar = GameServer()->GetPlayerChar(ClientID);
    if(!pChar)
        return;

    pChar->Core()->m_Protected = false;
}