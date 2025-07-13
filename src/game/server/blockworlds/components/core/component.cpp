#include "component.h"

#include <engine/server/server.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>

CGameContext *CComponent::GameServer() const { return m_pGameServer; }
CConfig *CComponent::Config() const { return m_pGameServer->Config(); }
IServer *CComponent::Server() const { return m_pGameServer->Server(); }
IConsole *CComponent::Console() const { return m_pGameServer->Console(); }

CComponent::CComponent(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
}

bool CComponent::IsDebug() const
{
	return Config()->m_Debug;
}
