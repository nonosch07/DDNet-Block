#ifndef BLOCKWORLDS_COMPONENTS_PORT_PROXY_PORT_PROXY_H
#define BLOCKWORLDS_COMPONENTS_PORT_PROXY_PORT_PROXY_H

#include <engine/shared/protocol.h>

#include <blockworlds/components/core/component.h>

#include <map>

class CPortProxy : public CComponent
{
	DECLARE_COMPONENT(CPortProxy, "portproxy")

	[[nodiscard]] int ClientPort(int ClientId) const;

private:
	struct SPort
	{
		int m_Port = 0;

		std::optional<int> m_ClientId;
		NETADDR m_ClientAddr{};
		const char *m_pClientAddrStr = nullptr;

		bool m_Waiting = false;
		int m_WaitingUntil = 0;
	};

	std::vector<SPort> m_PortsTaken;

	[[nodiscard]] int NextFreePort() const;
	[[nodiscard]] bool IsPortTaken(int Port) const;

protected:
	bool IsDebug() const override;

	void OnTick() override;

	void OnPlayerConnected(int ClientId) override;
	void OnPlayerDropping(int ClientId) override;
};

#endif // BLOCKWORLDS_COMPONENTS_PORT_PROXY_PORT_PROXY_H
