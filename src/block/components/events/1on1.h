
#ifndef BLOCK_COMPONENTS_EVENTS_1ON1_H
#define BLOCK_COMPONENTS_EVENTS_1ON1_H

#include "1on1_utils.h"
#include "event.h"

#include <base/vmath.h>

#include <game/server/gamecontext.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

// match configuration set during the Preparation (config) phase
struct SMatchConfig
{
	int m_PointsLimit = 10; // first to X wins (3, 5, 7, 10)
	int m_TimeLimit = 0; // seconds (0=unlimited, 120=2min, 300=5min, 600=10min)
	bool m_EndlessHook = false;
	bool m_aWeapons[6] = {true, true, false, false, false, false}; // HAMMER..NINJA (default: hammer+gun only)
	int m_SpawnMode = 0; // 0=normal (1on1_spawn gamezone quads), 1=random (1on1_rdm zone)
};

class COneOnOneEvent : public CComponent
{
public:
	explicit COneOnOneEvent(CGameContext *pGameServer);
	~COneOnOneEvent() override;

	// CComponent name
	[[nodiscard]] const char *GetName() const override { return "1on1"; }

	// lifecycle
	bool StartEvent();
	void FinishEvent();
	void ForceNextStage() { FinishEvent(); }
	bool CheckEndCondition();

	bool Leave(int ClientId);

	bool Initialize(int Player1ID, int Player2ID, int Wager = 0);

	// configuration phase: after acceptance, enter Preparation state for config
	bool InitializeConfigPhase(int Player1ID, int Player2ID, int Wager = 0);
	void StartMatchFromConfig(); // called when both players click Ready
	bool IsInConfigPhase() const { return GetState() == EEventState::Preparation; }

	SMatchConfig m_Config;
	// duel vote state per player: 0=pending, 1=voted Start (F3), -1=voted Cancel (F4)
	int m_aDuelVote[2] = {0, 0};
	int64_t m_DuelVoteLastSendTick = -1;
	int m_ConfigStartTick = -1; // tick when config phase started

	// Private F3/F4 vote overlay - called from CGameContext::OnVoteNetMessage
	bool OnDuelVote(int ClientId, int Vote);
	void ResetDuelReadyVotes(const char *pReason = nullptr);
	void SendDuelVoteUi(); // sends/refreshes the vote overlay to both players
	void ClearDuelVoteUi(); // clears the vote overlay for both players

	void OnTick() override;
	void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;
	void OnPlayerDropping(int ClientId) override;
	void EmergencyShutdown(const char *pMsg);

	[[nodiscard]] std::optional<int> GetScoreOf(int ClientId) const;
	const S1on1SpawnReservation &GetSpawnReservation() const { return m_SpawnReservation; }

	// Arena spawn positions for the configured spawn mode. Falls back to the other
	// source when the configured one is missing on this map, so that a running match
	// never leaves players to the regular map spawns.
	[[nodiscard]] std::vector<vec2> GetArenaSpawnPositions() const;

	// Reserves one distinct slot per player out of PositionCount, or clears the
	// reservation when there is nothing to reserve.
	void PickSpawnReservation(int PositionCount);

	// Spawns a participant at its reserved slot, clamping to a valid slot so the
	// player always ends up inside the arena.
	void SpawnAtReservedSlot(class CPlayer *pPlayer, const std::vector<vec2> &Positions, int Idx);

private:
public:
	// Minimal event-like state copied from CEventComponent so 1on1 can be independent
	enum class EEventState
	{
		Created,
		Registration,
		Preparation,
		Active,
		Ending,
		Finished
	};

	[[nodiscard]] EEventState GetState() const { return m_State.load(); }
	mutable std::mutex m_Mutex;
	[[nodiscard]] const char *GetStateName() const;
	[[nodiscard]] static const char *GetStateName(EEventState State);

	void SetState(EEventState NewState);

	void SavePosition(int ClientId);
	void LoadPosition(int ClientId);

	void SaveWeapons(int ClientId);
	void LoadWeapons(int ClientId);

	void SaveAndClearCosmetics(int ClientId);
	void RestoreCosmetics(int ClientId);

	int PlayerHookedGroundFor(int ClientId) const;

	// state
	std::atomic<EEventState> m_State{EEventState::Created};

	using FnOnStateChange = std::function<void(EEventState OldState, EEventState NewState)>;
	FnOnStateChange m_pfnOnStateChange;

	std::map<int, std::unique_ptr<class CSaveTee>> m_pSavedPlayers;
	std::map<int, std::array<CCharacterCore::CWeaponStat, NUM_WEAPONS>> m_SavedWeapons;
	std::map<int, CEventComponent::SCosmeticsSnapshot> m_SavedCosmetics;

	std::vector<int> m_Candidates;
	std::vector<int> m_Participants;

	[[nodiscard]] std::vector<int> Participants() const
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		return m_Participants;
	}

	bool m_EmergencyShutdown = false;
	char m_EmergencyMessage[256]{};

	S1on1SpawnReservation m_SpawnReservation;
	int m_Player1ID;
	int m_Player2ID;
	std::atomic<int> m_Score1{0};
	std::atomic<int> m_Score2{0};
	int m_Wager;
	int m_Team;
	int64_t m_StartTimer;
	int64_t m_CurrentTick;
	bool m_SuppressFinishBroadcast;

	// wager escrow handling
	bool m_EscrowCollected = false; // true once we've deducted Wager from both players
	int m_EscrowBalance = 0; // expected to be 2 * m_Wager after collection, 0 after payout/refund

	// draw handling helpers
	int m_Player1DeathTick = -1;
	int m_Player2DeathTick = -1;
	int m_LastAwardedPlayer = 0; // kept for telemetry; no longer used for draw reverts
	int m_LastAwardedTick = -1;
	bool m_DrawRestartInProgress = false; // guard to skip scoring during forced deaths
	bool m_DrawRestartPending = false; // (future use if we defer restarts)
	int m_RoundStartTick = -1; // tick when current round (initial or after draw) started
	int m_BothFrozenSinceTick = -1; // tick when both players became frozen simultanoeusly
	void RestartRoundAfterDraw();

	// deferred scoring to avoid wrong awards on same-tick or near-tick dual deaths
	int m_PendingAwardTo = 0; // 0 none, 1 award to P1, 2 award to P2
	int m_PendingAwardTick = -1; // tick when pending award was set

	// match time limit tracking (set from m_Config.m_TimeLimit when match starts)
	int m_MatchStartTick = -1; // tick when Active phase began (for time limit)

	// perma-freeze tracking (in-freeze tiles) to evaluate draw-at-death consistently
	bool m_P1InFreezeTile = false;
	bool m_P2InFreezeTile = false;
	int m_P1InFreezeTileTick = -1;
	int m_P2InFreezeTileTick = -1;

	// general freeze tracking for penalties
	bool m_P1Frozen = false;
	bool m_P2Frozen = false;
	int m_P1FrozenTick = -1;
	int m_P2FrozenTick = -1;

	// solo & collision state tracking for participants
	struct SoloCollisionState
	{
		bool m_Solo;
		bool m_Collision;
	};
	std::map<int, SoloCollisionState> m_PrevSoloState;

	// finish handling
	bool m_DeferFinishRestore = false;
	int m_RestoreAtTick = -1;

	// force winner handling (e.g., ragequit): if set, FinishEvent picks this winner
	int m_ForcedWinnerCid = -1;

	// helpers
	bool CollectEscrow();
	void RefundEscrow();
	void PayoutWinner(class CPlayer *pWinner, class CPlayer *pLoser);
	void AbortAndRefund(const char *pReason);
	void CheckFreezePenalties();
};

// End of class

#endif // BLOCK_COMPONENTS_EVENTS_1ON1_H
