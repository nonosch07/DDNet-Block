#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_TDM_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_TDM_H

#include "event.h"
#include <base/vmath.h>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

class CTeamDeathmatchEvent final : public CEventComponent, public std::enable_shared_from_this<CTeamDeathmatchEvent>
{
public:
	explicit CTeamDeathmatchEvent(CGameContext *pGameContext);

	[[nodiscard]] const char *GetName() const override { return "tdm"; }
	[[nodiscard]] const char *GetEventName() const override { return "Team Deathmatch"; }

	void OnTick() override;
	void OnSnapClientInfo(int ClientId, int SnappingClient, class CNetObj_ClientInfo *pClientInfo) override;
	void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
	void OnPlayerDropping(int ClientId) override;

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

	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;

	[[nodiscard]] std::optional<int> GetTeamIndexFor(int ClientId) const override;

protected:
	bool IsCandidate(int ClientId) const;
	bool IsParticipant(int ClientId) const;

private:
	// timing
	int m_RegistrationEndTick = -1;
	int m_ActiveStartTick = -1;
	int m_ActiveEndTick = -1;

	// spawns: use only shared event start positions (TILE_BW_EVENT_TDM_START_POS)
	std::vector<vec2> m_EventStartPositions;

	// team/score state
	int m_DDRaceTeam = -1;
	int m_ScoreTeam[2] = {0, 0};
	int m_PointsPerKill = 1;
	int m_TargetScore = 80; // adaptive later
	std::map<int, int> m_ClientTeam; // ClientId -> 0 (blue) or 1 (red)

	// participant state
	struct SoloCollisionState
	{
		bool solo = false;
		bool collision = false;
	};
	std::map<int, SoloCollisionState> m_PrevSoloState;
	std::unordered_map<int, int> m_FrozenSince; // ClientId -> tick when got frozen (0 = not frozen)

	// temporary, per-event player stats
	struct SPlayerStats
	{
		int Kills = 0;
		int Deaths = 0;
	};
	std::map<int, SPlayerStats> m_PlayerStats; // ClientId -> stats for this TDM event

	// utils no tneeded
	std::mt19937 m_Rng;

	// helpers (in cpp file)
	void AssignTeamsShuffled();
	void ApplyParticipantVisuals(int ClientId, int Side);
	void RestoreParticipantVisuals(int ClientId);
	void SaveAndPrepareParticipants();
	void RestoreParticipants();
	std::optional<vec2> ChooseSpawnFor(int ClientId);
	void TeleportToSpawn(int ClientId);
	void BroadcastStatus();
	void EnsureForcedTeamForAll();
	void TrackFreezeAndAutokill();
	void ApplyGroundHookPenalty(int ClientId);
	void UpdatePerPlayerScores();
	void ResetTransientState();
	int Opposite(int Side) const { return Side ^ 1; }
	int GetSideOf(int ClientId) const;
	void SetFrozenSince(int ClientId, int Tick);
	int GetFrozenSince(int ClientId) const;

	// results helpers
	void ResetStats();
	void AnnounceResults();
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_TDM_H
