/* (c) Blockworlds contributors. See licence.txt in the root of the distribution for more information. */
#ifndef BLOCKWORLDS_DISCORD_WEBHOOK_H
#define BLOCKWORLDS_DISCORD_WEBHOOK_H

#include <engine/engine.h>
#include <engine/http.h>

#include <base/types.h>

#include <memory>

// Job-backed Discord Webhook client
//
// Configuration (in server cfg):
//   sv_discord_enabled 1
//   sv_discord_webhook_url "https://discord.com/api/webhooks/ID/TOKEN"
// Optional:
//   sv_discord_webhook_username "Blockworlds Bot"
//   sv_discord_webhook_avatar   "https://.../avatar.png"
//   sv_discord_tts 0/1
//   sv_discord_thread_id "<thread id>"
//
// Usage:
//   CDiscordWebhook Discord(Engine(), Http());
//   Discord.Send("Hello");
class CDiscordWebhook
{
public:
	struct SSendOptions
	{
		// Overrides; if nullptr or empty, falls back to config values.
		const char *m_pUsername = nullptr;
		const char *m_pAvatarUrl = nullptr;
		// If set to -1, use config; otherwise force.
		int m_Tts = -1; // 0/1
		const char *m_pThreadId = nullptr;
		// Optional explicit webhook URL to send to; if null/empty, use config default
		const char *m_pWebhookUrl = nullptr;
	};

private:
	class CSendJob;

	IEngine *m_pEngine = nullptr;
	IHttp *m_pHttp = nullptr;

public:
	CDiscordWebhook(IEngine *pEngine, IHttp *pHttp);

	bool IsConfigured() const;
	bool IsConfigured(const char *pUrlOverride) const;

	// Queue a message send (non-blocking). Returns immediately.
	void Send(const char *pContent, const SSendOptions *pOpt = nullptr);
	void Send(const char *pContent, SSendOptions Opt) { Send(pContent, &Opt); }
};

#endif // BLOCKWORLDS_DISCORD_WEBHOOK_H
