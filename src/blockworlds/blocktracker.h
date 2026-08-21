#ifndef BLOCKWORLDS_BLOCKTRACKER_H
#define BLOCKWORLDS_BLOCKTRACKER_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
class CGameContext;

class CBlockTracker
{
public:
	struct SHourlyStats
	{
		int m_Kills = 0;
		int m_Deaths = 0;
		int m_BestStreak = 0;
		bool m_Active = false; // has participated at least once this interval
	};

private:
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
		vec2 m_SpawnPos; // for movement distance checks

		std::unordered_map<int, int64_t> m_LastBlockedTime;
		std::unordered_map<int, int> m_BlockerExpCount;
	};

	struct SKillEvent
	{
		int m_Killer;
		int m_Victim;
		int64_t m_Tick;
	};

	struct SKillerRecent
	{
		// victim id -> (count, firstTick, lastTick, wasAfkVictimCount aggregated separately)
		struct SVictimStats
		{
			int Count;
			int64_t FirstTick;
			int64_t LastTick;
			bool LastWasAfk;
		};
		std::unordered_map<int, SVictimStats> m_Victims;
		int m_TodayExp; // for daily soft cap
		int m_TodayDate; // yyyymmdd to reset
		int m_LoopSuppressedUntilTick = 0;
		std::string m_LastDebugMsg; // suppress duplicate spam
	};

	CGameContext *m_pGameContext;
	STrackedPlayer m_aTrackedPlayers[MAX_CLIENTS];
	SKillerRecent m_aKillerStats[MAX_CLIENTS];
	SHourlyStats m_aHourlyStats[MAX_CLIENTS];
	std::deque<SKillEvent> m_GlobalKillBuffer; // for loop detection across pairs

	// internal helpers
	int GetActiveNonAfkPlayers() const;
	bool IsPlayerActive(int ClientID) const;
	bool PassedRecentActionChecks(int VictimID, int KillerID) const;
	bool PassedSameVictimLimit(int VictimID, int KillerID, int64_t NowTick);
	float PopulationScale() const;
	float UniqueVictimRatio(int KillerID) const;
	bool DetectLoopPattern(int KillerID, int VictimID, int64_t NowTick);
	float LevelDiffScale(int KillerID, int VictimID) const;
	float DailySoftCapScale(int KillerID);
	float AfkVictimRatio(int KillerID) const;
	void RecordKill(int KillerID, int VictimID, bool VictimWasAfk, int64_t NowTick);
	void DebugMsg(int KillerID, const char *pMsg) const;

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

	// Hourly session stats (not account-bound, covers all in-game players)
	const SHourlyStats &GetHourlyStats(int ClientID) const { return m_aHourlyStats[ClientID]; }

	// Returns the client ID currently tracked as the impactor for ClientId, or -1 if none.
	int GetImpactorOf(int ClientId) const { return (ClientId >= 0 && ClientId < MAX_CLIENTS) ? m_aTrackedPlayers[ClientId].m_ImpactedClientID : -1; }
	// Returns true when the tracker currently has an active freeze timestamp for ClientId.
	bool IsTrackerFrozen(int ClientId) const { return (ClientId >= 0 && ClientId < MAX_CLIENTS) && m_aTrackedPlayers[ClientId].m_FreezedTick >= 0; }
	void ResetHourlyStats(int ClientID);
	void ResetAllHourlyStats();
};

#endif // BLOCKWORLDS_BLOCKTRACKER_H
