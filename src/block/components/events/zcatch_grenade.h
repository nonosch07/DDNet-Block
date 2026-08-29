#ifndef BLOCK_COMPONENTS_EVENTS_ZCATCH_GRENADE_H
#define BLOCK_COMPONENTS_EVENTS_ZCATCH_GRENADE_H

#include "event.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

// zCatch Grenade event:
// Classic grenade-launcher-only zCatch gameplay.
// Players who are killed become "caught" spectators locked on to their killer.
// When a catcher dies, all their captives are released back into the arena.
// First player to reach SvZCatchKillsToWin catches wins, or last free fighter standing.
class CZCatchGrenadeEvent final : public CEventComponent, public std::enable_shared_from_this<CZCatchGrenadeEvent>
{
public:
	explicit CZCatchGrenadeEvent(CGameContext *pGameServer);

	[[nodiscard]] const char *GetName() const override { return "zcatch_grenade"; }
	[[nodiscard]] const char *GetEventName() const override { return "zCatch Grenade"; }

	void OnTick() override;
	void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;
	void OnCharacterTakeDamage(vec2 Force, int Dmg, int From, int ClientId, int Weapon) override;
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

	[[nodiscard]] std::optional<int> GetScoreOf(int ClientId) const override;
	// Allow zoom only for currently dead players
	[[nodiscard]] bool AllowZoomFor(int ClientId) const override;
	[[nodiscard]] int GetMinCandidates() const override;

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

	// Anticamper values
	int m_CampTick[MAX_CLIENTS];
	vec2 m_CampPos[MAX_CLIENTS];
	bool m_SentCampMsg[MAX_CLIENTS];

	// previous solo/collision state for participants
	struct SSoloCollisionState
	{
		bool m_Solo;
		bool m_Collision;
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

	// Anticamper, freezes a player whenever he stays around the same spot.
	void HandleCamping(int ClientId);

	// Release all captives of a catcher back into the arena.
	void ReleaseCaptives(int CatcherId);

	// Force a caught player into spectator mode watching their catcher.
	void LockToSpectator(int VictimId, int WatchedId);

	// Return a free arena spawn position cycling through m_SpawnPositions.
	vec2 NextSpawnPos();

	// Give grenade only to a character (replaces any current weapons).
	void ArmWithGrenade(class CCharacter *pChar);
};

#endif // BLOCK_COMPONENTS_EVENTS_ZCATCH_GRENADE_H
