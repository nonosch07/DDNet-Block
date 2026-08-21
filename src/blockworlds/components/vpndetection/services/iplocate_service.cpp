#include "iplocate_service.h"

#include "json_helpers.h"

#include <engine/external/json-parser/json.h>

#include <blockworlds/bw_base.h>

std::string CIPLocateService::GetEndpoint(const char *pIpAddress) const
{
	std::string Endpoint = std::string("https://iplocate.io/api/lookup/") + pIpAddress;
	const std::string ApiKey = VpnServiceConfig::Trim(m_pApiKey);
	if(!ApiKey.empty())
	{
		Endpoint += "?apikey=";
		Endpoint += ApiKey;
	}
	return Endpoint;
}

std::shared_ptr<IVpnServiceResult> CIPLocateService::ParseResponse(
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
		pResult->m_ErrorMessage = "Invalid or unauthorized API key";
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
	if(JsonHelpers::ParseString(pRoot, "error", aMessage, sizeof(aMessage)) ||
		JsonHelpers::ParseString(pRoot, "message", aMessage, sizeof(aMessage)))
	{
		pResult->m_ErrorMessage = aMessage;
		pResult->m_ErrorCode = -1;
		json_value_free(pRoot);
		return pResult;
	}

	char aAsn[64];
	if(JsonHelpers::ParseString(pRoot, "asn.asn", aAsn, sizeof(aAsn)))
		pResult->m_Asn = aAsn;

	char aIsp[256];
	if(JsonHelpers::ParseString(pRoot, "asn.name", aIsp, sizeof(aIsp)) ||
		JsonHelpers::ParseString(pRoot, "company.name", aIsp, sizeof(aIsp)))
		pResult->m_Isp = aIsp;

	bool IsVpn = false;
	bool IsProxy = false;
	bool IsTor = false;
	bool IsHosting = false;
	bool IsAnonymous = false;
	JsonHelpers::ParseBool(pRoot, "privacy.is_vpn", IsVpn);
	JsonHelpers::ParseBool(pRoot, "privacy.is_proxy", IsProxy);
	JsonHelpers::ParseBool(pRoot, "privacy.is_tor", IsTor);
	JsonHelpers::ParseBool(pRoot, "privacy.is_hosting", IsHosting);
	JsonHelpers::ParseBool(pRoot, "privacy.is_anonymous", IsAnonymous);

	json_value_free(pRoot);

	pResult->m_IsBadIP = IsVpn || IsProxy || IsTor || IsHosting || IsAnonymous;
	if(IsVpn || IsProxy || IsTor)
		pResult->m_RiskScore = 100;
	else if(IsHosting || IsAnonymous)
		pResult->m_RiskScore = 66;
	else
		pResult->m_RiskScore = 0;
	pResult->m_IsValid = true;
	return pResult;
}
