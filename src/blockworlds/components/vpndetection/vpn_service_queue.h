#ifndef BLOCKWORLDS_COMPONENTS_VPN_SERVICE_QUEUE_H
#define BLOCKWORLDS_COMPONENTS_VPN_SERVICE_QUEUE_H

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
	int m_RateLimitMs;

	SVpnServiceQueue();
	SVpnServiceQueue(const char *pServiceName, int RateLimitMs = 100);

	bool CanProcessRequest() const;
	void EnqueueRequest(std::shared_ptr<IVpnServiceRequest> pRequest);
	std::shared_ptr<IVpnServiceRequest> DequeueRequest();
	bool IsEmpty() const { return m_RequestQueue.empty(); }
	int GetQueueSize() const { return (int)m_RequestQueue.size(); }
};

#endif // BLOCKWORLDS_COMPONENTS_VPN_SERVICE_QUEUE_H
