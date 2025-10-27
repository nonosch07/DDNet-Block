#include "events.h"

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>

#include <blockworlds/utils/memory.h>

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events/1on1.h>
#include <blockworlds/components/events/clanwar.h>
#include <blockworlds/components/events/colorsoldiers.h>
#include <blockworlds/components/events/event.h>
#include <blockworlds/components/events/lmb.h>
#include <blockworlds/components/events/priv_tdm.h>
#include <blockworlds/components/events/tdm.h>
#include <blockworlds/components/events/zcatch.h>

CEvents::CEvents(CGameContext *pGameServer) :
	CComponent(pGameServer), m_pEventToDelete(nullptr)
{
	// Public events
	m_EventsFactory.emplace("lmb", SFactoryRec{EEventCategory::Public, [](class CGameContext *pGS) { return std::make_shared<CLastManBlockingEvent>(pGS); }});
	m_EventsFactory.emplace("tdm", SFactoryRec{EEventCategory::Public, [](class CGameContext *pGS) { return std::make_shared<CTeamDeathmatchEvent>(pGS); }});
	m_EventsFactory.emplace("zcatch", SFactoryRec{EEventCategory::Public, [](class CGameContext *pGS) { return std::make_shared<CZCatchEvent>(pGS); }});
	m_EventsFactory.emplace("colorsoldiers", SFactoryRec{EEventCategory::Public, [](class CGameContext *pGS) { return std::make_shared<CColorSoldiersEvent>(pGS); }});

	// Private events
	m_EventsFactory.emplace("1on1", SFactoryRec{EEventCategory::Private, [](class CGameContext *pGS) { return std::make_shared<COneOnOneEvent>(pGS); }});
	m_EventsFactory.emplace("priv_tdm", SFactoryRec{EEventCategory::Private, [](class CGameContext *pGS) { return std::make_shared<CPrivateTdmEvent>(pGS); }});
	m_EventsFactory.emplace("clanwar", SFactoryRec{EEventCategory::Private, [](class CGameContext *pGS) { return std::make_shared<CClanwarEvent>(pGS); }});
	// multi-event support for now
	m_vActiveEvents.clear();
}

CEvents::~CEvents()
{
	if(m_pEventToDelete)
	{
		m_pEventToDelete.reset();
		m_pEventToDelete = nullptr;
	}
	m_vActiveEvents.clear();
}

std::vector<ComponentAccessor<CComponent>> CEvents::GetSubComponents() const
{
	std::vector<ComponentAccessor<CComponent>> vSubComponents;
	for(const auto &ev : m_vActiveEvents)
		vSubComponents.emplace_back(ev);
	return vSubComponents;
}

void CEvents::OnDisable()
{
	for(const auto &ev : m_vActiveEvents)
	{
		if(ev)
			ev->EmergencyShutdown("Events Shutdown");
	}
	m_vActiveEvents.clear();
}

#define LIST_OF_ALL_COMMANDS(DEF) \
	DEF("events_status", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsStatus, this, "Status of current event") \
	DEF("events_list", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsList, this, "List all events (public and private)") \
	DEF("events_list_public", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsListPublic, this, "List public events") \
	DEF("events_list_private", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsListPrivate, this, "List private events") \
	DEF("events_start", "r[name]", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsStart, this, "Start specified event") \
	DEF("events_next_stage", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsForceNextState, this, "Force current event to next stage") \
	DEF("events_end", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsForceEnd, this, "Forcefully end current event") \
\
	DEF("join", "", CFGFLAG_SERVER | CFGFLAG_CHAT | CFGFLAG_ANNOUNCE, ConJoin, this, "Chat command, try to register to current event") \
	DEF("leave", "", CFGFLAG_SERVER | CFGFLAG_CHAT | CFGFLAG_ANNOUNCE, ConLeave, this, "Chat command, try to deregister from current event")

void CEvents::OnConsoleInit()
{
#define CommandRegister(name, args, flags, callback, user, help) Console()->Register(name, args, flags, callback, user, help);
	LIST_OF_ALL_COMMANDS(CommandRegister)
#undef CommandRegister
}

std::shared_ptr<CEventComponent> CEvents::CreateEventByName(const char *pName)
{
	if(!pName)
		return nullptr;
	char aClearName[64];
	str_copy(aClearName, pName);
	str_clean_whitespaces(aClearName);
	auto it = m_EventsFactory.find(aClearName);
	if(it == m_EventsFactory.end())
		return nullptr;
	return it->second.m_Factory(GameServer());
}

// Multi-event management
void CEvents::AddActiveEvent(std::shared_ptr<CEventComponent> pEvent)
{
	m_vActiveEvents.push_back(std::move(pEvent));
}

void CEvents::RemoveActiveEvent(CEventComponent *pEventPtr)
{
	m_vActiveEvents.erase(
		std::remove_if(m_vActiveEvents.begin(), m_vActiveEvents.end(),
			[pEventPtr](const std::shared_ptr<CEventComponent> &ev) { return ev.get() == pEventPtr; }),
		m_vActiveEvents.end());
}

std::vector<std::shared_ptr<CEventComponent>> CEvents::GetActiveEvents() const
{
	return m_vActiveEvents;
}

std::vector<std::shared_ptr<COneOnOneEvent>> CEvents::GetActive1on1Events() const
{
	std::vector<std::shared_ptr<COneOnOneEvent>> result;
	for(const auto &ev : m_vActiveEvents)
	{
		if(ev && ev->GetEventName() && std::string(ev->GetEventName()) == "1on1")
		{
			result.push_back(std::static_pointer_cast<COneOnOneEvent>(ev));
		}
	}
	return result;
}
void CEvents::OnConsoleTerminate()
{
#define CommandDeregister(name, ...) Console()->Deregister(name);
	LIST_OF_ALL_COMMANDS(CommandDeregister)
#undef CommandDeregister
}

void CEvents::OnTick()
{
	if(m_pEventToDelete)
		return;

	for(auto it = m_vActiveEvents.begin(); it != m_vActiveEvents.end();)
	{
		auto &ev = *it;
		if(ev && ev->GetState() == CEventComponent::EEventState::Finished)
		{
			if(ev->EmergencyShutdown())
				Log("'%s' did emergency shutdown. Message: %s", ev->GetEventName(), ev->GetEmergencyMessage());
			Log("'%s' finished. Marking for clean up", ev->GetEventName());
			m_pEventToDelete = ev;
			it = m_vActiveEvents.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void CEvents::OnPostTick()
{
	if(m_pEventToDelete)
	{
		m_pEventToDelete.reset();
		Log("Cleanup finished");
	}
}

void CEvents::ConEventsStatus(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	const auto &events = pThis->GetActiveEvents();
	if(events.empty())
	{
		pThis->Log("No active events.");
		return;
	}
	for(const auto &ev : events)
	{
		pThis->Log("Event: %s", ev ? ev->GetEventName() : "none");
		pThis->Log("State: %s", ev ? ev->GetStateName() : "none");
		pThis->Log("Candidates: %" PRIzu, ev ? ev->Candidates().size() : 0);
		pThis->Log("Participants: %" PRIzu, ev ? ev->Participants().size() : 0);
	}
}
void CEvents::ConEventsList(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	pThis->Log("Available Events (Public):");
	for(const auto &[Name, Rec] : pThis->m_EventsFactory)
		if(Rec.m_Category == EEventCategory::Public)
			pThis->Log(" - %s", Name.c_str());
	pThis->Log("Available Events (Private):");
	for(const auto &[Name, Rec] : pThis->m_EventsFactory)
		if(Rec.m_Category == EEventCategory::Private)
			pThis->Log(" - %s", Name.c_str());
}

void CEvents::ConEventsListPublic(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	pThis->Log("Public Events:");
	for(const auto &[Name, Rec] : pThis->m_EventsFactory)
		if(Rec.m_Category == EEventCategory::Public)
			pThis->Log(" - %s", Name.c_str());
}
void CEvents::ConEventsListPrivate(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	pThis->Log("Private Events:");
	for(const auto &[Name, Rec] : pThis->m_EventsFactory)
		if(Rec.m_Category == EEventCategory::Private)
			pThis->Log(" - %s", Name.c_str());
}
void CEvents::ConEventsStart(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	const char *pName = pResult->GetString(0);
	char aClearName[64];
	str_copy(aClearName, pName);
	str_clean_whitespaces(aClearName);

	auto it = pThis->m_EventsFactory.find(aClearName);
	if(it == pThis->m_EventsFactory.end())
	{
		pThis->Log("Event with this name wasn't found");
		return;
	}

	// create and add new event
	auto newEvent = it->second.m_Factory(pThis->GameServer());
	auto pThisShared = ((CEvents *)pUserData)->Registry()->Get<CEvents>();
	newEvent->SetStateChangeCallback(MakeSafeCallback(&CEvents::OnEventStateChange, pThisShared.Store()));
	newEvent->SetStateChangeCallback([pThis](auto OldState, auto NewState) { pThis->OnEventStateChange(OldState, NewState); });
	if(!newEvent->EmergencyShutdown())
		newEvent->OpenRegistration();
	pThis->AddActiveEvent(newEvent);
}
void CEvents::ConEventsForceNextState(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	auto events = pThis->GetActiveEvents();
	if(events.empty())
	{
		pThis->Log("No active event at this time");
		return;
	}
	for(const auto &ev : events)
	{
		if(ev)
			ev->ForceNextStage();
	}
}
void CEvents::ConEventsForceEnd(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	auto events = pThis->GetActiveEvents();
	if(events.empty())
	{
		pThis->Log("No active event at this time");
		return;
	}
	for(const auto &ev : events)
	{
		if(ev)
			ev->EmergencyShutdown("Forced");
	}
}

void CEvents::ConJoin(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	bool joined = false;
	for(const auto &ev : pThis->GetActiveEvents())
	{
		if(ev && ev->Register(pResult->m_ClientId))
		{
			joined = true;
			break;
		}
	}
	if(!joined)
		pThis->GameServer()->SendChatTarget(pResult->m_ClientId, "No active event at this time");
}
void CEvents::ConLeave(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	bool left = false;
	for(const auto &ev : pThis->GetActiveEvents())
	{
		if(ev)
		{
			if(ev->GetState() == CEventComponent::EEventState::Registration)
			{
				if(ev->DeRegister(pResult->m_ClientId))
				{
					left = true;
					break;
				}
			}
			else if(ev->Leave(pResult->m_ClientId))
			{
				pThis->GameServer()->SendChatTarget(pResult->m_ClientId, "You have left the event and have been disqualified.");
				pThis->GameServer()->SendBroadcast(" ", pResult->m_ClientId, false);
				left = true;
				break;
			}
		}
	}
	if(!left)
		pThis->GameServer()->SendChatTarget(pResult->m_ClientId, "You are not participating in any active event.");
}

void CEvents::OnEventStateChange(CEventComponent::EEventState OldState, CEventComponent::EEventState NewState)
{
	LogDebug("Event state changed: from %s to %s", CEventComponent::GetStateName(OldState), CEventComponent::GetStateName(NewState));

	if(NewState == CEventComponent::EEventState::Finished)
	{
		for(const auto &ev : GetActiveEvents())
		{
			if(ev && ev->GetState() == CEventComponent::EEventState::Finished)
			{
				const auto &parts = ev->Participants();
				for(int ClientId : parts)
				{
					if(ClientId < 0)
						continue;
					GameServer()->SendBroadcast(" ", ClientId, false);
				}
			}
		}
	}
}

std::vector<std::string> CEvents::GetEventsByCategory(EEventCategory Category) const
{
	std::vector<std::string> out;
	out.reserve(m_EventsFactory.size());
	for(const auto &kv : m_EventsFactory)
		if(kv.second.m_Category == Category)
			out.push_back(kv.first);
	return out;
}

std::optional<CEvents::EEventCategory> CEvents::GetCategoryOf(const char *pName) const
{
	if(!pName)
		return std::nullopt;
	char aName[64];
	str_copy(aName, pName);
	str_clean_whitespaces(aName);
	auto it = m_EventsFactory.find(aName);
	if(it == m_EventsFactory.end())
		return std::nullopt;
	return it->second.m_Category;
}
