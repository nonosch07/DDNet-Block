#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_H

#include "core/component.h"
#include "events/1on1.h"
#include "events/event.h"

#include <engine/console.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

class CEvents final : public CComponent
{
public:
	static constexpr const char *GetNameStatic() { return "events"; }
	[[nodiscard]] const char *GetName() const override { return GetNameStatic(); };
	[[nodiscard]] std::vector<ComponentAccessor<CComponent>> GetSubComponents() const override;

public:
	// high-level category for events
	enum class EEventCategory
	{
		Public,
		Private
	};

protected:
	void OnDisable() override;

	void OnConsoleInit() override;
	void OnConsoleTerminate() override;

	void OnTick() override;
	void OnPostTick() override;

	static void ConEventsStatus(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsList(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsListPublic(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsListPrivate(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsStart(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsForceNextState(IConsole::IResult *pResult, void *pUserData);
	static void ConEventsForceEnd(IConsole::IResult *pResult, void *pUserData);

	static void ConJoin(IConsole::IResult *pResult, void *pUserData);
	static void ConLeave(IConsole::IResult *pResult, void *pUserData);

public:
	explicit CEvents(CGameContext *pGameServer);
	~CEvents() override;

	std::shared_ptr<CEventComponent> CreateEventByName(const char *pName);
	// multi-event managment
	void AddActiveEvent(std::shared_ptr<CEventComponent> pEvent);
	void RemoveActiveEvent(CEventComponent *pEventPtr);
	std::vector<std::shared_ptr<CEventComponent>> GetActiveEvents() const;
	std::vector<std::shared_ptr<COneOnOneEvent>> GetActive1on1Events() const;

	std::vector<std::string> GetEventsByCategory(EEventCategory Category) const;
	std::optional<EEventCategory> GetCategoryOf(const char *pName) const;

private:
	std::vector<std::shared_ptr<CEventComponent>> m_vActiveEvents;
	std::shared_ptr<CEventComponent> m_pEventToDelete;

	using FnFactory = std::function<std::shared_ptr<CEventComponent>(class CGameContext *)>;
	struct SFactoryRec
	{
		EEventCategory m_Category;
		FnFactory m_Factory;
	};
	std::map<std::string, SFactoryRec> m_EventsFactory;

	void OnEventStateChange(CEventComponent::EEventState OldState, CEventComponent::EEventState NewState);
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_H
