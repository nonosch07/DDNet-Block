#include "vpn_detection.h"

#include "services/getipintel_service.h"
#include "services/iphub_service.h"
#include "services/iplocate_service.h"
#include "services/ipquery_service.h"
#include "services/proxycheck_service.h"
#include "services/vpnapi_service.h"
#include "time_parser.h"
#include "vpn_commands.h"

#include <engine/server.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/shared/linereader.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/bw_config.h>
#include <blockworlds/bw_context.h>
#include <blockworlds/bw_util.h>

#include <algorithm>
#include <cctype>

namespace
{
	constexpr int MAX_CONCURRENT_VPN_REQUESTS = 8;
	constexpr int MAX_PENDING_VPN_REQUESTS_PER_SERVICE = MAX_CLIENTS;
	// A withheld join message is broadcast as soon as the services answered. This is only
	// the escape hatch for a request that never comes back at all.
	constexpr int JOIN_MSG_MAX_HOLD_SECONDS = 10;

	bool IsServiceListSeparator(char c)
	{
		return c == ',' || c == ';' || c == '"' || c == '\'' || std::isspace((unsigned char)c);
	}

	std::vector<std::string> ParseServiceList(const char *pServices)
	{
		std::vector<std::string> Services;
		std::string Current;

		auto Flush = [&]() {
			if(Current.empty())
				return;
			if(std::find(Services.begin(), Services.end(), Current) == Services.end())
				Services.push_back(Current);
			Current.clear();
		};

		if(!pServices)
			return Services;

		for(const char *p = pServices; *p; ++p)
		{
			if(IsServiceListSeparator(*p))
			{
				Flush();
				continue;
			}
			Current.push_back((char)std::tolower((unsigned char)*p));
		}
		Flush();
		return Services;
	}

	std::string JoinServiceList(const std::vector<std::string> &Services)
	{
		std::string Result;
		for(const auto &Service : Services)
		{
			if(!Result.empty())
				Result += ", ";
			Result += Service;
		}
		return Result.empty() ? "(none)" : Result;
	}

	bool IsAllServicesToken(const std::string &Token)
	{
		return Token == "*" || Token == "all";
	}
} // namespace

bool CVpnDetectionComponent::IsDebug() const
{
	return Config()->m_SvVpnDebug;
}

CVpnDetectionComponent::CVpnDetectionComponent(CGameContext *pGameServer) :
	CComponent(pGameServer),
	m_BanTimeMinutes(60),
	m_ActiveRequestThreads(0)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aClientInfo[i].m_ClientId = i;
	}

	auto pIPQuery = new CIPQueryService();
	RegisterService("ipquery", pIPQuery, Config()->m_SvVpnRateLimitIpquery);

	Log("VPN service registered: ipquery | API: https://api.ipquery.io/ | Rate limit: %dms", Config()->m_SvVpnRateLimitIpquery);

	auto pGetIPIntel = new CGetIPIntelService();
	pGetIPIntel->SetFlags("b");
	pGetIPIntel->SetOutputFlags("b");
	pGetIPIntel->SetContactEmail(Config()->m_SvVpnGetipintelContact);
	pGetIPIntel->SetThreshold(g_BwConfig.m_SvVpnGetipintelThreshold / 100.0f);
	RegisterService("getipintel", pGetIPIntel, Config()->m_SvVpnRateLimitGetipintel);

	Log("VPN service registered: getipintel | API: https://getipintel.net/ | Contact: %s | Threshold: %.2f%% | Rate limit: %dms",
		Config()->m_SvVpnGetipintelContact[0] ? Config()->m_SvVpnGetipintelContact : "(not set)",
		g_BwConfig.m_SvVpnGetipintelThreshold, Config()->m_SvVpnRateLimitGetipintel);

	auto pIPHub = new CIPHubService();
	pIPHub->SetApiKeyPtr(Config()->m_SvVpnIphubApiKey);
	RegisterService("iphub", pIPHub, Config()->m_SvVpnRateLimitIphub);

	Log("VPN service registered: iphub | API: https://v2.api.iphub.info/ | API key: %s | Rate limit: %dms",
		Config()->m_SvVpnIphubApiKey[0] ? "(set)" : "(not set)",
		Config()->m_SvVpnRateLimitIphub);

	auto pIPLocate = new CIPLocateService();
	pIPLocate->SetApiKeyPtr(Config()->m_SvVpnIplocateApiKey);
	RegisterService("iplocate", pIPLocate, Config()->m_SvVpnRateLimitIplocate);

	Log("VPN service registered: iplocate | API: https://iplocate.io/ | API key: %s | Rate limit: %dms",
		Config()->m_SvVpnIplocateApiKey[0] ? "(set)" : "(not set)",
		Config()->m_SvVpnRateLimitIplocate);

	auto pProxyCheck = new CProxyCheckService();
	pProxyCheck->SetApiKeyPtr(Config()->m_SvVpnProxycheckApiKey);
	RegisterService("proxycheck", pProxyCheck, Config()->m_SvVpnRateLimitProxycheck);

	Log("VPN service registered: proxycheck | API: https://proxycheck.io/ | API key: %s | Rate limit: %dms",
		Config()->m_SvVpnProxycheckApiKey[0] ? "(set)" : "(not set)",
		Config()->m_SvVpnRateLimitProxycheck);

	auto pVpnApi = new CVpnApiService();
	pVpnApi->SetApiKeyPtr(Config()->m_SvVpnVpnapiApiKey);
	RegisterService("vpnapi", pVpnApi, Config()->m_SvVpnRateLimitVpnapi);

	Log("VPN service registered: vpnapi | API: https://vpnapi.io/ | API key: %s | Rate limit: %dms",
		Config()->m_SvVpnVpnapiApiKey[0] ? "(set)" : "(not set)",
		Config()->m_SvVpnRateLimitVpnapi);

	SetDefaultService(Config()->m_SvVpnServiceDefault);

	CONSOLE_COMMAND("vpn_status", "?i[full_check]", VpnCommands::ConVPNStatus, "Show VPN status of connected clients (full_check=1 to queue fresh checks)")
	CONSOLE_COMMAND("vpn_service_list", "", VpnCommands::ConVPNServiceList, "List all registered VPN services")
	CONSOLE_COMMAND("vpn_services", "", VpnCommands::ConVPNServiceList, "List all registered VPN services")
	CONSOLE_COMMAND("vpn_check", "s[id_or_ip] ?s[service_name]", VpnCommands::ConVPNCheck, "Test VPN detection on a client ID or IP address (service optional, allows 'all')")
	CONSOLE_COMMAND("vpn_check_force", "s[id_or_ip] ?s[service_name]", VpnCommands::ConVPNCheckForce, "Force fresh VPN check, bypassing cache (service optional, allows 'all')")
	CONSOLE_COMMAND("vpn_whitelist_add", "s[ip]", VpnCommands::ConVPNWhitelistAdd, "Add an IP to the VPN detection whitelist")
	CONSOLE_COMMAND("vpn_whitelist_remove", "s[ip]", VpnCommands::ConVPNWhitelistRemove, "Remove an IP from the VPN detection whitelist")
	CONSOLE_COMMAND("vpn_whitelist_list", "", VpnCommands::ConVPNWhitelistList, "List all whitelisted IPs for VPN detection")

	CHAIN_COMMAND("sv_vpn_enabled", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;

		bool Enable = pResult->GetInteger(0);
		if(Enable)
		{
			Log("VPN detection enabled");
			pSelf->CheckAllClientsActiveServices();
		}
		else
		{
			Log("VPN detection disabled");
		}
	});
	CHAIN_COMMAND("sv_vpn_services_enabled", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;

		const auto ActiveServices = pSelf->GetActiveServiceNames();
		const std::string ActiveServiceList = JoinServiceList(ActiveServices);
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "VPN active configured services: %s", ActiveServiceList.c_str());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

		if(ActiveServices.empty())
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
				"No active configured VPN services. Use vpn_service_list to see available services and missing API keys/contact config.");
			return;
		}

		if(pResult->NumArguments() > 0 && pSelf->Config()->m_SvVpnEnabled)
			pSelf->CheckAllClientsActiveServices();
	});
	CHAIN_COMMAND("sv_vpn_ban_time", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
		{
			Log("VPN ban time: %d minutes", pSelf->m_BanTimeMinutes);
			return;
		}

		const char *pTimeStr = pResult->GetString(0);

		int Minutes = 0;
		if(ParseTimeStringMinutes(pTimeStr, &Minutes))
		{
			if(Minutes < 1)
				Minutes = 1;
			pSelf->SetBanTimeMinutes(Minutes);
			Log("VPN ban time set to %d minutes", Minutes);
		}
		else
		{
			Log("Invalid time format: '%s'. Examples: '60', '1h', '1d', '1d5h10m', '10 minutes'", pTimeStr);
		}
	});
	CHAIN_COMMAND("sv_vpn_ratelimit_ipquery", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;

		int Value = pResult->GetInteger(0);
		pSelf->SetServiceRateLimit("ipquery", Value);
	});
	CHAIN_COMMAND("sv_vpn_ratelimit_getipintel", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;

		int Value = pResult->GetInteger(0);
		pSelf->SetServiceRateLimit("getipintel", Value);
	});
	CHAIN_COMMAND("sv_vpn_service_getipintel_contact", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;

		const char *pEmail = pResult->GetString(0);
		auto *pService = dynamic_cast<CGetIPIntelService *>(pSelf->GetService("getipintel"));
		if(pService)
			pService->SetContactEmail(pEmail);
	});
	CHAIN_COMMAND("sv_vpn_service_getipintel_threshold", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;

		float Threshold = pResult->GetFloat(0);
		auto *pService = dynamic_cast<CGetIPIntelService *>(pSelf->GetService("getipintel"));
		if(pService)
			pService->SetThreshold(Threshold / 100.0f);
	});
	CHAIN_COMMAND("sv_vpn_ratelimit_iphub", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;

		int Value = pResult->GetInteger(0);
		pSelf->SetServiceRateLimit("iphub", Value);
	});
	CHAIN_COMMAND("sv_vpn_ratelimit_iplocate", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;
		pSelf->SetServiceRateLimit("iplocate", pResult->GetInteger(0));
	});
	CHAIN_COMMAND("sv_vpn_ratelimit_proxycheck", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;
		pSelf->SetServiceRateLimit("proxycheck", pResult->GetInteger(0));
	});
	CHAIN_COMMAND("sv_vpn_ratelimit_vpnapi", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;
		pSelf->SetServiceRateLimit("vpnapi", pResult->GetInteger(0));
	});
	CHAIN_COMMAND("sv_vpn_cache_ttl_days", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent *)pUserData;
		if(pResult->NumArguments() < 1)
			return;
		pSelf->GetCache()->SetTtlDays(pResult->GetInteger(0));
	});
	CHAIN_COMMAND("sv_vpn_service_default", VpnCommands::ConVPNSetDefaultService)

	m_Cache.SetTtlDays(Config()->m_SvVpnCacheTtlDays);
	if(m_Cache.Load("data/vpn_cache.json"))
	{
		Log("VPN cache loaded successfully | Entries: %d", m_Cache.GetEntryCount());
	}
	else
	{
		LogDebug("VPN cache file not found or empty, starting with fresh cache");
	}

	LoadWhitelist();
}

void CVpnDetectionComponent::OnTick()
{
	{
		std::lock_guard<std::mutex> Lock(m_MessageMutex);
		for(const auto &Msg : m_PendingMessages)
		{
			Log(Msg.m_Message.c_str());
		}
		m_PendingMessages.clear();
	}

	// process results queued by async threads (must run on main thread cuz ProcessResult changes m_aClientInfo and may ban ppl)
	{
		std::vector<SPendingResult> Results;
		{
			std::lock_guard<std::mutex> Lock(m_ResultMutex);
			Results.swap(m_PendingResults);
		}
		for(const auto &Pending : Results)
		{
			ProcessResult(Pending.m_ClientId, Pending.m_pResult);
		}
	}

	CleanupFinishedThreads();
	ProcessRequestQueues();
	ProcessJoinMessageHolds();
}

void CVpnDetectionComponent::OnDisable()
{
	// Nobody is left to finish the checks, so stop withholding any join message
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClientInfo[i].m_JoinMsgHoldExpire)
			ReleaseJoinMessage(i);
	}
}

void CVpnDetectionComponent::OnShutdown()
{
	{
		std::lock_guard<std::mutex> Lock(m_ThreadMutex);
		for(auto &Thread : m_RequestThreads)
		{
			if(Thread.joinable())
				Thread.join();
		}
		m_RequestThreads.clear();
	}

	if(m_Cache.Save("data/vpn_cache.json"))
	{
		Log("VPN cache saved successfully | Entries: %d", m_Cache.GetEntryCount());
	}
	else
	{
		Log("ERROR: Failed to save VPN cache to disk");
	}
}

bool CVpnDetectionComponent::IsLocalOrBogonIp(const char *pIp) const
{
	if(!pIp || !pIp[0])
		return true;

	NETADDR Addr;
	if(net_addr_from_str(&Addr, pIp) == 0 && (Addr.type & NETTYPE_IPV4))
	{
		const unsigned char A = Addr.ip[0];
		const unsigned char B = Addr.ip[1];
		return A == 0 || A == 10 || A == 127 ||
		       (A == 100 && B >= 64 && B <= 127) ||
		       (A == 169 && B == 254) ||
		       (A == 172 && B >= 16 && B <= 31) ||
		       (A == 192 && B == 168) ||
		       (A == 192 && B == 0 && Addr.ip[2] == 0) ||
		       (A == 198 && (B == 18 || B == 19)) ||
		       A >= 224;
	}

	if(str_comp(pIp, "::1") == 0 || str_comp(pIp, "[::1]") == 0 ||
		str_comp_nocase_num(pIp, "fc", 2) == 0 ||
		str_comp_nocase_num(pIp, "fd", 2) == 0 ||
		str_comp_nocase_num(pIp, "fe80", 4) == 0 ||
		str_comp_nocase_num(pIp, "[fc", 3) == 0 ||
		str_comp_nocase_num(pIp, "[fd", 3) == 0 ||
		str_comp_nocase_num(pIp, "[fe80", 5) == 0)
		return true;

	return false;
}

void CVpnDetectionComponent::OnPlayerEntering(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	CVpnClientInfo *pInfo = &m_aClientInfo[ClientId];
	pInfo->m_Results.clear();
	pInfo->m_PendingChecks = 0;
	pInfo->m_JoinMsgHoldExpire = 0;

	char aAddrStr[NETADDR_MAXSTRSIZE];
	BwClientAddr(Server(), ClientId, aAddrStr, sizeof(aAddrStr));
	pInfo->m_IpAddress = aAddrStr;

	LoadCachedResultsForClient(ClientId);

	// Runs before the join is announced and before the 0.7 client info is built, so
	// that a client a check can still ban is kept out of the chat from the start
	if(WillHoldJoinMessage(ClientId))
		HoldJoinMessage(ClientId);
}

void CVpnDetectionComponent::OnPlayerEnter(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	CVpnClientInfo *pInfo = &m_aClientInfo[ClientId];
	const char *pIpAddress = pInfo->m_IpAddress.c_str();

	LogDebug("Client connected | ID: %d | Name: %s | IP: %s",
		ClientId, Server()->ClientName(ClientId), pIpAddress);

	if(IsLocalOrBogonIp(pIpAddress) || IsIpWhitelisted(pIpAddress))
	{
		auto pLocalResult = std::make_shared<CVpnServiceResult>();
		pLocalResult->m_ServiceName = "local";
		pLocalResult->m_IpAddress = pInfo->m_IpAddress;
		pLocalResult->m_Asn = "Local Network";
		pLocalResult->m_Isp = "Private IP Address";
		pLocalResult->m_IsBadIP = false;
		pLocalResult->m_RiskScore = 0;
		pLocalResult->m_IsValid = true;
		pLocalResult->m_Timestamp = time_timestamp();

		LogDebug("Local IP detected | Client: %d | IP: %s | Skipping VPN check", ClientId, pIpAddress);
		ProcessResult(ClientId, pLocalResult);
		return;
	}

	// A bad cached result bans (and drops) the client right away, unless banning is
	// disabled, in which case the client stays and gets announced
	if(HandleFreshCachedBadResult(ClientId))
	{
		ReleaseJoinMessage(ClientId);
		return;
	}

	if(Config()->m_SvVpnEnabled)
	{
		CheckClient(ClientId, true);
	}

	// Nothing to wait for: no service was queued, everything was already cached
	if(pInfo->m_PendingChecks <= 0)
		ReleaseJoinMessage(ClientId);
}

void CVpnDetectionComponent::OnPlayerDrop(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const int RemovedRequests = RemoveQueuedRequestsForClient(ClientId);
	if(RemovedRequests > 0)
		LogDebug("Removed queued VPN checks for dropped client | Client: %d | Count: %d", ClientId, RemovedRequests);

	m_aClientInfo[ClientId].m_Results.clear();
	m_aClientInfo[ClientId].m_IpAddress.clear();
	m_aClientInfo[ClientId].m_PendingChecks = 0;
	m_aClientInfo[ClientId].m_JoinMsgHoldExpire = 0;
}

void CVpnDetectionComponent::CheckClient(int ClientId, bool FullCheck)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(FullCheck)
	{
		std::vector<std::string> ServiceNames = GetActiveServiceNames();

		for(const auto &ServiceName : ServiceNames)
		{
			// a cached bad result can ban and drop the client mid-loop
			if(!GameServer()->m_apPlayers[ClientId])
				break;

			CheckClientService(ClientId, ServiceName.c_str());
		}
	}
	else
	{
		if(!m_DefaultService.empty())
		{
			CheckClientService(ClientId, m_DefaultService.c_str());
		}
		else
		{
			Log("No default service set for VPN detection");
		}
	}
}

void CVpnDetectionComponent::CheckClientService(int ClientId, const char *pServiceName)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	CVpnClientInfo *pInfo = &m_aClientInfo[ClientId];

	if(pInfo->m_IpAddress.empty())
	{
		NETADDR Addr;
		Addr = *Server()->ClientAddr(ClientId);
		char aAddrStr[NETADDR_MAXSTRSIZE];
		net_addr_str(&Addr, aAddrStr, sizeof(aAddrStr), false);
		pInfo->m_IpAddress = aAddrStr;

		if(pInfo->m_IpAddress.empty())
		{
			Log("ERROR: Failed to retrieve IP address | Client: %d", ClientId);
			return;
		}

		LogDebug("IP address populated | Client: %d | IP: %s", ClientId, aAddrStr);
		LoadCachedResultsForClient(ClientId);
	}

	if(IsLocalOrBogonIp(pInfo->m_IpAddress.c_str()) || IsIpWhitelisted(pInfo->m_IpAddress.c_str()))
		return;

	if(pInfo->GetResultByService(pServiceName))
	{
		LogDebug("Result already exists | Client: %d | Service: %s | Skipping duplicate check", ClientId, pServiceName);
		return;
	}

	auto pCachedResult = m_Cache.Get(pInfo->m_IpAddress.c_str(), pServiceName);
	if(pCachedResult)
	{
		pInfo->m_Results.push_back(pCachedResult);
		LogDebug("Cache hit | Client: %d | Service: %s | IP: %s", ClientId, pServiceName, pInfo->m_IpAddress.c_str());
		if(pCachedResult->IsBadIP())
			HandleFreshCachedBadResult(ClientId);
		return;
	}

	IVpnService *pService = GetService(pServiceName);
	if(!pService)
	{
		Log("ERROR: Service not found | Service: %s", pServiceName);
		return;
	}
	if(!pService->IsConfigured())
	{
		Log("VPN service skipped because it is not configured | Service: %s", pServiceName);
		return;
	}

	auto pRequest = std::make_shared<CVpnServiceRequest>(
		pServiceName,
		pInfo->m_IpAddress.c_str(),
		ClientId,
		this,
		pService);

	if(EnqueueRequest(pRequest))
	{
		pInfo->m_PendingChecks++;
		LogDebug("VPN check queued | Client: %d | Service: %s | IP: %s",
			ClientId, pServiceName, pInfo->m_IpAddress.c_str());
	}
}

CVpnClientInfo *CVpnDetectionComponent::GetClientInfo(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;
	return &m_aClientInfo[ClientId];
}

const CVpnClientInfo *CVpnDetectionComponent::GetClientInfo(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;
	return &m_aClientInfo[ClientId];
}

void CVpnDetectionComponent::RegisterService(const char *pServiceName, IVpnService *pService, int RateLimitMs)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);

	if(m_ServiceQueues.find(pServiceName) != m_ServiceQueues.end())
	{
		Log("WARNING: Service already registered | Service: %s", pServiceName);
		return;
	}

	m_Services[pServiceName] = std::unique_ptr<IVpnService>(pService);
	m_ServiceQueues[pServiceName] = SVpnServiceQueue(pServiceName, RateLimitMs);

	LogDebug("Service registered | Service: %s | Rate limit: %dms", pServiceName, RateLimitMs);
}

SVpnServiceQueue *CVpnDetectionComponent::GetServiceQueue(const char *pServiceName)
{
	auto It = m_ServiceQueues.find(pServiceName);
	if(It != m_ServiceQueues.end())
		return &It->second;
	return nullptr;
}

IVpnService *CVpnDetectionComponent::GetService(const char *pServiceName)
{
	auto It = m_Services.find(pServiceName);
	if(It != m_Services.end())
		return It->second.get();
	return nullptr;
}

bool CVpnDetectionComponent::IsServiceActive(const char *pServiceName) const
{
	if(!pServiceName || !pServiceName[0])
		return false;

	const auto ServiceIt = m_Services.find(pServiceName);
	if(ServiceIt == m_Services.end() || !ServiceIt->second || !ServiceIt->second->IsConfigured())
		return false;

	const auto SelectedServices = ParseServiceList(Config()->m_SvVpnServicesEnabled);
	for(const auto &Item : SelectedServices)
	{
		if(IsAllServicesToken(Item) || str_comp(Item.c_str(), pServiceName) == 0)
			return true;
	}

	return false;
}

std::vector<std::string> CVpnDetectionComponent::GetActiveServiceNames() const
{
	std::vector<std::string> Names;
	const auto SelectedServices = ParseServiceList(Config()->m_SvVpnServicesEnabled);
	for(const auto &Item : SelectedServices)
	{
		if(Item.empty())
			continue;

		if(IsAllServicesToken(Item))
		{
			for(const auto &ServicePair : m_Services)
			{
				if(ServicePair.second && ServicePair.second->IsConfigured() &&
					std::find(Names.begin(), Names.end(), ServicePair.first) == Names.end())
					Names.push_back(ServicePair.first);
			}
			break;
		}

		const auto ServiceIt = m_Services.find(Item);
		if(ServiceIt != m_Services.end() && ServiceIt->second && ServiceIt->second->IsConfigured() &&
			std::find(Names.begin(), Names.end(), Item) == Names.end())
			Names.push_back(Item);
	}

	return Names;
}

void CVpnDetectionComponent::SetDefaultService(const char *pServiceName)
{
	bool ShouldCheckAll = false;
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);

		if(m_ServiceQueues.find(pServiceName) == m_ServiceQueues.end())
		{
			Log("ERROR: Cannot set default service | Service: %s | Reason: Not registered", pServiceName);
			return;
		}

		m_DefaultService = pServiceName;
		Log("Default service configured | Service: %s", pServiceName);

		ShouldCheckAll = Config()->m_SvVpnEnabled;
	}

	if(ShouldCheckAll)
	{
		LogDebug("Initiating VPN checks for all connected clients with active services");
		CheckAllClientsActiveServices();
	}
}

void CVpnDetectionComponent::ProcessResult(int ClientId, std::shared_ptr<IVpnServiceResult> pResult)
{
	if(!pResult)
		return;

	m_Cache.Add(pResult);

	if(ClientId == -1)
	{
		PrintManualResult(pResult, false);
		return;
	}

	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(!IsResultForCurrentClient(ClientId, pResult.get()))
	{
		LogDebug("Ignoring stale VPN result | Client: %d | Service: %s | IP: %s",
			ClientId, pResult->GetServiceName(), pResult->GetIpAddress());
		return;
	}

	CVpnClientInfo *pInfo = &m_aClientInfo[ClientId];
	auto Existing = std::find_if(pInfo->m_Results.begin(), pInfo->m_Results.end(),
		[&](const std::shared_ptr<IVpnServiceResult> &pExisting) {
			return pExisting && str_comp(pExisting->GetServiceName(), pResult->GetServiceName()) == 0;
		});
	if(Existing != pInfo->m_Results.end())
		*Existing = pResult;
	else
		pInfo->m_Results.push_back(pResult);
	if(pInfo->m_PendingChecks > 0)
		pInfo->m_PendingChecks--;

	if(pResult->IsValid())
	{
		Log("VPN check completed | Client: %d | Service: %s | Bad IP: %s | Risk: %d%s%s",
			ClientId, pResult->GetServiceName(),
			pResult->IsBadIP() ? "true" : "false", pResult->GetRiskScore(),
			pResult->GetAsn()[0] ? " | ASN: " : "", pResult->GetAsn()[0] ? pResult->GetAsn() : "");

		if(Config()->m_SvVpnBanEnabled && pResult->IsBadIP())
		{
			NETADDR Addr;
			Addr = *Server()->ClientAddr(ClientId);
			char aAddrStr[NETADDR_MAXSTRSIZE];
			net_addr_str(&Addr, aAddrStr, sizeof(aAddrStr), false);

			if(IsIpWhitelisted(aAddrStr))
			{
				Log("VPN detected but IP is whitelisted | Client: %d | IP: %s", ClientId, aAddrStr);
			}
			else
			{
				const char *pReason = Config()->m_SvVpnBanReason;
				BanClient(ClientId, pReason);
			}
		}
	}
	else
	{
		Log("VPN check failed | Client: %d | Service: %s | Error: %s",
			ClientId, pResult->GetServiceName(),
			pResult->GetErrorMessage()[0] ? pResult->GetErrorMessage() : "Unknown error");
	}

	// All checks came back and the client is still here, so it may be announced.
	// A banned client was already dropped above, which leaves nothing to release.
	if(pInfo->m_PendingChecks <= 0)
		ReleaseJoinMessage(ClientId);
}

void CVpnDetectionComponent::PrintManualResult(std::shared_ptr<IVpnServiceResult> pResult, bool Cached)
{
	if(!pResult)
		return;

	char aBuf[512];
	if(pResult->IsValid())
	{
		str_format(aBuf, sizeof(aBuf),
			"IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s%s",
			pResult->GetIpAddress(),
			pResult->GetServiceName(),
			pResult->IsBadIP() ? "true" : "false",
			pResult->GetRiskScore(),
			pResult->GetAsn()[0] ? " | ASN: " : "",
			pResult->GetAsn()[0] ? pResult->GetAsn() : "",
			pResult->GetIsp()[0] ? " | ISP: " : "",
			pResult->GetIsp()[0] ? pResult->GetIsp() : "",
			Cached ? " | (cached)" : "");
	}
	else
	{
		str_format(aBuf, sizeof(aBuf),
			"IP: %s | Service: %s | Error: %s%s",
			pResult->GetIpAddress(),
			pResult->GetServiceName(),
			pResult->GetErrorMessage()[0] ? pResult->GetErrorMessage() : "Unknown error",
			Cached ? " | (cached)" : "");
	}
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
}

void CVpnDetectionComponent::ProcessRequestQueues()
{
	std::vector<std::shared_ptr<IVpnServiceRequest>> RequestsToExecute;
	const int AvailableSlots = MAX_CONCURRENT_VPN_REQUESTS - m_ActiveRequestThreads.load();
	if(AvailableSlots <= 0)
		return;

	{
		std::lock_guard<std::mutex> Lock(m_Mutex);

		for(auto &QueuePair : m_ServiceQueues)
		{
			if((int)RequestsToExecute.size() >= AvailableSlots)
				break;

			SVpnServiceQueue *pQueue = &QueuePair.second;

			if(!pQueue->CanProcessRequest())
				continue;

			auto pRequest = pQueue->DequeueRequest();
			if(pRequest)
			{
				RequestsToExecute.push_back(pRequest);
			}
		}
	}

	for(auto &pRequest : RequestsToExecute)
	{
		AsyncExecuteRequest(pRequest);
	}
}

void CVpnDetectionComponent::AsyncExecuteRequest(std::shared_ptr<IVpnServiceRequest> pRequest)
{
	std::lock_guard<std::mutex> Lock(m_ThreadMutex);

	m_ActiveRequestThreads.fetch_add(1);
	m_RequestThreads.emplace_back([this, pRequest]() {
		auto pResult = pRequest->Execute();
		if(pResult)
			QueueResult(pRequest->GetClientId(), pResult);

		m_ActiveRequestThreads.fetch_sub(1);

		// Mark this thread as finished so CleanupFinishedThreads can join it
		{
			std::lock_guard<std::mutex> FinishLock(m_FinishedMutex);
			m_FinishedThreadIds.insert(std::this_thread::get_id());
		}
	});
}

void CVpnDetectionComponent::CleanupFinishedThreads()
{
	// Collect finished thread IDs under the finished-mutex
	std::set<std::thread::id> FinishedIds;
	{
		std::lock_guard<std::mutex> Lock(m_FinishedMutex);
		if(m_FinishedThreadIds.empty())
			return;
		FinishedIds.swap(m_FinishedThreadIds);
	}

	// Now join and remove finished threads under the thread-mutex
	std::lock_guard<std::mutex> Lock(m_ThreadMutex);
	m_RequestThreads.erase(
		std::remove_if(m_RequestThreads.begin(), m_RequestThreads.end(),
			[&FinishedIds](std::thread &t) {
				if(FinishedIds.count(t.get_id()))
				{
					if(t.joinable())
						t.join();
					return true;
				}
				return false;
			}),
		m_RequestThreads.end());
}

void CVpnDetectionComponent::QueueConsoleMessage(const char *pMessage)
{
	std::lock_guard<std::mutex> Lock(m_MessageMutex);
	SPendingMessage Msg;
	Msg.m_Message = pMessage;
	Msg.m_Timestamp = time_get();
	m_PendingMessages.push_back(Msg);
}

void CVpnDetectionComponent::QueueResult(int ClientId, std::shared_ptr<IVpnServiceResult> pResult)
{
	if(!pResult)
		return;
	std::lock_guard<std::mutex> Lock(m_ResultMutex);
	m_PendingResults.push_back({ClientId, pResult});
}

bool CVpnDetectionComponent::EnqueueRequest(std::shared_ptr<IVpnServiceRequest> pRequest)
{
	if(!pRequest)
		return false;

	std::lock_guard<std::mutex> Lock(m_Mutex);

	const char *pServiceName = pRequest->GetServiceName();
	auto It = m_ServiceQueues.find(pServiceName);

	if(It == m_ServiceQueues.end())
	{
		Log("Cannot enqueue request - service '%s' not registered", pServiceName);
		return false;
	}

	if(!It->second.EnqueueRequest(pRequest, MAX_PENDING_VPN_REQUESTS_PER_SERVICE))
	{
		LogDebug("VPN check not queued | Client: %d | Service: %s | IP: %s | Queue size: %d/%d",
			pRequest->GetClientId(), pServiceName, pRequest->GetIpAddress(),
			It->second.GetQueueSize(), MAX_PENDING_VPN_REQUESTS_PER_SERVICE);
		return false;
	}
	return true;
}

int CVpnDetectionComponent::RemoveQueuedRequestsForClient(int ClientId)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);

	int Removed = 0;
	for(auto &QueuePair : m_ServiceQueues)
	{
		Removed += QueuePair.second.RemoveRequestsForClient(ClientId);
	}
	return Removed;
}

void CVpnDetectionComponent::CheckAllClientsDefaultService()
{
	if(m_DefaultService.empty())
		return;

	LogDebug("Checking all connected clients | Service: %s", m_DefaultService.c_str());

	int CheckCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer)
			continue;

		if(m_aClientInfo[i].m_IpAddress.empty())
		{
			NETADDR Addr;
			Addr = *Server()->ClientAddr(i);
			char aAddrStr[NETADDR_MAXSTRSIZE];
			net_addr_str(&Addr, aAddrStr, sizeof(aAddrStr), false);
			m_aClientInfo[i].m_IpAddress = aAddrStr;

			LogDebug("IP address populated for existing client | Client: %d | IP: %s", i, aAddrStr);
			LoadCachedResultsForClient(i);
		}

		CheckClientService(i, m_DefaultService.c_str());
		CheckCount++;
	}

	if(CheckCount > 0)
		LogDebug("Queued VPN checks for %d connected client(s)", CheckCount);
}

void CVpnDetectionComponent::CheckAllClientsActiveServices()
{
	const auto ServiceNames = GetActiveServiceNames();
	if(ServiceNames.empty())
		return;

	LogDebug("Checking all connected clients | Active services: %d", (int)ServiceNames.size());

	int CheckCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer)
			continue;

		if(m_aClientInfo[i].m_IpAddress.empty())
		{
			NETADDR Addr;
			Addr = *Server()->ClientAddr(i);
			char aAddrStr[NETADDR_MAXSTRSIZE];
			net_addr_str(&Addr, aAddrStr, sizeof(aAddrStr), false);
			m_aClientInfo[i].m_IpAddress = aAddrStr;

			LogDebug("IP address populated for existing client | Client: %d | IP: %s", i, aAddrStr);
			LoadCachedResultsForClient(i);
		}

		if(IsLocalOrBogonIp(m_aClientInfo[i].m_IpAddress.c_str()) || IsIpWhitelisted(m_aClientInfo[i].m_IpAddress.c_str()))
			continue;

		if(HandleFreshCachedBadResult(i))
			continue;

		for(const auto &ServiceName : ServiceNames)
		{
			CheckClientService(i, ServiceName.c_str());
			CheckCount++;
		}
	}

	if(CheckCount > 0)
		LogDebug("Queued VPN checks for %d client/service pair(s)", CheckCount);
}

bool CVpnDetectionComponent::CheckIpService(const char *pIpAddress, const char *pServiceName, bool ForceRefresh, bool PrintManual)
{
	if(!pIpAddress || !pIpAddress[0] || !pServiceName || !pServiceName[0])
		return false;

	if(IsLocalOrBogonIp(pIpAddress) || IsIpWhitelisted(pIpAddress))
	{
		auto pLocalResult = std::make_shared<CVpnServiceResult>();
		pLocalResult->m_ServiceName = "local";
		pLocalResult->m_IpAddress = pIpAddress;
		pLocalResult->m_Asn = "Local Network";
		pLocalResult->m_Isp = "Private IP Address";
		pLocalResult->m_IsBadIP = false;
		pLocalResult->m_RiskScore = 0;
		pLocalResult->m_IsValid = true;
		pLocalResult->m_Timestamp = time_timestamp();
		if(PrintManual)
			PrintManualResult(pLocalResult, false);
		return true;
	}

	auto pCachedResult = !ForceRefresh ? m_Cache.Get(pIpAddress, pServiceName) : nullptr;
	if(pCachedResult)
	{
		if(PrintManual)
			PrintManualResult(pCachedResult, true);
		return true;
	}

	IVpnService *pService = GetService(pServiceName);
	if(!pService)
	{
		if(PrintManual)
		{
			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), "Service '%s' not found. Use vpn_service_list to see available services.", pServiceName);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		}
		return false;
	}

	if(!pService->IsConfigured())
	{
		if(PrintManual)
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "Service '%s' is not configured. Set its API key/contact config first.", pServiceName);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		}
		return false;
	}

	auto pRequest = std::make_shared<CVpnServiceRequest>(
		pServiceName,
		pIpAddress,
		-1,
		this,
		pService);

	return EnqueueRequest(pRequest);
}

void CVpnDetectionComponent::LoadCachedResultsForClient(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	CVpnClientInfo *pInfo = &m_aClientInfo[ClientId];
	if(pInfo->m_IpAddress.empty())
		return;

	std::vector<std::shared_ptr<IVpnServiceResult>> CachedResults;
	m_Cache.GetAllForIP(pInfo->m_IpAddress.c_str(), CachedResults);

	if(!CachedResults.empty())
	{
		pInfo->m_Results = CachedResults;
		LogDebug("Cached results loaded | Client: %d | IP: %s | Count: %d",
			ClientId, pInfo->m_IpAddress.c_str(), (int)CachedResults.size());
	}
}

bool CVpnDetectionComponent::HandleFreshCachedBadResult(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	CVpnClientInfo *pInfo = &m_aClientInfo[ClientId];
	for(const auto &pResult : pInfo->m_Results)
	{
		if(!pResult || !pResult->IsValid() || !pResult->IsBadIP())
			continue;

		Log("VPN cached bad result | Client: %d | Service: %s | IP: %s | Risk: %d",
			ClientId, pResult->GetServiceName(), pResult->GetIpAddress(), pResult->GetRiskScore());

		if(Config()->m_SvVpnBanEnabled && !IsIpWhitelisted(pInfo->m_IpAddress.c_str()))
			BanClient(ClientId, Config()->m_SvVpnBanReason);
		return true;
	}

	return false;
}

bool CVpnDetectionComponent::IsResultForCurrentClient(int ClientId, const IVpnServiceResult *pResult)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pResult)
		return false;

	if(!GameServer()->m_apPlayers[ClientId])
		return false;

	const CVpnClientInfo *pInfo = &m_aClientInfo[ClientId];
	if(pInfo->m_IpAddress.empty())
		return false;

	return str_comp(pInfo->m_IpAddress.c_str(), pResult->GetIpAddress()) == 0;
}

bool CVpnDetectionComponent::WillHoldJoinMessage(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	// Only a ban can still remove the client, so only then are the messages withheld
	if(!Config()->m_SvVpnEnabled || !Config()->m_SvVpnBanEnabled)
		return false;

	const CVpnClientInfo *pInfo = &m_aClientInfo[ClientId];
	if(IsLocalOrBogonIp(pInfo->m_IpAddress.c_str()) || IsIpWhitelisted(pInfo->m_IpAddress.c_str()))
		return false;

	// A cached bad verdict bans this client without asking anyone, keep it quiet
	if(pInfo->IsBadIP())
		return true;

	// Every service that would be asked answered recently enough for the cache, so no
	// request is sent and a clean client is announced without any delay
	for(const auto &ServiceName : GetActiveServiceNames())
	{
		if(!pInfo->GetResultByService(ServiceName.c_str()))
			return true;
	}

	return false;
}

void CVpnDetectionComponent::HoldJoinMessage(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(!GameServer()->m_apPlayers[ClientId])
		return;

	m_aClientInfo[ClientId].m_JoinMsgHoldExpire = time_get() + (int64_t)JOIN_MSG_MAX_HOLD_SECONDS * time_freq();
	GameServer()->Bw().HoldJoinMessage(ClientId);
}

void CVpnDetectionComponent::ReleaseJoinMessage(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	m_aClientInfo[ClientId].m_JoinMsgHoldExpire = 0;
	GameServer()->Bw().ReleaseJoinMessage(ClientId);
}

void CVpnDetectionComponent::ProcessJoinMessageHolds()
{
	const int64_t Now = time_get();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!m_aClientInfo[i].m_JoinMsgHoldExpire)
			continue;

		if(!GameServer()->m_apPlayers[i])
		{
			m_aClientInfo[i].m_JoinMsgHoldExpire = 0;
			continue;
		}

		if(Now < m_aClientInfo[i].m_JoinMsgHoldExpire)
			continue;

		// Announce the client anyway instead of hiding a join forever because a
		// service never answered
		Log("VPN check did not answer in %d seconds, announcing join | Client: %d | Pending checks: %d",
			JOIN_MSG_MAX_HOLD_SECONDS, i, m_aClientInfo[i].m_PendingChecks);
		ReleaseJoinMessage(i);
	}
}

void CVpnDetectionComponent::BanClient(int ClientId, const char *pReason)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(!GameServer()->m_apPlayers[ClientId])
		return;

	int BanSeconds = m_BanTimeMinutes * 60;
	Server()->Ban(ClientId, BanSeconds, pReason, false);

	Log("Client banned | ID: %d | Name: %s | Duration: %d minutes | Reason: %s",
		ClientId, Server()->ClientName(ClientId), m_BanTimeMinutes, pReason);
}

void CVpnDetectionComponent::SetServiceRateLimit(const char *pServiceName, int RateLimitMs)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);

	auto It = m_ServiceQueues.find(pServiceName);
	if(It != m_ServiceQueues.end())
	{
		It->second.m_RateLimitMs = RateLimitMs;
		LogDebug("Rate limit updated | Service: %s | Rate limit: %dms", pServiceName, RateLimitMs);
	}
}

void CVpnDetectionComponent::SetServiceCooldownMs(const char *pServiceName, int CooldownMs)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);

	auto It = m_ServiceQueues.find(pServiceName);
	if(It != m_ServiceQueues.end())
	{
		It->second.SetBackoffMs(CooldownMs);
		LogDebug("Service cooldown applied | Service: %s | Cooldown: %dms", pServiceName, CooldownMs);
	}
}

bool CVpnDetectionComponent::IsIpWhitelisted(const char *pIp) const
{
	return m_WhitelistedIps.find(pIp) != m_WhitelistedIps.end();
}

void CVpnDetectionComponent::WhitelistIpAdd(const char *pIp)
{
	m_WhitelistedIps.insert(pIp);
	SaveWhitelist();
}

void CVpnDetectionComponent::WhitelistIpRemove(const char *pIp)
{
	m_WhitelistedIps.erase(pIp);
	SaveWhitelist();
}

void CVpnDetectionComponent::SaveWhitelist()
{
	IOHANDLE File = io_open("data/vpn_whitelist.txt", IOFLAG_WRITE);
	if(!File)
	{
		Log("Failed to save VPN whitelist");
		return;
	}
	for(const auto &Ip : m_WhitelistedIps)
	{
		io_write(File, Ip.c_str(), Ip.size());
		io_write_newline(File);
	}
	io_close(File);
}

void CVpnDetectionComponent::LoadWhitelist()
{
	CLineReader LineReader;
	IOHANDLE File = io_open("data/vpn_whitelist.txt", IOFLAG_READ);
	if(!LineReader.OpenFile(File))
		return;
	while(const char *pLine = LineReader.Get())
	{
		if(pLine[0] != '\0')
			m_WhitelistedIps.insert(pLine);
	}
	if(!m_WhitelistedIps.empty())
		Log("VPN whitelist loaded | Entries: %d", (int)m_WhitelistedIps.size());
}
