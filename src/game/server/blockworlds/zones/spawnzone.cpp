#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "spawnzone.h"
#include "zonemanager.h"

CSpawnZone::CSpawnZone(CGameContext *pGameServer) :
    IZone(pGameServer, ZONE_SPAWN)
{
    mem_zero(m_aSpawnTicks, sizeof(m_aSpawnTicks));
    mem_zero(m_aSnapIds, sizeof(m_aSnapIds));
}

void CSpawnZone::Tick()
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

        if(InZone)
        {
            KeepInSpawn(i, GameServer()->Server()->TickSpeed());
        }
        else
        {
            continue;
        }
    }
}

void CSpawnZone::Snap(int ClientID)
{
    // don't need to snap anything
}

void CSpawnZone::OnCharacterDeath(CCharacter *pCharacter)
{
    int ClientID = pCharacter->GetPlayer()->GetCid();

    m_aSpawnTicks[ClientID] = 0;
    m_aSnapIds[ClientID] = 0;
    m_aIsInSpawnZone[ClientID] = false;
}

void CSpawnZone::KeepInSpawn(int ClientID, int Ticks)
{
    CCharacter *pChar = GameServer()->GetPlayerChar(ClientID);
    if(!pChar)
        return;

    m_aSpawnTicks[ClientID] = Ticks;
}
