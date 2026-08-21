#ifndef BLOCKWORLDS_COMPONENTS_GETIPINTEL_SERVICE_H
#define BLOCKWORLDS_COMPONENTS_GETIPINTEL_SERVICE_H

#include "vpn_service_interface.h"

/**
 * GetIPIntel.net VPN/Proxy detection service
 *
 * Free tier: 500 queries/day, 15 queries/minute
 * API returns probability (0-1) of IP being proxy/VPN/bad
 *
 * The "result" field contains the probability score (0.0-1.0)
 * We use our own threshold (sv_vpn_getipintel_threshold) to determine BadIP
 *
 * Flags:
 *   m - Dynamic ban list only (known proxies return 1.0)
 *   b - Dynamic ban + checks + partial bad IP detection
 *
 * Output flags:
 *   b - Include bad IP detection (we ignore this and use our threshold)
 *   c - Include country code
 *   a - Include ASN
 *
 * Error codes (negative values in the "result" field):
 *   -1: Invalid IP address or no input provided
 *   -2: Invalid IP address format
 *   -3: Unroutable or private IP address
 *   -4: Database temporarily unavailable (maintenance)
 *   -5: Access denied: IP banned or query limit exceeded
 *   -6: Invalid or missing contact email
 */
class CGetIPIntelService : public IVpnService
{
public:
	CGetIPIntelService();
	explicit CGetIPIntelService(const char *pContactEmail);

	const char *GetServiceName() const override { return "getipintel"; }
	std::string GetEndpoint(const char *pIpAddress) const override;
	std::shared_ptr<IVpnServiceResult> ParseResponse(
		const char *pIpAddress,
		const char *pResponseBody,
		int ResponseCode) override;
	bool RequiresAuth() const override { return false; }
	bool IsConfigured() const override { return !m_ContactEmail.empty(); }

	void SetContactEmail(const char *pEmail);
	void SetFlags(const char *pFlags);
	void SetOutputFlags(const char *pOFlags);
	void SetThreshold(float Threshold) { m_Threshold = Threshold; }

private:
	std::string m_ContactEmail;
	std::string m_Flags;
	std::string m_OutputFlags;
	float m_Threshold;
};

#endif // BLOCKWORLDS_COMPONENTS_GETIPINTEL_SERVICE_H
