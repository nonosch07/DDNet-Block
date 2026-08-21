/* (c) Blockworlds contributors. See licence.txt in the root of the distribution for more information. */

#include "webhook.h"

#include "webhook_queue.h"

#include <base/log.h>
#include <base/math.h>

#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/jsonwriter.h>

#include <blockworlds/bw_base.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
	constexpr int DISCORD_MAX_CONTENT = 2000;

	[[maybe_unused]] std::vector<std::string> ChunkMessage(const char *pMsg)
	{
		std::vector<std::string> v;
		if(!pMsg || !pMsg[0])
		{
			v.emplace_back("");
			return v;
		}
		const int Len = str_length(pMsg);
		for(int Pos = 0; Pos < Len;)
		{
			int Take = std::min(DISCORD_MAX_CONTENT, Len - Pos);
			v.emplace_back(pMsg + Pos, pMsg + Pos + Take);
			Pos += Take;
		}
		return v;
	}

	std::string SanitizeMentions(const char *pMsg)
	{
		if(!pMsg)
			return std::string();
		std::string s(pMsg);
		const std::string ZWSP = "\xE2\x80\x8B"; // zero width space
		auto ReplaceAll = [](std::string &Str, const char *Needle, const std::string &Replacement) {
			if(!Needle || !Needle[0])
				return;
			size_t Pos = 0;
			size_t Nlen = strlen(Needle);
			while((Pos = Str.find(Needle, Pos)) != std::string::npos)
			{
				Str.replace(Pos, Nlen, Replacement);
				Pos += Replacement.size();
			}
		};
		ReplaceAll(s, "@everyone", std::string("@") + ZWSP + "everyone");
		ReplaceAll(s, "@here", std::string("@") + ZWSP + "here");
		for(size_t i = 0; i < s.size(); ++i)
		{
			if(s[i] == '<' && i + 2 < s.size() && s[i + 1] == '@')
			{
				if(s.compare(i + 2, ZWSP.size(), ZWSP) != 0)
				{
					s.insert(i + 2, ZWSP);
					i += ZWSP.size() + 2;
				}
			}
			else if(s[i] == '@')
			{
				if(i + 1 < s.size() && (isalnum((unsigned char)s[i + 1]) || s[i + 1] == '&' || s[i + 1] == '!'))
				{
					s.insert(i + 1, ZWSP);
					i += ZWSP.size() + 1;
				}
			}
		}
		return s;
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
	CSendJob(IHttp *pHttp, const char *pUrl, const std::vector<std::string> &vChunks, const SSendOptions *pOpt) :
		m_pHttp(pHttp), m_vChunks(vChunks)
	{
		str_copy(m_aUrl, pUrl);
		// Merge options with config defaults.
		const char *pUser = (pOpt && pOpt->m_pUsername && pOpt->m_pUsername[0]) ? pOpt->m_pUsername : g_Config.m_SvDiscordWebhookUsername;
		const char *pAvatar = (pOpt && pOpt->m_pAvatarUrl && pOpt->m_pAvatarUrl[0]) ? pOpt->m_pAvatarUrl : g_Config.m_SvDiscordWebhookAvatar;
		const char *pThread = (pOpt && pOpt->m_pThreadId && pOpt->m_pThreadId[0]) ? pOpt->m_pThreadId : g_Config.m_SvDiscordThreadId;
		int Tts = (pOpt && pOpt->m_Tts != -1) ? pOpt->m_Tts : g_Config.m_SvDiscordTts;
		if(pUser)
			str_copy(m_aUsername, pUser);
		if(pAvatar)
			str_copy(m_aAvatar, pAvatar);
		if(pThread)
			str_copy(m_aThreadId, pThread);
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
			if(m_aUsername[0])
			{
				W.WriteAttribute("username");
				W.WriteStrValue(m_aUsername);
			}
			if(m_aAvatar[0])
			{
				W.WriteAttribute("avatar_url");
				W.WriteStrValue(m_aAvatar);
			}
			if(m_Tts)
			{
				W.WriteAttribute("tts");
				W.WriteBoolValue(true);
			}
			W.EndObject();
			std::string Payload = W.GetOutputString();

			char aUrl[1024];
			if(m_aThreadId[0])
				str_format(aUrl, sizeof(aUrl), "%s?thread_id=%s", m_aUrl, m_aThreadId);
			else
				str_copy(aUrl, m_aUrl);

			auto UpReq = HttpPostJson(aUrl, Payload.c_str());
			UpReq->HeaderString("Content-Type", "application/json");
			UpReq->Timeout(CTimeout{5000, 15000, 500, 5});
			UpReq->FailOnErrorStatus(false);
			std::shared_ptr<IHttpRequest> pReq(std::move(UpReq));
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

CDiscordWebhook::CDiscordWebhook(IEngine *pEngine, IHttp *pHttp) :
	m_pEngine(pEngine), m_pHttp(pHttp)
{
}

bool CDiscordWebhook::IsConfigured(const char *pUrlOverride) const
{
	if(!g_Config.m_SvDiscordEnabled)
		return false;
	return pUrlOverride && pUrlOverride[0] != '\0';
}

void CDiscordWebhook::Send(const char *pContent, const SSendOptions *pOpt)
{
	if(!m_pEngine || !m_pHttp)
	{
		log_warn("discord", "missing engine/http interfaces");
		return;
	}
	const char *pUrl = (pOpt && pOpt->m_pWebhookUrl && pOpt->m_pWebhookUrl[0]) ? pOpt->m_pWebhookUrl : nullptr;
	if(!IsConfigured(pUrl))
	{
		log_debug("discord", "webhook not configured (sv_discord_enabled=1 and per-feature webhook url required)");
		return;
	}
	std::string Sanitized = SanitizeMentions(pContent);

	CDiscordWebhookQueueManager::Instance().Init(m_pEngine);
	CDiscordWebhookQueueManager::Instance().Enqueue(pUrl, Sanitized.c_str());
}

void CDiscordWebhook::BroadcastCmd(const char *pCmd, const char *pExecutor, const char *pArgs)
{
	if(!m_pEngine || !m_pHttp)
	{
		return;
	}
	const char *pUrl = g_Config.m_SvDiscordWebhookUrlRconLogs;

	if(pUrl == nullptr)
	{
		return;
	}

	char aMsg[128];
	if(pExecutor && pExecutor[0])
	{
		if(pArgs && pArgs[0])
			str_format(aMsg, sizeof(aMsg), "**%s** executed command: **%s**", pExecutor, pArgs);
	}
	else
	{
		str_format(aMsg, sizeof(aMsg), "command executed: **%s**", pCmd);
	}

	SSendOptions Opt;
	Opt.m_pWebhookUrl = pUrl;
	Opt.m_pUsername = g_Config.m_SvDiscordWebhookUsername;
	Opt.m_Tts = 0;

	Send(aMsg, &Opt);
}

void CDiscordWebhook::SendRconLog(IEngine *pEngine, IHttp *pHttp, const char *pFmt, ...)
{
	const char *pUrl = g_Config.m_SvDiscordWebhookUrlRconLogs[0] ? g_Config.m_SvDiscordWebhookUrlRconLogs : nullptr;
	CDiscordWebhook Discord(pEngine, pHttp);
	if(!Discord.IsConfigured(pUrl))
		return;

	char aMsg[512];
	va_list Args;
	va_start(Args, pFmt);
	str_format_v(aMsg, sizeof(aMsg), pFmt, Args);
	va_end(Args);

	SSendOptions Options;
	Options.m_pWebhookUrl = pUrl;
	Options.m_pUsername = g_Config.m_SvDiscordWebhookUsername;
	Options.m_Tts = 0;
	Discord.Send(aMsg, Options);
}
