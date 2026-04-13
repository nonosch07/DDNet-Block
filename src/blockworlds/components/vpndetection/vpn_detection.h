#ifndef BLOCKWORLDS_COMPONENTS_VPN_DETECTION_H
#define BLOCKWORLDS_COMPONENTS_VPN_DETECTION_H

#include "services/vpn_service_interface.h"
#include "vpn_cache.h"
#include "vpn_client_info.h"
#include "vpn_service_queue.h"
#include "vpn_service_request.h"
#include "vpn_service_result.h"

#include <blockworlds/components/core/component.h>
#include <engine/console.h>

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

template<typename T>
struct SConVarCallbackData;

constexpr int VPN_BAN_TIME_STRING_MAX = 64;

constexpr int MAX_VPN_CLIENTS = 64;

/**
 * VPN Detection Component
 * 
 * Detects VPN, proxy, and datacenter connections using external API services.
 * Features:
 * - Multi-service support with rate limiting
 * - Persistent caching of results
 * - Async request processing
 * - Per-client tracking
 */
class CVpnDetectionComponent final : public CComponent
{
public:
	DECLARE_COMPONENT(CVpnDetectionComponent, "vpndetection")
	bool IsDebug() const override;

	void OnConsoleInit() override;
	void OnConsoleTerminate() override;
	void OnShutdown() override;
	void OnTick() override;
	void OnPlayerEnter(int ClientId) override;
	void OnPlayerDrop(int ClientId) override;

	/**
	 * Initiates VPN check for a client
	 * @param ClientId The client to check
	 * @param FullCheck If true, queries all services; if false, uses main service only
	 */
	void CheckClient(int ClientId, bool FullCheck = false);

	/**
	 * Checks a client using a specific service
	 * @param ClientId The client to check
	 * @param pServiceName Name of the service to use
	 */
	void CheckClientService(int ClientId, const char *pServiceName);

	CVpnClientInfo *GetClientInfo(int ClientId);
	const CVpnClientInfo *GetClientInfo(int ClientId) const;

	/**
	 * Registers a VPN detection service
	 * @param pServiceName Unique service identifier
	 * @param pService Service implementation (ownership transferred)
	 * @param RateLimitMs Minimum milliseconds between requests
	 */
	void RegisterService(const char *pServiceName, IVpnService *pService, int RateLimitMs = 100);

	SVpnServiceQueue *GetServiceQueue(const char *pServiceName);
	IVpnService *GetService(const char *pServiceName);

	/**
	 * Sets the default service used for automatic checks
	 */
	void SetDefaultService(const char *pServiceName);
	const char *GetDefaultService() const { return m_DefaultService.c_str(); }

	/**
	 * Processes and caches a VPN check result
	 * @param ClientId Client ID or -1 for manual tests
	 * @param pResult The result to process
	 */
	void ProcessResult(int ClientId, std::shared_ptr<IVpnServiceResult> pResult);

	bool IsEnabled() const { return m_Enabled; }
	void SetEnabled(bool Enabled) { m_Enabled = Enabled; }
	bool IsBanEnabled() const { return m_BanEnabled; }
	void SetBanEnabled(bool Enabled) { m_BanEnabled = Enabled; }
	int GetBanTimeMinutes() const { return m_BanTimeMinutes; }
	void SetBanTimeMinutes(int Minutes) { m_BanTimeMinutes = Minutes; }
	void SetServiceRateLimit(const char *pServiceName, int RateLimitMs);
	const std::unordered_map<std::string, SVpnServiceQueue> &GetServiceQueues() const { return m_ServiceQueues; }
	void CheckAllClientsDefaultService();
	CVpnCache *GetCache() { return &m_Cache; }

	/**
	 * Queues a message for thread-safe asynchronous RCON output on next tick
	 */
	void QueueConsoleMessage(const char *pMessage);

	/**
	 * Queues a result for thread-safe processing on the main thread tick.
	 * Must be used instead of ProcessResult when calling from background threads.
	 */
	void QueueResult(int ClientId, std::shared_ptr<IVpnServiceResult> pResult);

	bool IsIpWhitelisted(const char *pIp) const;
	void WhitelistIpAdd(const char *pIp);
	void WhitelistIpRemove(const char *pIp);
	const std::set<std::string> &GetWhitelistedIps() const { return m_WhitelistedIps; }
	void SaveWhitelist();
	void LoadWhitelist();

	char m_aGetipintelContact[128];
	char m_aBanTimeString[VPN_BAN_TIME_STRING_MAX];
	float m_GetipintelThreshold;

private:
	void LoadCachedResultsForClient(int ClientId);
	void ProcessRequestQueues();
	void EnqueueRequest(std::shared_ptr<IVpnServiceRequest> pRequest);
	void AsyncExecuteRequest(std::shared_ptr<IVpnServiceRequest> pRequest);
	void CleanupFinishedThreads();
	void BanClient(int ClientId, const char *pReason);

	bool m_Debug;
	bool m_Enabled;
	bool m_BanEnabled;
	int m_BanTimeMinutes;
	int m_RateLimitIpquery;
	int m_RateLimitGetipintel;
	int m_RateLimitIphub;
	std::mutex m_Mutex;
	std::string m_DefaultService;

	std::vector<void *> m_ConVarCallbacks;

	CVpnClientInfo m_aClientInfo[MAX_VPN_CLIENTS];
	std::unordered_map<std::string, SVpnServiceQueue> m_ServiceQueues;
	std::unordered_map<std::string, std::unique_ptr<IVpnService>> m_Services;
	CVpnCache m_Cache;

	std::vector<std::thread> m_RequestThreads;
	std::mutex m_ThreadMutex;
	std::set<std::thread::id> m_FinishedThreadIds;
	std::mutex m_FinishedMutex;

	struct SPendingMessage
	{
		std::string m_Message;
		int64_t m_Timestamp;
	};
	std::vector<SPendingMessage> m_PendingMessages;
	std::mutex m_MessageMutex;

	struct SPendingResult
	{
		int m_ClientId;
		std::shared_ptr<IVpnServiceResult> m_pResult;
	};
	std::vector<SPendingResult> m_PendingResults;
	std::mutex m_ResultMutex;

	std::set<std::string> m_WhitelistedIps;
};

#endif // BLOCKWORLDS_COMPONENTS_VPN_DETECTION_H
