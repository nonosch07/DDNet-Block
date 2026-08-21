#ifndef BLOCKWORLDS_COMPONENTS_VPNDETECTION_VPN_SERVICE_QUEUE_H
#define BLOCKWORLDS_COMPONENTS_VPNDETECTION_VPN_SERVICE_QUEUE_H

#include "vpn_service_request.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

/**
 * Rate-limited request queue for a VPN detection service
 */
struct SVpnServiceQueue
{
	std::string m_ServiceName;
	std::deque<std::shared_ptr<IVpnServiceRequest>> m_RequestQueue;
	int64_t m_LastRequestTime;
	int64_t m_BackoffUntil;
	int m_RateLimitMs;

	SVpnServiceQueue();
	SVpnServiceQueue(const char *pServiceName, int RateLimitMs = 100);

	bool CanProcessRequest() const;
	void SetBackoffMs(int BackoffMs);
	bool HasPendingRequest(const char *pIpAddress, int ClientId) const;
	bool EnqueueRequest(const std::shared_ptr<IVpnServiceRequest> &pRequest, int MaxQueueSize);
	std::shared_ptr<IVpnServiceRequest> DequeueRequest();
	int RemoveRequestsForClient(int ClientId);
	bool IsEmpty() const { return m_RequestQueue.empty(); }
	int GetQueueSize() const { return (int)m_RequestQueue.size(); }
};

#endif // BLOCKWORLDS_COMPONENTS_VPNDETECTION_VPN_SERVICE_QUEUE_H
