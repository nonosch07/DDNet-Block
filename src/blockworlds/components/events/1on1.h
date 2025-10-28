
#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_H

#include <base/vmath.h>
#include <game/server/gamecontext.h>
#include <vector>

#include "1on1_utils.h"

#include "event.h"
#include <atomic>
#include <memory>
#include <mutex>

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

	void OnTick() override;
	void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;
	void OnPlayerDropping(int ClientId) override;
	void EmergencyShutdown(const char *pMsg);

	[[nodiscard]] std::optional<int> GetScoreOf(int ClientId) const;
	const S1on1SpawnReservation &GetSpawnReservation() const { return m_SpawnReservation; }

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

	int PlayerHookedGroundFor(bool ClientId) const;

	// state
	std::atomic<EEventState> m_State{EEventState::Created};

	using FnOnStateChange = std::function<void(EEventState OldState, EEventState NewState)>;
	FnOnStateChange m_pfnOnStateChange;

	std::map<int, std::unique_ptr<class CSaveTee>> m_pSavedPlayers;
	std::map<int, std::array<CCharacterCore::WeaponStat, NUM_WEAPONS>> m_SavedWeapons;

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
	int m_RoundStartTick = -1; // tick when curent round (initial or after draw) started
	int m_BothFrozenSinceTick = -1; // tick when both players became frozen simultanoeusly
	void RestartRoundAfterDraw();

	// deferred scoring to avoid wrong awards on same-tick or near-tick dual deaths
	int m_PendingAwardTo = 0; // 0 none, 1 award to P1, 2 award to P2
	int m_PendingAwardTick = -1; // tick when pending award was set

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
		bool solo;
		bool collision;
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

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_H
