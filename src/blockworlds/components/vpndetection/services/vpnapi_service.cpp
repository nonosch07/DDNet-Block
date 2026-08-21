#include "vpnapi_service.h"

#include "json_helpers.h"

#include <engine/external/json-parser/json.h>

#include <blockworlds/bw_base.h>

std::string CVpnApiService::GetEndpoint(const char *pIpAddress) const
{
	std::string Endpoint = std::string("https://vpnapi.io/api/") + pIpAddress + "?key=";
	Endpoint += VpnServiceConfig::Trim(m_pApiKey);
	return Endpoint;
}

std::shared_ptr<IVpnServiceResult> CVpnApiService::ParseResponse(
	const char *pIpAddress,
	const char *pResponseBody,
	int ResponseCode)
{
	auto pResult = std::make_shared<CVpnServiceResult>();
	pResult->m_ServiceName = GetServiceName();
	pResult->m_IpAddress = pIpAddress;
	pResult->m_Timestamp = time_timestamp();
	pResult->m_IsValid = false;

	if(!pResponseBody || pResponseBody[0] == '\0')
	{
		pResult->m_ErrorMessage = "Empty response from API";
		pResult->m_ErrorCode = -999;
		return pResult;
	}

	if(ResponseCode == 429)
	{
		pResult->m_ErrorMessage = "Rate limit exceeded (HTTP 429)";
		pResult->m_ErrorCode = -429;
		return pResult;
	}
	if(ResponseCode == 401 || ResponseCode == 403)
	{
		pResult->m_ErrorMessage = "Invalid or missing API key";
		pResult->m_ErrorCode = -ResponseCode;
		return pResult;
	}
	if(ResponseCode != 200)
	{
		char aErrorBuf[128];
		str_format(aErrorBuf, sizeof(aErrorBuf), "HTTP error %d", ResponseCode);
		pResult->m_ErrorMessage = aErrorBuf;
		pResult->m_ErrorCode = -ResponseCode;
		return pResult;
	}
	// Parse JSON once and reuse the root for all field lookups.
	json_value *pRoot = json_parse(pResponseBody, str_length(pResponseBody));
	if(!pRoot)
	{
		pResult->m_ErrorMessage = "Invalid JSON response";
		pResult->m_ErrorCode = -998;
		return pResult;
	}

	char aMessage[256];
	if(JsonHelpers::ParseString(pRoot, "message", aMessage, sizeof(aMessage)) ||
		JsonHelpers::ParseString(pRoot, "error", aMessage, sizeof(aMessage)))
	{
		pResult->m_ErrorMessage = aMessage;
		pResult->m_ErrorCode = -1;
		json_value_free(pRoot);
		return pResult;
	}

	bool IsVpn = false;
	bool IsProxy = false;
	bool IsTor = false;
	bool IsRelay = false;
	JsonHelpers::ParseBool(pRoot, "security.vpn", IsVpn);
	JsonHelpers::ParseBool(pRoot, "security.proxy", IsProxy);
	JsonHelpers::ParseBool(pRoot, "security.tor", IsTor);
	JsonHelpers::ParseBool(pRoot, "security.relay", IsRelay);

	char aAsn[64];
	if(JsonHelpers::ParseString(pRoot, "network.autonomous_system_number", aAsn, sizeof(aAsn)))
		pResult->m_Asn = aAsn;

	char aIsp[256];
	if(JsonHelpers::ParseString(pRoot, "network.autonomous_system_organization", aIsp, sizeof(aIsp)))
		pResult->m_Isp = aIsp;

	json_value_free(pRoot);

	pResult->m_IsBadIP = IsVpn || IsProxy || IsTor || IsRelay;
	pResult->m_RiskScore = pResult->m_IsBadIP ? 100 : 0;
	pResult->m_IsValid = true;
	return pResult;
}
