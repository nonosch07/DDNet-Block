#ifndef BLOCKWORLDS_COMPONENTS_IPLOCATE_SERVICE_H
#define BLOCKWORLDS_COMPONENTS_IPLOCATE_SERVICE_H

#include "vpn_service_interface.h"

class CIPLocateService : public IVpnService
{
public:
	const char *GetServiceName() const override { return "iplocate"; }
	std::string GetEndpoint(const char *pIpAddress) const override;
	std::shared_ptr<IVpnServiceResult> ParseResponse(
		const char *pIpAddress,
		const char *pResponseBody,
		int ResponseCode) override;

	void SetApiKeyPtr(const char *pKeyBuffer) { m_pApiKey = pKeyBuffer; }

private:
	const char *m_pApiKey = nullptr;
};

#endif // BLOCKWORLDS_COMPONENTS_IPLOCATE_SERVICE_H
