#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_LMB_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_LMB_H

#include "event.h"

#include <set>
#include <vector>

class CLastManBlockingEvent final : public CEventComponent, public std::enable_shared_from_this<CLastManBlockingEvent>
{
public:
	explicit CLastManBlockingEvent(CGameContext *pGameContext);

public:
	[[nodiscard]] const char *GetName() const override { return "LMB"; }
	[[nodiscard]] const char *GetEventName() const override { return "LMB"; }
	[[nodiscard]] int GetMinCandidates() const override;

	void OnTick() override;
	void OnSnapClientInfo(int ClientId, int SnappingClient, struct CNetObj_ClientInfo *pClientInfo) override;
	void OnSnapPlayerInfo(int ClientId, int SnappingClient, struct CNetObj_PlayerInfo *pPlayerInfo) override;

	void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
	void OnEventPlayerDropping(int ClientId) override;

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

protected:
	bool IsCandidate(int ClientId) const;
	bool IsParticipant(int ClientId) const;

	void CheckFreezeTime();

private:
	int m_RegistrationEndTick;
	int m_ActiveStartTick;
	int m_ActiveEndTick;

	std::set<int> m_UsedSpawnIndices;
	std::vector<vec2> m_SpawnPositions;

	int m_DDRaceTeam;

	int m_Winner;

	std::map<int, int> m_FrozenSince;
	int GetFrozenSince(int ClientId) const;
	void SetFrozenSince(int ClientId, int Tick);

	// store previous solo and collision state for each participant
	struct SoloCollisionState
	{
		bool solo;
		bool collision;
	};
	std::map<int, SoloCollisionState> m_PrevSoloState;

	enum FinishingReason
	{
		NATURAL = 0,
		NOT_ENOUGH_CANDIDATES,
		EMERGENCY,
	} m_FinishingReason;
	void FinishEvent(FinishingReason Reason)
	{
		m_FinishingReason = Reason;
		FinishEvent();
	};
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_LMB_H
