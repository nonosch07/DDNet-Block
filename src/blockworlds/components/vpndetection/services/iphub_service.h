#ifndef BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_IPHUB_SERVICE_H
#define BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_IPHUB_SERVICE_H

#include "vpn_service_interface.h"

/**
 * IPHub.info VPN/Proxy detection service
 *
 * API: https://v2.api.iphub.info/ip/{IP}
 * Auth: X-Key header with API key
 *
 * Free tier: 1,000 requests/day
 * Response JSON fields:
 *   "ip"          - queried IP
 *   "countryCode" - ISO country code
 *   "countryName" - full country name
 *   "asn"         - ASN number
 *   "isp"         - ISP name
 *   "block"       - classification:
 *       0 = residential / business (clean)
 *       1 = non-residential / hosting / proxy / bad IP
 *       2 = suspicious lower-confidence result
 * same service we used in old source
 */
class CIPHubService : public IVpnService
{
public:
	CIPHubService();

	const char *GetServiceName() const override { return "iphub"; }

	std::string GetEndpoint(const char *pIpAddress) const override;

	std::shared_ptr<IVpnServiceResult> ParseResponse(
		const char *pIpAddress,
		const char *pResponseBody,
		int ResponseCode) override;

	bool RequiresAuth() const override { return true; }
	std::string GetAuthHeader() const override;
	bool IsConfigured() const override { return VpnServiceConfig::HasValue(m_pApiKey); }

	void SetApiKeyPtr(const char *pKeyBuffer) { m_pApiKey = pKeyBuffer; }

private:
	const char *m_pApiKey;
};

#endif // BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_IPHUB_SERVICE_H
