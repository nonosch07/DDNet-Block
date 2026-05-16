#include "getipintel_service.h"
#include "json_helpers.h"

#include <base/system.h>
#include <engine/external/json-parser/json.h>

CGetIPIntelService::CGetIPIntelService() :
	m_ContactEmail(""),
	m_Flags(""),
	m_OutputFlags(""),
	m_Threshold(0.95f)
{
}

CGetIPIntelService::CGetIPIntelService(const char *pContactEmail) :
	m_ContactEmail(VpnServiceConfig::Trim(pContactEmail)),
	m_Flags(""),
	m_OutputFlags(""),
	m_Threshold(0.95f)
{
}

void CGetIPIntelService::SetContactEmail(const char *pEmail)
{
	m_ContactEmail = VpnServiceConfig::Trim(pEmail);
}

void CGetIPIntelService::SetFlags(const char *pFlags)
{
	m_Flags = VpnServiceConfig::Trim(pFlags);
}

void CGetIPIntelService::SetOutputFlags(const char *pOFlags)
{
	m_OutputFlags = VpnServiceConfig::Trim(pOFlags);
}

std::string CGetIPIntelService::GetEndpoint(const char *pIpAddress) const
{
	std::string Endpoint = "http://check.getipintel.net/check.php?ip=";
	Endpoint += pIpAddress;
	Endpoint += "&contact=";
	Endpoint += m_ContactEmail;

	if(!m_Flags.empty())
	{
		Endpoint += "&flags=";
		Endpoint += m_Flags;
	}

	if(!m_OutputFlags.empty())
	{
		Endpoint += "&oflags=";
		Endpoint += m_OutputFlags;
	}

	Endpoint += "&format=json";

	return Endpoint;
}

std::shared_ptr<IVpnServiceResult> CGetIPIntelService::ParseResponse(
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

	if(ResponseCode != 200)
	{
		char aErrorBuf[128];
		str_format(aErrorBuf, sizeof(aErrorBuf), "HTTP error %d", ResponseCode);
		pResult->m_ErrorMessage = aErrorBuf;
		pResult->m_ErrorCode = -ResponseCode;
		return pResult;
	}

	bool IsJson = str_find(pResponseBody, "{") != nullptr;
	float Probability = -1.0f;

	if(IsJson)
	{
		// Parse JSON once and reuse the root for all field lookups.
		json_value *pRoot = json_parse(pResponseBody, str_length(pResponseBody));
		if(!pRoot)
		{
			pResult->m_ErrorMessage = "Invalid JSON response";
			pResult->m_ErrorCode = -998;
			return pResult;
		}

		char aStatus[32];
		if(JsonHelpers::ParseString(pRoot, "status", aStatus, sizeof(aStatus)))
		{
			if(str_comp(aStatus, "error") == 0)
			{
				char aMessage[512];
				if(JsonHelpers::ParseString(pRoot, "message", aMessage, sizeof(aMessage)))
					pResult->m_ErrorMessage = aMessage;
				else
					pResult->m_ErrorMessage = "Unknown API error";

				char aResultStr[16];
				if(JsonHelpers::ParseString(pRoot, "result", aResultStr, sizeof(aResultStr)))
				{
					int ErrorCode = str_toint(aResultStr);
					pResult->m_ErrorCode = ErrorCode;

					switch(ErrorCode)
					{
					case -1:
						if(pResult->m_ErrorMessage.empty())
							pResult->m_ErrorMessage = "Invalid IP address or no input provided";
						break;
					case -2:
						if(pResult->m_ErrorMessage.empty())
							pResult->m_ErrorMessage = "Invalid IP address format";
						break;
					case -3:
						if(pResult->m_ErrorMessage.empty())
							pResult->m_ErrorMessage = "Unroutable or private IP address";
						break;
					case -4:
						if(pResult->m_ErrorMessage.empty())
							pResult->m_ErrorMessage = "Database temporarily unavailable (maintenance)";
						break;
					case -5:
						if(pResult->m_ErrorMessage.empty())
							pResult->m_ErrorMessage = "Access denied: IP banned or query limit exceeded";
						break;
					case -6:
						if(pResult->m_ErrorMessage.empty())
							pResult->m_ErrorMessage = "Invalid or missing contact email";
						break;
					default:
						if(pResult->m_ErrorMessage.empty())
						{
							char aUnknownError[128];
							str_format(aUnknownError, sizeof(aUnknownError), "Unknown error code: %d", ErrorCode);
							pResult->m_ErrorMessage = aUnknownError;
						}
						break;
					}
				}
				json_value_free(pRoot);
				return pResult;
			}
		}

		char aResultStr[32];
		if(!JsonHelpers::ParseString(pRoot, "result", aResultStr, sizeof(aResultStr)))
		{
			pResult->m_ErrorMessage = "Failed to parse 'result' field from JSON response";
			pResult->m_ErrorCode = -998;
			json_value_free(pRoot);
			return pResult;
		}

		Probability = str_tofloat(aResultStr);

		char aAsn[64];
		if(JsonHelpers::ParseString(pRoot, "asn", aAsn, sizeof(aAsn)))
			pResult->m_Asn = aAsn;

		char aCountry[64];
		if(JsonHelpers::ParseString(pRoot, "country", aCountry, sizeof(aCountry)))
			pResult->m_Isp = aCountry;

		json_value_free(pRoot);
	}
	else
	{
		Probability = str_tofloat(pResponseBody);
	}

	if(Probability < 0.0f)
	{
		int ErrorCode = (int)Probability;
		pResult->m_ErrorCode = ErrorCode;

		switch(ErrorCode)
		{
		case -1:
			pResult->m_ErrorMessage = "Invalid IP address or no input provided";
			break;
		case -2:
			pResult->m_ErrorMessage = "Invalid IP address format";
			break;
		case -3:
			pResult->m_ErrorMessage = "Unroutable or private IP address";
			break;
		case -4:
			pResult->m_ErrorMessage = "Database temporarily unavailable (maintenance)";
			break;
		case -5:
			pResult->m_ErrorMessage = "Access denied: IP banned or query limit exceeded";
			break;
		case -6:
			pResult->m_ErrorMessage = "Invalid or missing contact email";
			break;
		default:
		{
			char aUnknownError[128];
			str_format(aUnknownError, sizeof(aUnknownError), "Unknown error code: %d", ErrorCode);
			pResult->m_ErrorMessage = aUnknownError;
			break;
		}
		}
		return pResult;
	}

	pResult->m_RiskScore = (int)(Probability * 100.0f);
	pResult->m_IsBadIP = (Probability >= m_Threshold);
	pResult->m_IsValid = true;

	return pResult;
}
