#include "iphub_service.h"
#include "json_helpers.h"

#include <base/system.h>

CIPHubService::CIPHubService() :
	m_pApiKey(nullptr)
{
}

std::string CIPHubService::GetAuthHeader() const
{
	std::string Header = "X-Key: ";
	if(m_pApiKey)
		Header += m_pApiKey;
	return Header;
}

std::string CIPHubService::GetEndpoint(const char *pIpAddress) const
{
	return std::string("https://v2.api.iphub.info/ip/") + pIpAddress;
}

std::shared_ptr<IVpnServiceResult> CIPHubService::ParseResponse(
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

	if(ResponseCode == 401 || ResponseCode == 403)
	{
		pResult->m_ErrorMessage = "Invalid or missing API key";
		pResult->m_ErrorCode = -ResponseCode;
		return pResult;
	}

	if(ResponseCode == 422)
	{
		pResult->m_ErrorMessage = "Invalid IP address (HTTP 422)";
		pResult->m_ErrorCode = -422;
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

	// Check for error field in response
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

	// Parse the "block" field - this is the key classification:
	//   0 = residential/business (clean)
	//   1 = non-residential/datacenter (suspicious)
	//   2 = non-residential & known proxy/VPN/tor (bad)
	int Block = -1;
	if(!JsonHelpers::ParseInt(pResponseBody, "block", Block))
	{
		pResult->m_ErrorMessage = "Failed to parse 'block' field from response";
		pResult->m_ErrorCode = -998;
		return pResult;
	}

	// Parse optional metadata
	char aAsn[64] = {0};
	char aIsp[256] = {0};
	char aCountryCode[8] = {0};

	// IPHub returns "asn" as integer, so we try int first, then string
	int AsnNum = 0;
	if(JsonHelpers::ParseInt(pResponseBody, "asn", AsnNum))
	{
		str_format(aAsn, sizeof(aAsn), "AS%d", AsnNum);
		pResult->m_Asn = aAsn;
	}
	else if(JsonHelpers::ParseString(pResponseBody, "asn", aAsn, sizeof(aAsn)))
	{
		pResult->m_Asn = aAsn;
	}

	if(JsonHelpers::ParseString(pResponseBody, "isp", aIsp, sizeof(aIsp)))
		pResult->m_Isp = aIsp;

	if(JsonHelpers::ParseString(pResponseBody, "countryCode", aCountryCode, sizeof(aCountryCode)))
	{
		// Append country code to ISP info if available
		if(pResult->m_Isp.empty())
			pResult->m_Isp = aCountryCode;
		else
		{
			pResult->m_Isp += " (";
			pResult->m_Isp += aCountryCode;
			pResult->m_Isp += ")";
		}
	}

	// block=2 is definitively bad (proxy/VPN/tor)
	// block=1 is datacenter/non-residential - also flag as bad since
	// legitimate players don't connect from datacenters
	pResult->m_IsBadIP = (Block >= 1);

	// Map block value to a risk score:
	//   0 -> 0   (clean residential)
	//   1 -> 66  (datacenter, suspicious)
	//   2 -> 100 (known VPN/proxy/tor)
	if(Block == 0)
		pResult->m_RiskScore = 0;
	else if(Block == 1)
		pResult->m_RiskScore = 66;
	else if(Block == 2)
		pResult->m_RiskScore = 100;
	else
		pResult->m_RiskScore = 50; // unknown block value

	pResult->m_IsValid = true;
	return pResult;
}
