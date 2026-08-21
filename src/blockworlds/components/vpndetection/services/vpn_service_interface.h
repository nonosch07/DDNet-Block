#ifndef BLOCKWORLDS_COMPONENTS_VPN_SERVICE_INTERFACE_H
#define BLOCKWORLDS_COMPONENTS_VPN_SERVICE_INTERFACE_H

#include "../vpn_service_result.h"

#include <cctype>
#include <cstring>
#include <memory>
#include <string>

class CVpnDetectionComponent;

namespace VpnServiceConfig
{
	inline std::string Trim(const char *pValue)
	{
		if(!pValue)
			return "";

		const char *pStart = pValue;
		while(*pStart && std::isspace((unsigned char)*pStart))
			pStart++;

		const char *pEnd = pStart + std::char_traits<char>::length(pStart);
		while(pEnd > pStart && std::isspace((unsigned char)*(pEnd - 1)))
			pEnd--;

		return std::string(pStart, pEnd);
	}

	inline bool HasValue(const char *pValue)
	{
		return !Trim(pValue).empty();
	}
} // namespace VpnServiceConfig

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
		int ResponseCode) = 0;

	virtual bool RequiresAuth() const { return false; }
	virtual std::string GetAuthHeader() const { return ""; }
	virtual bool IsConfigured() const { return true; }
};

#endif // BLOCKWORLDS_COMPONENTS_VPN_SERVICE_INTERFACE_H
