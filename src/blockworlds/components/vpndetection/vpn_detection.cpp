#include "vpn_detection.h"
#include "convar_helpers.h"
#include "services/getipintel_service.h"
#include "services/ipquery_service.h"
#include "time_parser.h"
#include "vpn_commands.h"

#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>

bool CVpnDetectionComponent::IsDebug() const
{
	return m_Debug;
}

CVpnDetectionComponent::CVpnDetectionComponent(CGameContext *pGameServer) :
	CComponent(pGameServer),
	m_GetipintelThreshold(99.00f),
	m_Debug(false),
	m_Enabled(false),
	m_BanEnabled(false),
	m_BanTimeMinutes(60),
	m_RateLimitIpquery(100),
	m_RateLimitGetipintel(4000),
	m_DefaultService("")
{
	m_aGetipintelContact[0] = '\0';
	m_aBanTimeString[0] = '\0';
	str_copy(m_aBanTimeString, "60", sizeof(m_aBanTimeString));

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aClientInfo[i].m_ClientId = i;
	}

	auto pIPQuery = new CIPQueryService();
	RegisterService("ipquery", pIPQuery, m_RateLimitIpquery);
	SetDefaultService("ipquery");

	Log("VPN service registered: ipquery | API: https://api.ipquery.io/ | Rate limit: %dms", m_RateLimitIpquery);

	auto pGetIPIntel = new CGetIPIntelService();
	pGetIPIntel->SetFlags("b");
	pGetIPIntel->SetOutputFlags("b");
	pGetIPIntel->SetContactEmail(m_aGetipintelContact);
	pGetIPIntel->SetThreshold(m_GetipintelThreshold / 100.0f);
	RegisterService("getipintel", pGetIPIntel, m_RateLimitGetipintel);

	Log("VPN service registered: getipintel | API: https://getipintel.net/ | Contact: %s | Threshold: %.2f%% | Rate limit: %dms",
		m_aGetipintelContact[0] ? m_aGetipintelContact : "(not set)",
		m_GetipintelThreshold, m_RateLimitGetipintel);
}

#define LIST_OF_ALL_COMMANDS(DEF) \
	DEF("vpn_status", "?i[full_check]", CFGFLAG_SERVER, VpnCommands::ConVPNStatus, this, "Show VPN status of connected clients (full_check=1 to queue fresh checks)") \
	DEF("vpn_service_default", "s[service_name]", CFGFLAG_SERVER, VpnCommands::ConVPNSetDefaultService, this, "Set the default VPN detection service") \
	DEF("vpn_service_list", "", CFGFLAG_SERVER, VpnCommands::ConVPNServiceList, this, "List all registered VPN services") \
	DEF("vpn_check", "s[id_or_ip] ?s[service_name]", CFGFLAG_SERVER, VpnCommands::ConVPNCheck, this, "Test VPN detection on a client ID or IP address (service optional, allows 'all')") \
	DEF("vpn_check_force", "s[id_or_ip] ?s[service_name]", CFGFLAG_SERVER, VpnCommands::ConVPNCheckForce, this, "Force fresh VPN check, bypassing cache (service optional, allows 'all')")

void CVpnDetectionComponent::OnConsoleInit()
{
#define REGISTER_COMMAND(name, params, flags, callback, userdata, help) Console()->Register(name, params, flags, callback, userdata, help);
	LIST_OF_ALL_COMMANDS(REGISTER_COMMAND)
#undef REGISTER_COMMAND

	m_ConVarCallbacks.push_back(CONVAR_BOOL(m_Debug, false));
	Console()->Register("vpn_debug", "?i[value]", CFGFLAG_SERVER, ConVarBoolCallback, m_ConVarCallbacks.back(),
		"Enable verbose debug logging for VPN detection (default: 0)");

	m_ConVarCallbacks.push_back(CONVAR_BOOL_ONCHANGE(m_Enabled, false,
		[this](bool Enable) {
			if(Enable)
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "VPN detection enabled");
				CheckAllClientsDefaultService();
			}
			else
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "VPN detection disabled");
			}
		}));
	Console()->Register("vpn_enable", "?i[value]", CFGFLAG_SERVER, ConVarBoolCallback, m_ConVarCallbacks.back(),
		"Enable or disable VPN detection (default: 0)");

	m_ConVarCallbacks.push_back(CONVAR_BOOL(m_BanEnabled, false));
	Console()->Register("vpn_ban_enable", "?i[value]", CFGFLAG_SERVER, ConVarBoolCallback, m_ConVarCallbacks.back(),
		"Enable automatic banning of detected VPN users (default: 0)");

	m_ConVarCallbacks.push_back(CONVAR_STRING_ONCHANGE(m_aBanTimeString, "60",
		[this](const char *pTimeStr) {
			int Minutes = 0;
			if(ParseTimeStringMinutes(pTimeStr, &Minutes))
			{
				if(Minutes < 1)
					Minutes = 1;
				SetBanTimeMinutes(Minutes);
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "VPN ban time set to %d minutes", Minutes);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			}
			else
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf),
					"Invalid time format: '%s'. Examples: '60', '1h', '1d', '1d5h10m', '10 minutes'",
					pTimeStr);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			}
		}));
	Console()->Register("vpn_ban_time", "?r[value]", CFGFLAG_SERVER, ConVarStringCallback, m_ConVarCallbacks.back(),
		"Ban duration for VPN users (supports: '60', '1h', '1d', '1d5h10m', etc. Plain numbers = minutes) (default: \"60\")");

	m_ConVarCallbacks.push_back(CONVAR_INT_ONCHANGE(m_RateLimitIpquery, 100, 500, 10000,
		[this](int Value) { SetServiceRateLimit("ipquery", Value); }));
	Console()->Register("vpn_ratelimit_ipquery", "?i[value]", CFGFLAG_SERVER, ConVarIntCallback, m_ConVarCallbacks.back(),
		"Rate limit in milliseconds for ipquery service API requests (default: 100, min: 500, max: 10000)");

	m_ConVarCallbacks.push_back(CONVAR_INT_ONCHANGE(m_RateLimitGetipintel, 4000, 500, 10000,
		[this](int Value) { SetServiceRateLimit("getipintel", Value); }));
	Console()->Register("vpn_ratelimit_getipintel", "?i[value]", CFGFLAG_SERVER, ConVarIntCallback, m_ConVarCallbacks.back(),
		"Rate limit in milliseconds for getipintel service API requests (default: 4000, min: 500, max: 10000)");

	m_ConVarCallbacks.push_back(CONVAR_STRING_ONCHANGE(m_aGetipintelContact, "",
		[this](const char *pEmail) {
			auto *pService = dynamic_cast<CGetIPIntelService *>(GetService("getipintel"));
			if(pService)
				pService->SetContactEmail(pEmail);
		}));
	Console()->Register("vpn_service_getipintel_contact", "?r[value]", CFGFLAG_SERVER, ConVarStringCallback, m_ConVarCallbacks.back(),
		"Contact email for GetIPIntel API (required for service to work) (default: \"\")");

	m_ConVarCallbacks.push_back(CONVAR_FLOAT_ONCHANGE(m_GetipintelThreshold, 99.00f, 0.0f, 100.0f,
		[this](float Threshold) {
			auto *pService = dynamic_cast<CGetIPIntelService *>(GetService("getipintel"));
			if(pService)
				pService->SetThreshold(Threshold / 100.0f);
		}));
	Console()->Register("vpn_service_getipintel_threshold", "?f[value]", CFGFLAG_SERVER, ConVarFloatCallback, m_ConVarCallbacks.back(),
		"Probability threshold (0.00-100.00) for marking IP as bad (default: 99.00, min: 0.00, max: 100.00)");

	if(m_Cache.Load("data/vpn_cache.json"))
	{
		Log("VPN cache loaded successfully | Entries: %d", m_Cache.GetEntryCount());
	}
	else
	{
		LogDebug("VPN cache file not found or empty, starting with fresh cache");
	}
}

void CVpnDetectionComponent::OnTick()
{
	{
		std::lock_guard<std::mutex> Lock(m_MessageMutex);
		for(const auto &Msg : m_PendingMessages)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", Msg.m_Message.c_str());
		}
		m_PendingMessages.clear();
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

void CVpnDetectionComponent::OnConsoleTerminate()
{
#define UNREGISTER_COMMAND(name, params, flags, callback, userdata, help) Console()->Deregister(name);
	LIST_OF_ALL_COMMANDS(UNREGISTER_COMMAND)
#undef UNREGISTER_COMMAND

	Console()->Deregister("vpn_debug");
	Console()->Deregister("vpn_enable");
	Console()->Deregister("vpn_ban_enable");
	Console()->Deregister("vpn_ban_time");
	Console()->Deregister("vpn_ratelimit_ipquery");
	Console()->Deregister("vpn_ratelimit_getipintel");
	Console()->Deregister("vpn_service_getipintel_contact");
	Console()->Deregister("vpn_service_getipintel_threshold");

	for(void *pCallback : m_ConVarCallbacks)
		delete static_cast<char *>(pCallback);
	m_ConVarCallbacks.clear();
}

#undef LIST_OF_ALL_COMMANDS

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

	if(IsLocalIP)
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

	if(m_Enabled && !m_DefaultService.empty())
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
	std::lock_guard<std::mutex> Lock(m_Mutex);

	if(m_ServiceQueues.find(pServiceName) == m_ServiceQueues.end())
	{
		Log("ERROR: Cannot set default service | Service: %s | Reason: Not registered", pServiceName);
		return;
	}

	m_DefaultService = pServiceName;
	Log("Default service configured | Service: %s", pServiceName);

	if(m_Enabled)
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

		if(m_BanEnabled && pResult->IsBadIP())
		{
			const char *pReason = "VPN/Proxy detected. Appeal at .gg/fYaBTzY";
			BanClient(ClientId, pReason);
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
		{
			ProcessResult(pRequest->GetClientId(), pResult);
		}
	});
}

void CVpnDetectionComponent::CleanupFinishedThreads()
{
	std::lock_guard<std::mutex> Lock(m_ThreadMutex);

	m_RequestThreads.erase(
		std::remove_if(m_RequestThreads.begin(), m_RequestThreads.end(),
			[](std::thread &t) {
				if(t.joinable())
				{
					t.detach();
					return true;
				}
				return false;
			}),
		m_RequestThreads.end());

	m_RequestThreads.clear();
}

void CVpnDetectionComponent::QueueConsoleMessage(const char *pMessage)
{
	std::lock_guard<std::mutex> Lock(m_MessageMutex);
	SPendingMessage Msg;
	Msg.m_Message = pMessage;
	Msg.m_Timestamp = time_get();
	m_PendingMessages.push_back(Msg);
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
