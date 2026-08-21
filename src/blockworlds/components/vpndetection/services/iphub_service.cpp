#include "iphub_service.h"

#include "json_helpers.h"

#include <engine/external/json-parser/json.h>

#include <blockworlds/bw_base.h>

CIPHubService::CIPHubService() :
	m_pApiKey(nullptr)
{
}

std::string CIPHubService::GetAuthHeader() const
{
	std::string Header = "X-Key: ";
	Header += VpnServiceConfig::Trim(m_pApiKey);
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

	// Parse the "block" field - this is the key classification:
	//   0 = residential/business (clean)
	//   1 = non-residential/hosting/proxy/bad IP
	//   2 = suspicious lower-confidence result
	int Block = -1;
	if(!JsonHelpers::ParseInt(pRoot, "block", Block))
	{
		pResult->m_ErrorMessage = "Failed to parse 'block' field from response";
		pResult->m_ErrorCode = -998;
		json_value_free(pRoot);
		return pResult;
	}

	// Parse optional metadata
	char aAsn[64] = {0};
	char aIsp[256] = {0};
	char aCountryCode[8] = {0};

	// IPHub returns "asn" as integer, so we try int first, then string
	int AsnNum = 0;
	if(JsonHelpers::ParseInt(pRoot, "asn", AsnNum))
	{
		str_format(aAsn, sizeof(aAsn), "AS%d", AsnNum);
		pResult->m_Asn = aAsn;
	}
	else if(JsonHelpers::ParseString(pRoot, "asn", aAsn, sizeof(aAsn)))
	{
		pResult->m_Asn = aAsn;
	}

	if(JsonHelpers::ParseString(pRoot, "isp", aIsp, sizeof(aIsp)))
		pResult->m_Isp = aIsp;

	if(JsonHelpers::ParseString(pRoot, "countryCode", aCountryCode, sizeof(aCountryCode)))
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

	json_value_free(pRoot);

	// IPHub recommends using block for the primary allow/deny decision.
	pResult->m_IsBadIP = (Block >= 1);

	// Map block value to a risk score:
	//   0 -> 0   (clean residential)
	//   1 -> 100 (hosting/proxy/bad IP)
	//   2 -> 66  (suspicious lower-confidence result)
	if(Block == 0)
		pResult->m_RiskScore = 0;
	else if(Block == 1)
		pResult->m_RiskScore = 100;
	else if(Block == 2)
		pResult->m_RiskScore = 66;
	else
		pResult->m_RiskScore = 50; // unknown block value

	pResult->m_IsValid = true;
	return pResult;
}
