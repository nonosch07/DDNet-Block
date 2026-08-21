#ifndef BLOCKWORLDS_COMPONENTS_VPN_CACHE_H
#define BLOCKWORLDS_COMPONENTS_VPN_CACHE_H

#include "vpn_service_result.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Thread-safe cache for VPN detection results
 *
 * Stores results keyed by "IP:ServiceName" for fast lookup.
 * Supports persistence to/from JSON files.
 */
class CVpnCache
{
public:
	CVpnCache() = default;

	void Add(std::shared_ptr<IVpnServiceResult> pResult);
	std::shared_ptr<IVpnServiceResult> Get(const char *pIpAddress, const char *pServiceName);
	void GetAllForIP(const char *pIpAddress, std::vector<std::shared_ptr<IVpnServiceResult>> &Results);
	void SetTtlDays(int Days);
	bool IsExpired(std::shared_ptr<IVpnServiceResult> pResult) const;

	bool Load(const char *pFilename);
	bool Save(const char *pFilename);
	void Clear();
	int GetEntryCount() const;

private:
	static std::string MakeCacheKey(const char *pIpAddress, const char *pServiceName);
	bool IsTimestampFresh(int64_t Timestamp) const;
	void PruneExpiredLocked();

	mutable std::mutex m_Mutex;
	std::unordered_map<std::string, std::shared_ptr<IVpnServiceResult>> m_Cache;
	int64_t m_TtlSeconds = 30 * 24 * 60 * 60;
};

#endif // BLOCKWORLDS_COMPONENTS_VPN_CACHE_H
