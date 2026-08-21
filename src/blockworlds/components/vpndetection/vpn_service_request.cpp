#include "vpn_service_request.h"

#include "services/vpn_service_interface.h"
#include "vpn_detection.h"

#include <engine/http.h>

#include <game/server/gamecontext.h>

#include <blockworlds/bw_base.h>
#include <blockworlds/bw_context.h>

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

	// This used to call curl_easy_perform directly. DDNet ships a stub libcurl
	// that only exports the symbols the engine itself uses -- the easy-perform
	// interface is not among them, so linking failed everywhere but a machine
	// with a full system curl. The engine's own HTTP layer is the supported
	// route and gets connection reuse and the shared thread pool for free.
	if(!m_pComponent || !m_pComponent->GameServer())
		return false;
	IHttp *pHttp = m_pComponent->GameServer()->Bw().Http();
	if(!pHttp)
	{
		m_pComponent->Log("ERROR: HTTP subsystem unavailable");
		return false;
	}

	std::shared_ptr<IHttpRequest> pRequest = CreateHttpRequest(pUrl);
	pRequest->WriteToMemory();
	// 5s to connect, 10s overall, matching what the old curl options asked for
	pRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	// a 429 still has to be read, that is where the rate-limit headers live
	pRequest->FailOnErrorStatus(false);
	pRequest->CaptureResponseHeaders(true);
	pRequest->LogProgress(HTTPLOG::NONE);

	if(m_pService && m_pService->RequiresAuth())
	{
		const std::string AuthHeader = m_pService->GetAuthHeader();
		if(!AuthHeader.empty())
			pRequest->Header(AuthHeader.c_str());
	}

	const std::string SafeUrl = RedactSensitiveUrl(pUrl ? pUrl : "");
	m_pComponent->LogDebug("HTTP request initiated | URL: %s", SafeUrl.c_str());

	const int64_t Start = time_get();
	pHttp->Run(pRequest);
	// this runs on the VPN worker thread, never on the game thread
	pRequest->Wait();
	const float Elapsed = (float)(time_get() - Start) / (float)time_freq();

	const bool Failed = pRequest->State() != EHttpState::DONE;
	ResponseCode = pRequest->StatusCode();
	ResponseHeaders = pRequest->ResponseHeaders();

	unsigned char *pBody = nullptr;
	size_t BodyLength = 0;
	pRequest->Result(&pBody, &BodyLength);
	if(pBody && BodyLength)
		ResponseBody.assign((const char *)pBody, BodyLength);

	m_pComponent->LogDebug("HTTP request completed | State: %d | HTTP code: %d | Time: %.2fs | Body size: %d bytes",
		(int)pRequest->State(), ResponseCode, Elapsed, (int)ResponseBody.size());

	if(Failed)
	{
		m_pComponent->Log("ERROR: HTTP request failed | URL: %s | HTTP code: %d", SafeUrl.c_str(), ResponseCode);
		return false;
	}

	if(ResponseCode != 200)
	{
		m_pComponent->LogDebug("Non-200 HTTP response | Code: %d | URL: %s | Body preview: %.200s",
			ResponseCode, SafeUrl.c_str(), ResponseBody.c_str());
	}

	// Return true whenever the transfer itself succeeded - let ParseResponse
	// handle non-200 codes (401, 403, 429, etc.) with service-specific messages
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
