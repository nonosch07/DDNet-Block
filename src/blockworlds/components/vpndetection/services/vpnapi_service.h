#ifndef BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_VPNAPI_SERVICE_H
#define BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_VPNAPI_SERVICE_H

#include "vpn_service_interface.h"

class CVpnApiService : public IVpnService
{
public:
	const char *GetServiceName() const override { return "vpnapi"; }
	std::string GetEndpoint(const char *pIpAddress) const override;
	std::shared_ptr<IVpnServiceResult> ParseResponse(
		const char *pIpAddress,
		const char *pResponseBody,
		int ResponseCode) override;

	bool IsConfigured() const override { return VpnServiceConfig::HasValue(m_pApiKey); }
	void SetApiKeyPtr(const char *pKeyBuffer) { m_pApiKey = pKeyBuffer; }

private:
	const char *m_pApiKey = nullptr;
};

#endif // BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_VPNAPI_SERVICE_H
