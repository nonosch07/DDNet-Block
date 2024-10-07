#ifndef GAME_SERVER_BLOCKWORLDS_BLOCKTRACKER_H
#define GAME_SERVER_BLOCKWORLDS_BLOCKTRACKER_H

#include <engine/shared/protocol.h>
#include <unordered_map>

class CGameContext;
class CAccountManager;

class CBlockTracker
{
    struct STrackedPlayer
    {
        bool m_Tracked;
        bool m_IsResisted;
        int m_LastActionTick;
        int m_ImpactedClientID;
        int m_LastImpactedTick;
        int m_FreezedTick;
        int m_UnfreezedTick;
        int m_KilledTick;

        std::unordered_map<int, int64_t> m_LastBlockedTime;
        std::unordered_map<int, int> m_BlockerExpCount;
    };

    CGameContext *m_pGameContext;
    STrackedPlayer m_aTrackedPlayers[MAX_CLIENTS];

    float SecondsPassed(int SinceTick) const;
    bool Blocked(int ClientID, int BlockerID);
    void KillStreaks(int ClientID, int BlockerID);

public:
    CBlockTracker(CGameContext *pGameServer);

    void Tick();

    void StartTrackPlayer(int ClientID);
    void StopTrackPlayer(int ClientID);

    void OnPlayerFreeze(int ClientID);
    void OnPlayerUnfreeze(int ClientID);
    void OnPlayerImpacted(int ClientID, int InitiatorID);
    bool OnPlayerKill(int ClientID);
    void OnPlayerDeath(int ClientID);
};

#endif // GAME_SERVER_BLOCKWORLDS_BLOCKTRACKER_H
