#include "events.h"

#include <engine/shared/config.h>
#include <engine/server.h>

#include <blockworlds/components/events/event.h>

CEvents::CEvents(CGameContext *pGameServer) :
	CComponent(pGameServer), m_pActiveEvent(nullptr) {}

std::vector<CComponent *> CEvents::GetSubComponents() const
{
	if(m_pActiveEvent)
		return { m_pActiveEvent };
	return {};
}

void CEvents::OnDisable()
{
	Log("Disabled!");
}

void CEvents::OnConsoleInit()
{
	Console()->Register("events_test", "", CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, ConEventsTest, this, "");
}
void CEvents::OnConsoleTerminate()
{
	Console()->Deregister("events_test");
}

void CEvents::OnTick()
{
	if(m_pActiveEvent)
	{
		if(m_pActiveEvent->GetState() == CEventComponent::EEventState::Finished)
		{
			if(m_pActiveEvent->EmergencyShutdown())
				Log("Event '%s' did emergency shutdown. Message: %s", m_pActiveEvent->GetEventName(), m_pActiveEvent->GetEmergencyMessage());
			Log("Event '%s' finished. Cleaning up", m_pActiveEvent->GetEventName());
			delete m_pActiveEvent;
			m_pActiveEvent = nullptr;
		}
	}
}

void CEvents::ConEventsTest(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	pThis->Log("Just testing...");
}
