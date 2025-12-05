#include "client_detect.h"

#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>

CClientDetectComponent::CClientDetectComponent(CGameContext *pGameServer) :
	CComponent(pGameServer)
{
}

#define LIST_OF_ALL_COMMANDS(DEF) \
	DEF("status_client", "?r[name]", CFGFLAG_SERVER, ConStatusClient, this, "List client versions")

void CClientDetectComponent::OnConsoleInit()
{
#define CommandRegister(name, args, flags, callback, user, help) Console()->Register(name, args, flags, callback, user, help);
	LIST_OF_ALL_COMMANDS(CommandRegister)
#undef CommandRegister
}

void CClientDetectComponent::OnConsoleTerminate()
{
#define CommandDeregister(name, ...) Console()->Deregister(name);
	LIST_OF_ALL_COMMANDS(CommandDeregister)
#undef CommandDeregister
}

void CClientDetectComponent::ConStatusClient(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = (CClientDetectComponent *)pUserData;
	const char *pName = pResult->NumArguments() == 1 ? pResult->GetString(0) : "";

	char aBuf[512];
	char aAddrStr[NETADDR_MAXSTRSIZE];

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pSelf->Server()->ClientIngame(i))
			continue;

		const char *pClientName = pSelf->Server()->ClientName(i);
		if(!str_utf8_find_nocase(pClientName, pName))
			continue;

		pSelf->Server()->GetClientAddr(i, aAddrStr, sizeof(aAddrStr));

		IServer::CClientInfo Info;
		if(pSelf->Server()->GetClientInfo(i, &Info))
		{
			const char *pVersionStr = Info.m_pDDNetVersionStr ? Info.m_pDDNetVersionStr : "unknown";
			str_format(aBuf, sizeof(aBuf), "id=%d name='%s' addr=<{%s}> version=%d version_str='%s'",
				i, pClientName, aAddrStr, Info.m_DDNetVersion, pVersionStr);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "id=%d name='%s' addr=<{%s}> version=n/a",
				i, pClientName, aAddrStr);
		}

		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

// later on some Redis config that has banned version_str, and/or matches versions/version_strings to known bot clients
// ofc also then with potential auto ban / auto deep / maybe if we find some crash function auto-crash (would be fun to see krx kids write in discord that they are crashing on join haha)