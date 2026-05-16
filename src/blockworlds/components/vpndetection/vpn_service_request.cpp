#include "vpn_service_request.h"
#include "services/vpn_service_interface.h"
#include "vpn_detection.h"

#include <base/system.h>
#include <curl/curl.h>

#include <algorithm>
#include <cctype>

CVpnServiceRequest::CVpnServiceRequest(
	const char *pServiceName,
	const char *pIpAddress,
	int ClientId,
	CVpnDetectionComponent *pComponent,
	IVpnService *pService) :
	m_ServiceName(pServiceName),
	m_IpAddress(pIpAddress),
	m_ClientId(ClientId),
	m_pComponent(pComponent),
	m_pService(pService)
{
}

static size_t WriteCallback(void *pContents, size_t Size, size_t NumMembers, void *pUserData)
{
	std::string *pStr = (std::string *)pUserData;
	size_t TotalSize = Size * NumMembers;
	pStr->append((char *)pContents, TotalSize);
	return TotalSize;
}

static size_t HeaderCallback(char *pBuffer, size_t Size, size_t NumMembers, void *pUserData)
{
	std::string *pStr = (std::string *)pUserData;
	size_t TotalSize = Size * NumMembers;
	pStr->append(pBuffer, TotalSize);
	return TotalSize;
}

static std::string ToLower(std::string Value)
{
	std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Value;
}

static std::string Trim(std::string Value)
{
	while(!Value.empty() && (Value.back() == '\r' || Value.back() == '\n' || Value.back() == ' ' || Value.back() == '\t'))
		Value.pop_back();
	size_t Start = 0;
	while(Start < Value.size() && (Value[Start] == ' ' || Value[Start] == '\t'))
		Start++;
	return Value.substr(Start);
}

static void RedactQueryValue(std::string &Url, const char *pKey)
{
	std::string Pattern = pKey;
	Pattern += '=';

	size_t Pos = 0;
	while((Pos = Url.find(Pattern, Pos)) != std::string::npos)
	{
		const size_t ValueStart = Pos + Pattern.length();
		size_t ValueEnd = Url.find_first_of("& \t\r\n", ValueStart);
		if(ValueEnd == std::string::npos)
			ValueEnd = Url.length();

		Url.replace(ValueStart, ValueEnd - ValueStart, "<redacted>");
		Pos = ValueStart + 10;
	}
}

static std::string RedactSensitiveUrl(std::string Url)
{
	RedactQueryValue(Url, "key");
	RedactQueryValue(Url, "apikey");

	return Url;
}

bool CVpnServiceRequest::PerformHttpRequest(const char *pUrl, std::string &ResponseBody, std::string &ResponseHeaders, int &ResponseCode)
{
	ResponseBody.clear();
	ResponseHeaders.clear();
	ResponseCode = 0;

	CURL *pCurl = curl_easy_init();
	if(!pCurl)
	{
		if(m_pComponent)
			m_pComponent->Log("ERROR: cURL initialization failed");
		return false;
	}

	curl_easy_setopt(pCurl, CURLOPT_URL, pUrl);
	curl_easy_setopt(pCurl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
	curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(pCurl, CURLOPT_USERAGENT, "curl/8.0");
	curl_easy_setopt(pCurl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(pCurl, CURLOPT_MAXREDIRS, 3L);
	curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &ResponseBody);
	curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, HeaderCallback);
	curl_easy_setopt(pCurl, CURLOPT_HEADERDATA, &ResponseHeaders);

	curl_slist *pHeaders = nullptr;
	if(m_pService && m_pService->RequiresAuth())
	{
		std::string AuthHeader = m_pService->GetAuthHeader();
		if(!AuthHeader.empty())
		{
			pHeaders = curl_slist_append(pHeaders, AuthHeader.c_str());
			curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHeaders);
		}
	}

	const std::string SafeUrl = RedactSensitiveUrl(pUrl ? pUrl : "");

	if(m_pComponent)
		m_pComponent->LogDebug("HTTP request initiated | URL: %s", SafeUrl.c_str());

	CURLcode Res = curl_easy_perform(pCurl);

	long HttpCode = 0;
	curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &HttpCode);
	ResponseCode = (int)HttpCode;

	double TotalTime = 0;
	curl_easy_getinfo(pCurl, CURLINFO_TOTAL_TIME, &TotalTime);

	if(m_pComponent)
	{
		m_pComponent->LogDebug("HTTP request completed | Result: %d | HTTP code: %d | Time: %.2fs | Body size: %d bytes",
			Res, ResponseCode, TotalTime, (int)ResponseBody.size());
	}

	if(pHeaders)
		curl_slist_free_all(pHeaders);
	curl_easy_cleanup(pCurl);

	if(Res != CURLE_OK)
	{
		if(m_pComponent)
		{
			const char *pErrorType = "Unknown";
			switch(Res)
			{
			case CURLE_COULDNT_RESOLVE_HOST:
				pErrorType = "DNS resolution failed";
				break;
			case CURLE_COULDNT_CONNECT:
				pErrorType = "Connection failed";
				break;
			case CURLE_OPERATION_TIMEDOUT:
				pErrorType = "Request timed out";
				break;
			case CURLE_SSL_CONNECT_ERROR:
				pErrorType = "SSL connection error";
				break;
			case CURLE_RECV_ERROR:
				pErrorType = "Failed to receive data";
				break;
			case CURLE_SEND_ERROR:
				pErrorType = "Failed to send data";
				break;
			default:
				break;
			}
			m_pComponent->Log("ERROR: HTTP request failed | URL: %s | Error: %s | cURL code: %d",
				SafeUrl.c_str(), pErrorType, Res);
			m_pComponent->LogDebug("cURL error details: %s", curl_easy_strerror(Res));
		}
		return false;
	}

	if(ResponseCode != 200)
	{
		if(m_pComponent)
		{
			m_pComponent->LogDebug("Non-200 HTTP response | Code: %d | URL: %s | Body preview: %.200s",
				ResponseCode, SafeUrl.c_str(), ResponseBody.c_str());
		}
	}

	// Return true whenever curl succeeded - let ParseResponse handle
	// non-200 codes (401, 403, 429, etc.) with service-specific messages
	return true;
}

int CVpnServiceRequest::ParseRetryAfterMs(const std::string &ResponseHeaders, int ResponseCode) const
{
	if(ResponseCode != 429)
		return 0;

	int RetrySeconds = 0;
	int64_t ResetTimestamp = 0;
	size_t LineStart = 0;
	while(LineStart < ResponseHeaders.size())
	{
		size_t LineEnd = ResponseHeaders.find('\n', LineStart);
		if(LineEnd == std::string::npos)
			LineEnd = ResponseHeaders.size();

		std::string Line = ResponseHeaders.substr(LineStart, LineEnd - LineStart);
		size_t Colon = Line.find(':');
		if(Colon != std::string::npos)
		{
			std::string Name = ToLower(Trim(Line.substr(0, Colon)));
			std::string Value = Trim(Line.substr(Colon + 1));
			if(Name == "retry-after" || Name == "x-ttl")
				RetrySeconds = std::max(RetrySeconds, str_toint(Value.c_str()));
			else if(Name == "x-ratelimit-reset")
				ResetTimestamp = std::max<int64_t>(ResetTimestamp, str_toint(Value.c_str()));
		}

		LineStart = LineEnd + 1;
	}

	if(ResetTimestamp > 0)
	{
		const int64_t Now = time_timestamp();
		if(ResetTimestamp > Now)
			RetrySeconds = std::max<int>(RetrySeconds, (int)(ResetTimestamp - Now));
	}

	if(RetrySeconds <= 0)
		RetrySeconds = 60;

	return RetrySeconds * 1000;
}

std::shared_ptr<IVpnServiceResult> CVpnServiceRequest::Execute()
{
	if(!m_pService)
	{
		if(m_pComponent)
			m_pComponent->Log("ERROR: Service instance is null | Service: %s", m_ServiceName.c_str());

		auto pResult = std::make_shared<CVpnServiceResult>();
		pResult->m_ServiceName = m_ServiceName;
		pResult->m_IpAddress = m_IpAddress;
		pResult->m_IsValid = false;
		pResult->m_ErrorMessage = "Service instance not available";
		pResult->m_ErrorCode = -997;
		pResult->m_Timestamp = time_timestamp();
		return pResult;
	}

	std::string Endpoint = Trim(m_pService->GetEndpoint(m_IpAddress.c_str()));
	const std::string SafeEndpoint = RedactSensitiveUrl(Endpoint);

	if(m_pComponent)
		m_pComponent->LogDebug("API request executing | Service: %s | IP: %s | Endpoint: %s",
			m_ServiceName.c_str(), m_IpAddress.c_str(), SafeEndpoint.c_str());

	std::string ResponseBody;
	std::string ResponseHeaders;
	int ResponseCode = 0;
	bool Success = PerformHttpRequest(Endpoint.c_str(), ResponseBody, ResponseHeaders, ResponseCode);

	if(!Success)
	{
		auto pResult = std::make_shared<CVpnServiceResult>();
		pResult->m_ServiceName = m_ServiceName;
		pResult->m_IpAddress = m_IpAddress;
		pResult->m_IsValid = false;
		pResult->m_ErrorMessage = "HTTP request failed (connection error)";
		pResult->m_ErrorCode = -996;
		pResult->m_Timestamp = time_timestamp();
		return pResult;
	}

	// Let ParseResponse handle ALL HTTP response codes (including
	// non-200) so services can provide specific error messages
	auto pResult = m_pService->ParseResponse(m_IpAddress.c_str(), ResponseBody.c_str(), ResponseCode);
	if(ResponseCode == 429 && m_pComponent)
		m_pComponent->SetServiceCooldownMs(m_ServiceName.c_str(), ParseRetryAfterMs(ResponseHeaders, ResponseCode));

	if(pResult && m_pComponent)
	{
		if(pResult->IsValid())
		{
			m_pComponent->LogDebug("Response parsed successfully | Service: %s | Valid: true | Bad IP: %s | Risk: %d",
				m_ServiceName.c_str(), pResult->IsBadIP() ? "true" : "false", pResult->GetRiskScore());
		}
		else
		{
			m_pComponent->LogDebug("Response parsing failed | Service: %s | Valid: false | Error: %s",
				m_ServiceName.c_str(), pResult->GetErrorMessage()[0] ? pResult->GetErrorMessage() : "Unknown");
		}
	}

	return pResult;
}
