#ifndef BLOCKWORLDS_COMPONENTS_VPN_COMMANDS_H
#define BLOCKWORLDS_COMPONENTS_VPN_COMMANDS_H

#include <engine/console.h>

// Forward declaration
class CVpnDetectionComponent;

namespace VpnCommands {
void ConVPNEnable(IConsole::IResult *pResult, void *pUserData);
void ConVPNStatus(IConsole::IResult *pResult, void *pUserData);
void ConVPNSetDefaultService(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
void ConVPNServiceList(IConsole::IResult *pResult, void *pUserData);
void ConVPNCheck(IConsole::IResult *pResult, void *pUserData);
void ConVPNCheckForce(IConsole::IResult *pResult, void *pUserData);
void ConVPNWhitelistAdd(IConsole::IResult *pResult, void *pUserData);
void ConVPNWhitelistRemove(IConsole::IResult *pResult, void *pUserData);
void ConVPNWhitelistList(IConsole::IResult *pResult, void *pUserData);
} // namespace VpnCommands

#endif // BLOCKWORLDS_COMPONENTS_VPN_COMMANDS_H
