#ifndef BLOCK_COMPONENTS_AGONES_AGONES_H
#define BLOCK_COMPONENTS_AGONES_AGONES_H

#include <engine/http.h>
#include <engine/shared/protocol.h>

#include <block/components/core/component.h>
#include <block/utils/jobs.h>

#include <block/external/json-modern/json.hpp>

class CAgonesComponent final : public CComponent
{
	DECLARE_COMPONENT(CAgonesComponent, "agones")
	~CAgonesComponent() override;

protected:
	void OnEnable() override;

	void OnPlayerEnter(int ClientId) override;
	void OnPlayerDrop(int ClientId) override;

	void OnTick() override;

	// TODO: chain sv_max_clients. Main issue is removing chain on unplug, too lazy rn
	// static void ConChainMaxClients(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

private:
	CModernJobPool m_JobPool; // TODO: make mutual pool for all components

	std::string m_CachedNames[MAX_CLIENTS];

	int m_LastHealthTick;
	int m_Port;

	enum class EHttpType
	{
		POST,
		PUT
	};

	void SendHttp(std::string_view Path, const std::string &Data = "{}", EHttpType Type = EHttpType::POST);

	void SendReady();
	void SendHealth();

	void SendPlayerCapacity(int Capacity);
	void SendPlayerConnect(int ClientId);
	void SendPlayerDisconnect(int ClientId);
};

#endif // BLOCK_COMPONENTS_AGONES_AGONES_H
