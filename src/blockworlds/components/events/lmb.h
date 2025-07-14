#ifndef GAME_SERVER_BLOCKWORLDS_COMPONENTS_EVENTS_LMB_H
#define GAME_SERVER_BLOCKWORLDS_COMPONENTS_EVENTS_LMB_H

#include "event.h"

class CLastManBlockingEvent final : public CEventComponent
{
public:
	explicit CLastManBlockingEvent(CGameContext *pGameContext);

public:
	const char *GetName() const override { return "LMB"; }
 	const char *GetEventName() const override { return "LMB"; }

	void OnTick() override;

	void OpenRegistration() override;
	void CloseRegistration() override;
	void StartEvent() override;
	void FinishEvent() override;

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

	int m_DDRaceTeam;

	int m_Winner;
};

#endif // GAME_SERVER_BLOCKWORLDS_COMPONENTS_EVENTS_LMB_H
