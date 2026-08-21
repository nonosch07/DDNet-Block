#include "ipquery_service.h"

#include "json_helpers.h"

#include <engine/external/json-parser/json.h>

#include <blockworlds/bw_base.h>

std::string CIPQueryService::GetEndpoint(const char *pIpAddress) const
{
	return std::string("https://api.ipquery.io/") + pIpAddress;
}

std::shared_ptr<IVpnServiceResult> CIPQueryService::ParseResponse(
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

	if(ResponseCode == 400)
	{
		pResult->m_ErrorMessage = "Bad request (HTTP 400)";
		pResult->m_ErrorCode = -400;
		return pResult;
	}

	if(ResponseCode == 404)
	{
		pResult->m_ErrorMessage = "IP address not found (HTTP 404)";
		pResult->m_ErrorCode = -404;
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

	char aErrorMsg[256];
	if(JsonHelpers::ParseString(pRoot, "error", aErrorMsg, sizeof(aErrorMsg)))
	{
		pResult->m_ErrorMessage = aErrorMsg;
		pResult->m_ErrorCode = -1;
		json_value_free(pRoot);
		return pResult;
	}

	char aAsn[64];
	if(JsonHelpers::ParseString(pRoot, "asn", aAsn, sizeof(aAsn)) ||
		JsonHelpers::ParseString(pRoot, "isp.asn", aAsn, sizeof(aAsn)))
		pResult->m_Asn = aAsn;

	char aIsp[256];
	if(JsonHelpers::ParseString(pRoot, "isp", aIsp, sizeof(aIsp)) ||
		JsonHelpers::ParseString(pRoot, "isp.isp", aIsp, sizeof(aIsp)) ||
		JsonHelpers::ParseString(pRoot, "isp.org", aIsp, sizeof(aIsp)))
		pResult->m_Isp = aIsp;

	bool IsVpn = false, IsProxy = false, IsTor = false, IsDatacenter = false;
	JsonHelpers::ParseBool(pRoot, "is_vpn", IsVpn) || JsonHelpers::ParseBool(pRoot, "risk.is_vpn", IsVpn);
	JsonHelpers::ParseBool(pRoot, "is_proxy", IsProxy) || JsonHelpers::ParseBool(pRoot, "risk.is_proxy", IsProxy);
	JsonHelpers::ParseBool(pRoot, "is_tor", IsTor) || JsonHelpers::ParseBool(pRoot, "risk.is_tor", IsTor);
	JsonHelpers::ParseBool(pRoot, "is_datacenter", IsDatacenter) || JsonHelpers::ParseBool(pRoot, "risk.is_datacenter", IsDatacenter);

	int RiskScore = -1;
	JsonHelpers::ParseInt(pRoot, "risk_score", RiskScore) || JsonHelpers::ParseInt(pRoot, "risk.risk_score", RiskScore);

	json_value_free(pRoot);

	pResult->m_IsBadIP = IsVpn || IsProxy || IsTor || IsDatacenter;
	pResult->m_RiskScore = RiskScore;
	pResult->m_IsValid = true;

	return pResult;
}
