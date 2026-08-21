/* (c) Blockworlds contributors. See licence.txt in the root of the distribution for more information. */
#ifndef BLOCKWORLDS_DISCORD_WEBHOOK_QUEUE_H
#define BLOCKWORLDS_DISCORD_WEBHOOK_QUEUE_H

#include <engine/http.h>

#include <blockworlds/bw_base.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class IEngine;
class IHttp;
class IEngineHttp;

struct SDiscordMessageQueue
{
	std::deque<std::pair<int64_t, std::string>> m_Messages;
	int64_t m_LastSendTime = 0;
	int64_t m_BackoffUntil = 0;
	int m_ConsecutiveErrors = 0;
	bool m_RateLimitLogged = false;
};

class CDiscordQueueJob;

class CDiscordWebhookQueueManager
{
	friend class CDiscordQueueJob;

	std::mutex m_Mutex;
	std::unordered_map<std::string, SDiscordMessageQueue> m_Queues;
	IEngine *m_pEngine = nullptr;
	// Dedicated HTTP instance so webhooks don't starve master server
	// registration. Upstream turned CHttp into the IHttp interface plus a
	// CreateEngineHttp() factory, so this owns the engine implementation.
	std::unique_ptr<IEngineHttp> m_pHttp;
	std::shared_ptr<CDiscordQueueJob> m_pProcessJob;
	bool m_Running = false;
	bool m_Initialized = false;

	static constexpr int RATE_LIMIT_MS = 2500;
	static constexpr int MAX_MESSAGE_AGE_MS = 30000;
	static constexpr int DISCORD_MAX_CONTENT = 2000;
	static constexpr int INITIAL_BACKOFF_MS = 5000;
	static constexpr int MAX_BACKOFF_MS = 60000;

	void StartProcessingJob();
	void ProcessQueues();
	std::string BatchMessages(SDiscordMessageQueue *pQueue);
	bool HasPendingMessages();

public:
	static CDiscordWebhookQueueManager &Instance();

	void Init(IEngine *pEngine);
	void Shutdown();

	void Enqueue(const char *pUrl, const char *pMessage);
	void OnRequestComplete(const std::string &Url, int StatusCode);
};

#endif // BLOCKWORLDS_DISCORD_WEBHOOK_QUEUE_H
