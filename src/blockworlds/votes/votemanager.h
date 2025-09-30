#ifndef BLOCKWORLDS_VOTES_VOTEMANAGER_H
#define BLOCKWORLDS_VOTES_VOTEMANAGER_H

#include <game/server/gamecontext.h>
#include <memory>
#include <vector>

class IVoteModule
{
public:
	virtual ~IVoteModule() = default;
	virtual void EnsureInitialized() = 0;
	virtual void SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext) = 0;
	// return true if handled
	virtual bool HandleVote(CPlayer *pPlayer, const std::string &voteInput, int ClientId, CGameContext *pGameContext) = 0;
};

class CVoteManager
{
public:
	CVoteManager();
	void RegisterModule(std::unique_ptr<IVoteModule> pModule);
	void EnsureInitialized();
	void SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext);
	bool HandleVote(CPlayer *pPlayer, const std::string &voteInput, int ClientId, CGameContext *pGameContext);

private:
	std::vector<std::unique_ptr<IVoteModule>> m_vModules;
	bool m_Initialized = false;
};

extern CVoteManager g_VoteManager;

#endif // BLOCKWORLDS_VOTES_VOTEMANAGER_H
