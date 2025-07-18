#include "events.h"

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>

#include <blockworlds/utils/memory.h>

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events/event.h>
#include <blockworlds/components/events/lmb.h>

CEvents::CEvents(CGameContext *pGameServer) :
	CComponent(pGameServer), m_pActiveEvent(nullptr), m_pEventToDelete(nullptr)
{
	m_EventsFactory.emplace("lmb", [](class CGameContext *pGS) { return std::make_shared<CLastManBlockingEvent>(pGS); });
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
	DEF("events_list", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsList, this, "List of available to start events") \
	DEF("events_start", "r[name]", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsStart, this, "Start specified event") \
	DEF("events_next_stage", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsForceNextState, this, "Force current event to next stage") \
	DEF("events_end", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsForceEnd, this, "Forcefully end current event")  \
                                  \
	DEF("join", "", CFGFLAG_SERVER | CFGFLAG_CHAT | CFGFLAG_ANNOUNCE, ConJoin, this, "Chat command, try to register to current event")  \
	DEF("leave", "", CFGFLAG_SERVER | CFGFLAG_CHAT | CFGFLAG_ANNOUNCE, ConLeave, this, "Chat command, try to deregister from current event")


void CEvents::OnConsoleInit()
{
#define CommandRegister(name, args, flags, callback, user, help) Console()->Register(name, args, flags, callback, user, help);
	LIST_OF_ALL_COMMANDS(CommandRegister)
#undef CommandRegister
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

	pThis->Log("Available Events:");
	for(const auto &item : pThis->m_EventsFactory)
		pThis->Log(" - %s", item.first.c_str());
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
	pThis->m_pActiveEvent = it->second(pThis->GameServer());
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

	if(pThis->m_pActiveEvent)
	{
		pThis->m_pActiveEvent->DeRegister(pResult->m_ClientId);
		return;
	}

	pThis->GameServer()->SendChatTarget(pResult->m_ClientId, "No active event at this time");
}

void CEvents::OnEventStateChange(CEventComponent::EEventState OldState, CEventComponent::EEventState NewState)
{
	LogDebug("Event state changed: from %s to %s", CEventComponent::GetStateName(OldState), CEventComponent::GetStateName(NewState));
}
