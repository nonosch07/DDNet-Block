#ifndef BLOCKWORLDS_VOTES_EXTRAS_H
#define BLOCKWORLDS_VOTES_EXTRAS_H

#include "votemanager.h"

class CVoteExtras : public IVoteModule
{
public:
	CVoteExtras() = default;
	void EnsureInitialized() override;
	void SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext) override;
	bool HandleVote(CPlayer *pPlayer, const std::string &voteInput, int ClientId, CGameContext *pGameContext) override;
};

#endif // BLOCKWORLDS_VOTES_EXTRAS_H
