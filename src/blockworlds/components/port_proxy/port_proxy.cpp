#include "port_proxy.h"

#include <engine/server.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <blockworlds/bw_context.h>
#include <game/server/gamecontext.h>

CPortProxy::CPortProxy(CGameContext *pGameServer) : CComponent(pGameServer) {}

int CPortProxy::ClientPort(int ClientId) const {
    for (const auto &PortEntry : m_PortsTaken) {
        if (!PortEntry.m_Waiting && PortEntry.m_ClientId == ClientId) {
            return PortEntry.m_Port;
        }
    }
    return Config()->m_SvPort;
}

int CPortProxy::NextFreePort() const {
    int Base = Config()->m_SvProxyRangeStart;
    int Length = Config()->m_SvProxyRangeLength;

    int NextPort = Config()->m_SvPort;
    if ((int)m_PortsTaken.size() >= Length)
        return NextPort;
    do {
        NextPort = secure_rand_below(Length) + Base;
    } while (IsPortTaken(NextPort));

    return NextPort;
}

bool CPortProxy::IsPortTaken(int Port) const {
    return std::any_of(m_PortsTaken.begin(), m_PortsTaken.end(), [Port](auto PortEntry){ return PortEntry.m_Port == Port; });
}

bool CPortProxy::IsDebug() const {
    return Config()->m_SvProxyDebug;
}

void CPortProxy::OnTick() {
    for (auto PortEntry = m_PortsTaken.begin(); PortEntry != m_PortsTaken.end();) {
        if (PortEntry->m_Waiting && PortEntry->m_WaitingUntil < Server()->Tick()) {
            Log("Port %d expired for client %s", PortEntry->m_Port, PortEntry->m_pClientAddrStr);
            PortEntry = m_PortsTaken.erase(PortEntry);
        }
        else
            ++PortEntry;
    }
}

void CPortProxy::OnPlayerConnected(int ClientId) {
    NETADDR ClientAddr = *Server()->ClientAddr(ClientId);
    const char *pClientAddrStr = Server()->ClientAddrString(ClientId, false);

    for (auto &PortEntry : m_PortsTaken) {
        if (!PortEntry.m_Waiting)
            continue;

        if (net_addr_comp_noport(&PortEntry.m_ClientAddr, &ClientAddr) == 0) {
            PortEntry.m_Waiting = false;
            PortEntry.m_ClientId = ClientId;
            LogDebug("Client %s accepted on port %d", PortEntry.m_pClientAddrStr, PortEntry.m_Port);
            return;
        }
    }

    SPort PortEntry;

    int Port = NextFreePort();
    PortEntry.m_Port = Port;
    PortEntry.m_Waiting = true;
    PortEntry.m_WaitingUntil = Server()->Tick() + Config()->m_SvProxyRedirectTimeout;
    PortEntry.m_ClientAddr = ClientAddr;
    PortEntry.m_pClientAddrStr = pClientAddrStr;
    m_PortsTaken.push_back(PortEntry);
    LogDebug("Client %d redirected to port %d", ClientId, Port);
    GameServer()->Bw().RedirectClient(ClientId, Port, true);
}

void CPortProxy::OnPlayerDropping(int ClientId) {
    for (auto it = m_PortsTaken.begin(); it != m_PortsTaken.end();) {
        if (!it->m_Waiting && it->m_ClientId == ClientId) {
            LogDebug("Dropping port %d of client %s", it->m_Port, it->m_pClientAddrStr);
            it = m_PortsTaken.erase(it);
        }
        else
            ++it;
    }
}
