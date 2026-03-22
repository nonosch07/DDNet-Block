#include "vpn_commands.h"
#include "services/getipintel_service.h"
#include "services/vpn_service_interface.h"
#include "vpn_detection.h"
#include "vpn_service_request.h"

#include <base/system.h>
#include <engine/shared/config.h>
#include <game/server/player.h>

#include <memory>
#include <thread>

namespace {
bool IsPrivateIP(const char *pIpAddress)
{
	return str_startswith(pIpAddress, "192.168.") ||
	       str_startswith(pIpAddress, "10.") ||
	       str_startswith(pIpAddress, "172.16.") ||
	       str_startswith(pIpAddress, "127.");
}
} // namespace

namespace VpnCommands {

void ConVPNEnable(IConsole::IResult *pResult, void *pUserData)
{
	CVpnDetectionComponent *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);

	if(pResult->NumArguments() == 0)
	{
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			pSelf->IsEnabled() ? "VPN detection: enabled" : "VPN detection: disabled");
		return;
	}

	bool Enable = pResult->GetInteger(0) != 0;

	if(Enable && !pSelf->IsEnabled())
	{
		pSelf->SetEnabled(true);
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			"VPN detection enabled successfully");
		pSelf->CheckAllClientsDefaultService();
	}
	else if(!Enable && pSelf->IsEnabled())
	{
		pSelf->SetEnabled(false);
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			"VPN detection disabled successfully");
	}
	else
	{
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			Enable ? "VPN detection is already enabled" : "VPN detection is already disabled");
	}
}

void ConVPNSetDefaultService(IConsole::IResult *pResult, void *pUserData)
{
	CVpnDetectionComponent *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);

	if(pResult->NumArguments() == 0)
	{
		char aBuf[256];
		const char *pDefaultService = pSelf->GetDefaultService();
		if(pDefaultService[0] == '\0')
		{
			str_copy(aBuf, "No default service set", sizeof(aBuf));
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "Default service: %s", pDefaultService);
		}
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		return;
	}

	const char *pServiceName = pResult->GetString(0);
	pSelf->SetDefaultService(pServiceName);
}

void ConVPNStatus(IConsole::IResult *pResult, void *pUserData)
{
	CVpnDetectionComponent *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);
	bool FullCheck = pResult->NumArguments() > 0 && pResult->GetInteger(0) != 0;

	char aBuf[256];

	pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
		"=== VPN Detection Status ===");

	// Show configuration
	str_format(aBuf, sizeof(aBuf), "Status: %s", pSelf->IsEnabled() ? "enabled" : "disabled");
	pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

	// Show service queues
	pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "");
	pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "Services:");

	auto &ServiceQueues = pSelf->GetServiceQueues();
	const char *pDefaultService = pSelf->GetDefaultService();
	for(const auto &QueuePair : ServiceQueues)
	{
		bool IsDefault = str_comp(QueuePair.first.c_str(), pDefaultService) == 0;
		str_format(aBuf, sizeof(aBuf), "  - %s%s | Queue: %d | Rate limit: %dms",
			QueuePair.first.c_str(),
			IsDefault ? " (Default)" : "",
			QueuePair.second.GetQueueSize(),
			QueuePair.second.m_RateLimitMs);
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
	}

	// Show connected clients
	pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "");
	pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "Connected Clients:");

	// Count clients that need checking (for full_check mode)
	int TotalChecksNeeded = 0;
	int TotalClients = 0;

	// First pass: display current status and count checks needed
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pSelf->GetGameServer()->m_apPlayers[i])
			continue;

		const CVpnClientInfo *pInfo = pSelf->GetClientInfo(i);
		if(!pInfo)
			continue;

		// Skip private IPs
		if(IsPrivateIP(pInfo->m_IpAddress.c_str()))
		{
			str_format(aBuf, sizeof(aBuf), "Client %d (%s) | IP: %s | Private IP (skipped)",
				i,
				pSelf->GetServer()->ClientName(i),
				pInfo->m_IpAddress.c_str());
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			continue;
		}

		TotalClients++;

		// Display client info
		str_format(aBuf, sizeof(aBuf), "Client %d (%s) | IP: %s | Results: %d",
			i,
			pSelf->GetServer()->ClientName(i),
			pInfo->m_IpAddress.c_str(),
			(int)pInfo->m_Results.size());
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

		// Count missing services for this client
		int MissingServices = 0;
		bool HasIpquery = false;
		bool HasGetipintel = false;

		for(const auto &pServiceResult : pInfo->m_Results)
		{
			if(!pServiceResult)
				continue;

			if(str_comp(pServiceResult->GetServiceName(), "ipquery") == 0)
				HasIpquery = true;
			if(str_comp(pServiceResult->GetServiceName(), "getipintel") == 0)
				HasGetipintel = true;

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
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		}

		if(FullCheck)
		{
			if(!HasIpquery)
				MissingServices++;
			if(!HasGetipintel)
				MissingServices++;
			TotalChecksNeeded += MissingServices;
		}
	}

	pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
		"=== End VPN Status ===");

	// If full check requested, queue the checks and provide ETA
	if(FullCheck && TotalChecksNeeded > 0)
	{
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "");

		// Calculate ETA based on rate limits
		int IpqueryRateLimit = 100; // Default
		int GetipintelRateLimit = 4000; // Default

		for(const auto &QueuePair : ServiceQueues)
		{
			if(str_comp(QueuePair.first.c_str(), "ipquery") == 0)
				IpqueryRateLimit = QueuePair.second.m_RateLimitMs;
			if(str_comp(QueuePair.first.c_str(), "getipintel") == 0)
				GetipintelRateLimit = QueuePair.second.m_RateLimitMs;
		}

		// Rough ETA calculation (worst case: all checks are sequential)
		int EstimatedTimeMs = (TotalChecksNeeded / 2) * (IpqueryRateLimit + GetipintelRateLimit);
		int EstimatedTimeSec = (EstimatedTimeMs + 999) / 1000;

		str_format(aBuf, sizeof(aBuf), "Queueing %d checks for %d clients (ETA: ~%d seconds)...",
			TotalChecksNeeded, TotalClients, EstimatedTimeSec);
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

		// Queue checks for all non-private IPs
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!pSelf->GetGameServer()->m_apPlayers[i])
				continue;

			const CVpnClientInfo *pInfo = pSelf->GetClientInfo(i);
			if(!pInfo || IsPrivateIP(pInfo->m_IpAddress.c_str()))
				continue;

			pSelf->CheckClient(i, true);
		}

		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			"Checks queued. Results will appear asynchronously as they complete.");
	}
	else if(FullCheck && TotalChecksNeeded == 0)
	{
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "");
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			"All clients already have complete VPN check results.");
	}
}

void ConVPNServiceList(IConsole::IResult *pResult, void *pUserData)
{
	CVpnDetectionComponent *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);

	char aBuf[256];

	pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
		"=== VPN Services ===");

	// List all services
	auto &ServiceQueues = pSelf->GetServiceQueues();
	if(ServiceQueues.empty())
	{
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			"No services registered");
	}
	else
	{
		const char *pDefaultService = pSelf->GetDefaultService();
		for(const auto &ServicePair : ServiceQueues)
		{
			const char *pServiceName = ServicePair.first.c_str();
			bool IsDefault = str_comp(pServiceName, pDefaultService) == 0;

			str_format(aBuf, sizeof(aBuf), "  - %s%s | Queue: %d | Rate limit: %dms",
				pServiceName,
				IsDefault ? " (Default)" : "",
				ServicePair.second.GetQueueSize(),
				ServicePair.second.m_RateLimitMs);

			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		}
	}

	pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
		"=== End Service List ===");
}

static bool IsNumeric(const char *pStr)
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

static bool IsValidIPAddress(const char *pStr)
{
	if(!pStr || !pStr[0])
		return false;

	// Simple check: contains dots and digits
	bool HasDot = false;
	for(int i = 0; pStr[i]; i++)
	{
		if(pStr[i] == '.')
			HasDot = true;
		else if((pStr[i] < '0' || pStr[i] > '9') && pStr[i] != ':')
			return false;
	}
	return HasDot;
}

static void PerformVPNCheck(IConsole::IResult *pResult, void *pUserData, bool ForceRefresh)
{
	CVpnDetectionComponent *pSelf = static_cast<CVpnDetectionComponent *>(pUserData);

	if(pResult->NumArguments() < 1)
	{
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			ForceRefresh ? "Usage: vpn_check_force <client_id|ip_address> [service_name]" : "Usage: vpn_check <client_id|ip_address> [service_name]");
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			"Examples:");
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			ForceRefresh ? "  vpn_check_force 0              - Force check client 0 with default service" : "  vpn_check 0              - Check client 0 with default service");
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			ForceRefresh ? "  vpn_check_force 0 ipquery      - Force check client 0 with ipquery service" : "  vpn_check 0 ipquery      - Check client 0 with ipquery service");
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			ForceRefresh ? "  vpn_check_force 8.8.8.8        - Force check IP with default service" : "  vpn_check 8.8.8.8        - Check IP with default service");
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection",
			ForceRefresh ? "  vpn_check_force 8.8.8.8 all    - Force check IP with all services" : "  vpn_check 8.8.8.8 all    - Check IP with all services");
		return;
	}

	const char *pInput = pResult->GetString(0);
	const char *pServiceName = pResult->NumArguments() >= 2 ? pResult->GetString(1) : nullptr;

	// Auto-detect: is it a client ID or IP address?
	bool IsClientId = IsNumeric(pInput);
	bool IsIP = IsValidIPAddress(pInput);

	// If it's numeric and could be either, prefer client ID if the client exists
	if(IsClientId && !IsIP)
	{
		int ClientId = str_toint(pInput);

		// Validate client ID
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Invalid client ID: %d (must be 0-%d)", ClientId, MAX_CLIENTS - 1);
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		// Check if client is connected
		if(!pSelf->GetGameServer()->m_apPlayers[ClientId])
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Client %d is not connected", ClientId);
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		// Get client info
		const CVpnClientInfo *pInfo = pSelf->GetClientInfo(ClientId);
		if(!pInfo || pInfo->m_IpAddress.empty())
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Failed to get IP address for client %d", ClientId);
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		const char *pIpAddress = pInfo->m_IpAddress.c_str();

		if(IsPrivateIP(pIpAddress))
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf),
				"Client %d (%s) | IP: %s | Service: local | Bad IP: false | Risk: 0 | ASN: Local Network | ISP: Private IP Address",
				ClientId, pSelf->GetServer()->ClientName(ClientId), pIpAddress);
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		// If no service specified, use default
		if(!pServiceName || !pServiceName[0])
			pServiceName = pSelf->GetDefaultService();

		// Check if testing all services
		if(str_comp(pServiceName, "all") == 0)
		{
			// Test with ipquery
			{
				const char *pSvcName = "ipquery";
				IVpnService *pService = pSelf->GetService(pSvcName);
				if(pService)
				{
					auto pCachedResult = !ForceRefresh ? pSelf->GetCache()->Get(pIpAddress, pSvcName) : nullptr;
					if(pCachedResult)
					{
						char aBuf[512];
						if(pCachedResult->IsValid())
						{
							str_format(aBuf, sizeof(aBuf),
								"Client %d (%s) | IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s | (cached)",
								ClientId, pSelf->GetServer()->ClientName(ClientId),
								pIpAddress,
								pSvcName,
								pCachedResult->IsBadIP() ? "true" : "false",
								pCachedResult->GetRiskScore(),
								pCachedResult->GetAsn()[0] ? " | ASN: " : "",
								pCachedResult->GetAsn()[0] ? pCachedResult->GetAsn() : "",
								pCachedResult->GetIsp()[0] ? " | ISP: " : "",
								pCachedResult->GetIsp()[0] ? pCachedResult->GetIsp() : "");
						}
						else
						{
							str_format(aBuf, sizeof(aBuf),
								"Client %d (%s) | IP: %s | Service: %s | Error: %s | (cached)",
								ClientId, pSelf->GetServer()->ClientName(ClientId),
								pIpAddress,
								pSvcName,
								pCachedResult->GetErrorMessage()[0] ? pCachedResult->GetErrorMessage() : "Unknown error");
						}
						pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
					}
					else
					{
						auto pRequest = std::make_shared<CVpnServiceRequest>(
							pSvcName,
							pIpAddress,
							-1,
							pSelf,
							pService);

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "Testing client %d (%s) IP '%s' with service '%s'...",
							ClientId, pSelf->GetServer()->ClientName(ClientId), pIpAddress, pSvcName);
						pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

						std::thread([pSelf, pRequest, ClientId]() {
							auto pApiResult = pRequest->Execute();

							if(pApiResult && pApiResult->IsValid())
							{
								pSelf->QueueResult(-1, pApiResult);

								char aResultBuf[512];
								str_format(aResultBuf, sizeof(aResultBuf),
									"Client %d | IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s",
									ClientId,
									pApiResult->GetIpAddress(),
									pApiResult->GetServiceName(),
									pApiResult->IsBadIP() ? "true" : "false",
									pApiResult->GetRiskScore(),
									pApiResult->GetAsn()[0] ? " | ASN: " : "",
									pApiResult->GetAsn()[0] ? pApiResult->GetAsn() : "",
									pApiResult->GetIsp()[0] ? " | ISP: " : "",
									pApiResult->GetIsp()[0] ? pApiResult->GetIsp() : "");

								pSelf->QueueConsoleMessage(aResultBuf);
							}
							else
							{
								char aErrorBuf[512];
								const char *pErrorMsg = "Unknown error";
								if(pApiResult && pApiResult->GetErrorMessage()[0])
									pErrorMsg = pApiResult->GetErrorMessage();

								str_format(aErrorBuf, sizeof(aErrorBuf),
									"Client %d | IP: %s | Service: %s | Error: %s",
									ClientId,
									pApiResult ? pApiResult->GetIpAddress() : "unknown",
									pApiResult ? pApiResult->GetServiceName() : "unknown",
									pErrorMsg);

								pSelf->QueueConsoleMessage(aErrorBuf);
							}
						}).detach();
					}
				}
			}

			// Test with getipintel
			{
				const char *pSvcName = "getipintel";
				IVpnService *pService = pSelf->GetService(pSvcName);
				if(pService)
				{
					auto pCachedResult = !ForceRefresh ? pSelf->GetCache()->Get(pIpAddress, pSvcName) : nullptr;
					if(pCachedResult)
					{
						char aBuf[512];
						if(pCachedResult->IsValid())
						{
							str_format(aBuf, sizeof(aBuf),
								"Client %d (%s) | IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s | (cached)",
								ClientId, pSelf->GetServer()->ClientName(ClientId),
								pIpAddress,
								pSvcName,
								pCachedResult->IsBadIP() ? "true" : "false",
								pCachedResult->GetRiskScore(),
								pCachedResult->GetAsn()[0] ? " | ASN: " : "",
								pCachedResult->GetAsn()[0] ? pCachedResult->GetAsn() : "",
								pCachedResult->GetIsp()[0] ? " | ISP: " : "",
								pCachedResult->GetIsp()[0] ? pCachedResult->GetIsp() : "");
						}
						else
						{
							str_format(aBuf, sizeof(aBuf),
								"Client %d (%s) | IP: %s | Service: %s | Error: %s | (cached)",
								ClientId, pSelf->GetServer()->ClientName(ClientId),
								pIpAddress,
								pSvcName,
								pCachedResult->GetErrorMessage()[0] ? pCachedResult->GetErrorMessage() : "Unknown error");
						}
						pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
					}
					else
					{
						auto pRequest = std::make_shared<CVpnServiceRequest>(
							pSvcName,
							pIpAddress,
							-1,
							pSelf,
							pService);

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "Testing client %d (%s) IP '%s' with service '%s'...",
							ClientId, pSelf->GetServer()->ClientName(ClientId), pIpAddress, pSvcName);
						pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

						std::thread([pSelf, pRequest, ClientId]() {
							auto pApiResult = pRequest->Execute();

							if(pApiResult && pApiResult->IsValid())
							{
								pSelf->QueueResult(-1, pApiResult);

								char aResultBuf[512];
								str_format(aResultBuf, sizeof(aResultBuf),
									"Client %d | IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s",
									ClientId,
									pApiResult->GetIpAddress(),
									pApiResult->GetServiceName(),
									pApiResult->IsBadIP() ? "true" : "false",
									pApiResult->GetRiskScore(),
									pApiResult->GetAsn()[0] ? " | ASN: " : "",
									pApiResult->GetAsn()[0] ? pApiResult->GetAsn() : "",
									pApiResult->GetIsp()[0] ? " | ISP: " : "",
									pApiResult->GetIsp()[0] ? pApiResult->GetIsp() : "");

								pSelf->QueueConsoleMessage(aResultBuf);
							}
							else
							{
								char aErrorBuf[512];
								const char *pErrorMsg = "Unknown error";
								if(pApiResult && pApiResult->GetErrorMessage()[0])
									pErrorMsg = pApiResult->GetErrorMessage();

								str_format(aErrorBuf, sizeof(aErrorBuf),
									"Client %d | IP: %s | Service: %s | Error: %s",
									ClientId,
									pApiResult ? pApiResult->GetIpAddress() : "unknown",
									pApiResult ? pApiResult->GetServiceName() : "unknown",
									pErrorMsg);

								pSelf->QueueConsoleMessage(aErrorBuf);
							}
						}).detach();
					}
				}
			}
			return;
		}

		auto pCachedResult = !ForceRefresh ? pSelf->GetCache()->Get(pIpAddress, pServiceName) : nullptr;
		if(pCachedResult)
		{
			char aBuf[512];
			if(pCachedResult->IsValid())
			{
				str_format(aBuf, sizeof(aBuf),
					"Client %d (%s) | IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s | (cached)",
					ClientId, pSelf->GetServer()->ClientName(ClientId),
					pIpAddress,
					pServiceName,
					pCachedResult->IsBadIP() ? "true" : "false",
					pCachedResult->GetRiskScore(),
					pCachedResult->GetAsn()[0] ? " | ASN: " : "",
					pCachedResult->GetAsn()[0] ? pCachedResult->GetAsn() : "",
					pCachedResult->GetIsp()[0] ? " | ISP: " : "",
					pCachedResult->GetIsp()[0] ? pCachedResult->GetIsp() : "");
			}
			else
			{
				str_format(aBuf, sizeof(aBuf),
					"Client %d (%s) | IP: %s | Service: %s | Error: %s | (cached)",
					ClientId, pSelf->GetServer()->ClientName(ClientId),
					pIpAddress,
					pServiceName,
					pCachedResult->GetErrorMessage()[0] ? pCachedResult->GetErrorMessage() : "Unknown error");
			}
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		IVpnService *pService = pSelf->GetService(pServiceName);
		if(!pService)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Service '%s' not found. Use vpn_service_list to see available services.", pServiceName);
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		auto pRequest = std::make_shared<CVpnServiceRequest>(
			pServiceName,
			pIpAddress,
			-1,
			pSelf,
			pService);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Testing client %d (%s) IP '%s' with service '%s'...",
			ClientId, pSelf->GetServer()->ClientName(ClientId), pIpAddress, pServiceName);
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

		std::string IpCopy(pIpAddress);
		std::string ServiceCopy(pServiceName);
		std::thread([pSelf, pRequest, ClientId, IpCopy, ServiceCopy]() {
			auto pApiResult = pRequest->Execute();

			if(pApiResult && pApiResult->IsValid())
			{
				pSelf->QueueResult(-1, pApiResult);

				char aResultBuf[512];
				str_format(aResultBuf, sizeof(aResultBuf),
					"Client %d | IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s",
					ClientId,
					IpCopy.c_str(),
					ServiceCopy.c_str(),
					pApiResult->IsBadIP() ? "true" : "false",
					pApiResult->GetRiskScore(),
					pApiResult->GetAsn()[0] ? " | ASN: " : "",
					pApiResult->GetAsn()[0] ? pApiResult->GetAsn() : "",
					pApiResult->GetIsp()[0] ? " | ISP: " : "",
					pApiResult->GetIsp()[0] ? pApiResult->GetIsp() : "");

				pSelf->QueueConsoleMessage(aResultBuf);
			}
			else
			{
				char aErrorBuf[512];
				const char *pErrorMsg = "Unknown error";
				if(pApiResult && pApiResult->GetErrorMessage()[0])
					pErrorMsg = pApiResult->GetErrorMessage();

				str_format(aErrorBuf, sizeof(aErrorBuf),
					"Client %d | IP: %s | Service: %s | Error: %s",
					ClientId,
					IpCopy.c_str(),
					ServiceCopy.c_str(),
					pErrorMsg);

				pSelf->QueueConsoleMessage(aErrorBuf);
			}
		}).detach();
	}
	else if(IsIP)
	{
		// It's an IP address
		const char *pIpAddress = pInput;

		if(IsPrivateIP(pIpAddress))
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf),
				"IP: %s | Service: local | Bad IP: false | Risk: 0 | ASN: Local Network | ISP: Private IP Address",
				pIpAddress);
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		// If no service specified, use default
		if(!pServiceName || !pServiceName[0])
			pServiceName = pSelf->GetDefaultService();

		// Check if testing all services
		if(str_comp(pServiceName, "all") == 0)
		{
			// Test with ipquery
			{
				const char *pSvcName = "ipquery";
				IVpnService *pService = pSelf->GetService(pSvcName);
				if(pService)
				{
					auto pCachedResult = !ForceRefresh ? pSelf->GetCache()->Get(pIpAddress, pSvcName) : nullptr;
					if(pCachedResult)
					{
						char aBuf[512];
						if(pCachedResult->IsValid())
						{
							str_format(aBuf, sizeof(aBuf),
								"IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s | (cached)",
								pIpAddress,
								pSvcName,
								pCachedResult->IsBadIP() ? "true" : "false",
								pCachedResult->GetRiskScore(),
								pCachedResult->GetAsn()[0] ? " | ASN: " : "",
								pCachedResult->GetAsn()[0] ? pCachedResult->GetAsn() : "",
								pCachedResult->GetIsp()[0] ? " | ISP: " : "",
								pCachedResult->GetIsp()[0] ? pCachedResult->GetIsp() : "");
						}
						else
						{
							str_format(aBuf, sizeof(aBuf),
								"IP: %s | Service: %s | Error: %s | (cached)",
								pIpAddress,
								pSvcName,
								pCachedResult->GetErrorMessage()[0] ? pCachedResult->GetErrorMessage() : "Unknown error");
						}
						pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
					}
					else
					{
						auto pRequest = std::make_shared<CVpnServiceRequest>(
							pSvcName,
							pIpAddress,
							-1,
							pSelf,
							pService);

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "Testing IP '%s' with service '%s'...", pIpAddress, pSvcName);
						pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

						std::thread([pSelf, pRequest]() {
							auto pApiResult = pRequest->Execute();

							if(pApiResult && pApiResult->IsValid())
							{
								pSelf->QueueResult(-1, pApiResult);

								char aResultBuf[512];
								str_format(aResultBuf, sizeof(aResultBuf),
									"IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s",
									pApiResult->GetIpAddress(),
									pApiResult->GetServiceName(),
									pApiResult->IsBadIP() ? "true" : "false",
									pApiResult->GetRiskScore(),
									pApiResult->GetAsn()[0] ? " | ASN: " : "",
									pApiResult->GetAsn()[0] ? pApiResult->GetAsn() : "",
									pApiResult->GetIsp()[0] ? " | ISP: " : "",
									pApiResult->GetIsp()[0] ? pApiResult->GetIsp() : "");

								pSelf->QueueConsoleMessage(aResultBuf);
							}
							else
							{
								char aErrorBuf[512];
								const char *pErrorMsg = "Unknown error";
								if(pApiResult && pApiResult->GetErrorMessage()[0])
									pErrorMsg = pApiResult->GetErrorMessage();

								str_format(aErrorBuf, sizeof(aErrorBuf),
									"IP: %s | Service: %s | Error: %s",
									pApiResult ? pApiResult->GetIpAddress() : "unknown",
									pApiResult ? pApiResult->GetServiceName() : "unknown",
									pErrorMsg);

								pSelf->QueueConsoleMessage(aErrorBuf);
							}
						}).detach();
					}
				}
			}

			// Test with getipintel
			{
				const char *pSvcName = "getipintel";
				IVpnService *pService = pSelf->GetService(pSvcName);
				if(pService)
				{
					auto pCachedResult = !ForceRefresh ? pSelf->GetCache()->Get(pIpAddress, pSvcName) : nullptr;
					if(pCachedResult)
					{
						char aBuf[512];
						if(pCachedResult->IsValid())
						{
							str_format(aBuf, sizeof(aBuf),
								"IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s | (cached)",
								pIpAddress,
								pSvcName,
								pCachedResult->IsBadIP() ? "true" : "false",
								pCachedResult->GetRiskScore(),
								pCachedResult->GetAsn()[0] ? " | ASN: " : "",
								pCachedResult->GetAsn()[0] ? pCachedResult->GetAsn() : "",
								pCachedResult->GetIsp()[0] ? " | ISP: " : "",
								pCachedResult->GetIsp()[0] ? pCachedResult->GetIsp() : "");
						}
						else
						{
							str_format(aBuf, sizeof(aBuf),
								"IP: %s | Service: %s | Error: %s | (cached)",
								pIpAddress,
								pSvcName,
								pCachedResult->GetErrorMessage()[0] ? pCachedResult->GetErrorMessage() : "Unknown error");
						}
						pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
					}
					else
					{
						auto pRequest = std::make_shared<CVpnServiceRequest>(
							pSvcName,
							pIpAddress,
							-1,
							pSelf,
							pService);

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "Testing IP '%s' with service '%s'...", pIpAddress, pSvcName);
						pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

						std::thread([pSelf, pRequest]() {
							auto pApiResult = pRequest->Execute();

							if(pApiResult && pApiResult->IsValid())
							{
								pSelf->QueueResult(-1, pApiResult);

								char aResultBuf[512];
								str_format(aResultBuf, sizeof(aResultBuf),
									"IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s",
									pApiResult->GetIpAddress(),
									pApiResult->GetServiceName(),
									pApiResult->IsBadIP() ? "true" : "false",
									pApiResult->GetRiskScore(),
									pApiResult->GetAsn()[0] ? " | ASN: " : "",
									pApiResult->GetAsn()[0] ? pApiResult->GetAsn() : "",
									pApiResult->GetIsp()[0] ? " | ISP: " : "",
									pApiResult->GetIsp()[0] ? pApiResult->GetIsp() : "");

								pSelf->QueueConsoleMessage(aResultBuf);
							}
							else
							{
								char aErrorBuf[512];
								const char *pErrorMsg = "Unknown error";
								if(pApiResult && pApiResult->GetErrorMessage()[0])
									pErrorMsg = pApiResult->GetErrorMessage();

								str_format(aErrorBuf, sizeof(aErrorBuf),
									"IP: %s | Service: %s | Error: %s",
									pApiResult ? pApiResult->GetIpAddress() : "unknown",
									pApiResult ? pApiResult->GetServiceName() : "unknown",
									pErrorMsg);

								pSelf->QueueConsoleMessage(aErrorBuf);
							}
						}).detach();
					}
				}
			}
			return;
		}

		auto pCachedResult = !ForceRefresh ? pSelf->GetCache()->Get(pIpAddress, pServiceName) : nullptr;
		if(pCachedResult)
		{
			char aBuf[512];
			if(pCachedResult->IsValid())
			{
				str_format(aBuf, sizeof(aBuf),
					"IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s | (cached)",
					pIpAddress,
					pServiceName,
					pCachedResult->IsBadIP() ? "true" : "false",
					pCachedResult->GetRiskScore(),
					pCachedResult->GetAsn()[0] ? " | ASN: " : "",
					pCachedResult->GetAsn()[0] ? pCachedResult->GetAsn() : "",
					pCachedResult->GetIsp()[0] ? " | ISP: " : "",
					pCachedResult->GetIsp()[0] ? pCachedResult->GetIsp() : "");
			}
			else
			{
				str_format(aBuf, sizeof(aBuf),
					"IP: %s | Service: %s | Error: %s | (cached)",
					pIpAddress,
					pServiceName,
					pCachedResult->GetErrorMessage()[0] ? pCachedResult->GetErrorMessage() : "Unknown error");
			}
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		IVpnService *pService = pSelf->GetService(pServiceName);
		if(!pService)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Service '%s' not found. Use vpn_service_list to see available services.", pServiceName);
			pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
			return;
		}

		auto pRequest = std::make_shared<CVpnServiceRequest>(
			pServiceName,
			pIpAddress,
			-1,
			pSelf,
			pService);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Testing IP '%s' with service '%s'...", pIpAddress, pServiceName);
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);

		std::string IpCopy(pIpAddress);
		std::string ServiceCopy(pServiceName);
		std::thread([pSelf, pRequest, IpCopy, ServiceCopy]() {
			auto pApiResult = pRequest->Execute();

			if(pApiResult && pApiResult->IsValid())
			{
				pSelf->QueueResult(-1, pApiResult);

				char aResultBuf[512];
				str_format(aResultBuf, sizeof(aResultBuf),
					"IP: %s | Service: %s | Bad IP: %s | Risk: %d%s%s%s%s",
					IpCopy.c_str(),
					ServiceCopy.c_str(),
					pApiResult->IsBadIP() ? "true" : "false",
					pApiResult->GetRiskScore(),
					pApiResult->GetAsn()[0] ? " | ASN: " : "",
					pApiResult->GetAsn()[0] ? pApiResult->GetAsn() : "",
					pApiResult->GetIsp()[0] ? " | ISP: " : "",
					pApiResult->GetIsp()[0] ? pApiResult->GetIsp() : "");

				pSelf->QueueConsoleMessage(aResultBuf);
			}
			else
			{
				char aErrorBuf[512];
				const char *pErrorMsg = "Unknown error";
				if(pApiResult && pApiResult->GetErrorMessage()[0])
					pErrorMsg = pApiResult->GetErrorMessage();

				str_format(aErrorBuf, sizeof(aErrorBuf),
					"IP: %s | Service: %s | Error: %s",
					IpCopy.c_str(),
					ServiceCopy.c_str(),
					pErrorMsg);

				pSelf->QueueConsoleMessage(aErrorBuf);
			}
		}).detach();
	}
	else
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Invalid input: '%s' (must be a client ID or IP address)", pInput);
		pSelf->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
	}
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
	pVpn->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
}

void ConVPNWhitelistRemove(IConsole::IResult *pResult, void *pUserData)
{
	auto *pVpn = static_cast<CVpnDetectionComponent *>(pUserData);
	const char *pIp = pResult->GetString(0);
	if(pVpn->GetWhitelistedIps().find(pIp) == pVpn->GetWhitelistedIps().end())
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "IP '%s' is not in the VPN whitelist", pIp);
		pVpn->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
		return;
	}
	pVpn->WhitelistIpRemove(pIp);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "IP '%s' removed from VPN whitelist", pIp);
	pVpn->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
}

void ConVPNWhitelistList(IConsole::IResult *pResult, void *pUserData)
{
	auto *pVpn = static_cast<CVpnDetectionComponent *>(pUserData);
	const auto &Ips = pVpn->GetWhitelistedIps();
	if(Ips.empty())
	{
		pVpn->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", "VPN whitelist is empty");
		return;
	}
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "VPN whitelisted IPs (%d):", (int)Ips.size());
	pVpn->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
	for(const auto &Ip : Ips)
	{
		str_format(aBuf, sizeof(aBuf), "  %s", Ip.c_str());
		pVpn->GetConsole()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vpndetection", aBuf);
	}
}

} // namespace VpnCommands
