#include "promises.h"

#include <blockworlds/bw_base.h>
#include <engine/server.h>
#include <engine/server/server.h>

CPromises::CPromises(CGameContext *pGameServer) :
	CComponent(pGameServer) {}

unsigned long CPromises::GetCallbackHash(std::function<void(std::shared_ptr<void>)> FnCallback)
{
	if(!IsDebug())
		return 0L;

	const auto Raw = FnCallback.target<void(void *)>();
	const auto Hash = Raw ?
				  (size_t)(uintptr_t)(void *)*Raw :
				  FnCallback.target_type().hash_code();
	return Hash;
}

const SPromise *CPromises::AddPromise(int ExecuteTick, std::weak_ptr<void> pUserData, std::function<void(std::shared_ptr<void>)> FnCallback)
{
	SPromise NewPromise;
	NewPromise.m_ExecuteTick = ExecuteTick;
	NewPromise.m_pUserData = std::move(pUserData);
	NewPromise.m_Callback = std::move(FnCallback);
	m_Promises.push_back(NewPromise);
	LogDebug("Promise Created. Execution: %d, Callback: %" PRIzu, NewPromise.m_ExecuteTick, GetCallbackHash(NewPromise.m_Callback));
	return &m_Promises.back();
}

void CPromises::OnTick()
{
	for(auto it = m_Promises.begin(); it != m_Promises.end();)
	{
		if(Server()->Tick() < it->m_ExecuteTick)
		{
			it++;
			continue;
		}

		if(auto pLockedUserData = it->m_pUserData.lock())
		{
			LogDebug("Promise Finished. Callback: %" PRIzu, GetCallbackHash(it->m_Callback));
			it->m_Callback(pLockedUserData);
		}
		else
			LogDebug("Promise Discarded. Owner is dead. Callback: %" PRIzu, GetCallbackHash(it->m_Callback));

		it = m_Promises.erase(it);
	}
}

// I'm not sure if we should discard or force callbacks
// Discarding might result in infinite waiting for other components
// Forcing might result in broken logic for other components
// In either way, user is dumbass

// Alternatively, on shutdown we can store start tick in promise and shift execution tick, so timings won't break
// But some components might disable or reset during shutdown
// So basically we will call invalid promise
// Keep in mind, that OnShutdown is called both on real shutdown and on gameserver restart, like map reload or map change
void CPromises::OnShutdown()
{
	auto it = m_Promises.begin();
	for(; it != m_Promises.end();)
	{
		auto &Promise = *it;
		LogDebug("Promise Forcefully Discarded. Callback: %" PRIzu, GetCallbackHash(Promise.m_Callback));
		m_Promises.erase(it);
	}
}
void CPromises::OnDisable()
{
	auto it = m_Promises.begin();
	for(; it != m_Promises.end();)
	{
		auto &Promise = *it;
		LogDebug("Promise Forcefully Discarded. Callback: %" PRIzu, GetCallbackHash(Promise.m_Callback));
		m_Promises.erase(it);
	}
}
