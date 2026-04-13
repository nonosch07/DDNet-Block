// page-based, safe voting manager implementation - Nouaa
#include "votemanager.h"

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events/1on1.h>
#include <blockworlds/components/oneonone_manager.h>
#include <blockworlds/cosmetics/cosmetics.h>
#include <blockworlds/shop/storemanager.h>
#include <blockworlds/zones/zonemanager.h>
#include <engine/shared/config.h>
#include <game/server/player.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_set>

CVoteManager g_VoteManager;

// string helpers for robust client selection matching
static inline std::string Trim(const std::string &s)
{
	size_t b = 0, e = s.size();
	while(b < e && std::isspace(static_cast<unsigned char>(s[b])))
		++b;
	while(e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
		--e;
	return s.substr(b, e - b);
}

// make it beautiful small-caps
static std::string SmallCaps(const std::string &s)
{
	static const std::unordered_map<char, const char *> MAP = {
		{'A', "\xE1\xB4\x80"}, {'a', "\xE1\xB4\x80"}, // ᴀ
		{'B', "\xCA\x99"}, {'b', "\xCA\x99"}, // ʙ
		{'C', "\xE1\xB4\x84"}, {'c', "\xE1\xB4\x84"}, // ᴄ
		{'D', "\xE1\xB4\x85"}, {'d', "\xE1\xB4\x85"}, // ᴅ
		{'E', "\xE1\xB4\x87"}, {'e', "\xE1\xB4\x87"}, // ᴇ
		{'F', "\xEA\x9C\xB0"}, {'f', "\xEA\x9C\xB0"}, // ꜰ
		{'G', "\xC9\xA2"}, {'g', "\xC9\xA2"}, // ɢ
		{'H', "\xCA\x9C"}, {'h', "\xCA\x9C"}, // ʜ
		{'I', "\xC9\xAA"}, {'i', "\xC9\xAA"}, // ɪ
		{'J', "\xE1\xB4\x8A"}, {'j', "\xE1\xB4\x8A"}, // ᴊ
		{'K', "\xE1\xB4\x8B"}, {'k', "\xE1\xB4\x8B"}, // ᴋ
		{'L', "\xCA\x9F"}, {'l', "\xCA\x9F"}, // ʟ
		{'M', "\xE1\xB4\x8D"}, {'m', "\xE1\xB4\x8D"}, // ᴍ
		{'N', "\xC9\xB4"}, {'n', "\xC9\xB4"}, // ɴ
		{'O', "\xE1\xB4\x8F"}, {'o', "\xE1\xB4\x8F"}, // ᴏ
		{'P', "\xE1\xB4\x98"}, {'p', "\xE1\xB4\x98"}, // ᴘ
		{'Q', "Q"}, {'q', "q"}, // no good small-cap
		{'R', "\xCA\x80"}, {'r', "\xCA\x80"}, // ʀ
		{'S', "\xEA\x9C\xB1"}, {'s', "\xEA\x9C\xB1"}, // ꜱ
		{'T', "\xE1\xB4\x9B"}, {'t', "\xE1\xB4\x9B"}, // ᴛ
		{'U', "\xE1\xB4\x9C"}, {'u', "\xE1\xB4\x9C"}, // ᴜ
		{'V', "\xE1\xB4\xA0"}, {'v', "\xE1\xB4\xA0"}, // ᴠ
		{'W', "\xE1\xB4\xA1"}, {'w', "\xE1\xB4\xA1"}, // ᴡ
		{'X', "X"}, {'x', "x"}, // no good small-cap
		{'Y', "\xCA\x8F"}, {'y', "\xCA\x8F"}, // ʏ
		{'Z', "\xE1\xB4\xA2"}, {'z', "\xE1\xB4\xA2"} // ᴢ
	};

	std::string out;
	out.reserve(s.size() * 2);
	for(unsigned char ch : s)
	{
		auto it = MAP.find((char)ch);
		if(it != MAP.end())
			out.append(it->second);
		else
			out.push_back((char)ch);
	}
	return out;
}

static inline std::string StripPrefix(const std::string &s, const std::string &prefix)
{
	if(s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin()))
		return s.substr(prefix.size());
	return s;
}

static inline std::string StripSuffix(const std::string &s, const std::string &suffix)
{
	if(s.size() >= suffix.size() && std::equal(suffix.begin(), suffix.end(), s.end() - suffix.size()))
		return s.substr(0, s.size() - suffix.size());
	return s;
}

static void AddWithAliases(std::vector<std::pair<std::string, CVoteManager::Action>> &Map,
	std::unordered_set<std::string> &Seen,
	const std::string &Label,
	const CVoteManager::Action &Act)
{
	auto add_once = [&](const std::string &k) {
		if(k.empty())
			return;
		if(Seen.insert(k).second)
			Map.emplace_back(k, Act);
	};

	add_once(Label);

	// generic aliases: trim spaces
	add_once(Trim(Label));

	// remove leading checkbox icons
	add_once(Trim(StripPrefix(Label, "☑ ")));
	add_once(Trim(StripPrefix(Label, "☐ ")));

	// navigation aliases: replace/removing arrows and cross
	add_once(Trim(StripSuffix(Label, " ›")));
	add_once(Trim(StripSuffix(Label, " >")));
	add_once(Trim(StripPrefix(Label, "× "))); // e.g., "× Close" -> "Close"
	add_once(Trim(StripPrefix(Label, "« "))); // e.g., "« Back" -> "Back"

	// case-insensitive variants
	auto to_lower = [](std::string s) {
		for(char &ch : s)
			ch = (char)std::tolower((unsigned char)ch);
		return s;
	};
	// snapshot current aliases and add lowercased equivalents
	std::vector<std::string> current;
	current.reserve(8);
	current.push_back(Label);
	current.push_back(Trim(Label));
	current.push_back(Trim(StripPrefix(Label, "☑ ")));
	current.push_back(Trim(StripPrefix(Label, "☐ ")));
	current.push_back(Trim(StripSuffix(Label, " ›")));
	current.push_back(Trim(StripSuffix(Label, " >")));
	current.push_back(Trim(StripPrefix(Label, "× ")));
	current.push_back(Trim(StripPrefix(Label, "« ")));
	for(const auto &s : current)
		add_once(to_lower(s));
}

// helpers to format option lists
static void SetVoteDescriptionAtIndex(int &Index, const char *pStr, CNetMsg_Sv_VoteOptionListAdd &Msg)
{
	switch(Index)
	{
	case 0: Msg.m_pDescription0 = pStr; break;
	case 1: Msg.m_pDescription1 = pStr; break;
	case 2: Msg.m_pDescription2 = pStr; break;
	case 3: Msg.m_pDescription3 = pStr; break;
	case 4: Msg.m_pDescription4 = pStr; break;
	case 5: Msg.m_pDescription5 = pStr; break;
	case 6: Msg.m_pDescription6 = pStr; break;
	case 7: Msg.m_pDescription7 = pStr; break;
	case 8: Msg.m_pDescription8 = pStr; break;
	case 9: Msg.m_pDescription9 = pStr; break;
	case 10: Msg.m_pDescription10 = pStr; break;
	case 11: Msg.m_pDescription11 = pStr; break;
	case 12: Msg.m_pDescription12 = pStr; break;
	case 13: Msg.m_pDescription13 = pStr; break;
	case 14: Msg.m_pDescription14 = pStr; break;
	default: return;
	}
	++Index;
}

// removed unused CreateStripline helper (was previously used for decorative separators)

void CVoteManager::ClearClient(int ClientId)
{
	m_MapByClient.erase(ClientId);
	m_PageStack.erase(ClientId);
}

void CVoteManager::NavigateToRoot(int ClientId)
{
	auto &Stack = m_PageStack[ClientId];
	Stack.clear();
	Stack.push_back(Page{Page::ROOT, -1});
}

void CVoteManager::SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext)
{
	// Do NOT reset the stack on menu open preserve last page
	// if this is the first time for this client, put him to root page
	if(m_PageStack.find(ClientID) == m_PageStack.end() || m_PageStack[ClientID].empty())
	{
		m_PageStack[ClientID].clear();
		PushPage(ClientID, Page::ROOT);
	}
	if(IsAtRoot(ClientID))
		pGameContext->ProgressVoteOptions(ClientID, true);
	RenderCurrentPage(pPlayer, ClientID, pServer, pGameContext);
}

bool CVoteManager::HandleVote(CPlayer *pPlayer, const std::string &VoteInput, int ClientId, CGameContext *pGameContext)
{
	auto it = m_MapByClient.find(ClientId);
	if(it == m_MapByClient.end())
		return false; // not one of ours, let server handle

	// exact match only so no coll with real server votes
	const auto &Entries = it->second;
	for(const auto &E : Entries)
	{
		if(E.first == VoteInput)
		{
			const Action &A = E.second;

			switch(A.Kind)
			{
			case EActionKind::Back:
				if(PopPage(ClientId))
				{
					pGameContext->ClearVotes(ClientId);
					// make sure server votes are always listed above bw menu when returning to ROOT
					if(IsAtRoot(ClientId))
						pGameContext->ProgressVoteOptions(ClientId, true);
					RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
					return true;
				}
				return true;
			case EActionKind::Close:
			{
				// go to ROOT page inseatd
				auto &Stack = m_PageStack[ClientId];
				Stack.clear();
				Stack.push_back(Page{Page::ROOT, -1});
				pGameContext->ClearVotes(ClientId);
				// ensure server votes are on top at root
				pGameContext->ProgressVoteOptions(ClientId, true);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			}
			case EActionKind::OpenCosmetics:
				PushPage(ClientId, Page::COSMETICS_ROOT);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenLeaderboards:
				PushPage(ClientId, Page::LEADERBOARDS);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenServerInfos:
				PushPage(ClientId, Page::SERVER_INFOS);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenServerInfosTopic:
				PushPage(ClientId, Page::SERVER_INFOS_TOPIC, A.A); // A holds topic index
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenMapTransfers:
				PushPage(ClientId, Page::MAP_TRANSFERS);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::RedirectToPort:
				if(pGameContext && pGameContext->Server())
				{
					int Port = A.A;
					bool DoRedirect = true;
					if(DoRedirect)
						pGameContext->Server()->RedirectClient(ClientId, Port, true);
				}
				return true;
			case EActionKind::OpenRules:
				PushPage(ClientId, Page::RULES);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenLeaderboardCategory:
				if(pPlayer)
				{
					pPlayer->m_CaptureTopToMenu = true;
					pPlayer->m_CaptureTopCategory = A.A; // 0..3
					pPlayer->m_TopMessagesCount = 0;
				}
				PushPage(ClientId, Page::LEADERBOARD_DETAIL, A.A);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				if(pGameContext)
				{
					if(A.A == 0 && pGameContext->Accounts())
						pGameContext->Accounts()->ShowTopLevel(ClientId);
					else if(A.A == 1 && pGameContext->Accounts())
						pGameContext->Accounts()->ShowTopBlockpoints(ClientId);
					else if(A.A == 2 && pGameContext->Accounts())
						pGameContext->Accounts()->ShowTopKillStreak(ClientId);
					else if(A.A == 3 && pGameContext->Clans())
						pGameContext->Clans()->ShowTopClans(ClientId);
				}
				return true;
			case EActionKind::OpenCosmeticsCategory:
				PushPage(ClientId, Page::COSMETICS_CATEGORY, A.A);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::TogglePassive:
				if(pPlayer)
				{
					pPlayer->TogglePassive();
					bool EnabledForMessage = pPlayer->IsUsingPassiveProtection() || pPlayer->IsPassivePendingEnable();
					pGameContext->SendChatTarget(ClientId, EnabledForMessage ? "Passive protection enabled." : "Passive protection disabled.");
					pGameContext->ClearVotes(ClientId);
					if(IsAtRoot(ClientId))
						pGameContext->ProgressVoteOptions(ClientId, true);
					RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				}
				return true;
			case EActionKind::ToggleCosmeticItem:
				if(pGameContext)
				{
					// A=A(category), B=item
					const int Cat = A.A;
					const int Item = A.B;
					bool Changed = false;
					if(Cat == 0) // Skin Manipulations
						Changed = pGameContext->Cosmetics()->ToggleSkinmani(ClientId, CCosmeticsHandler::ms_SkinmaniNames[Item]);
					else if(Cat == 1) // Gun Designs
						Changed = pGameContext->Cosmetics()->ToggleGundesign(ClientId, CCosmeticsHandler::ms_GundesignNames[Item]);
					else if(Cat == 2) // Knockout Effects
						Changed = pGameContext->Cosmetics()->ToggleKnockout(ClientId, CCosmeticsHandler::ms_KnockoutNames[Item]);
					else if(Cat == 3) // VIP Specials
					{
						// VIP specials indices: 0..3 mapped directly
						static const char *s_VipNames[] = {"Ball", "Crown", "Epic Circle", "Halo"};
						Changed = pGameContext->Cosmetics()->ToggleSpecial(ClientId, s_VipNames[Item]);
					}
					if(Changed)
					{
						pGameContext->ClearVotes(ClientId);
						RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
					}
				}
				return true;
			case EActionKind::ToggleHideCosmetics:
				if(pPlayer)
				{
					pPlayer->m_HideCosmetics = !pPlayer->m_HideCosmetics;
					pGameContext->SendChatTarget(ClientId, pPlayer->m_HideCosmetics ? "Cosmetics hidden." : "Cosmetics visible.");
					pGameContext->ClearVotes(ClientId);
					if(IsAtRoot(ClientId))
						pGameContext->ProgressVoteOptions(ClientId, true);
					RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				}
				return true;
			case EActionKind::SetScoreMode:
				if(pPlayer)
				{
					pPlayer->m_ScoreDisplayMode = A.A;
					const char *apModes[] = {"Level", "Blockpoints"};
					char aMsg[128];
					str_format(aMsg, sizeof(aMsg), "Scoreboard now shows: %s", apModes[A.A]);
					pGameContext->SendChatTarget(ClientId, aMsg);
					pGameContext->ClearVotes(ClientId);
					if(IsAtRoot(ClientId))
						pGameContext->ProgressVoteOptions(ClientId, true);
					RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				}
				return true;
			case EActionKind::OpenShop:
				PushPage(ClientId, Page::SHOP);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenShopCategory:
				PushPage(ClientId, Page::SHOP_CATEGORY, A.A);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::BuyShopItem:
				if(pPlayer && pGameContext)
				{
					if(!pPlayer->IsLoggedIn())
					{
						pGameContext->SendChatTarget(ClientId, "You need to be logged in to make purchases.");
					}
					else
					{
						CShop::InstantPurchase(pGameContext, pPlayer, A.A, A.B);
						// Re-render the current shop category page so the item shows as "Owned"
						pGameContext->ClearVotes(ClientId);
						RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
					}
				}
				return true;
			case EActionKind::DuelSetPoints:
			{
				auto mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto match = mgr ? mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(match && match->IsInConfigPhase())
				{
					const bool Changed = match->m_Config.m_PointsLimit != A.A;
					match->m_Config.m_PointsLimit = A.A;
					if(Changed)
						match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					// re-render for both players
					for(int cid : {match->m_Player1ID, match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->GetPlayer(cid);
						if(pP)
						{
							pGameContext->ClearVotes(cid);
							RenderCurrentPage(pP, cid, pGameContext->Server(), pGameContext);
						}
					}
				}
				return true;
			}
			case EActionKind::DuelToggleWeapon:
			{
				auto mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto match = mgr ? mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(match && match->IsInConfigPhase())
				{
					bool Changed = false;
					int w = A.A;
					if(w >= 0 && w < 6)
					{
						const bool Before = match->m_Config.m_aWeapons[w];
						match->m_Config.m_aWeapons[w] = !match->m_Config.m_aWeapons[w];
						Changed = Before != match->m_Config.m_aWeapons[w];
					}
					if(Changed)
						match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					for(int cid : {match->m_Player1ID, match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->GetPlayer(cid);
						if(pP)
						{
							pGameContext->ClearVotes(cid);
							RenderCurrentPage(pP, cid, pGameContext->Server(), pGameContext);
						}
					}
				}
				return true;
			}
			case EActionKind::DuelToggleEndlessHook:
			{
				auto mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto match = mgr ? mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(match && match->IsInConfigPhase())
				{
					match->m_Config.m_EndlessHook = !match->m_Config.m_EndlessHook;
					match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					for(int cid : {match->m_Player1ID, match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->GetPlayer(cid);
						if(pP)
						{
							pGameContext->ClearVotes(cid);
							RenderCurrentPage(pP, cid, pGameContext->Server(), pGameContext);
						}
					}
				}
				return true;
			}
			case EActionKind::DuelSetTimeLimit:
			{
				auto mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto match = mgr ? mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(match && match->IsInConfigPhase())
				{
					const bool Changed = match->m_Config.m_TimeLimit != A.A;
					match->m_Config.m_TimeLimit = A.A;
					if(Changed)
						match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					for(int cid : {match->m_Player1ID, match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->GetPlayer(cid);
						if(pP)
						{
							pGameContext->ClearVotes(cid);
							RenderCurrentPage(pP, cid, pGameContext->Server(), pGameContext);
						}
					}
				}
				return true;
			}
			case EActionKind::DuelToggleSpawnMode:
			{
				auto mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto match = mgr ? mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(match && match->IsInConfigPhase())
				{
					const bool Changed = match->m_Config.m_SpawnMode != A.A;
					match->m_Config.m_SpawnMode = A.A; // 0=normal, 1=random
					if(Changed)
						match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					for(int cid : {match->m_Player1ID, match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->GetPlayer(cid);
						if(pP)
						{
							pGameContext->ClearVotes(cid);
							RenderCurrentPage(pP, cid, pGameContext->Server(), pGameContext);
						}
					}
				}
				return true;
			}
			case EActionKind::DuelReady:
				// Ready is now handled via F3/F4 vote overlay, no-op here
				return true;
			case EActionKind::None:
			default:
				return true;
			}
		}
	}

	// let core handle real server votes
	return false;
}

// ------- Internal helpers -------

void CVoteManager::RenderCurrentPage(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext)
{
	std::vector<std::string> Labels;
	std::vector<Action> Actions;
	Labels.reserve(64);
	Actions.reserve(64);

	const auto &Stack = GetStack(ClientID);
	Page Current = Stack.empty() ? Page{} : Stack.back();

	switch(Current.PageType)
	{
	case Page::ROOT: BuildRoot(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case Page::RULES: BuildRules(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case Page::LEADERBOARDS: BuildLeaderboards(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case Page::LEADERBOARD_DETAIL: BuildLeaderboardDetail(pPlayer, ClientID, pServer, pGameContext, Current.Data, Labels, Actions); break;
	case Page::SERVER_INFOS: BuildServerInfos(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case Page::SERVER_INFOS_TOPIC: BuildServerInfosTopic(pPlayer, ClientID, pServer, pGameContext, Current.Data, Labels, Actions); break;
	case Page::MAP_TRANSFERS: BuildMapTransfers(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case Page::COSMETICS_ROOT: BuildCosmeticsRoot(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case Page::COSMETICS_CATEGORY: BuildCosmeticsCategory(pPlayer, ClientID, pServer, pGameContext, Current.Data, Labels, Actions); break;
	case Page::SHOP: BuildShop(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case Page::SHOP_CATEGORY: BuildShopCategory(pPlayer, ClientID, pServer, pGameContext, Current.Data, Labels, Actions); break;
	case Page::DUEL_CONFIG: BuildDuelConfig(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	}

	// add nav ctrls (skip for DUEL_CONFIG - players can't leave config phase)
	if(Current.PageType != Page::ROOT && Current.PageType != Page::DUEL_CONFIG)
	{
		Labels.insert(Labels.begin(), SmallCaps("« Back"));
		Actions.insert(Actions.begin(), Action{EActionKind::Back, -1, -1});
	}
	if(Current.PageType != Page::ROOT && Current.PageType != Page::DUEL_CONFIG)
	{
		Labels.push_back(SmallCaps("× Main Page"));
		Actions.push_back(Action{EActionKind::Close, -1, -1});
	}

	// beautify: wrap page with box-drawing header/footer
	{
		std::string Title;
		switch(Current.PageType)
		{
		case Page::ROOT: Title = "Blockworlds Menu"; break;
		case Page::LEADERBOARDS: Title = "Leaderboards"; break;
		case Page::RULES: Title = "Rules"; break;
		case Page::LEADERBOARD_DETAIL:
		{
			Title = "Leaderboard";
			break;
		}
		case Page::SERVER_INFOS: Title = "Server Infos"; break;
		case Page::SERVER_INFOS_TOPIC:
		{
			int Topic = Current.Data;
			if(Topic == 0)
				Title = "Account System";
			else if(Topic == 1)
				Title = "Clan System";
			else
				Title = "Info";
			break;
		}
		case Page::MAP_TRANSFERS: Title = "Map Transfers"; break;
		case Page::COSMETICS_ROOT: Title = "Cosmetics"; break;
		case Page::COSMETICS_CATEGORY:
		{
			int Cat = Current.Data;
			if(Cat == 0)
				Title = "Skin Manipulations";
			else if(Cat == 1)
				Title = "Gun Designs";
			else if(Cat == 2)
				Title = "Knockout Effects";
			else if(Cat == 3)
				Title = "VIP Items";
			else
				Title = "Cosmetics";
			break;
		}
		case Page::SHOP: Title = "Shop"; break;
		case Page::SHOP_CATEGORY:
		{
			int Cat = Current.Data;
			if(Cat == CShop::CATEGORY_SKINMANI)
				Title = "Shop - Skin Manipulations";
			else if(Cat == CShop::CATEGORY_GUNDESIGN)
				Title = "Shop - Gun Designs";
			else if(Cat == CShop::CATEGORY_KNOCKOUT)
				Title = "Shop - Knockout Effects";
			else if(Cat == CShop::CATEGORY_UTILITY)
				Title = "Shop - Utilities";
			else
				Title = "Shop";
			break;
		}
		case Page::DUEL_CONFIG:
			Title = "1on1 Config";
			break;
		}
		std::string Caps = SmallCaps(Title);
		std::string Top = std::string("╭─ ") + Caps + " ─────────────────";
		// Bottom dashes must match the runes between the header corners: "─ " + Title + " ─" => len = capsLen + 4
		int capsLen = str_length(Caps.c_str());
		int dashCount = capsLen + 4;
		if(dashCount < 1)
			dashCount = 1;
		std::string Bottom = "╰";
		for(int i = 0; i < dashCount; ++i)
			Bottom += "─";
		Bottom += "────────────";

		Labels.insert(Labels.begin(), Top);
		Actions.insert(Actions.begin(), Action{EActionKind::None, -1, -1});

		// add vertical side bars to all inner lines (excluding top and bottom)
		for(size_t i = 0; i < Labels.size(); ++i)
		{
			if(i == 0)
				continue; // keep top border as-is
			Labels[i] = std::string("│ ") + Labels[i];
		}

		// append bottom
		Labels.push_back(Bottom);
		Actions.push_back(Action{EActionKind::None, -1, -1});
	}

	// store mapping with a few safe aliases to handle client-side string differences
	std::vector<std::pair<std::string, Action>> Map;
	Map.reserve(Labels.size() * 3);
	std::unordered_set<std::string> Seen;
	for(size_t i = 0; i < Labels.size(); ++i)
		AddWithAliases(Map, Seen, Labels[i], Actions[i]);
	m_MapByClient[ClientID] = std::move(Map);

	// send to client in chunks of 15
	int Sent = 0;
	const int Total = (int)Labels.size();
	while(Sent < Total)
	{
		int idx = 0;
		CNetMsg_Sv_VoteOptionListAdd Msg{};
		Msg.m_pDescription0 = "";
		Msg.m_pDescription1 = "";
		Msg.m_pDescription2 = "";
		Msg.m_pDescription3 = "";
		Msg.m_pDescription4 = "";
		Msg.m_pDescription5 = "";
		Msg.m_pDescription6 = "";
		Msg.m_pDescription7 = "";
		Msg.m_pDescription8 = "";
		Msg.m_pDescription9 = "";
		Msg.m_pDescription10 = "";
		Msg.m_pDescription11 = "";
		Msg.m_pDescription12 = "";
		Msg.m_pDescription13 = "";
		Msg.m_pDescription14 = "";
		while(idx < 15 && Sent < Total)
		{
			SetVoteDescriptionAtIndex(idx, Labels[Sent].c_str(), Msg);
			++Sent;
		}
		Msg.m_NumOptions = idx;
		pServer->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
}

void CVoteManager::BuildRoot(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	if(pPlayer)
	{
		bool hasPassive = (pPlayer->m_LocalPassiveDuration > 0) || (pPlayer->IsLoggedIn() && pPlayer->GetPlayerPassive() > 0);
		if(hasPassive)
		{
			std::string PassiveLine;
			if(pPlayer->IsUsingPassiveProtection() || pPlayer->IsPassivePendingEnable())
				PassiveLine = std::string("☑ ") + SmallCaps("Wayblock Protection");
			else
				PassiveLine = std::string("☐ ") + SmallCaps("Wayblock Protection");
			OutLabels.emplace_back(PassiveLine);
			OutActions.emplace_back(Action{EActionKind::TogglePassive});
		}
	}

	// Hide cosmetics toggle
	if(pPlayer)
	{
		std::string HideLine;
		if(pPlayer->m_HideCosmetics)
			HideLine = std::string("☑ ") + SmallCaps("Hide Cosmetics");
		else
			HideLine = std::string("☐ ") + SmallCaps("Hide Cosmetics");
		OutLabels.emplace_back(HideLine);
		OutActions.emplace_back(Action{EActionKind::ToggleHideCosmetics});
	}

	// Score display mode (radio-button style checkboxes)
	if(pPlayer)
	{
		int Mode = pPlayer->m_ScoreDisplayMode;
		if(Mode < 0 || Mode > 1)
			Mode = 0;

		OutLabels.emplace_back(SmallCaps("Score Display:"));
		OutActions.emplace_back(Action{EActionKind::None});

		// Level option
		{
			std::string Line = std::string(Mode == 0 ? "  ☑ " : "  ☐ ") + SmallCaps("Level");
			OutLabels.emplace_back(Line);
			OutActions.emplace_back(Action{EActionKind::SetScoreMode, 0});
		}
		// Blockpoints option
		{
			std::string Line = std::string(Mode == 1 ? "  ☑ " : "  ☐ ") + SmallCaps("Blockpoints");
			OutLabels.emplace_back(Line);
			OutActions.emplace_back(Action{EActionKind::SetScoreMode, 1});
		}
	}

	// ─── separator ───
	OutLabels.emplace_back(SmallCaps("───────────"));
	OutActions.emplace_back(Action{EActionKind::None});

	// rules (always available)
	{
		std::string label = SmallCaps("Rules");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenRules});
	}

	// leaderboards (always available)
	{
		std::string label = SmallCaps("Leaderboards");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenLeaderboards});
	}

	// server infos (always available)
	{
		std::string label = SmallCaps("Server Infos");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenServerInfos});
	}

	{
		std::string label = SmallCaps("Map Transfers");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenMapTransfers});
	}

	// shop (requires login)
	if(pPlayer && pPlayer->IsLoggedIn())
	{
		std::string label = SmallCaps("Shop");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenShop});
	}

	if(pPlayer && pPlayer->IsLoggedIn())
	{
		std::string label = SmallCaps("Cosmetics");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenCosmetics});
	}
	else
	{
		OutLabels.emplace_back(SmallCaps("/login to use cosmetics!"));
		OutActions.emplace_back(Action{EActionKind::None});
	}
	OutLabels.emplace_back(SmallCaps("discord: dsc.gg/bw-tw"));
	OutActions.emplace_back(Action{EActionKind::None});
}

void CVoteManager::BuildMapTransfers(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	bool Any = false;

	auto add_entry = [&](int Port, const char *pName) {
		if(Port <= 0)
			return;
		Any = true;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s (:%d)", pName, Port);
		std::string Line = SmallCaps(aBuf);
		OutLabels.emplace_back(Line);
		OutActions.emplace_back(Action{EActionKind::RedirectToPort, Port});
	};

	add_entry(g_Config.m_SvBlmapV3RoyalPort, "BlmapV3Royal");
	add_entry(g_Config.m_SvStorePort, "Store");
	add_entry(g_Config.m_SvMulteasyPort, "Multeasymap");

	if(!Any)
	{
		OutLabels.emplace_back(SmallCaps("No map transfers configured."));
		OutActions.emplace_back(Action{EActionKind::None});
	}
}

void CVoteManager::BuildRules(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	// send raw (no SmallCaps) to keep packet size minimal
	char *apRuleLines[] = {
		g_Config.m_SvRulesLine1,
		g_Config.m_SvRulesLine2,
		g_Config.m_SvRulesLine3,
		g_Config.m_SvRulesLine4,
		g_Config.m_SvRulesLine5,
		g_Config.m_SvRulesLine6,
		g_Config.m_SvRulesLine7,
		g_Config.m_SvRulesLine8,
		g_Config.m_SvRulesLine9,
		g_Config.m_SvRulesLine10,
	};

	bool Any = false;
	for(auto &pLine : apRuleLines)
	{
		if(pLine && pLine[0])
		{
			OutLabels.emplace_back(pLine);
			OutActions.emplace_back(Action{EActionKind::None});
			Any = true;
		}
	}

	if(!Any)
	{
		OutLabels.emplace_back(SmallCaps("Be nice."));
		OutActions.emplace_back(Action{EActionKind::None});
	}
}

void CVoteManager::BuildLeaderboards(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	{
		std::string label = SmallCaps("Top Level");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenLeaderboardCategory, 0});
	}

	{
		std::string label = SmallCaps("Top Blockpoints");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenLeaderboardCategory, 1});
	}

	{
		std::string label = SmallCaps("Top Killstreaks");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenLeaderboardCategory, 2});
	}

	{
		std::string label = SmallCaps("Top Clans");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenLeaderboardCategory, 3});
	}
}

void CVoteManager::BuildServerInfos(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	// two subpages: Accounts, Clans
	{
		std::string label = SmallCaps("Accounts");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenServerInfosTopic, 0});
	}
	{
		std::string label = SmallCaps("Clans");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenServerInfosTopic, 1});
	}
}

void CVoteManager::BuildServerInfosTopic(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int TopicIndex, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	if(TopicIndex == 0)
	{
		OutLabels.emplace_back("Accounts let you save your progress:");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("Level, EXP, Blockpoints, Cosmetics, Stats, and more.");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Chat commands:"));
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/register <name> <pass> - Create an account");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/login <name> <pass> - Log in");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/logout_account - Log out");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/password <old> <new> - Change password");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/exp - Show your EXP");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/profile [name] - View a profile");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/bp - Show your blockpoints");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/give_bp <player> <amount> - Offer BP transfer");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/accept_bp [player] | /decline_bp [player]");
		OutActions.emplace_back(Action{EActionKind::None});
	}
	else if(TopicIndex == 1)
	{
		OutLabels.emplace_back("Clans let players team up and progress together:");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("Compete on leaderboards, share a name, and manage roles.");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Member Commands:"));
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_create <name> - Create a new clan");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_leave - Leave your clan");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_accept | /clan_decline - Respond to invite");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_exp - Show clan EXP progress");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_list - List clan members");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Co-Leader & Leader:"));
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_invite <player> - Invite a player");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_kick <player> - Kick a member");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Leader Only:"));
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_role <player> <member|coleader> - Set role");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_rename <newname> - Rename clan");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_transfer <player> - Transfer clan leadership");
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back("/clan_delete - Delete your clan");
		OutActions.emplace_back(Action{EActionKind::None});
	}
}
void CVoteManager::BuildLeaderboardDetail(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	const char *pTitle = "";
	switch(CategoryIndex)
	{
	case 0: pTitle = "Top Level"; break;
	case 1: pTitle = "Top Blockpoints"; break;
	case 2: pTitle = "Top Killstreaks"; break;
	case 3: pTitle = "Top Clans"; break;
	default: pTitle = "Leaderboard"; break;
	}

	// generate a symmetric inner box around the content
	auto make_inner_top = [&](const char *pT) {
		return std::string("  ╭─[ ") + SmallCaps(pT) + " ]──────";
	};
	auto make_inner_bottom = [&](const char *pT) {
		// match the number of runes between the two corners in the header:
		// header between corners is: "─[ " + SmallCaps(title) + " ]─" -> len = capsLen + 6 - might have fucked something up somewhere idk
		std::string caps = SmallCaps(pT);
		int capsLen = str_length(caps.c_str());
		int dashCount = capsLen + 6;
		if(dashCount < 1)
			dashCount = 1;
		std::string s = "  ╰";
		for(int i = 0; i < dashCount; ++i)
			s += "─";
		s += "╯";
		return s;
	};

	if(!pPlayer)
	{
		// spacer above inner box
		OutLabels.emplace_back("");
		OutActions.emplace_back(Action{EActionKind::None});

		// inner boxed section header
		{
			std::string top = make_inner_top(pTitle);
			OutLabels.emplace_back(top);
			OutActions.emplace_back(Action{EActionKind::None});
		}

		// content
		OutLabels.emplace_back(std::string("  │  ") + SmallCaps("Unavailable."));
		OutActions.emplace_back(Action{EActionKind::None});

		// inner boxed section footer sized like the header
		OutLabels.emplace_back(make_inner_bottom(pTitle));
		OutActions.emplace_back(Action{EActionKind::None});

		// spacer below inner box
		OutLabels.emplace_back("");
		OutActions.emplace_back(Action{EActionKind::None});
		return;
	}

	if(pPlayer->m_TopMessagesCount <= 0 || pPlayer->m_CaptureTopCategory != CategoryIndex)
	{
		// spacer above inner box
		OutLabels.emplace_back("");
		OutActions.emplace_back(Action{EActionKind::None});

		// inner boxed section header
		{
			std::string top = make_inner_top(pTitle);
			OutLabels.emplace_back(top);
			OutActions.emplace_back(Action{EActionKind::None});
		}

		// content placeholder
		OutLabels.emplace_back(std::string("  │  ") + SmallCaps("Loading..."));
		OutActions.emplace_back(Action{EActionKind::None});

		// inner boxed section footer sized like the header
		OutLabels.emplace_back(make_inner_bottom(pTitle));
		OutActions.emplace_back(Action{EActionKind::None});

		// spacer below inner box
		OutLabels.emplace_back("");
		OutActions.emplace_back(Action{EActionKind::None});
		return;
	}

	// spacer above inner box
	OutLabels.emplace_back("");
	OutActions.emplace_back(Action{EActionKind::None});

	// inner boxed section header
	{
		std::string top = make_inner_top(pTitle);
		OutLabels.emplace_back(top);
		OutActions.emplace_back(Action{EActionKind::None});
	}

	// content lines (skip decorative chat headers like "------------ Global Top ... ------------")
	auto is_decor_header = [&](const char *p) {
		if(!p)
			return false;
		std::string t = Trim(std::string(p));
		if(t.empty())
			return false;
		int n = (int)t.size();
		int lead = 0;
		while(lead < n && t[lead] == '-')
			lead++;
		int trail = 0;
		while(trail < n && t[n - 1 - trail] == '-')
			trail++;
		if(lead >= 3 && trail >= 3)
			return true; // looks like dashed banner
		// also tolerate explicit phrase check
		std::string low = t;
		for(char &ch : low)
			ch = (char)std::tolower((unsigned char)ch);
		if(low.find("global top") != std::string::npos)
			return true;
		return false;
	};

	for(int i = 0; i < pPlayer->m_TopMessagesCount; ++i)
	{
		const char *pLine = pPlayer->m_aTopMessages[i];
		if(!pLine || !pLine[0])
			break;
		if(is_decor_header(pLine))
			continue;
		OutLabels.emplace_back(std::string("  │  ") + pLine);
		OutActions.emplace_back(Action{EActionKind::None});
	}

	// inner boxed section footer sized like the header
	OutLabels.emplace_back(make_inner_bottom(pTitle));
	OutActions.emplace_back(Action{EActionKind::None});

	// spacer below inner box
	OutLabels.emplace_back("");
	OutActions.emplace_back(Action{EActionKind::None});
}

void CVoteManager::BuildCosmeticsRoot(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	// guard: only for logged-in players
	if(!(pPlayer && pPlayer->IsLoggedIn()))
	{
		OutLabels.emplace_back(SmallCaps("/login to use cosmetics!"));
		OutActions.emplace_back(Action{EActionKind::None});
		return;
	}

	bool AnyOwned = false;

	// helper to render one section
	auto RenderSection = [&](const char *pTitle, const char **ppNames, int Count, int CategoryIndex, int ActiveIndex, auto HasFn) {
		// collect owned indices
		std::vector<int> OwnedIndices;
		for(int i = 0; i < Count; ++i)
		{
			if(HasFn(i))
				OwnedIndices.push_back(i);
		}
		if(OwnedIndices.empty())
			return;

		AnyOwned = true;

		// section header
		OutLabels.emplace_back("─── " + SmallCaps(pTitle) + " ───");
		OutActions.emplace_back(Action{EActionKind::None});

		for(int i : OwnedIndices)
		{
			const bool IsActive = (ActiveIndex == i);
			std::string name = (ppNames && ppNames[i] ? ppNames[i] : "");
			std::string Line = std::string(IsActive ? "☑ " : "☐ ") + SmallCaps(name);
			OutLabels.emplace_back(Line);
			OutActions.emplace_back(Action{EActionKind::ToggleCosmeticItem, CategoryIndex, i});
		}
	};

	int Cid = pPlayer->GetCid();

	// Skin Manipulations (category 0)
	RenderSection("Skin Manipulations",
		CCosmeticsHandler::ms_SkinmaniNames, CCosmeticsHandler::NUM_SKINMANIS,
		0, pPlayer->GetSkinMani(),
		[&](int i) { return pGameContext->Cosmetics()->HasSkinmani(Cid, i); });

	// Gun Designs (category 1)
	RenderSection("Gun Designs",
		CCosmeticsHandler::ms_GundesignNames, CCosmeticsHandler::NUM_GUNDESIGNS,
		1, pPlayer->GetGunDesign(),
		[&](int i) { return pGameContext->Cosmetics()->HasGundesign(Cid, i); });

	// Knockout Effects (category 2)
	RenderSection("Knockout Effects",
		CCosmeticsHandler::ms_KnockoutNames, CCosmeticsHandler::NUM_KNOCKOUTS,
		2, pPlayer->GetKnockout(),
		[&](int i) { return pGameContext->Cosmetics()->HasKnockoutEffect(Cid, i); });

	// VIP Items (category 3) — only for VIP players
	if(pPlayer->GetPlayerVip() || (pServer && pServer->ClientAuthed(Cid)))
	{
		static const char *s_Vip[] = {"Ball", "Crown", "Epic Circle", "Halo"};
		RenderSection("VIP Items",
			s_Vip, 4,
			3, pPlayer->GetCurrentSpecial(),
			[&](int i) { return pGameContext->Cosmetics()->HasSpecial(Cid, i); });
	}

	if(!AnyOwned)
	{
		OutLabels.emplace_back(SmallCaps("No owned cosmetics."));
		OutActions.emplace_back(Action{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Buy some in the shop!"));
		OutActions.emplace_back(Action{EActionKind::None});
	}
}

void CVoteManager::BuildCosmeticsCategory(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	// always render a header
	const char **ppNames = nullptr;
	int Count = 0;

	if(!(pPlayer && pGameContext && pPlayer->IsLoggedIn()))
	{
		OutLabels.emplace_back(SmallCaps("/login to use cosmetics!"));
		OutActions.emplace_back(Action{EActionKind::None});
		return;
	}

	if(CategoryIndex == 0)
	{
		ppNames = CCosmeticsHandler::ms_SkinmaniNames;
		Count = CCosmeticsHandler::NUM_SKINMANIS;
	}
	else if(CategoryIndex == 1)
	{
		ppNames = CCosmeticsHandler::ms_GundesignNames;
		Count = CCosmeticsHandler::NUM_GUNDESIGNS;
	}
	else if(CategoryIndex == 2)
	{
		ppNames = CCosmeticsHandler::ms_KnockoutNames;
		Count = CCosmeticsHandler::NUM_KNOCKOUTS;
	}
	else if(CategoryIndex == 3)
	{
		// require player to be VIP
		if(!(pPlayer->GetPlayerVip() || (pServer && pServer->ClientAuthed(pPlayer->GetCid()))))
		{
			OutLabels.emplace_back(SmallCaps("You are not VIP."));
			OutActions.emplace_back(Action{EActionKind::None});
			OutLabels.emplace_back("Contact an admin if you're interested in contributing!");
			OutActions.emplace_back(Action{EActionKind::None});
			OutLabels.emplace_back("discord: dsc.gg/bw-tw");
			OutActions.emplace_back(Action{EActionKind::None});
			return; // stop rendering items
		}
		static const char *s_Vip[] = {"Ball", "Crown", "Epic Circle", "Halo"};
		ppNames = s_Vip;
		Count = 4;
	}

	// collect owned items and render with selection checkbox
	int Active = -1;
	if(CategoryIndex == 0)
		Active = pPlayer->GetSkinMani();
	else if(CategoryIndex == 1)
		Active = pPlayer->GetGunDesign();
	else if(CategoryIndex == 2)
		Active = pPlayer->GetKnockout();
	else if(CategoryIndex == 3)
		Active = pPlayer->GetCurrentSpecial();

	std::unordered_set<int> Owned;
	for(int i = 0; i < Count; ++i)
	{
		bool Has = false;
		if(CategoryIndex == 0)
			Has = pGameContext->Cosmetics()->HasSkinmani(pPlayer->GetCid(), i);
		else if(CategoryIndex == 1)
			Has = pGameContext->Cosmetics()->HasGundesign(pPlayer->GetCid(), i);
		else if(CategoryIndex == 2)
			Has = pGameContext->Cosmetics()->HasKnockoutEffect(pPlayer->GetCid(), i);
		else if(CategoryIndex == 3)
			Has = pGameContext->Cosmetics()->HasSpecial(pPlayer->GetCid(), i);
		if(Has)
			Owned.insert(i);
	}

	if(Owned.empty())
	{
		OutLabels.emplace_back(SmallCaps("No owned items."));
		OutLabels.emplace_back(SmallCaps("Buy some in the store!"));
		OutActions.emplace_back(Action{EActionKind::None});
		OutActions.emplace_back(Action{EActionKind::None});
	}

	for(int i = 0; i < Count; ++i)
	{
		if(!Owned.count(i))
			continue;
		const bool IsActive = (Active == i);
		std::string name = (ppNames && ppNames[i] ? ppNames[i] : "");
		std::string Line = std::string(IsActive ? "☑ " : "☐ ") + SmallCaps(name);
		OutLabels.emplace_back(Line);
		OutActions.emplace_back(Action{EActionKind::ToggleCosmeticItem, CategoryIndex, i});
	}
}

void CVoteManager::BuildShop(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	if(!(pPlayer && pPlayer->IsLoggedIn()))
	{
		OutLabels.emplace_back(SmallCaps("/login to use the shop!"));
		OutActions.emplace_back(Action{EActionKind::None});
		return;
	}

	// show player's blockpoints balance
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Your BP: %d", pPlayer->GetPlayerBlockpoints());
		OutLabels.emplace_back(aBuf);
		OutActions.emplace_back(Action{EActionKind::None});

		OutLabels.emplace_back("───────────");
		OutActions.emplace_back(Action{EActionKind::None});
	}

	// shop categories (excluding VIP)
	struct ShopCat
	{
		const char *Name;
		int CategoryIndex; // maps to CShop::CATEGORY_*
	} Cats[] = {
		{"Utilities", CShop::CATEGORY_UTILITY},
		{"Skin Manipulations", CShop::CATEGORY_SKINMANI},
		{"Gun Designs", CShop::CATEGORY_GUNDESIGN},
		{"Knockout Effects", CShop::CATEGORY_KNOCKOUT},
	};

	for(const auto &C : Cats)
	{
		std::string label = C.Name;
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenShopCategory, C.CategoryIndex});
	}
}

void CVoteManager::BuildShopCategory(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	if(!(pPlayer && pGameContext && pPlayer->IsLoggedIn()))
	{
		OutLabels.emplace_back("/login to use the shop!");
		OutActions.emplace_back(Action{EActionKind::None});
		return;
	}

	CCosmeticsHandler *pCos = pGameContext->Cosmetics();
	if(!pCos)
		return;

	// show player's blockpoints balance at top
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Your BP: %d  |  Level: %d", pPlayer->GetPlayerBlockpoints(), pPlayer->GetPlayerLevel());
		OutLabels.emplace_back(aBuf);
		OutActions.emplace_back(Action{EActionKind::None});

		OutLabels.emplace_back("───────────");
		OutActions.emplace_back(Action{EActionKind::None});
	}

	// iterate over items in the category that have valid shop info (excludes VIP)
	int Count = 0;
	const char **ppNames = nullptr;

	if(CategoryIndex == CShop::CATEGORY_SKINMANI)
	{
		Count = CCosmeticsHandler::NUM_SKINMANIS;
		ppNames = CCosmeticsHandler::ms_SkinmaniNames;
	}
	else if(CategoryIndex == CShop::CATEGORY_GUNDESIGN)
	{
		Count = CCosmeticsHandler::NUM_GUNDESIGNS;
		ppNames = CCosmeticsHandler::ms_GundesignNames;
	}
	else if(CategoryIndex == CShop::CATEGORY_KNOCKOUT)
	{
		Count = CCosmeticsHandler::NUM_KNOCKOUTS;
		ppNames = CCosmeticsHandler::ms_KnockoutNames;
	}
	else if(CategoryIndex == CShop::CATEGORY_UTILITY)
	{
		Count = CCosmeticsHandler::NUM_UTILITY_ITEMS;
		ppNames = nullptr; // utility items don't have a static name array
	}

	bool AnyItems = false;
	for(int i = 0; i < Count; ++i)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		bool HasInfo = false;

		if(CategoryIndex == CShop::CATEGORY_SKINMANI)
			HasInfo = pCos->ShopInfoSkinmani(i, Price, Level, PreviewPos);
		else if(CategoryIndex == CShop::CATEGORY_GUNDESIGN)
			HasInfo = pCos->ShopInfoGundesign(i, Price, Level, PreviewPos);
		else if(CategoryIndex == CShop::CATEGORY_KNOCKOUT)
			HasInfo = pCos->ShopInfoKnockout(i, Price, Level, PreviewPos);
		else if(CategoryIndex == CShop::CATEGORY_UTILITY)
			HasInfo = pCos->ShopInfoUtility(i, Price, Level, PreviewPos);

		if(!HasInfo)
			continue;

		// check if player already owns this item
		bool Owned = false;
		if(CategoryIndex == CShop::CATEGORY_SKINMANI)
			Owned = pCos->HasSkinmani(pPlayer->GetCid(), i);
		else if(CategoryIndex == CShop::CATEGORY_GUNDESIGN)
			Owned = pCos->HasGundesign(pPlayer->GetCid(), i);
		else if(CategoryIndex == CShop::CATEGORY_KNOCKOUT)
			Owned = pCos->HasKnockoutEffect(pPlayer->GetCid(), i);

		// get item name
		const char *pName = nullptr;
		if(CategoryIndex == CShop::CATEGORY_UTILITY)
		{
			if(i == 0)
				pName = "Weapon Kit";
			else if(i == 1)
				pName = "Deathnote Page";
			else if(i == 2)
				pName = "Passive Remover";
			else
				pName = "Utility Item";
		}
		else if(ppNames)
		{
			pName = ppNames[i];
		}
		if(!pName)
			pName = "???";

		char aBuf[256];
		if(Owned)
			str_format(aBuf, sizeof(aBuf), "%s - Owned", pName);
		else
			str_format(aBuf, sizeof(aBuf), "%s - %d BP (Lvl %d)", pName, Price, Level);

		OutLabels.emplace_back(aBuf);

		if(Owned)
			OutActions.emplace_back(Action{EActionKind::None}); // already owned, no action
		else
			OutActions.emplace_back(Action{EActionKind::BuyShopItem, CategoryIndex, i});

		AnyItems = true;
	}

	if(!AnyItems)
	{
		OutLabels.emplace_back("No items available.");
		OutActions.emplace_back(Action{EActionKind::None});
	}
}

void CVoteManager::PushPage(int ClientID, Page::Type T, int Data)
{
	m_PageStack[ClientID].push_back(Page{T, Data});
}

bool CVoteManager::PopPage(int ClientID)
{
	auto it = m_PageStack.find(ClientID);
	if(it == m_PageStack.end() || it->second.empty())
		return false;
	it->second.pop_back();
	if(it->second.empty())
		it->second.push_back(Page{Page::ROOT, -1});
	return true;
}

const std::vector<CVoteManager::Page> &CVoteManager::GetStack(int ClientID)
{
	return m_PageStack[ClientID];
}

bool CVoteManager::IsAtRoot(int ClientId)
{
	auto it = m_PageStack.find(ClientId);
	if(it == m_PageStack.end() || it->second.empty())
		return true;
	return it->second.back().PageType == Page::ROOT;
}

void CVoteManager::BuildDuelConfig(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	auto mgr = g_ComponentRegistry.Get<COneOnOneManager>();
	auto match = mgr ? mgr->GetMatchForPlayer(ClientID) : nullptr;
	if(!match || !match->IsInConfigPhase())
	{
		OutLabels.emplace_back(SmallCaps("No active duel config."));
		OutActions.emplace_back(Action{EActionKind::None});
		return;
	}

	const SMatchConfig &cfg = match->m_Config;

	// opponent name
	int opponentId = (ClientID == match->m_Player1ID) ? match->m_Player2ID : match->m_Player1ID;
	const char *pOpponentName = pServer->ClientName(opponentId);
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "vs %s", pOpponentName);
		OutLabels.emplace_back(SmallCaps(aBuf));
		OutActions.emplace_back(Action{EActionKind::None});
	}

	// warmup timer
	if(match->m_ConfigStartTick > 0)
	{
		int elapsed = (int)((pServer->Tick() - match->m_ConfigStartTick) / pServer->TickSpeed());
		int remaining = g_Config.m_Sv1on1WarmupSeconds - elapsed;
		if(remaining < 0)
			remaining = 0;
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Warmup: %ds", remaining);
		OutLabels.emplace_back(SmallCaps(aBuf));
		OutActions.emplace_back(Action{EActionKind::None});
	}

	// separator
	OutLabels.emplace_back(SmallCaps("───────────"));
	OutActions.emplace_back(Action{EActionKind::None});

	// ── Points Limit (radio checkboxes) ──
	{
		static const int s_PointOptions[] = {3, 10, 20, 30, 50};
		static const int s_NumOptions = 5;

		OutLabels.emplace_back(SmallCaps("Points:"));
		OutActions.emplace_back(Action{EActionKind::None});
		for(int i = 0; i < s_NumOptions; i++)
		{
			bool selected = (cfg.m_PointsLimit == s_PointOptions[i]);
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "  %s %d", selected ? "\xe2\x98\x91" : "\xe2\x98\x90", s_PointOptions[i]);
			OutLabels.emplace_back(aBuf);
			OutActions.emplace_back(Action{EActionKind::DuelSetPoints, s_PointOptions[i]});
		}
	}

	// ── Time Limit (radio checkboxes) ──
	{
		static const int s_TimeOptions[] = {0, 120, 300, 600};
		static const char *s_TimeNames[] = {"No Limit", "2 min", "5 min", "10 min"};
		static const int s_NumOptions = 4;

		OutLabels.emplace_back(SmallCaps("Time Limit:"));
		OutActions.emplace_back(Action{EActionKind::None});
		for(int i = 0; i < s_NumOptions; i++)
		{
			bool selected = (cfg.m_TimeLimit == s_TimeOptions[i]);
			std::string line = std::string(selected ? "  \xe2\x98\x91 " : "  \xe2\x98\x90 ") + SmallCaps(s_TimeNames[i]);
			OutLabels.emplace_back(line);
			OutActions.emplace_back(Action{EActionKind::DuelSetTimeLimit, s_TimeOptions[i]});
		}
	}

	// ── Endless Hook ──
	{
		std::string line = std::string(cfg.m_EndlessHook ? "\xe2\x98\x91 " : "\xe2\x98\x90 ") + SmallCaps("Endless Hook");
		OutLabels.emplace_back(line);
		OutActions.emplace_back(Action{EActionKind::DuelToggleEndlessHook});
	}

	// ── Spawn Mode ──
	{
		OutLabels.emplace_back(SmallCaps("Spawn:"));
		OutActions.emplace_back(Action{EActionKind::None});

		bool isNormal = (cfg.m_SpawnMode == 0);
		OutLabels.emplace_back(std::string(isNormal ? "  \xe2\x98\x91 " : "  \xe2\x98\x90 ") + SmallCaps("Normal"));
		OutActions.emplace_back(isNormal ? Action{EActionKind::None} : Action{EActionKind::DuelToggleSpawnMode, 0});

		bool isRandom = (cfg.m_SpawnMode == 1);
		OutLabels.emplace_back(std::string(isRandom ? "  \xe2\x98\x91 " : "  \xe2\x98\x90 ") + SmallCaps("Random"));
		OutActions.emplace_back(isRandom ? Action{EActionKind::None} : Action{EActionKind::DuelToggleSpawnMode, 1});
	}

	// separator
	OutLabels.emplace_back(SmallCaps("───────────"));
	OutActions.emplace_back(Action{EActionKind::None});

	// ── Weapons ──
	OutLabels.emplace_back(SmallCaps("Weapons:"));
	OutActions.emplace_back(Action{EActionKind::None});

	static const char *s_WeaponNames[] = {"Hammer", "Gun", "Shotgun", "Grenade", "Laser", "Ninja"};
	for(int w = 0; w < 6; w++)
	{
		std::string line = std::string(cfg.m_aWeapons[w] ? "  ☑ " : "  ☐ ") + SmallCaps(s_WeaponNames[w]);
		OutLabels.emplace_back(line);
		OutActions.emplace_back(Action{EActionKind::DuelToggleWeapon, w});
	}

	// separator
	OutLabels.emplace_back(SmallCaps("───────────"));
	OutActions.emplace_back(Action{EActionKind::None});

	// ── Vote Status ──
	{
		const char *pP1Name = pServer->ClientName(match->m_Player1ID);
		const char *pP2Name = pServer->ClientName(match->m_Player2ID);

		auto VoteLabel = [](int v) -> const char * {
			if(v == 1)
				return "\xe2\x9c\x93 Start";
			if(v == -1)
				return "\xe2\x9c\x97 Cancel";
			return "Waiting...";
		};

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s: %s", pP1Name, VoteLabel(match->m_aDuelVote[0]));
		OutLabels.emplace_back(SmallCaps(aBuf));
		OutActions.emplace_back(Action{EActionKind::None});

		str_format(aBuf, sizeof(aBuf), "%s: %s", pP2Name, VoteLabel(match->m_aDuelVote[1]));
		OutLabels.emplace_back(SmallCaps(aBuf));
		OutActions.emplace_back(Action{EActionKind::None});

		OutLabels.emplace_back(SmallCaps("F3 = Start  |  F4 = Cancel"));
		OutActions.emplace_back(Action{EActionKind::None});
	}
}

void CVoteManager::ForceDuelConfigPage(int ClientId, CPlayer *pPlayer, IServer *pServer, CGameContext *pGameContext)
{
	// replace entire page stack with DUEL_CONFIG
	auto &Stack = m_PageStack[ClientId];
	Stack.clear();
	Stack.push_back(Page{Page::DUEL_CONFIG, -1});

	// clear and re-render
	pGameContext->ClearVotes(ClientId);
	RenderCurrentPage(pPlayer, ClientId, pServer, pGameContext);
}
