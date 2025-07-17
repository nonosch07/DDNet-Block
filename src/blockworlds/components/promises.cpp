#include "promises.h"

#include <base/system.h>
#include <engine/server.h>

CPromises::CPromises(CGameContext *pGameServer) :
	CComponent(pGameServer) {}

unsigned long CPromises::GetCallbackHash(std::function<void(void *)> FnCallback)
{
	if(!IsDebug())
		return 0L;

	const auto Raw = FnCallback.target<void(void *)>();
	const auto Hash = Raw ?
				  (size_t)(uintptr_t)(void*)*Raw :
				  FnCallback.target_type().hash_code();
	return Hash;
}

void CPromises::AddPromise(const int ExecuteTick, void *pUserData, std::function<void(void *)> FnCallback)
{
	SPromise NewPromise;
	NewPromise.m_ExecuteTick = ExecuteTick;
	NewPromise.m_pUserData = pUserData;
	NewPromise.m_Callback = std::move(FnCallback);
	m_Promises.push_back(NewPromise);
	LogDebug("New Promise. Execution: %d, Callback: %" PRIzu, NewPromise.m_ExecuteTick, GetCallbackHash(NewPromise.m_Callback));
}

void CPromises::OnTick()
{
	auto it = m_Promises.begin();
	for(; it != m_Promises.end();)
	{
		auto &Promise = *it;
		if(Server()->Tick() < Promise.m_ExecuteTick)
		{
			it++;
			continue;
		}
		LogDebug("Promise Expired. Callback: %" PRIzu, GetCallbackHash(Promise.m_Callback));
		Promise.m_Callback(Promise.m_pUserData);
		m_Promises.erase(it);
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
//		Promise.m_Callback(Promise.m_pUserData);
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
		//		Promise.m_Callback(Promise.m_pUserData);
		m_Promises.erase(it);
	}
}
