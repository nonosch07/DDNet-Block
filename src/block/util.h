#ifndef BLOCK_UTIL_H
#define BLOCK_UTIL_H

#include <block/base.h>

#include <ctime>

class IServer;

// Upstream removed IServer::GetClientAddr(int, char *, int) in favour of
// ClientAddrString(). Block called the old one from a lot of places, often for
// client ids that are not ingame (logging, whois, mutes), where the new API
// must not be called at all -- so this keeps the old, tolerant behaviour in one
// Block-owned place.
void BlockClientAddr(IServer *pServer, int ClientId, char *pBuf, int Size);

// Cross-platform localtime. This used to be added to base/system.{h,cpp};
// keeping it here leaves upstream's base/ alone.
// Two client slots sharing an IP (port ignored). Used by the anti-farm and the
// event candidate checks; used to be IServer::IsClientsSameAddr.
bool BlockIsClientsSameAddr(IServer *pServer, int FirstClientId, int SecondClientId);

struct tm *time_localtime_safe(const time_t *pTime, struct tm *pResult);

#endif // BLOCK_UTIL_H
