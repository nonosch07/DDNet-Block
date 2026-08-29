#ifndef BLOCK_COMPONENTS_EVENTS_CLANWAR_H
#define BLOCK_COMPONENTS_EVENTS_CLANWAR_H

#include "event.h"

// Private event placeholder: Clanwar (between clans)
class CClanwarEvent final : public CEventComponent
{
public:
	explicit CClanwarEvent(CGameContext *pGameServer) :
		CEventComponent(pGameServer) {}

	[[nodiscard]] const char *GetName() const override { return "clanwar"; }
	[[nodiscard]] const char *GetEventName() const override { return "Clanwar"; }

	void OpenRegistration() override { SetState(EEventState::Registration); }
	void CloseRegistration() override
	{
		SetState(EEventState::Preparation);
		StartEvent();
	}
	void StartEvent() override { SetState(EEventState::Active); }
	void FinishEvent() override { SetState(EEventState::Finished); }
	void ForceNextStage() override { FinishEvent(); }
	bool CheckEndCondition() override { return false; }

	bool Register(int) override { return false; }
	bool DeRegister(int) override { return false; }
	bool Join(int) override { return false; }
	bool Leave(int) override { return false; }
};

#endif // BLOCK_COMPONENTS_EVENTS_CLANWAR_H
