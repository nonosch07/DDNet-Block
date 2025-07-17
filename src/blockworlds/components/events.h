#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_H

#include "core/component.h"
#include "events/event.h"

#include <engine/console.h>

class CEvents final : public CComponent
{
public:
	static constexpr const char *GetNameStatic() { return "Events"; }
	[[nodiscard]] const char *GetName() const override { return GetNameStatic(); };
	[[nodiscard]] std::vector<CComponent *> GetSubComponents() const override;

protected:
	void OnDisable() override;

	void OnConsoleInit() override;
	void OnConsoleTerminate() override;

	void OnTick() override;
	void OnPostTick() override;

	static void ConEventsTest(IConsole::IResult *pResult, void *pUserData);

public:
	explicit CEvents(CGameContext *pGameServer);

private:
	class CEventComponent *m_pActiveEvent;
	class CEventComponent *m_pEventToDelete;

	static void OnEventStateChange(CEventComponent::EEventState OldState, CEventComponent::EEventState NewState, void *pUserData);
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_H
