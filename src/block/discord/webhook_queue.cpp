/* (c) Block contributors. See licence.txt in the root of the distribution for more information. */

#include "webhook_queue.h"

#include <base/log.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/jsonwriter.h>

#include <block/base.h>

#include <algorithm>
#include <chrono>
#include <thread>

class CDiscordQueueJob : public IJob
{
	CDiscordWebhookQueueManager *m_pManager;

public:
	explicit CDiscordQueueJob(CDiscordWebhookQueueManager *pManager) :
		m_pManager(pManager)
	{
		Abortable(true);
	}

	void Run() override
	{
		while(State() != STATE_ABORTED)
		{
			m_pManager->ProcessQueues();

			if(State() == STATE_ABORTED)
				break;

			bool HasMessages = m_pManager->HasPendingMessages();
			if(!HasMessages)
			{
				std::lock_guard<std::mutex> Lock(m_pManager->m_Mutex);
				m_pManager->m_Running = false;
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
};

class CDiscordSendJob : public IJob
{
	IHttp *m_pHttp;
	std::string m_Url;
	std::string m_Payload;

public:
	CDiscordSendJob(IHttp *pHttp, const std::string &Url, const std::string &Payload) :
		m_pHttp(pHttp), m_Url(Url), m_Payload(Payload)
	{
		Abortable(true);
	}

	void Run() override
	{
		if(State() == STATE_ABORTED)
			return;

		auto UpReq = HttpPostJson(m_Url.c_str(), m_Payload.c_str());
		UpReq->HeaderString("Content-Type", "application/json");
		UpReq->Timeout(CTimeout{10000, 30000, 100, 10});
		UpReq->FailOnErrorStatus(false);
		std::shared_ptr<IHttpRequest> pReq(std::move(UpReq));
		m_pHttp->Run(pReq);
		pReq->Wait();

		if(pReq->State() == EHttpState::ABORTED || State() == STATE_ABORTED)
			return;

		int Code = 0;
		if(pReq->State() == EHttpState::DONE)
			Code = pReq->StatusCode();

		CDiscordWebhookQueueManager::Instance().OnRequestComplete(m_Url, Code);
	}
};

CDiscordWebhookQueueManager &CDiscordWebhookQueueManager::Instance()
{
	static CDiscordWebhookQueueManager s_Instance;
	return s_Instance;
}

void CDiscordWebhookQueueManager::Init(IEngine *pEngine)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(m_Initialized)
		return;
	m_pEngine = pEngine;
	m_pHttp.reset(CreateEngineHttp());
	if(!m_pHttp || !m_pHttp->Init(std::chrono::seconds{2}))
	{
		log_error("discord", "failed to initialize dedicated HTTP client for webhooks");
		return;
	}
	m_Initialized = true;
}

void CDiscordWebhookQueueManager::Shutdown()
{
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Running = false;
		m_Queues.clear();
		m_pProcessJob.reset();
	}
	if(m_pHttp)
		m_pHttp->Shutdown();
}

void CDiscordWebhookQueueManager::Enqueue(const char *pUrl, const char *pMessage)
{
	if(!pUrl || !pUrl[0] || !pMessage)
		return;

	std::lock_guard<std::mutex> Lock(m_Mutex);

	if(!m_Initialized)
		return;

	std::string Url(pUrl);
	auto &Queue = m_Queues[Url];
	Queue.m_Messages.emplace_back(time_get(), std::string(pMessage));

	StartProcessingJob();
}

void CDiscordWebhookQueueManager::StartProcessingJob()
{
	if(m_Running || !m_pEngine)
		return;

	m_Running = true;
	m_pProcessJob = std::make_shared<CDiscordQueueJob>(this);
	m_pEngine->AddJob(m_pProcessJob);
}

bool CDiscordWebhookQueueManager::HasPendingMessages()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return std::ranges::any_of(m_Queues, [](const auto &Pair) {
		return !Pair.second.m_Messages.empty();
	});
}

std::string CDiscordWebhookQueueManager::BatchMessages(SDiscordMessageQueue *pQueue)
{
	std::string Batched;
	int64_t Now = time_get();
	int64_t MaxAgeThreshold = Now - (MAX_MESSAGE_AGE_MS * time_freq() / 1000);

	while(!pQueue->m_Messages.empty())
	{
		auto &Front = pQueue->m_Messages.front();

		if(Front.first < MaxAgeThreshold)
		{
			pQueue->m_Messages.pop_front();
			continue;
		}

		size_t NewLen = Batched.empty() ? Front.second.size() : Batched.size() + 1 + Front.second.size();
		if(NewLen > DISCORD_MAX_CONTENT)
			break;

		if(!Batched.empty())
			Batched += '\n';
		Batched += Front.second;
		pQueue->m_Messages.pop_front();
	}

	return Batched;
}

void CDiscordWebhookQueueManager::ProcessQueues()
{
	std::vector<std::pair<std::string, std::string>> ToSend;

	{
		std::lock_guard<std::mutex> Lock(m_Mutex);

		int64_t Now = time_get();
		int64_t RateLimitThreshold = RATE_LIMIT_MS * time_freq() / 1000;

		for(auto &Pair : m_Queues)
		{
			SDiscordMessageQueue *pQueue = &Pair.second;

			if(pQueue->m_Messages.empty())
				continue;

			if(pQueue->m_BackoffUntil > Now)
				continue;

			if(Now - pQueue->m_LastSendTime < RateLimitThreshold)
				continue;

			std::string Batched = BatchMessages(pQueue);
			if(Batched.empty())
				continue;

			pQueue->m_LastSendTime = Now;
			ToSend.emplace_back(Pair.first, std::move(Batched));
		}
	}

	for(auto &SendPair : ToSend)
	{
		CJsonStringWriter W;
		W.BeginObject();
		W.WriteAttribute("content");
		W.WriteStrValue(SendPair.second.c_str());
		if(g_Config.m_SvDiscordWebhookUsername[0])
		{
			W.WriteAttribute("username");
			W.WriteStrValue(g_Config.m_SvDiscordWebhookUsername);
		}
		if(g_Config.m_SvDiscordWebhookAvatar[0])
		{
			W.WriteAttribute("avatar_url");
			W.WriteStrValue(g_Config.m_SvDiscordWebhookAvatar);
		}
		W.EndObject();
		std::string Payload = W.GetOutputString();

		auto pJob = std::make_shared<CDiscordSendJob>(m_pHttp.get(), SendPair.first, Payload);
		m_pEngine->AddJob(pJob);
	}
}

void CDiscordWebhookQueueManager::OnRequestComplete(const std::string &Url, int StatusCode)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);

	auto It = m_Queues.find(Url);
	if(It == m_Queues.end())
		return;

	SDiscordMessageQueue *pQueue = &It->second;

	if(StatusCode == 429)
	{
		pQueue->m_ConsecutiveErrors++;
		int BackoffMs = INITIAL_BACKOFF_MS * (1 << (pQueue->m_ConsecutiveErrors - 1));
		if(BackoffMs > MAX_BACKOFF_MS)
			BackoffMs = MAX_BACKOFF_MS;

		pQueue->m_BackoffUntil = time_get() + (BackoffMs * time_freq() / 1000);

		if(!pQueue->m_RateLimitLogged)
		{
			log_warn("discord", "rate limited, backing off for %ds", BackoffMs / 1000);
			pQueue->m_RateLimitLogged = true;
		}
	}
	else if(StatusCode >= 200 && StatusCode < 300)
	{
		pQueue->m_ConsecutiveErrors = 0;
		pQueue->m_RateLimitLogged = false;
	}
	else if(StatusCode != 0)
	{
		log_error("discord", "webhook request failed with status %d", StatusCode);
	}
}
