#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_PRIV_TDM_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_PRIV_TDM_H

#include "event.h"

// Private event placeholder: Private TDM (XvsX)
class CPrivateTdmEvent final : public CEventComponent
{
public:
	explicit CPrivateTdmEvent(CGameContext *pGameServer) :
		CEventComponent(pGameServer) {}

	[[nodiscard]] const char *GetName() const override { return "priv_tdm"; }
	[[nodiscard]] const char *GetEventName() const override { return "Private TDM"; }

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

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_PRIV_TDM_H
