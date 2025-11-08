#ifndef BLOCKWORLDS_COMPONENTS_VPN_COMMANDS_H
#define BLOCKWORLDS_COMPONENTS_VPN_COMMANDS_H

#include <engine/console.h>

// Forward declaration
class CVpnDetectionComponent;

namespace VpnCommands
{
	void ConVPNEnable(IConsole::IResult *pResult, void *pUserData);
	void ConVPNStatus(IConsole::IResult *pResult, void *pUserData);
	void ConVPNSetDefaultService(IConsole::IResult *pResult, void *pUserData);
	void ConVPNServiceList(IConsole::IResult *pResult, void *pUserData);
	void ConVPNCheck(IConsole::IResult *pResult, void *pUserData);
	void ConVPNCheckForce(IConsole::IResult *pResult, void *pUserData);
}

#endif // BLOCKWORLDS_COMPONENTS_VPN_COMMANDS_H

