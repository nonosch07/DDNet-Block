#include "ipquery_service.h"
#include "json_helpers.h"

#include <base/system.h>

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
	pResult->m_Timestamp = time_get();
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

	if(str_find(pResponseBody, "\"error\""))
	{
		char aErrorMsg[256];
		if(JsonHelpers::ParseString(pResponseBody, "error", aErrorMsg, sizeof(aErrorMsg)))
			pResult->m_ErrorMessage = aErrorMsg;
		else
			pResult->m_ErrorMessage = "API returned an error";
		pResult->m_ErrorCode = -1;
		return pResult;
	}

	char aAsn[64];
	if(JsonHelpers::ParseString(pResponseBody, "asn", aAsn, sizeof(aAsn)))
		pResult->m_Asn = aAsn;

	char aIsp[256];
	if(JsonHelpers::ParseString(pResponseBody, "isp", aIsp, sizeof(aIsp)))
		pResult->m_Isp = aIsp;

	bool IsVpn = false, IsProxy = false, IsTor = false, IsDatacenter = false;
	JsonHelpers::ParseBool(pResponseBody, "is_vpn", IsVpn);
	JsonHelpers::ParseBool(pResponseBody, "is_proxy", IsProxy);
	JsonHelpers::ParseBool(pResponseBody, "is_tor", IsTor);
	JsonHelpers::ParseBool(pResponseBody, "is_datacenter", IsDatacenter);

	int RiskScore = -1;
	JsonHelpers::ParseInt(pResponseBody, "risk_score", RiskScore);

	pResult->m_IsBadIP = IsVpn || IsProxy || IsTor || IsDatacenter;
	pResult->m_RiskScore = RiskScore;
	pResult->m_IsValid = true;

	return pResult;
}
