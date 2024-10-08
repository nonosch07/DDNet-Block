#include "blocktracker.h"
#include <engine/shared/config.h>
#include <game/server/blockworlds/accounts.h>
#include <game/server/blockworlds/entities/experience.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <unordered_map>

CBlockTracker::CBlockTracker(CGameContext *pGameServer)
    : m_pGameContext(pGameServer)
{
    for (int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
        m_aTrackedPlayers[ClientID].m_Tracked = false;
}

float CBlockTracker::SecondsPassed(int SinceTick) const
{
    int Tick = m_pGameContext->Server()->Tick();
    int TickSpeed = m_pGameContext->Server()->TickSpeed();
    return static_cast<float>(Tick - SinceTick) / TickSpeed;
}

bool CBlockTracker::Blocked(int ClientID, int BlockerID)
{
    if (!m_pGameContext->PlayerExists(ClientID) || !m_pGameContext->PlayerExists(BlockerID))
        return false;

    if (m_pGameContext->Server()->IsClientsSameAddr(ClientID, BlockerID) && !g_Config.m_SvAllowExpFromSameIp)
        return false;

    CCharacter *pChr = m_pGameContext->GetPlayerChar(ClientID);
    if (!pChr)
        return false;

    int64_t CurrentTick = m_pGameContext->Server()->Tick();
    int64_t TickSpeed = m_pGameContext->Server()->TickSpeed();
    int64_t AliveTime = CurrentTick - pChr->m_AliveSince;

    if (AliveTime < g_Config.m_SvBlockMinAliveTime * TickSpeed)
        return false;

    auto &LastBlockedTime = m_aTrackedPlayers[ClientID].m_LastBlockedTime;
    if (LastBlockedTime.find(ClientID) != LastBlockedTime.end())
    {
        int64_t LastBlockTick = LastBlockedTime[ClientID];
        if ((CurrentTick - LastBlockTick) < g_Config.m_SvBlockInterval * TickSpeed)
            return false;
    }

    auto &BlockerExpCount = m_aTrackedPlayers[ClientID].m_BlockerExpCount;
    if (BlockerExpCount.find(BlockerID) != BlockerExpCount.end() && BlockerExpCount[BlockerID] >= 2)
    {
        return false;
    }

    BlockerExpCount[BlockerID]++;
    LastBlockedTime[ClientID] = CurrentTick;

    new CExperience(&m_pGameContext->m_World, pChr->m_Pos, g_Config.m_SvBlockExperience, BlockerID);
    
    KillStreaks(ClientID, BlockerID);

    m_pGameContext->Cosmetics()->DoKnockoutEffect(m_aTrackedPlayers[ClientID].m_ImpactedClientID, pChr->m_Pos);

    return true;
}


void CBlockTracker::KillStreaks(int ClientID, int BlockerID)
{
    CCharacter *pBlockerChr = m_pGameContext->GetPlayerChar(BlockerID);
    CCharacter *pClientChr = m_pGameContext->GetPlayerChar(ClientID);

    if (!pBlockerChr || !pClientChr)
        return;

    pBlockerChr->m_KillStreak++;

    if (pBlockerChr->m_KillStreak % g_Config.m_SvKillStreakCount == 0)
    {
        char aBuf[64];
        str_format(aBuf, sizeof(aBuf), "'%s' has a killstreak of %i!", m_pGameContext->Server()->ClientName(BlockerID), pBlockerChr->m_KillStreak);
        m_pGameContext->SendChat(-1, -2, aBuf);
    }

    if (pClientChr->m_KillStreak >= g_Config.m_SvKillStreakCount)
    {
        char aBuf[64];
        str_format(aBuf, sizeof(aBuf), "'%s's killing spree of %d ended by '%s'!", m_pGameContext->Server()->ClientName(ClientID), pClientChr->m_KillStreak, m_pGameContext->Server()->ClientName(BlockerID));
        m_pGameContext->SendChat(-1, -2, aBuf);
    }

    pClientChr->m_KillStreak = 0;
}

void CBlockTracker::Tick()
{
    for (int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
    {
        auto &Player = m_aTrackedPlayers[ClientID];

        if (!Player.m_Tracked)
            continue;

        if (Player.m_FreezedTick >= 0 && SecondsPassed(Player.m_FreezedTick) >= g_Config.m_SvBlockFreezedInterval && Player.m_ImpactedClientID >= 0)
        {
            if (Blocked(ClientID, Player.m_ImpactedClientID))
            {
                Player.m_IsResisted = false;
                Player.m_ImpactedClientID = -1;
                Player.m_LastImpactedTick = -1;
            }
        }

        if (Player.m_UnfreezedTick >= 0 && SecondsPassed(Player.m_UnfreezedTick) > g_Config.m_SvBlockResetUnfreezedInterval && SecondsPassed(Player.m_LastImpactedTick) > g_Config.m_SvBlockResetNoImpactInterval)
        {
            Player.m_ImpactedClientID = -1;
            Player.m_LastImpactedTick = -1;
        }
    }
}

void CBlockTracker::StartTrackPlayer(int ClientID)
{
    STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
    Player.m_Tracked = true;
    Player.m_IsResisted = false;
    Player.m_LastActionTick = -1;
    Player.m_ImpactedClientID = -1;
    Player.m_LastImpactedTick = -1;
    Player.m_FreezedTick = -1;
    Player.m_UnfreezedTick = m_pGameContext->Server()->Tick();
    Player.m_KilledTick = m_pGameContext->Server()->Tick();

    Player.m_BlockerExpCount.clear();
}


void CBlockTracker::StopTrackPlayer(int ClientID)
{
    STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
    Player.m_Tracked = false;
    Player.m_LastBlockedTime.clear();
}

void CBlockTracker::OnPlayerFreeze(int ClientID)
{
    STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
	if (!Player.m_Tracked || Player.m_FreezedTick != -1)
        return;

    Player.m_FreezedTick = m_pGameContext->Server()->Tick();
    Player.m_UnfreezedTick = -1;

    if (SecondsPassed(Player.m_LastActionTick) < g_Config.m_SvBlockImpactIntervalToResist)
        Player.m_IsResisted = true;
}

void CBlockTracker::OnPlayerUnfreeze(int ClientID)
{
    STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
    if (!Player.m_Tracked)
        return;

    Player.m_FreezedTick = -1;
    Player.m_UnfreezedTick = m_pGameContext->Server()->Tick();
}

void CBlockTracker::OnPlayerImpacted(int ClientID, int InitiatorID)
{
    if (ClientID == InitiatorID)
        return;

    STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
    if (!Player.m_Tracked || Player.m_FreezedTick >= 0 || SecondsPassed(Player.m_UnfreezedTick) < g_Config.m_SvBlockUnfreezeNoImpactInterval)
        return;

    Player.m_ImpactedClientID = InitiatorID;
    Player.m_LastImpactedTick = m_pGameContext->Server()->Tick();
}

bool CBlockTracker::OnPlayerKill(int ClientID)
{
    STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
    if (Player.m_ImpactedClientID < 0 || !Player.m_Tracked)
        return false;

    Blocked(ClientID, Player.m_ImpactedClientID);

    OnPlayerDeath(ClientID);

    CCharacter *pChr = m_pGameContext->GetPlayerChar(ClientID);
    if (CPlayer *pPlayer = pChr->GetPlayer(); pPlayer && pPlayer->IsLoggedIn())
    {
        pPlayer->SetPlayerDeaths(pPlayer->GetPlayerDeaths() + 1);
    }

    return true;
}


void CBlockTracker::OnPlayerDeath(int ClientID)
{
    STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
    Player.m_BlockerExpCount.clear();

    Player.m_IsResisted = false;
    Player.m_ImpactedClientID = -1;
    Player.m_LastImpactedTick = -1;
    Player.m_FreezedTick = -1;
    Player.m_UnfreezedTick = -1;
    Player.m_KilledTick = m_pGameContext->Server()->Tick();
}


