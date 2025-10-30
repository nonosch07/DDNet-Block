#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_TDM_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_TDM_H

#include "event.h"
#include <array>
#include <base/vmath.h>
#include <map>
#include <vector>
#include <random>

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

protected:
	bool IsCandidate(int ClientId) const;
	bool IsParticipant(int ClientId) const;

private:
	int m_RegistrationEndTick;
	int m_ActiveStartTick;
	int m_ActiveEndTick;

	// per-team spawn positions (0 = blue, 1 = red)
	std::vector<vec2> m_SpawnPositionsTeam[2];
	// per-team spawn quads (full quad corner coords) - prefer spawning inside quads when present
	std::vector<std::array<vec2, 4>> m_SpawnQuadsTeam[2];
	// store previous solo and collision state for each participant (mirrors LMB behaviour)
	struct SoloCollisionState
	{
		bool solo;
		bool collision;
	};
	std::map<int, SoloCollisionState> m_PrevSoloState;
	int m_SpawnOffsetTeam[2];

	// event start positions (used for respawning participants after death)
	std::vector<vec2> m_EventStartPositions;

	int m_DDRaceTeam;
	int m_ScoreTeam1;
	int m_ScoreTeam2;
	int m_PointsPerKill;
	int m_TargetScore;

	// maps client id -> team index (0 or 1)
	std::map<int, int> m_ClientTeam;

	static constexpr int MIN_PLAYERS = 8;
	static constexpr int MAX_PLAYERS = 16;
	static constexpr int REGISTRATION_SECONDS = 60; // default registration time

	std::mt19937 m_Rng;
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_TDM_H
