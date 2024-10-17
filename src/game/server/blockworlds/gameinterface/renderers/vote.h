#ifndef GAME_SERVER_BLOCKWORLDS_GAMEINTERFACE_RENDERERS_VOTE_H
#define GAME_SERVER_BLOCKWORLDS_GAMEINTERFACE_RENDERERS_VOTE_H

#include <game/voting.h>

#include "../object.h"

#include "renderer.h"

struct SVoteInput
{
	const char *m_pDesc;
	const char *m_pReason;
};

class CVotes final : public IGameInterfaceRenderer
{
	int m_ClientState[MAX_CLIENTS] = {};

	CHeap m_Heap[MAX_CLIENTS];

	std::list<const char *> m_ClientLines[MAX_CLIENTS];
	std::list<const char *>::iterator m_ClientLinesCursor[MAX_CLIENTS];

public:
	void Init(CGameContext *pGameServer) override;
	void Tick() override;

	void Activate(int ClientID) override;
	void Deactivate(int ClientID) override;

	void Render(int ClientID) override;

	bool OnClientInput(int ClientID, void *pUserData) override;

private:
	void StartVoteGroup(int ClientID);
	void SendVoteGroup(int ClientID);
	void EndVoteGroup(int ClientID);
	void ClearVotes(int ClientID);
	void ProgressVoteOptions(int ClientID);
};

#endif
