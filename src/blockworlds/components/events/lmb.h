#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_LMB_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_LMB_H

#include "event.h"

class CLastManBlockingEvent final : public CEventComponent, public std::enable_shared_from_this<CLastManBlockingEvent>
{
public:
	explicit CLastManBlockingEvent(CGameContext *pGameContext);

public:
	[[nodiscard]] const char *GetName() const override { return "LMB"; }
 	[[nodiscard]] const char *GetEventName() const override { return "LMB"; }

	void OnTick() override;

	void OpenRegistration() override;
	void CloseRegistration() override;
	void StartEvent() override;
	void FinishEvent() override;

	void ForceNextStage() override;

	bool CheckEndCondition() override;

	bool CanPlayerRegister(int ClientId) const override;
	bool Register(int ClientId) override;
	bool DeRegister(int ClientId) override;

	bool Join(int ClientId) override;
	bool Leave(int ClientId) override;

	void EmergencyShutdown(const char *pMsg) override;

private:
	int m_SpawnOffset;
	std::vector<vec2> m_SpawnPositions;

	int m_Timelimit; // end tick
	int m_DDRaceTeam;

	int m_Winner;

	enum FinishingReason
	{
		NATURAL = 0,
		NOT_ENOUGH_CANDIDATES,
		EMERGENCY,
	} m_FinishingReason;
	void FinishEvent(FinishingReason Reason) { m_FinishingReason = Reason; FinishEvent(); };
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_LMB_H
