// New Blockworlds voting system with page-based menus and safe, internal actions.
#ifndef BLOCKWORLDS_VOTES_VOTEMANAGER_H
#define BLOCKWORLDS_VOTES_VOTEMANAGER_H

#include <game/server/gamecontext.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class IServer;
class CPlayer;
class CGameContext;

// page-aware, safe voting manager. it renders a menu as vote options and
// maps the exact option texts it sent to internal actions per-client

// only if a player's selection doesn't match a known option do we fall back
// to the engine's real vote handling.
class CVoteManager
{
public:
	CVoteManager() = default;

	// public to simplify helper usage in implementation
	enum class EActionKind
	{
		None = 0,
		Back,
		Close,
		OpenRules,
		OpenLeaderboards,
		OpenLeaderboardCategory, // data = category index (0=Level,1=Blockpoints,2=Killstreaks,3=Clans)
		OpenServerInfos,
		OpenServerInfosTopic, // data = topic index (0=Accounts,1=Clans)
		OpenMapTransfers,
		RedirectToPort, // data = A=port
		OpenCosmetics,
		TogglePassive,
		OpenCosmeticsCategory, // data = category index
		ToggleCosmeticItem, // data = (category index, item index)
		ToggleHideCosmetics,
		SetScoreMode, // data = mode (0=level, 1=blockpoints, 2=time)
		OpenShop,
		OpenShopCategory, // data = shop category index (0=skinmani, 1=gundesign, 2=knockout, 3=utility)
		BuyShopItem, // data = A(category), B(item index)
		// 1on1 duel config actions
		DuelSetPoints, // data = A(new points limit)
		DuelToggleWeapon, // data = A(weapon index 0..5)
		DuelToggleEndlessHook,
		DuelSetTimeLimit, // data = A(new time limit in seconds)
		DuelToggleSpawnMode, // data = A(0=normal, 1=random)
		DuelReady,
	};

	struct SAction
	{
		EActionKind m_Kind{EActionKind::None};
		int m_A{-1};
		int m_B{-1};
	};

	// render the root voting menu for the given player (page-based UI)
	void SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext);

	// handle a click/selection from the voting menu. returns true if it was
	// handled internally (no real server vote triggered). returns false to
	// allow the engine to handle real server vote options.
	bool HandleVote(CPlayer *pPlayer, const std::string &VoteInput, int ClientId, CGameContext *pGameContext);

	// clear transient state for a client (e.g. on disconnect)
	void ClearClient(int ClientId);

	// query helpers
	bool IsAtRoot(int ClientId);

	// force a client onto the DUEL_CONFIG page (used when entering 1on1 config phase)
	void ForceDuelConfigPage(int ClientId, CPlayer *pPlayer, IServer *pServer, CGameContext *pGameContext);
	void RenderCurrentPage(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext);

	// reset the page stack to root (used when leaving duel config flows)
	void NavigateToRoot(int ClientId);

private:
	// exact option text => action mapping for last sent menu for that client
	std::unordered_map<int, std::vector<std::pair<std::string, SAction>>> m_MapByClient;

	// page stack: 0=root, 1=cosmetics-root, >=2=cosmetics-category (value stores category index)
	struct SPage
	{
		// type encodes which builder to use; data holds category index for cosmetics pages.
		enum Type
		{
			ROOT = 0,
			RULES,
			LEADERBOARDS,
			LEADERBOARD_DETAIL, // Data = category index
			SERVER_INFOS,
			SERVER_INFOS_TOPIC, // Data = topic index
			MAP_TRANSFERS,
			COSMETICS_ROOT,
			COSMETICS_CATEGORY,
			SHOP,
			SHOP_CATEGORY, // Data = shop category index
			DUEL_CONFIG, // 1on1 match configuration dashboard
		} m_PageType{ROOT};
		int m_Data{-1};
	};

	std::unordered_map<int, std::vector<SPage>> m_PageStack; // per client

	// rendering helpers
	void BuildRoot(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildRules(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildLeaderboards(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildLeaderboardDetail(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildServerInfos(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildServerInfosTopic(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int TopicIndex, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildMapTransfers(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildCosmeticsRoot(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildCosmeticsCategory(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildShop(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildShopCategory(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);
	void BuildDuelConfig(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions);

	// nav helpers
	void PushPage(int ClientID, SPage::Type T, int Data = -1);
	bool PopPage(int ClientID);
	const std::vector<SPage> &GetStack(int ClientID);
	std::vector<SPage> &GetPageStackMut(int ClientID) { return m_PageStack[ClientID]; }
};

extern CVoteManager g_VoteManager;

#endif // BLOCKWORLDS_VOTES_VOTEMANAGER_H
