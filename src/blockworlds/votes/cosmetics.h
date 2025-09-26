#ifndef BLOCKWORLDS_VOTES_COSMETICS_H
#define BLOCKWORLDS_VOTES_COSMETICS_H

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <string>
#include <vector>

void SetVoteDescriptionAtIndex(int *pIndex, const char *pStr, CNetMsg_Sv_VoteOptionListAdd *pOptionMsg);

struct CosmeticCategory
{ // will be used for profile's stats & tops / interactive navigation
	const char *Header;
	int NumItems;
	const char **Names;
	const char *(CPlayer::*GetOwned)();
	int (CPlayer::*GetActive)() const;
};

class CosmeticsVoteManager
{
public:
	CosmeticsVoteManager();
	void EnsureCategoriesInitialized();
	void SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext);
	bool HandleVote(CPlayer *pPlayer, const std::string &voteInput, int ClientId, CGameContext *pGameContext);
	void AddCategory(const CosmeticCategory &category);
	void ClearOptions();
	const std::vector<std::string> &GetOptions() const;
	static void CreateStripline(char *pDst, int DstSize, const char *pTitle);
	static void SetVoteDescriptionAtIndex(int *pIndex, const char *pStr, CNetMsg_Sv_VoteOptionListAdd *pOptionMsg);

private:
	std::vector<CosmeticCategory> m_Categories;
	std::vector<std::string> m_Options;
	bool m_CategoriesInitialized = false;
};

#endif // BLOCKWORLDS_VOTES_COSMETICS_H
