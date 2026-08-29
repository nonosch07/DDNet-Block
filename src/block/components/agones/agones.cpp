#include "agones.h"

#include <engine/http.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>

#include <block/components/core/component_registry.h>
#include <block/context.h>

CAgonesComponent::CAgonesComponent(CGameContext *pGameServer) :
	CComponent(pGameServer),
	m_JobPool(Config()->m_SvAgonesThreads),
	m_LastHealthTick(0),
	m_Port(0) {}

CAgonesComponent::~CAgonesComponent()
{
	m_JobPool.Shutdown();
}

void CAgonesComponent::OnEnable()
{
	m_Port = g_Config.m_SvAgonesPort;
	if(m_Port == 0)
	{
		const char *PortStr = std::getenv("AGONES_SDK_HTTP_PORT");
		if(PortStr == nullptr)
		{
			Log("Agones port is not set! Removing component");
			Registry()->Remove(GetName());
			return;
		}
		m_Port = str_toint(PortStr);
	}

	Log("Agones port found: %d", m_Port);

	SendReady();
	SendHealth();
	SendPlayerCapacity(Config()->m_SvMaxClients);
}

void CAgonesComponent::OnPlayerEnter(int ClientId)
{
	m_CachedNames[ClientId] = Server()->ClientName(ClientId);
	SendPlayerConnect(ClientId);
}

void CAgonesComponent::OnPlayerDrop(int ClientId)
{
	SendPlayerDisconnect(ClientId);
	m_CachedNames[ClientId].clear();
}

void CAgonesComponent::OnTick()
{
	if(Server()->Tick() - m_LastHealthTick >= Config()->m_SvAgonesHealthFrequency)
	{
		SendHealth();
	}
}

// void CAgonesComponent::ConChainMaxClients(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
//     pfnCallback(pResult, pCallbackUserData);
//     if(pResult->NumArguments())
//         ((CAgonesComponent *)pUserData)->SendPlayerCapacity(pResult->GetInteger(0));
// }

void CAgonesComponent::SendHttp(std::string_view Path, const std::string &Data, EHttpType Type)
{
	m_JobPool.Submit([PathStr = std::string(Path), Type](const std::string &Payload, int Port, IHttp *Http) {
		std::string_view BaseUrlView = g_Config.m_SvAgonesBaseUrl;
		std::string_view PathView = PathStr;
		BaseUrlView.remove_suffix(BaseUrlView.ends_with("/"));
		PathView.remove_prefix(PathStr.starts_with("/"));

		// not std::format: libstdc++ only got it in GCC 13 and CI builds on 22.04
		char aUrl[512];
		str_format(aUrl, sizeof(aUrl), "%.*s:%d/%.*s",
			(int)BaseUrlView.length(), BaseUrlView.data(), Port,
			(int)PathView.length(), PathView.data());
		const std::string Url = aUrl;

		std::unique_ptr<IHttpRequest> Request;
		switch(Type)
		{
		case EHttpType::POST:
			Request = HttpPostJson(Url.c_str(), Payload.c_str());
			break;
		case EHttpType::PUT:
			Request = HttpPutJson(Url.c_str(), Payload.c_str());
			break;
		}

		Request->LogProgress(g_Config.m_SvAgonesDebug ? HTTPLOG::ALL : HTTPLOG::FAILURE);

		Http->Run(std::move(Request));
	},
		Data, m_Port, GameServer()->Block().Http());
}

void CAgonesComponent::SendReady()
{
	SendHttp("ready");
}

void CAgonesComponent::SendHealth()
{
	m_LastHealthTick = Server()->Tick();
	SendHttp("health");
}

void CAgonesComponent::SendPlayerCapacity(int Capacity)
{
	nlohmann::json Payload;
	Payload["count"] = Capacity;

	SendHttp("alpha/player/capacity", Payload.dump(), EHttpType::PUT);
}

void CAgonesComponent::SendPlayerConnect(int ClientId)
{
	char aBase64NameBuf[128];
	str_base64(aBase64NameBuf, sizeof(aBase64NameBuf), m_CachedNames[ClientId].c_str(), m_CachedNames[ClientId].length());
	char aCombinedName[160];
	str_format(aCombinedName, sizeof(aCombinedName), "%d_%s", ClientId, aBase64NameBuf);
	std::string CombinedName = aCombinedName;

	nlohmann::json Payload;
	Payload["playerID"] = CombinedName;

	SendHttp("alpha/player/connect", Payload.dump());
}

void CAgonesComponent::SendPlayerDisconnect(int ClientId)
{
	char aBase64NameBuf[128];
	str_base64(aBase64NameBuf, sizeof(aBase64NameBuf), m_CachedNames[ClientId].c_str(), m_CachedNames[ClientId].length());
	char aCombinedName[160];
	str_format(aCombinedName, sizeof(aCombinedName), "%d_%s", ClientId, aBase64NameBuf);
	std::string CombinedName = aCombinedName;

	nlohmann::json Payload;
	Payload["playerID"] = CombinedName;

	SendHttp("alpha/player/disconnect", Payload.dump());
}
