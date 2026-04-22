#include "vpn_detection.h"
#include "services/getipintel_service.h"
#include "services/iphub_service.h"
#include "services/ipquery_service.h"
#include "time_parser.h"
#include "vpn_commands.h"

#include <engine/server.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/shared/linereader.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>

bool CVpnDetectionComponent::IsDebug() const
{
	return Config()->m_SvVpnDebug;
}

CVpnDetectionComponent::CVpnDetectionComponent(CGameContext *pGameServer) :
	CComponent(pGameServer),
	m_BanTimeMinutes(60)
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
	pGetIPIntel->SetThreshold(Config()->m_SvVpnGetipintelThreshold / 100.0f);
	RegisterService("getipintel", pGetIPIntel, Config()->m_SvVpnRateLimitGetipintel);

	Log("VPN service registered: getipintel | API: https://getipintel.net/ | Contact: %s | Threshold: %.2f%% | Rate limit: %dms",
		Config()->m_SvVpnGetipintelContact[0] ? Config()->m_SvVpnGetipintelContact : "(not set)",
		Config()->m_SvVpnGetipintelThreshold, Config()->m_SvVpnRateLimitGetipintel);

	auto pIPHub = new CIPHubService();
	pIPHub->SetApiKeyPtr(Config()->m_SvVpnIphubApiKey);
	RegisterService("iphub", pIPHub, Config()->m_SvVpnRateLimitIphub);

	Log("VPN service registered: iphub | API: https://v2.api.iphub.info/ | API key: %s | Rate limit: %dms",
		Config()->m_SvVpnIphubApiKey[0] ? "(set)" : "(not set)",
		Config()->m_SvVpnRateLimitIphub);

	SetDefaultService(Config()->m_SvVpnServiceDefault);

	CONSOLE_COMMAND("vpn_status", "?i[full_check]", VpnCommands::ConVPNStatus, "Show VPN status of connected clients (full_check=1 to queue fresh checks)")
	CONSOLE_COMMAND("vpn_service_list", "", VpnCommands::ConVPNServiceList, "List all registered VPN services")
	CONSOLE_COMMAND("vpn_check", "s[id_or_ip] ?s[service_name]", VpnCommands::ConVPNCheck, "Test VPN detection on a client ID or IP address (service optional, allows 'all')")
	CONSOLE_COMMAND("vpn_check_force", "s[id_or_ip] ?s[service_name]", VpnCommands::ConVPNCheckForce, "Force fresh VPN check, bypassing cache (service optional, allows 'all')")
	CONSOLE_COMMAND("vpn_whitelist_add", "s[ip]", VpnCommands::ConVPNWhitelistAdd, "Add an IP to the VPN detection whitelist")
	CONSOLE_COMMAND("vpn_whitelist_remove", "s[ip]", VpnCommands::ConVPNWhitelistRemove, "Remove an IP from the VPN detection whitelist")
	CONSOLE_COMMAND("vpn_whitelist_list", "", VpnCommands::ConVPNWhitelistList, "List all whitelisted IPs for VPN detection")

	CHAIN_COMMAND("sv_vpn_enabled", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent*)pUserData;
		if (pResult->NumArguments() < 1)
			return;

		bool Enable = pResult->GetInteger(0);
		if(Enable)
		{
			Log("VPN detection enabled");
			pSelf->CheckAllClientsDefaultService();
		}
		else
		{
			Log("VPN detection disabled");
		}
	});
	CHAIN_COMMAND("sv_vpn_ban_time", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		auto *pSelf = (ThisComponent*)pUserData;
		if (pResult->NumArguments() < 1) {
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
		auto *pSelf = (ThisComponent*)pUserData;
		if (pResult->NumArguments() < 1)
			return;

		int Value = pResult->GetInteger(0);
		pSelf->SetServiceRateLimit("ipquery", Value);
	});
	CHAIN_COMMAND("sv_vpn_ratelimit_getipintel", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent*)pUserData;
		if (pResult->NumArguments() < 1)
			return;

		int Value = pResult->GetInteger(0);
		pSelf->SetServiceRateLimit("getipintel", Value);
	});
	CHAIN_COMMAND("sv_vpn_service_getipintel_contact", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent*)pUserData;
		if (pResult->NumArguments() < 1)
			return;

		const char *pEmail = pResult->GetString(0);
		auto *pService = dynamic_cast<CGetIPIntelService *>(pSelf->GetService("getipintel"));
		if(pService)
			pService->SetContactEmail(pEmail);
	});
	CHAIN_COMMAND("sv_vpn_service_getipintel_threshold", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent*)pUserData;
		if (pResult->NumArguments() < 1)
			return;

		float Threshold = pResult->GetFloat(0);
		auto *pService = dynamic_cast<CGetIPIntelService *>(pSelf->GetService("getipintel"));
		if(pService)
			pService->SetThreshold(Threshold / 100.0f);
	});
	CHAIN_COMMAND("sv_vpn_ratelimit_iphub", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		auto *pSelf = (ThisComponent*)pUserData;
		if (pResult->NumArguments() < 1)
			return;

		int Value = pResult->GetInteger(0);
		pSelf->SetServiceRateLimit("iphub", Value);
	});
	CHAIN_COMMAND("sv_vpn_service_default", VpnCommands::ConVPNSetDefaultService)

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

	// process results queued by async threads (must run on main thread cuz ProcessResult cahnges m_aClientInfo and may ban ppl)
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

void CVpnDetectionComponent::OnPlayerEnter(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	m_aClientInfo[ClientId].m_Results.clear();
	m_aClientInfo[ClientId].m_CheckInProgress = false;
	m_aClientInfo[ClientId].m_LastCheckTime = 0;

	NETADDR Addr;
	Server()->GetClientAddr(ClientId, &Addr);
	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Addr, aAddrStr, sizeof(aAddrStr), false);
	m_aClientInfo[ClientId].m_IpAddress = aAddrStr;

	LogDebug("Client connected | ID: %d | Name: %s | IP: %s",
		ClientId, Server()->ClientName(ClientId), aAddrStr);

	LoadCachedResultsForClient(ClientId);

	bool IsLocalIP = str_startswith(aAddrStr, "192.168.") ||
			 str_startswith(aAddrStr, "10.") ||
			 str_startswith(aAddrStr, "172.16.") ||
			 str_startswith(aAddrStr, "127.");

	if(IsLocalIP || IsIpWhitelisted(aAddrStr))
	{
		auto pLocalResult = std::make_shared<CVpnServiceResult>();
		pLocalResult->m_ServiceName = "local";
		pLocalResult->m_IpAddress = aAddrStr;
		pLocalResult->m_Asn = "Local Network";
		pLocalResult->m_Isp = "Private IP Address";
		pLocalResult->m_IsBadIP = false;
		pLocalResult->m_RiskScore = 0;
		pLocalResult->m_IsValid = true;
		pLocalResult->m_Timestamp = time_get();

		LogDebug("Local IP detected | Client: %d | IP: %s | Skipping VPN check", ClientId, aAddrStr);
		ProcessResult(ClientId, pLocalResult);
		return;
	}

	if(Config()->m_SvVpnEnabled && !m_DefaultService.empty())
	{
		CheckClientService(ClientId, m_DefaultService.c_str());
	}
}

void CVpnDetectionComponent::OnPlayerDrop(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	m_aClientInfo[ClientId].m_Results.clear();
	m_aClientInfo[ClientId].m_CheckInProgress = false;
	m_aClientInfo[ClientId].m_IpAddress.clear();
}

void CVpnDetectionComponent::CheckClient(int ClientId, bool FullCheck)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(FullCheck)
	{
		// Collect service names while holding the lock, then release it
		// before calling CheckClientService (which calls EnqueueRequest that needs the lock)
		std::vector<std::string> ServiceNames;
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			for(const auto &QueuePair : m_ServiceQueues)
			{
				ServiceNames.push_back(QueuePair.first);
			}
		}

		for(const auto &ServiceName : ServiceNames)
		{
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
		Server()->GetClientAddr(ClientId, &Addr);
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

	bool IsLocalIP = str_startswith(pInfo->m_IpAddress.c_str(), "192.168.") ||
			 str_startswith(pInfo->m_IpAddress.c_str(), "10.") ||
			 str_startswith(pInfo->m_IpAddress.c_str(), "172.16.") ||
			 str_startswith(pInfo->m_IpAddress.c_str(), "127.");

	if(IsLocalIP)
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
		return;
	}

	IVpnService *pService = GetService(pServiceName);
	if(!pService)
	{
		Log("ERROR: Service not found | Service: %s", pServiceName);
		return;
	}

	auto pRequest = std::make_shared<CVpnServiceRequest>(
		pServiceName,
		pInfo->m_IpAddress.c_str(),
		ClientId,
		this,
		pService);

	EnqueueRequest(pRequest);
	pInfo->m_CheckInProgress = true;

	LogDebug("VPN check queued | Client: %d | Service: %s | IP: %s",
		ClientId, pServiceName, pInfo->m_IpAddress.c_str());
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
		LogDebug("Initiating VPN checks for all connected clients with default service");
		CheckAllClientsDefaultService();
	}
}

void CVpnDetectionComponent::ProcessResult(int ClientId, std::shared_ptr<IVpnServiceResult> pResult)
{
	if(!pResult)
		return;

	m_Cache.Add(pResult);

	if(ClientId == -1)
	{
		if(pResult->IsValid())
		{
			LogDebug("Manual test completed | Service: %s | IP: %s | Bad IP: %s | Risk: %d",
				pResult->GetServiceName(), pResult->GetIpAddress(),
				pResult->IsBadIP() ? "true" : "false", pResult->GetRiskScore());
		}
		else
		{
			LogDebug("Manual test failed | Service: %s | IP: %s | Error: %s",
				pResult->GetServiceName(), pResult->GetIpAddress(),
				pResult->GetErrorMessage()[0] ? pResult->GetErrorMessage() : "Unknown error");
		}
		return;
	}

	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	CVpnClientInfo *pInfo = &m_aClientInfo[ClientId];
	pInfo->m_Results.push_back(pResult);
	pInfo->m_CheckInProgress = false;

	if(pResult->IsValid())
	{
		Log("VPN check completed | Client: %d | Service: %s | Bad IP: %s | Risk: %d%s%s",
			ClientId, pResult->GetServiceName(),
			pResult->IsBadIP() ? "true" : "false", pResult->GetRiskScore(),
			pResult->GetAsn()[0] ? " | ASN: " : "", pResult->GetAsn()[0] ? pResult->GetAsn() : "");

		if(Config()->m_SvVpnBanEnabled && pResult->IsBadIP())
		{
			NETADDR Addr;
			Server()->GetClientAddr(ClientId, &Addr);
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
}

void CVpnDetectionComponent::ProcessRequestQueues()
{
	std::vector<std::shared_ptr<IVpnServiceRequest>> RequestsToExecute;

	{
		std::lock_guard<std::mutex> Lock(m_Mutex);

		for(auto &QueuePair : m_ServiceQueues)
		{
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

	m_RequestThreads.emplace_back([this, pRequest]() {
		auto pResult = pRequest->Execute();
		if(pResult)
			QueueResult(pRequest->GetClientId(), pResult);

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

void CVpnDetectionComponent::EnqueueRequest(std::shared_ptr<IVpnServiceRequest> pRequest)
{
	if(!pRequest)
		return;

	std::lock_guard<std::mutex> Lock(m_Mutex);

	const char *pServiceName = pRequest->GetServiceName();
	auto It = m_ServiceQueues.find(pServiceName);

	if(It == m_ServiceQueues.end())
	{
		Log("Cannot enqueue request - service '%s' not registered", pServiceName);
		return;
	}

	It->second.EnqueueRequest(pRequest);
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
			Server()->GetClientAddr(i, &Addr);
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
