#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_H

#include "core/component.h"
#include "events/event.h"

#include <engine/console.h>

#include <map>

class CEvents final : public CComponent
{
public:
	static constexpr const char *GetNameStatic() { return "Events"; }
	[[nodiscard]] const char *GetName() const override { return GetNameStatic(); };
	[[nodiscard]] std::vector<ComponentAccessor<CComponent>> GetSubComponents() const override;

protected:
	void OnDisable() override;

	void OnConsoleInit() override;
	void OnConsoleTerminate() override;

	void OnTick() override;
	void OnPostTick() override;

	static void ConEventsStatus(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsList(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsStart(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsForceNextState(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsForceEnd(IConsole::IResult *pResult, void *pUserData);

	static void ConJoin(IConsole::IResult *pResult, void *pUserData);
	static void ConLeave(IConsole::IResult *pResult, void *pUserData);

public:
	explicit CEvents(CGameContext *pGameServer);
	~CEvents() override;

private:
	std::shared_ptr<CEventComponent> m_pActiveEvent;
	std::shared_ptr<CEventComponent> m_pEventToDelete;

	using FnFactory = std::function<std::shared_ptr<CEventComponent>(class CGameContext *)>;
	std::map<std::string, FnFactory> m_EventsFactory;

	void OnEventStateChange(CEventComponent::EEventState OldState, CEventComponent::EEventState NewState);
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_H
