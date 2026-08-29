#include "util.h"

#include <engine/server.h>
#include <engine/shared/protocol.h>

void BlockClientAddr(IServer *pServer, int ClientId, char *pBuf, int Size)
{
	if(!pBuf || Size <= 0)
		return;
	pBuf[0] = '\0';
	if(!pServer || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(!pServer->ClientIngame(ClientId))
		return;
	str_copy(pBuf, pServer->ClientAddrString(ClientId, false), Size);
}

bool BlockIsClientsSameAddr(IServer *pServer, int FirstClientId, int SecondClientId)
{
	if(!pServer || FirstClientId < 0 || FirstClientId >= MAX_CLIENTS || SecondClientId < 0 || SecondClientId >= MAX_CLIENTS)
		return false;
	if(FirstClientId == SecondClientId)
		return true;
	return net_addr_comp_noport(pServer->ClientAddr(FirstClientId), pServer->ClientAddr(SecondClientId)) == 0;
}

struct tm *time_localtime_safe(const time_t *pTime, struct tm *pResult)
{
#if defined(CONF_FAMILY_WINDOWS)
	// on Windows localtime_s is the safe variant
	return localtime_s(pResult, pTime) == 0 ? pResult : nullptr;
#else
	return localtime_r(pTime, pResult);
#endif
}
