#include "vpn_commands.h"
#include "vpn_detection.h"

#include <blockworlds/bw_base.h>
#include <engine/server/server.h>
#include <engine/shared/console.h>
#include <game/server/player.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {
bool IsNumeric(const char *pStr)
{
	if(!pStr || !pStr[0])
		return false;

	for(int i = 0; pStr[i]; i++)
	{
		if(pStr[i] < '0' || pStr[i] > '9')
			return false;
	}
	return true;
}

bool LooksLikeIpAddress(const char *pStr)
{
	if(!pStr || !pStr[0])
		return false;

	NETADDR Addr;
	if(net_addr_from_str(&Addr, pStr) == 0)
		return true;

	bool HasColon = false;
	for(int i = 0; pStr[i]; i++)
	{
		const char c = pStr[i];
		if(c == ':')
			HasColon = true;
		else if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '.'))
			return false;
	}
	return HasColon;
}

void PrintUsage(CVpnDetectionComponent *pSelf, bool ForceRefresh)
{
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
		ForceRefresh ? "Usage: vpn_check_force <client_id|ip_address> [service_name|all]" : "Usage: vpn_check <client_id|ip_address> [service_name|all]");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
		"Examples: vpn_check 0 | vpn_check 8.8.8.8 all | vpn_check_force 8.8.8.8 proxycheck");
}

std::vector<std::string> ResolveServices(CVpnDetectionComponent *pSelf, const char *pServiceName)
{
	if(pServiceName && pServiceName[0] && str_comp(pServiceName, "all") != 0)
		return {pServiceName};
	if(pServiceName && str_comp(pServiceName, "all") == 0)
	{
		std::vector<std::string> Services;
		for(const auto &ServicePair : pSelf->GetServiceQueues())
		{
			IVpnService *pService = pSelf->GetService(ServicePair.first.c_str());
			if(pService && pService->IsConfigured())
				Services.push_back(ServicePair.first);
		}
		return Services;
	}

	const char *pDefault = pSelf->GetDefaultService();
	if(pDefault && pDefault[0])
		return {pDefault};
	return {};
}

std::string JoinNames(const std::vector<std::string> &Names)
{
	std::string Result;
	for(const auto &Name : Names)
	{
		if(!Result.empty())
			Result += ", ";
		Result += Name;
	}
	return Result.empty() ? "(none)" : Result;
}

void PerformVPNCheck(IConsole::IResult *pResult, void *pUserData, bool ForceRefresh)
{
	auto *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);
	if(pResult->NumArguments() < 1)
	{
		PrintUsage(pSelf, ForceRefresh);
		return;
	}

	const char *pInput = pResult->GetString(0);
	const char *pRequestedService = pResult->NumArguments() >= 2 ? pResult->GetString(1) : nullptr;

	std::string IpAddress;
	int ClientId = -1;
	if(IsNumeric(pInput) && !LooksLikeIpAddress(pInput))
	{
		ClientId = str_toint(pInput);
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Invalid client ID: %d (must be 0-%d)", ClientId, MAX_CLIENTS - 1);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}
		if(!pSelf->GameServer()->m_apPlayers[ClientId])
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Client %d is not connected", ClientId);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		const CVpnClientInfo *pInfo = pSelf->GetClientInfo(ClientId);
		if(!pInfo || pInfo->m_IpAddress.empty())
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Failed to get IP address for client %d", ClientId);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}
		IpAddress = pInfo->m_IpAddress;
	}
	else if(LooksLikeIpAddress(pInput))
	{
		IpAddress = pInput;
	}
	else
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Invalid input: '%s' (must be a client ID or IP address)", pInput);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		return;
	}

	const auto Services = ResolveServices(pSelf, pRequestedService);
	if(Services.empty())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "No VPN services selected or configured");
		return;
	}

	for(const auto &ServiceName : Services)
	{
		char aBuf[256];
		if(ClientId >= 0)
		{
			str_format(aBuf, sizeof(aBuf), "Checking client %d (%s) IP '%s' with service '%s'%s...",
				ClientId, pSelf->Server()->ClientName(ClientId), IpAddress.c_str(), ServiceName.c_str(),
				ForceRefresh ? " (force)" : "");
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "Checking IP '%s' with service '%s'%s...",
				IpAddress.c_str(), ServiceName.c_str(), ForceRefresh ? " (force)" : "");
		}
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		pSelf->CheckIpService(IpAddress.c_str(), ServiceName.c_str(), ForceRefresh, true);
	}

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
		"Uncached checks, if any, were queued. Fresh results will appear asynchronously as they complete.");
}
} // namespace

namespace VpnCommands {

void ConVPNEnable(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);

	if(pResult->NumArguments() == 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			pSelf->IsEnabled() ? "VPN detection: enabled" : "VPN detection: disabled");
		return;
	}

	const bool Enable = pResult->GetInteger(0) != 0;
	pSelf->SetEnabled(Enable);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
		Enable ? "VPN detection enabled successfully" : "VPN detection disabled successfully");
	if(Enable)
		pSelf->CheckAllClientsActiveServices();
}

void ConVPNSetDefaultService(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	auto *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);

	if(pResult->NumArguments() == 0)
	{
		char aBuf[256];
		const char *pDefaultService = pSelf->GetDefaultService();
		if(pDefaultService[0] == '\0')
			str_copy(aBuf, "No default service set", sizeof(aBuf));
		else
			str_format(aBuf, sizeof(aBuf), "Default service: %s", pDefaultService);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		return;
	}

	pSelf->SetDefaultService(pResult->GetString(0));
	pfnCallback(pResult, pCallbackUserData);
}

void ConVPNStatus(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);
	const bool FullCheck = pResult->NumArguments() > 0 && pResult->GetInteger(0) != 0;

	char aBuf[512];
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "=== VPN Detection Status ===");
	str_format(aBuf, sizeof(aBuf), "Status: %s | Cache TTL: %d day(s)",
		pSelf->IsEnabled() ? "enabled" : "disabled", pSelf->Config()->m_SvVpnCacheTtlDays);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "Services:");
	const auto &ServiceQueues = pSelf->GetServiceQueues();
	const char *pDefaultService = pSelf->GetDefaultService();
	for(const auto &QueuePair : ServiceQueues)
	{
		IVpnService *pService = pSelf->GetService(QueuePair.first.c_str());
		const bool IsDefault = str_comp(QueuePair.first.c_str(), pDefaultService) == 0;
		const bool IsActive = pSelf->IsServiceActive(QueuePair.first.c_str());
		const bool IsConfigured = pService && pService->IsConfigured();
		str_format(aBuf, sizeof(aBuf), "  - %s%s%s%s | Queue: %d | Rate limit: %dms",
			QueuePair.first.c_str(),
			IsDefault ? " (default)" : "",
			IsActive ? " (active)" : "",
			IsConfigured ? "" : " (not configured)",
			QueuePair.second.GetQueueSize(),
			QueuePair.second.m_RateLimitMs);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
	}

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "Connected Clients:");
	const auto ActiveServices = pSelf->GetActiveServiceNames();
	int TotalChecksNeeded = 0;
	int TotalClients = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pSelf->GameServer()->m_apPlayers[i])
			continue;

		const CVpnClientInfo *pInfo = pSelf->GetClientInfo(i);
		if(!pInfo)
			continue;

		if(pSelf->IsLocalOrBogonIp(pInfo->m_IpAddress.c_str()))
		{
			str_format(aBuf, sizeof(aBuf), "Client %d (%s) | IP: %s | Private/bogon IP (skipped)",
				i, pSelf->Server()->ClientName(i), pInfo->m_IpAddress.c_str());
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			continue;
		}

		TotalClients++;
		str_format(aBuf, sizeof(aBuf), "Client %d (%s) | IP: %s | Results: %d",
			i, pSelf->Server()->ClientName(i), pInfo->m_IpAddress.c_str(), (int)pInfo->m_Results.size());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

		for(const auto &pServiceResult : pInfo->m_Results)
		{
			if(!pServiceResult)
				continue;

			if(pServiceResult->IsValid())
			{
				str_format(aBuf, sizeof(aBuf), "  - %s | Bad IP: %s | Risk: %d%s%s%s%s",
					pServiceResult->GetServiceName(),
					pServiceResult->IsBadIP() ? "true" : "false",
					pServiceResult->GetRiskScore(),
					pServiceResult->GetAsn()[0] ? " | ASN: " : "",
					pServiceResult->GetAsn()[0] ? pServiceResult->GetAsn() : "",
					pServiceResult->GetIsp()[0] ? " | ISP: " : "",
					pServiceResult->GetIsp()[0] ? pServiceResult->GetIsp() : "");
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "  - %s | Error: %s",
					pServiceResult->GetServiceName(),
					pServiceResult->GetErrorMessage()[0] ? pServiceResult->GetErrorMessage() : "Unknown error");
			}
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		}

		if(FullCheck)
		{
			for(const auto &ServiceName : ActiveServices)
			{
				if(!pInfo->GetResultByService(ServiceName.c_str()))
					TotalChecksNeeded++;
			}
		}
	}

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "=== End VPN Status ===");

	if(FullCheck)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "");
		if(TotalChecksNeeded > 0)
		{
			str_format(aBuf, sizeof(aBuf), "Queueing %d missing active-service checks for %d clients...",
				TotalChecksNeeded, TotalClients);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(pSelf->GameServer()->m_apPlayers[i])
					pSelf->CheckClient(i, true);
			}
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
				"All clients already have active-service VPN check results.");
		}
	}
}

void ConVPNServiceList(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);
	char aBuf[256];

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "=== VPN Services ===");
	str_format(aBuf, sizeof(aBuf), "Enabled config: %s", pSelf->Config()->m_SvVpnServicesEnabled);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
	const std::string ActiveList = JoinNames(pSelf->GetActiveServiceNames());
	str_format(aBuf, sizeof(aBuf), "Active configured services: %s", ActiveList.c_str());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

	const auto &ServiceQueues = pSelf->GetServiceQueues();
	const char *pDefaultService = pSelf->GetDefaultService();
	for(const auto &ServicePair : ServiceQueues)
	{
		IVpnService *pService = pSelf->GetService(ServicePair.first.c_str());
		const bool IsDefault = str_comp(ServicePair.first.c_str(), pDefaultService) == 0;
		const bool IsActive = pSelf->IsServiceActive(ServicePair.first.c_str());
		const bool IsConfigured = pService && pService->IsConfigured();
		str_format(aBuf, sizeof(aBuf), "  - %s%s%s%s | Queue: %d | Rate limit: %dms",
			ServicePair.first.c_str(),
			IsDefault ? " (default)" : "",
			IsActive ? " (active)" : "",
			IsConfigured ? "" : " (not configured)",
			ServicePair.second.GetQueueSize(),
			ServicePair.second.m_RateLimitMs);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
	}
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "=== End Service List ===");
}

void ConVPNCheck(IConsole::IResult *pResult, void *pUserData)
{
	PerformVPNCheck(pResult, pUserData, false);
}

void ConVPNCheckForce(IConsole::IResult *pResult, void *pUserData)
{
	PerformVPNCheck(pResult, pUserData, true);
}

void ConVPNWhitelistAdd(IConsole::IResult *pResult, void *pUserData)
{
	auto *pVpn = static_cast<CVpnDetectionComponent *>(pUserData);
	const char *pIp = pResult->GetString(0);
	pVpn->WhitelistIpAdd(pIp);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "IP '%s' added to VPN whitelist", pIp);
	pVpn->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
}

void ConVPNWhitelistRemove(IConsole::IResult *pResult, void *pUserData)
{
	auto *pVpn = static_cast<CVpnDetectionComponent *>(pUserData);
	const char *pIp = pResult->GetString(0);
	if(pVpn->GetWhitelistedIps().find(pIp) == pVpn->GetWhitelistedIps().end())
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "IP '%s' is not in the VPN whitelist", pIp);
		pVpn->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		return;
	}
	pVpn->WhitelistIpRemove(pIp);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "IP '%s' removed from VPN whitelist", pIp);
	pVpn->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
}

void ConVPNWhitelistList(IConsole::IResult *pResult, void *pUserData)
{
	auto *pVpn = static_cast<CVpnDetectionComponent *>(pUserData);
	const auto &Ips = pVpn->GetWhitelistedIps();
	if(Ips.empty())
	{
		pVpn->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "VPN whitelist is empty");
		return;
	}
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "VPN whitelisted IPs (%d):", (int)Ips.size());
	pVpn->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
	for(const auto &Ip : Ips)
	{
		str_format(aBuf, sizeof(aBuf), "  %s", Ip.c_str());
		pVpn->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
	}
}

} // namespace VpnCommands
