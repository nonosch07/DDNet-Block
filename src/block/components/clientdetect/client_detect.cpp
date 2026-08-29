#include "client_detect.h"

#include <engine/server.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>

#include <game/server/gamecontext.h>

#include <block/util.h>

CClientDetectComponent::CClientDetectComponent(CGameContext *pGameServer) :
	CComponent(pGameServer)
{
	CONSOLE_COMMAND("status_client", "?r[name]", ConStatusClient, "List client versions")
}

void CClientDetectComponent::ConStatusClient(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = (CClientDetectComponent *)pUserData;
	const char *pName = pResult->NumArguments() == 1 ? pResult->GetString(0) : "";

	const int RequesterAuth = pResult->m_ClientId >= 0 && pResult->m_ClientId < MAX_CLIENTS ?
					  pSelf->Server()->GetAuthedState(pResult->m_ClientId) :
					  AUTHED_ADMIN;

	char aBuf[512];
	char aAddrStr[NETADDR_MAXSTRSIZE];

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pSelf->Server()->ClientIngame(i))
			continue;

		const char *pClientName = pSelf->Server()->ClientName(i);
		if(!str_utf8_find_nocase(pClientName, pName))
			continue;

		const bool HideAdminIp = RequesterAuth < AUTHED_ADMIN && pSelf->Server()->GetAuthedState(i) == AUTHED_ADMIN;
		if(HideAdminIp)
			str_copy(aAddrStr, "hidden", sizeof(aAddrStr));
		else
			BlockClientAddr(pSelf->Server(), i, aAddrStr, sizeof(aAddrStr));

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
