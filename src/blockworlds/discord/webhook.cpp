/* (c) Blockworlds contributors. See licence.txt in the root of the distribution for more information. */

#include "webhook.h"

#include <engine/shared/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/jsonwriter.h>

#include <engine/shared/config.h>

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr int DISCORD_MAX_CONTENT = 2000;

static std::vector<std::string> ChunkMessage(const char *pMsg)
{
	std::vector<std::string> v;
	if(!pMsg || !pMsg[0])
	{
		v.emplace_back("");
		return v;
	}
	const int Len = str_length(pMsg);
	for(int pos = 0; pos < Len; )
	{
		int take = minimum(DISCORD_MAX_CONTENT, Len - pos);
		v.emplace_back(std::string(pMsg + pos, pMsg + pos + take));
		pos += take;
	}
	return v;
}
} // namespace

class CDiscordWebhook::CSendJob : public IJob
{
	IHttp *m_pHttp = nullptr;
	char m_aUrl[512] = {0};
	std::vector<std::string> m_vChunks;
	char m_aUsername[128] = {0};
	char m_aAvatar[256] = {0};
	char m_aThreadId[128] = {0};
	bool m_Tts = false;

public:
	CSendJob(IHttp *pHttp, const char *pUrl, const std::vector<std::string> &vChunks, const SSendOptions *pOpt)
		: m_pHttp(pHttp)
		, m_vChunks(vChunks)
	{
		str_copy(m_aUrl, pUrl);
		// Merge options with config defaults.
		const char *pUser = (pOpt && pOpt->m_pUsername && pOpt->m_pUsername[0]) ? pOpt->m_pUsername : g_Config.m_SvDiscordWebhookUsername;
		const char *pAvatar = (pOpt && pOpt->m_pAvatarUrl && pOpt->m_pAvatarUrl[0]) ? pOpt->m_pAvatarUrl : g_Config.m_SvDiscordWebhookAvatar;
		const char *pThread = (pOpt && pOpt->m_pThreadId && pOpt->m_pThreadId[0]) ? pOpt->m_pThreadId : g_Config.m_SvDiscordThreadId;
		int Tts = (pOpt && pOpt->m_Tts != -1) ? pOpt->m_Tts : g_Config.m_SvDiscordTts;
		if(pUser) str_copy(m_aUsername, pUser);
		if(pAvatar) str_copy(m_aAvatar, pAvatar);
		if(pThread) str_copy(m_aThreadId, pThread);
		m_Tts = Tts != 0;
		Abortable(true);
	}

	void Run() override
	{
		for(const auto &Chunk : m_vChunks)
		{
			if(State() == STATE_ABORTED)
				return;

			CJsonStringWriter W;
			W.BeginObject();
			W.WriteAttribute("content");
			W.WriteStrValue(Chunk.c_str());
			if(m_aUsername[0]) { W.WriteAttribute("username"); W.WriteStrValue(m_aUsername); }
			if(m_aAvatar[0]) { W.WriteAttribute("avatar_url"); W.WriteStrValue(m_aAvatar); }
			if(m_Tts) { W.WriteAttribute("tts"); W.WriteBoolValue(true); }
			W.EndObject();
			std::string Payload = W.GetOutputString();

			char aUrl[1024];
			if(m_aThreadId[0])
				str_format(aUrl, sizeof(aUrl), "%s?thread_id=%s", m_aUrl, m_aThreadId);
			else
				str_copy(aUrl, m_aUrl);

			auto upReq = HttpPostJson(aUrl, Payload.c_str());
			upReq->HeaderString("Content-Type", "application/json");
			upReq->Timeout(CTimeout{5000, 15000, 500, 5});
			upReq->FailOnErrorStatus(false);
			std::shared_ptr<CHttpRequest> pReq(std::move(upReq));
			m_pHttp->Run(pReq);
			pReq->Wait();

			if(pReq->State() == EHttpState::ABORTED || State() == STATE_ABORTED)
				return;
			if(pReq->State() != EHttpState::DONE)
			{
				log_error("discord", "webhook post failed");
				continue;
			}
			int Code = pReq->StatusCode();
			if(Code < 200 || Code >= 300)
			{
				log_error("discord", "webhook status %d", Code);
			}
		}
	}
};

CDiscordWebhook::CDiscordWebhook(IEngine *pEngine, IHttp *pHttp)
	: m_pEngine(pEngine)
	, m_pHttp(pHttp)
{
}

bool CDiscordWebhook::IsConfigured() const
{
	return g_Config.m_SvDiscordEnabled != 0 && g_Config.m_SvDiscordWebhookUrl[0] != '\0';
}

bool CDiscordWebhook::IsConfigured(const char *pUrlOverride) const
{
	if(!g_Config.m_SvDiscordEnabled)
		return false;
	if(pUrlOverride && pUrlOverride[0] != '\0')
		return true;
	return g_Config.m_SvDiscordWebhookUrl[0] != '\0';
}

void CDiscordWebhook::Send(const char *pContent, const SSendOptions *pOpt)
{
	if(!m_pEngine || !m_pHttp)
	{
		log_warn("discord", "missing engine/http interfaces");
		return;
	}
	const char *pUrl = (pOpt && pOpt->m_pWebhookUrl && pOpt->m_pWebhookUrl[0]) ? pOpt->m_pWebhookUrl : g_Config.m_SvDiscordWebhookUrl;
	if(!IsConfigured(pUrl))
	{
		log_debug("discord", "webhook not configured (sv_discord_enabled=1 and webhook url required)");
		return;
	}
	auto vChunks = ChunkMessage(pContent);
	auto pJob = std::make_shared<CSendJob>(m_pHttp, pUrl, vChunks, pOpt);
	m_pEngine->AddJob(pJob);
}
