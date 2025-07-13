#include "events.h"

#include <engine/shared/config.h>
#include <engine/server.h>

bool CEvents::IsDebug() const
{
	return Config()->m_Debug;
}

CEvents::CEvents(CGameContext *pGameServer) :
	CComponent(pGameServer) {}

void CEvents::OnEnable()
{
	Log("Enabled!");
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
	if(Server()->Tick() % (Server()->TickSpeed() * 3) == 0)
	{
		Log("3 seconds passed, Tick %d", Server()->Tick());
	}
}

void CEvents::ConEventsTest(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	pThis->Log("Just testing...");
}
