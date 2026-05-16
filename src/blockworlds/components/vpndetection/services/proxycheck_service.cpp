#include "proxycheck_service.h"
#include "json_helpers.h"

#include <base/system.h>
#include <engine/external/json-parser/json.h>

std::string CProxyCheckService::GetEndpoint(const char *pIpAddress) const
{
	std::string Endpoint = std::string("https://proxycheck.io/v2/") + pIpAddress + "?vpn=3&asn=1&risk=1&days=14";
	const std::string ApiKey = VpnServiceConfig::Trim(m_pApiKey);
	if(!ApiKey.empty())
	{
		Endpoint += "&key=";
		Endpoint += ApiKey;
	}
	return Endpoint;
}

std::shared_ptr<IVpnServiceResult> CProxyCheckService::ParseResponse(
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
	if(ResponseCode != 200)
	{
		char aErrorBuf[128];
		str_format(aErrorBuf, sizeof(aErrorBuf), "HTTP error %d", ResponseCode);
		pResult->m_ErrorMessage = aErrorBuf;
		pResult->m_ErrorCode = -ResponseCode;
		return pResult;
	}

	json_value *pRoot = json_parse(pResponseBody, str_length(pResponseBody));
	if(!pRoot)
	{
		pResult->m_ErrorMessage = "Invalid JSON response";
		pResult->m_ErrorCode = -998;
		return pResult;
	}

	const json_value *pIpObject = JsonHelpers::GetObjectItem(pRoot, pIpAddress);
	if(!pIpObject || pIpObject->type != json_object)
	{
		const json_value *pMessage = JsonHelpers::GetObjectItem(pRoot, "message");
		if(pMessage && pMessage->type == json_string)
			pResult->m_ErrorMessage = pMessage->u.string.ptr;
		else
			pResult->m_ErrorMessage = "IP result missing from API response";
		pResult->m_ErrorCode = -997;
		json_value_free(pRoot);
		return pResult;
	}

	auto GetString = [](const json_value *pObject, const char *pKey) -> const char * {
		const json_value *pValue = JsonHelpers::GetObjectItem(pObject, pKey);
		return pValue && pValue->type == json_string ? pValue->u.string.ptr : "";
	};
	auto GetBool = [](const json_value *pObject, const char *pKey) -> bool {
		const json_value *pValue = JsonHelpers::GetObjectItem(pObject, pKey);
		if(!pValue)
			return false;
		if(pValue->type == json_boolean)
			return pValue->u.boolean != 0;
		if(pValue->type == json_string)
			return str_comp_nocase(pValue->u.string.ptr, "yes") == 0 ||
			       str_comp_nocase(pValue->u.string.ptr, "true") == 0 ||
			       str_comp(pValue->u.string.ptr, "1") == 0;
		return false;
	};
	auto GetInt = [](const json_value *pObject, const char *pKey, int Default) -> int {
		const json_value *pValue = JsonHelpers::GetObjectItem(pObject, pKey);
		if(!pValue)
			return Default;
		if(pValue->type == json_integer)
			return (int)pValue->u.integer;
		if(pValue->type == json_double)
			return (int)pValue->u.dbl;
		if(pValue->type == json_string)
			return str_toint(pValue->u.string.ptr);
		return Default;
	};

	const char *pError = GetString(pIpObject, "error");
	if(pError[0])
	{
		pResult->m_ErrorMessage = pError;
		pResult->m_ErrorCode = -1;
		json_value_free(pRoot);
		return pResult;
	}

	const bool IsProxy = GetBool(pIpObject, "proxy");
	const bool IsVpn = GetBool(pIpObject, "vpn");
	const bool IsTor = GetBool(pIpObject, "tor");
	const char *pType = GetString(pIpObject, "type");
	const bool TypeIsBad = str_comp_nocase(pType, "VPN") == 0 ||
	                       str_comp_nocase(pType, "TOR") == 0 ||
	                       str_comp_nocase(pType, "Proxy") == 0 ||
	                       str_comp_nocase(pType, "Hosting") == 0;

	const char *pAsn = GetString(pIpObject, "asn");
	if(pAsn[0])
		pResult->m_Asn = pAsn;

	const char *pProvider = GetString(pIpObject, "provider");
	if(pProvider[0])
		pResult->m_Isp = pProvider;

	pResult->m_RiskScore = GetInt(pIpObject, "risk", IsProxy || IsVpn || IsTor || TypeIsBad ? 100 : 0);
	pResult->m_IsBadIP = IsProxy || IsVpn || IsTor || TypeIsBad || pResult->m_RiskScore >= 90;
	pResult->m_IsValid = true;
	json_value_free(pRoot);
	return pResult;
}
