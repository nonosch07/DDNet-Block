#ifndef GAME_SERVER_BLOCKWORLDS_GAMEINTERFACE_HANDLER_H
#define GAME_SERVER_BLOCKWORLDS_GAMEINTERFACE_HANDLER_H

#include "renderers/renderer.h"

class CGameContext;
struct CNetObj_PlayerInput;

enum
{
	GAMEINT_VOTE,
	NUM_GAME_INTERFACES
};

class CGameInterfaceHandler
{
	IGameInterfaceRenderer *m_aRenderers[NUM_GAME_INTERFACES];

public:
	void Init(CGameContext *pGameServer);
	void Tick();

	void OnClientEnter(int ClientID);
	void OnClientDrop(int ClientID);

	void OnClientDirectInput(int ClientID, void *pInput);
	bool OnVoteNetMessage(int ClientID, void *pInput);
	bool OnCallVote(int ClientID, const char *pDesc, const char *pReason);

	IGameInterfaceRenderer *GetRenderer(int Type) const { return m_aRenderers[Type]; }
};

#endif
