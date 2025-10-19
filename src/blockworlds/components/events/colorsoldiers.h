#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_COLORSOLDIERS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_COLORSOLDIERS_H

#include "event.h"

// Public event placeholder: Color Soldiers
class CColorSoldiersEvent final : public CEventComponent
{
public:
	explicit CColorSoldiersEvent(CGameContext *pGameServer) : CEventComponent(pGameServer) {}

	[[nodiscard]] const char *GetName() const override { return "colorsoldiers"; }
	[[nodiscard]] const char *GetEventName() const override { return "Color Soldiers"; }

	void OpenRegistration() override { SetState(EEventState::Registration); }
	void CloseRegistration() override { SetState(EEventState::Preparation); StartEvent(); }
	void StartEvent() override { SetState(EEventState::Active); }
	void FinishEvent() override { SetState(EEventState::Finished); }
	void ForceNextStage() override { FinishEvent(); }
	bool CheckEndCondition() override { return false; }

	bool Register(int) override { return false; }
	bool DeRegister(int) override { return false; }
	bool Join(int) override { return false; }
	bool Leave(int) override { return false; }
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_COLORSOLDIERS_H
