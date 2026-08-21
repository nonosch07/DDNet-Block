#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_ZCATCH_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_ZCATCH_H

#include "event.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

// zCatch block event:
// Players who are block-killed become "caught" spectators locked on to their killer
// when a catcher is themselves caught, all their captives are released back into the arena
// first player to reach SvZCatchKillsToWin catches wins, they can also win by killing every other participants
class CZCatchEvent final : public CEventComponent, public std::enable_shared_from_this<CZCatchEvent>
{
public:
	explicit CZCatchEvent(CGameContext *pGameServer);

	[[nodiscard]] const char *GetName() const override { return "zcatch"; }
	[[nodiscard]] const char *GetEventName() const override { return "zCatch"; }

	void OnTick() override;
	void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;
	void OnEventPlayerDropping(int ClientId) override;
	void OnSnapPlayerInfo(int ClientId, int SnappingClient, struct CNetObj_PlayerInfo *pPlayerInfo) override;

	void OpenRegistration() override;
	void CloseRegistration() override;
	void StartEvent() override;
	void FinishEvent() override;
	void ForceNextStage() override;

	bool CheckEndCondition() override;

	bool Register(int ClientId) override;
	bool DeRegister(int ClientId) override;
	bool Join(int ClientId) override;
	bool Leave(int ClientId) override;

	void EmergencyShutdown(const char *pMsg) override;

	void OnBlockedKill(int VictimID, int KillerID) override;

	// track hammer/hook impacts to attribute catches when block tracker is not active
	void OnPlayerImpacted(int VictimId, int InitiatorId) override;

	// allow selfkills only when there is a valid last impactor who can receive the catch credit
	[[nodiscard]] bool AllowKillCommandFor(int ClientId) const override;
	[[nodiscard]] int GetMinCandidates() const override;

	[[nodiscard]] std::optional<int> GetScoreOf(int ClientId) const override;

private:
	// --- timing ---
	int m_RegistrationEndTick = -1;
	int m_ActiveStartTick = -1;
	int m_ActiveEndTick = -1;

	// --- arena ---
	std::set<int> m_UsedSpawnIndices;
	std::vector<vec2> m_SpawnPositions;
	int m_DDRaceTeam = -1;

	// --- state ---
	int m_Winner = -1;

	// kill counts (only free/fighting players increment this by catching others)
	std::map<int, int> m_Scores;

	// caught[victim] = catcher: victim is spectating catcher
	std::map<int, int> m_CaughtBy;

	// captives[catcher] = set of victims caught by this catcher
	std::map<int, std::set<int>> m_Captives;

	// freeze start tick per participant (for freeze-kill timeout detection)
	std::map<int, int> m_FrozenSince;

	// shadow-tracks the last free participant to impact each victim (hammer / hook)
	// used as a fallback catch attribution when the block tracker was not active at death time
	std::map<int, int> m_LastImpactorOf;

	// previous solo/collision state for participants
	struct SSoloCollisionState
	{
		bool solo;
		bool collision;
	};
	std::map<int, SSoloCollisionState> m_PrevSoloState;

	enum EFinishReason
	{
		NATURAL = 0,
		NOT_ENOUGH_CANDIDATES,
		EMERGENCY,
	} m_FinishReason = NATURAL;

	void FinishEvent(EFinishReason Reason)
	{
		m_FinishReason = Reason;
		FinishEvent();
	}

	// --- helpers ---
	bool IsCandidate(int ClientId) const;
	bool IsParticipant(int ClientId) const;
	bool IsCaught(int ClientId) const;
	bool IsFighting(int ClientId) const;

	// Release all captives of a catcher back into the arena.
	void ReleaseCaptives(int CatcherId);

	// Force a caught player into spectator mode watching their catcher.
	void LockToSpectator(int VictimId, int WatchedId);

	// Return a free arena spawn position cycling through m_SpawnPositions.
	vec2 NextSpawnPos();
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_ZCATCH_H
