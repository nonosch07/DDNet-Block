#ifndef BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_IPQUERY_SERVICE_H
#define BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_IPQUERY_SERVICE_H

#include "vpn_service_interface.h"

/**
 * IPQuery.io VPN detection service implementation
 *
 * API: https://api.ipquery.io/{IP}
 * Also allows for batch operations in a comma separated list. /1.1.1.1,2.2.2.2,3.3.3.3
 */
class CIPQueryService : public IVpnService
{
public:
	const char *GetServiceName() const override { return "ipquery"; }

	std::string GetEndpoint(const char *pIpAddress) const override;

	std::shared_ptr<IVpnServiceResult> ParseResponse(
		const char *pIpAddress,
		const char *pResponseBody,
		int ResponseCode) override;

	bool RequiresAuth() const override { return false; }
};

#endif // BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_IPQUERY_SERVICE_H
