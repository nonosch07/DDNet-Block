#ifndef BLOCKWORLDS_COMPONENTS_VPN_SERVICE_INTERFACE_H
#define BLOCKWORLDS_COMPONENTS_VPN_SERVICE_INTERFACE_H

#include "../vpn_service_result.h"

#include <memory>
#include <string>

class CVpnDetectionComponent;

/**
 * Base interface for VPN detection service implementations
 */
class IVpnService
{
public:
	virtual ~IVpnService() = default;
	
	virtual const char *GetServiceName() const = 0;
	virtual std::string GetEndpoint(const char *pIpAddress) const = 0;
	virtual std::shared_ptr<IVpnServiceResult> ParseResponse(
		const char *pIpAddress,
		const char *pResponseBody,
		int ResponseCode
	) = 0;
	
	virtual bool RequiresAuth() const { return false; }
	virtual std::string GetAuthHeader() const { return ""; }
};

#endif // BLOCKWORLDS_COMPONENTS_VPN_SERVICE_INTERFACE_H

