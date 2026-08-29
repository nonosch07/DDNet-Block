#include "vpn_service_queue.h"

#include <block/base.h>

#include <algorithm>

SVpnServiceQueue::SVpnServiceQueue() :
	m_LastRequestTime(0),
	m_BackoffUntil(0),
	m_RateLimitMs(100)
{
}

SVpnServiceQueue::SVpnServiceQueue(const char *pServiceName, int RateLimitMs) :
	m_ServiceName(pServiceName),
	m_LastRequestTime(0),
	m_BackoffUntil(0),
	m_RateLimitMs(RateLimitMs)
{
}

bool SVpnServiceQueue::CanProcessRequest() const
{
	if(m_RequestQueue.empty())
		return false;

	int64_t Now = time_get();
	if(Now < m_BackoffUntil)
		return false;

	// no integer truncation losing sub-second precision
	int64_t ElapsedMs = (Now - m_LastRequestTime) * 1000 / time_freq();

	return ElapsedMs >= m_RateLimitMs;
}

void SVpnServiceQueue::SetBackoffMs(int BackoffMs)
{
	if(BackoffMs <= 0)
		return;
	m_BackoffUntil = time_get() + (int64_t)BackoffMs * time_freq() / 1000;
}

bool SVpnServiceQueue::HasPendingRequest(const char *pIpAddress, int ClientId) const
{
	if(!pIpAddress || !pIpAddress[0])
		return false;

	return std::any_of(m_RequestQueue.begin(), m_RequestQueue.end(), [&](const std::shared_ptr<IVpnServiceRequest> &pRequest) {
		return pRequest &&
		       pRequest->GetClientId() == ClientId &&
		       str_comp(pRequest->GetIpAddress(), pIpAddress) == 0;
	});
}

bool SVpnServiceQueue::EnqueueRequest(const std::shared_ptr<IVpnServiceRequest> &pRequest, int MaxQueueSize)
{
	if(!pRequest)
		return false;

	if(HasPendingRequest(pRequest->GetIpAddress(), pRequest->GetClientId()))
		return false;

	if(MaxQueueSize > 0 && (int)m_RequestQueue.size() >= MaxQueueSize)
		return false;

	m_RequestQueue.push_back(pRequest);
	return true;
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

int SVpnServiceQueue::RemoveRequestsForClient(int ClientId)
{
	const int OldSize = (int)m_RequestQueue.size();
	m_RequestQueue.erase(
		std::remove_if(m_RequestQueue.begin(), m_RequestQueue.end(), [ClientId](const std::shared_ptr<IVpnServiceRequest> &pRequest) {
			return pRequest && pRequest->GetClientId() == ClientId;
		}),
		m_RequestQueue.end());
	return OldSize - (int)m_RequestQueue.size();
}
