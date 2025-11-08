#include "vpn_service_queue.h"

#include <base/system.h>

SVpnServiceQueue::SVpnServiceQueue() :
	m_LastRequestTime(0),
	m_RateLimitMs(100)
{
}

SVpnServiceQueue::SVpnServiceQueue(const char *pServiceName, int RateLimitMs) :
	m_ServiceName(pServiceName),
	m_LastRequestTime(0),
	m_RateLimitMs(RateLimitMs)
{
}

bool SVpnServiceQueue::CanProcessRequest() const
{
	if(m_RequestQueue.empty())
		return false;
	
	int64_t Now = time_get();
	int64_t TimeSinceLastRequest = (Now - m_LastRequestTime) / time_freq() * 1000; // Convert to ms
	
	return TimeSinceLastRequest >= m_RateLimitMs;
}

void SVpnServiceQueue::EnqueueRequest(std::shared_ptr<IVpnServiceRequest> pRequest)
{
	m_RequestQueue.push_back(pRequest);
}

std::shared_ptr<IVpnServiceRequest> SVpnServiceQueue::DequeueRequest()
{
	if(m_RequestQueue.empty())
		return nullptr;
	
	auto pRequest = m_RequestQueue.front();
	m_RequestQueue.pop_front();
	m_LastRequestTime = time_get();
	
	return pRequest;
}

