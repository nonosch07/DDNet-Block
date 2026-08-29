#ifndef BLOCK_COMPONENTS_EVENTS_H
#define BLOCK_COMPONENTS_EVENTS_H

#include "core/component.h"
#include "events/event.h"

#include <engine/console.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

class CEvents final : public CComponent
{
	DECLARE_COMPONENT(CEvents, "events")
	[[nodiscard]] std::vector<CComponentAccessor<CComponent>> GetSubComponents() const override;

public:
	// high-level category for events
	enum class EEventCategory
	{
		Public,
		Private
	};

protected:
	void OnDisable() override;

	void OnTick() override;
	void OnPostTick() override;
	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;

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
	~CEvents() override;

	std::shared_ptr<CEventComponent> CreateEventByName(const char *pName);
	void SetActiveEvent(std::shared_ptr<CEventComponent> pEvent);
	// Returns currently active event or nullptr if none
	std::shared_ptr<CEventComponent> GetActiveEvent() const;

	std::vector<std::string> GetEventsByCategory(EEventCategory Category) const;
	std::optional<EEventCategory> GetCategoryOf(const char *pName) const; // just an helper cause i'm retarded

	// Takes a player out of the registration of the pending event, so that they are
	// not promoted to participant while they are busy elsewhere (1on1).
	// Returns true when the player was registered and got removed.
	bool DropRegistration(int ClientId);

private:
	std::shared_ptr<CEventComponent> m_pActiveEvent;
	std::shared_ptr<CEventComponent> m_pEventToDelete;

	using FnFactory = std::function<std::shared_ptr<CEventComponent>(class CGameContext *)>;
	struct SFactoryRec
	{
		EEventCategory m_Category;
		FnFactory m_Factory;
	};
	std::map<std::string, SFactoryRec> m_EventsFactory; // key: internal event name

	void OnEventStateChange(CEventComponent::EEventState OldState, CEventComponent::EEventState NewState);

	template<typename TEvent>
	void RegisterEvent(std::string_view Name, const EEventCategory Visibility)
	{
		m_EventsFactory.emplace(Name, SFactoryRec{Visibility, [](CGameContext *pGS) { return std::make_shared<TEvent>(pGS); }});
	}
};

#endif // BLOCK_COMPONENTS_EVENTS_H
