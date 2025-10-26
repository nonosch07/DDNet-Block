
#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_H

#include <base/vmath.h>
#include <game/server/gamecontext.h>
#include <vector>

#include "1on1_utils.h"

#include "event.h"

class COneOnOneEvent : public CEventComponent
{
public:
	explicit COneOnOneEvent(CGameContext *pGameServer);

	const char *GetEventName() const override { return "1on1"; }
	void OpenRegistration() override {}
	void CloseRegistration() override {}
	void StartEvent() override;
	void FinishEvent() override;
	void ForceNextStage() override { FinishEvent(); }
	bool CheckEndCondition() override;

	bool Register(int ClientId) override { return false; }
	bool DeRegister(int ClientId) override { return false; }
	bool Join(int ClientId) override { return false; }
	bool Leave(int ClientId) override;

	void Initialize(int Player1ID, int Player2ID, int Wager = 0);

	void OnTick() override;
	void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;
	void OnPlayerDropping(int ClientId) override;
	void EmergencyShutdown(const char *pMsg) override;

	[[nodiscard]] std::optional<int> GetScoreOf(int ClientId) const override;
	const S1on1SpawnReservation &GetSpawnReservation() const { return m_SpawnReservation; }

private:
	S1on1SpawnReservation m_SpawnReservation;
	int m_Player1ID;
	int m_Player2ID;
	int m_Score1;
	int m_Score2;
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

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_H
