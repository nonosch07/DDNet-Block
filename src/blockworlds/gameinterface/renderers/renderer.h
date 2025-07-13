#ifndef GAME_SERVER_BLOCKWORLDS_GAMEINTERFACE_RENDERER_H
#define GAME_SERVER_BLOCKWORLDS_GAMEINTERFACE_RENDERER_H

#include <engine/shared/protocol.h>

#include "../object.h"

class CGameContext;

class IGameInterfaceRenderer
{
	CGameContext *m_pGameServer = nullptr;

	CGenericTreeElement<CGameInterfaceObject> m_lClientInterface[MAX_CLIENTS] = {};

	bool m_aIsActive[MAX_CLIENTS] = {};

public:
	virtual ~IGameInterfaceRenderer() = default;

	virtual void Init(CGameContext *pGameServer) { m_pGameServer = pGameServer; };
	virtual void Tick() = 0;

	virtual void Activate(int ClientID) { m_aIsActive[ClientID] = true; }
	virtual void Deactivate(int ClientID) { m_aIsActive[ClientID] = false; }

	virtual void Render(int ClientID) = 0;

	virtual bool OnClientInput(int ClientID, void *pUserData) = 0;

	bool IsActive(int ClientID) const { return m_aIsActive[ClientID]; }
	CGenericTreeElement<CGameInterfaceObject> *InterfaceTree(int ClientID) { return &m_lClientInterface[ClientID]; }
	CGameContext *GameServer() const { return m_pGameServer; }
};

#endif
