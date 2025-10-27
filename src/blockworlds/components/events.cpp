#include "events.h"

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>

#include <blockworlds/utils/memory.h>

#include <blockworlds/components/core/component_registry.h>
#if 0 // 1on1 moved out of the single-active events system; handled by oneonone_manager
#include <blockworlds/components/events/1on1.h>
#endif
#include <blockworlds/components/events/clanwar.h>
#include <blockworlds/components/events/colorsoldiers.h>
#include <blockworlds/components/events/event.h>
#include <blockworlds/components/events/lmb.h>
#include <blockworlds/components/events/priv_tdm.h>
#include <blockworlds/components/events/tdm.h>
#include <blockworlds/components/events/zcatch.h>

CEvents::CEvents(CGameContext *pGameServer) :
	CComponent(pGameServer), m_pActiveEvent(nullptr), m_pEventToDelete(nullptr)
{
	// Public events
	m_EventsFactory.emplace("lmb", SFactoryRec{EEventCategory::Public, [](class CGameContext *pGS) { return std::make_shared<CLastManBlockingEvent>(pGS); }});
	m_EventsFactory.emplace("tdm", SFactoryRec{EEventCategory::Public, [](class CGameContext *pGS) { return std::make_shared<CTeamDeathmatchEvent>(pGS); }});
	m_EventsFactory.emplace("zcatch", SFactoryRec{EEventCategory::Public, [](class CGameContext *pGS) { return std::make_shared<CZCatchEvent>(pGS); }});
	m_EventsFactory.emplace("colorsoldiers", SFactoryRec{EEventCategory::Public, [](class CGameContext *pGS) { return std::make_shared<CColorSoldiersEvent>(pGS); }});

	// Private events
	// Note: 1on1 is intentionally NOT registered here. 1on1 matches are managed by COneOnOneManager
	m_EventsFactory.emplace("priv_tdm", SFactoryRec{EEventCategory::Private, [](class CGameContext *pGS) { return std::make_shared<CPrivateTdmEvent>(pGS); }});
	m_EventsFactory.emplace("clanwar", SFactoryRec{EEventCategory::Private, [](class CGameContext *pGS) { return std::make_shared<CClanwarEvent>(pGS); }});
}

CEvents::~CEvents()
{
	if(m_pEventToDelete)
	{
		m_pEventToDelete.reset();
		m_pEventToDelete = nullptr;
	}
}

std::vector<ComponentAccessor<CComponent>> CEvents::GetSubComponents() const
{
	std::vector<ComponentAccessor<CComponent>> vSubComponents;
	if(m_pActiveEvent)
		vSubComponents.emplace_back(m_pActiveEvent);
	return vSubComponents;
}

void CEvents::OnDisable()
{
	if(m_pActiveEvent)
	{
		m_pActiveEvent->EmergencyShutdown("Events Shutdown");
		m_pEventToDelete = m_pActiveEvent;
		m_pActiveEvent.reset();
	}
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

void CEvents::SetActiveEvent(std::shared_ptr<CEventComponent> pEvent)
{
	m_pActiveEvent = std::move(pEvent);
}

std::shared_ptr<CEventComponent> CEvents::GetActiveEvent() const
{
	return m_pActiveEvent;
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

	if(m_pActiveEvent)
	{
		if(m_pActiveEvent->GetState() == CEventComponent::EEventState::Finished)
		{
			if(m_pActiveEvent->EmergencyShutdown())
				Log("'%s' did emergency shutdown. Message: %s", m_pActiveEvent->GetEventName(), m_pActiveEvent->GetEmergencyMessage());
			Log("'%s' finished. Marking for clean up", m_pActiveEvent->GetEventName());
			m_pEventToDelete = m_pActiveEvent;
			m_pActiveEvent.reset();
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

	pThis->Log("Current event: %s", pThis->m_pActiveEvent ? pThis->m_pActiveEvent->GetEventName() : "none");
	pThis->Log("State: %s", pThis->m_pActiveEvent ? pThis->m_pActiveEvent->GetStateName() : "none");
	pThis->Log("Candidates: %" PRIzu, pThis->m_pActiveEvent ? pThis->m_pActiveEvent->Candidates().size() : 0);
	pThis->Log("Participants: %" PRIzu, pThis->m_pActiveEvent ? pThis->m_pActiveEvent->Participants().size() : 0);
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

	if(pThis->m_pActiveEvent)
	{
		pThis->Log("Event is already running");
		return;
	}

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

	// Don't ask, just believe
	auto pThisShared = ((CEvents *)pUserData)->Registry()->Get<CEvents>();
	pThis->m_pActiveEvent = it->second.m_Factory(pThis->GameServer());
	pThis->m_pActiveEvent->SetStateChangeCallback(MakeSafeCallback(&CEvents::OnEventStateChange, pThisShared.Store()));
	pThis->m_pActiveEvent->SetStateChangeCallback([pThis](auto OldState, auto NewState) { pThis->OnEventStateChange(OldState, NewState); });
	if(!pThis->m_pActiveEvent->EmergencyShutdown())
		pThis->m_pActiveEvent->OpenRegistration();
}
void CEvents::ConEventsForceNextState(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	if(!pThis->m_pActiveEvent)
	{
		pThis->Log("No active event at this time");
		return;
	}

	pThis->m_pActiveEvent->ForceNextStage();
}
void CEvents::ConEventsForceEnd(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	if(!pThis->m_pActiveEvent)
	{
		pThis->Log("No active event at this time");
		return;
	}

	pThis->m_pActiveEvent->EmergencyShutdown("Forced");
}

void CEvents::ConJoin(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	if(pThis->m_pActiveEvent)
	{
		pThis->m_pActiveEvent->Register(pResult->m_ClientId);
		return;
	}

	pThis->GameServer()->SendChatTarget(pResult->m_ClientId, "No active event at this time");
}
void CEvents::ConLeave(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	if(!pThis->m_pActiveEvent)
	{
		pThis->GameServer()->SendChatTarget(pResult->m_ClientId, "No active event at this time");
		return;
	}

	if(pThis->m_pActiveEvent->GetState() == CEventComponent::EEventState::Registration)
	{
		if(!pThis->m_pActiveEvent->DeRegister(pResult->m_ClientId))
			pThis->GameServer()->SendChatTarget(pResult->m_ClientId, "You aren't registered to participate.");
		return;
	}

	if(pThis->m_pActiveEvent->Leave(pResult->m_ClientId))
	{
		pThis->GameServer()->SendChatTarget(pResult->m_ClientId, "You have left the event and have been disqualified.");
		pThis->GameServer()->SendBroadcast(" ", pResult->m_ClientId, false);
	}
	else
	{
		pThis->GameServer()->SendChatTarget(pResult->m_ClientId, "You are not participating in the current event.");
	}
}

void CEvents::OnEventStateChange(CEventComponent::EEventState OldState, CEventComponent::EEventState NewState)
{
	LogDebug("Event state changed: from %s to %s", CEventComponent::GetStateName(OldState), CEventComponent::GetStateName(NewState));

	if(NewState == CEventComponent::EEventState::Finished && m_pActiveEvent)
	{
		const auto &parts = m_pActiveEvent->Participants();
		for(int ClientId : parts)
		{
			if(ClientId < 0)
				continue;
			GameServer()->SendBroadcast(" ", ClientId, false);
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
