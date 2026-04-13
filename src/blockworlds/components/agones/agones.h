#pragma once

#include <engine/shared/http.h>
#include <engine/shared/protocol.h>
#include <engine/external/json-modern/json.hpp>

#include <blockworlds/components/core/component.h>
#include <blockworlds/utils/jobs.h>

class CAgonesComponent final : public CComponent {
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

    enum class EHttpType {
        POST,
        PUT
    };

    void SendHttp(std::string_view Path, std::string Data = "{}", EHttpType Type = EHttpType::POST);

    void SendReady();
    void SendHealth();

    void SendPlayerCapacity(int Capacity);
    void SendPlayerConnect(int ClientId);
    void SendPlayerDisconnect(int ClientId);
};
