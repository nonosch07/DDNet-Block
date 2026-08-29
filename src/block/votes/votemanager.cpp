// page-based, safe voting manager implementation - Nouaa
#include "votemanager.h"

#include <engine/shared/config.h>

#include <game/server/player.h>

#include <block/components/core/component_registry.h>
#include <block/components/events/1on1.h>
#include <block/components/oneonone_manager.h>
#include <block/context.h>
#include <block/cosmetics/cosmetics.h>
#include <block/shop/storemanager.h>
#include <block/zones/zonemanager.h>

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
static const int VOTE_BOX_WIDTH = 21;

static int Utf8Length(const std::string &Str)
{
	int Count = 0;
	for(char c : Str)
		if((static_cast<unsigned char>(c) & 0xC0) != 0x80)
			++Count;
	return Count;
}

static std::string DropLastUtf8(const std::string &Str)
{
	if(Str.empty())
		return Str;
	size_t End = Str.size() - 1;
	while(End > 0 && (static_cast<unsigned char>(Str[End]) & 0xC0) == 0x80)
		--End;
	return Str.substr(0, End);
}

static std::string TruncateForVote(const std::string &Str)
{
	const size_t Max = VOTE_DESC_LENGTH - 1; // room for the terminator
	if(Str.size() <= Max)
		return Str;
	size_t End = Max;
	// step back off any continuation byte so the cut lands on a boundary
	while(End > 0 && (static_cast<unsigned char>(Str[End]) & 0xC0) == 0x80)
		--End;
	return Str.substr(0, End);
}

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

	std::string Out;
	Out.reserve(s.size() * 2);
	for(unsigned char Ch : s)
	{
		auto It = MAP.find((char)Ch);
		if(It != MAP.end())
			Out.append(It->second);
		else
			Out.push_back((char)Ch);
	}
	return Out;
}

static inline std::string StripPrefix(const std::string &s, const std::string &Prefix)
{
	if(s.size() >= Prefix.size() && std::equal(Prefix.begin(), Prefix.end(), s.begin()))
		return s.substr(Prefix.size());
	return s;
}

static inline std::string StripSuffix(const std::string &s, const std::string &Suffix)
{
	if(s.size() >= Suffix.size() && std::equal(Suffix.begin(), Suffix.end(), s.end() - Suffix.size()))
		return s.substr(0, s.size() - Suffix.size());
	return s;
}

static void AddWithAliases(std::vector<std::pair<std::string, CVoteManager::SAction>> &Map,
	std::unordered_set<std::string> &Seen,
	const std::string &Label,
	const CVoteManager::SAction &Act)
{
	auto AddOnce = [&](const std::string &k) {
		if(k.empty())
			return;
		if(Seen.insert(k).second)
			Map.emplace_back(k, Act);
	};

	AddOnce(Label);

	// generic aliases: trim spaces
	AddOnce(Trim(Label));

	// remove leading checkbox icons
	AddOnce(Trim(StripPrefix(Label, "☑ ")));
	AddOnce(Trim(StripPrefix(Label, "☐ ")));

	// navigation aliases: replace/removing arrows and cross
	AddOnce(Trim(StripSuffix(Label, " ›")));
	AddOnce(Trim(StripSuffix(Label, " >")));
	AddOnce(Trim(StripPrefix(Label, "× "))); // e.g., "× Close" -> "Close"
	AddOnce(Trim(StripPrefix(Label, "« "))); // e.g., "« Back" -> "Back"

	// case-insensitive variants
	auto ToLower = [](std::string s) {
		for(char &Ch : s)
			Ch = (char)std::tolower((unsigned char)Ch);
		return s;
	};
	// snapshot current aliases and add lowercased equivalents
	std::vector<std::string> Current;
	Current.reserve(8);
	Current.push_back(Label);
	Current.push_back(Trim(Label));
	Current.push_back(Trim(StripPrefix(Label, "☑ ")));
	Current.push_back(Trim(StripPrefix(Label, "☐ ")));
	Current.push_back(Trim(StripSuffix(Label, " ›")));
	Current.push_back(Trim(StripSuffix(Label, " >")));
	Current.push_back(Trim(StripPrefix(Label, "× ")));
	Current.push_back(Trim(StripPrefix(Label, "« ")));
	for(const auto &s : Current)
		AddOnce(ToLower(s));
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
	Stack.push_back(SPage{SPage::ROOT, -1});
}

void CVoteManager::BuildBoxBorders(const std::string &Title, std::string &Top, std::string &Bottom)
{
	// the box sizes itself to the title rather than padding to a fixed width:
	// padding forced long titles to be cut mid-word ("Shop - Skin Man"). titles
	// that don't fit are shortened at call site instead
	std::string Caps = SmallCaps(Title);
	// "╭─ " + title + " ─" is title + 5 characters
	while(Utf8Length(Caps) > VOTE_BOX_WIDTH - 5)
		Caps = DropLastUtf8(Caps);

	Top = "╭─ " + Caps + " ─";

	// the footer spans the same number of characters as the header
	Bottom = "╰";
	for(int i = 1; i < Utf8Length(Top); ++i)
		Bottom += "─";
}

void CVoteManager::SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext)
{
	// Do NOT reset the stack on menu open preserve last page
	// if this is the first time for this client, put him to root page
	if(m_PageStack.find(ClientID) == m_PageStack.end() || m_PageStack[ClientID].empty())
	{
		m_PageStack[ClientID].clear();
		PushPage(ClientID, SPage::ROOT);
	}
	if(IsAtRoot(ClientID))
		pGameContext->ProgressVoteOptions(ClientID, true);
	RenderCurrentPage(pPlayer, ClientID, pServer, pGameContext);
}

bool CVoteManager::HandleVote(CPlayer *pPlayer, const std::string &VoteInput, int ClientId, CGameContext *pGameContext)
{
	auto It = m_MapByClient.find(ClientId);
	if(It == m_MapByClient.end())
		return false; // not one of ours, let server handle

	// exact match only so no coll with real server votes
	const auto &Entries = It->second;
	for(const auto &E : Entries)
	{
		if(E.first == VoteInput)
		{
			const SAction &A = E.second;

			switch(A.m_Kind)
			{
			case EActionKind::Back:
				if(PopPage(ClientId))
				{
					pGameContext->Block().ClearVotes(ClientId);
					// make sure server votes are always listed above the block menu when returning to ROOT
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
				Stack.push_back(SPage{SPage::ROOT, -1});
				pGameContext->Block().ClearVotes(ClientId);
				// ensure server votes are on top at root
				pGameContext->ProgressVoteOptions(ClientId, true);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			}
			case EActionKind::OpenCosmetics:
				PushPage(ClientId, SPage::COSMETICS_ROOT);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenLeaderboards:
				PushPage(ClientId, SPage::LEADERBOARDS);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenServerInfos:
				PushPage(ClientId, SPage::SERVER_INFOS);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenServerVotes:
				PushPage(ClientId, SPage::SERVER_VOTES);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::ServerVote:
				// not ours: let CGameContext::OnCallVoteNetMessage run the real vote
				return false;
			case EActionKind::OpenServerInfosTopic:
				PushPage(ClientId, SPage::SERVER_INFOS_TOPIC, A.m_A); // A holds topic index
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenMapTransfers:
				PushPage(ClientId, SPage::MAP_TRANSFERS);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::RedirectToPort:
				if(pGameContext && pGameContext->Server())
				{
					int Port = A.m_A;
					bool DoRedirect = true;
					if(DoRedirect)
						pGameContext->Block().RedirectClient(ClientId, Port);
				}
				return true;
			case EActionKind::OpenRules:
				PushPage(ClientId, SPage::RULES);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenLeaderboardCategory:
				if(pPlayer)
				{
					pPlayer->Block().m_CaptureTopToMenu = true;
					pPlayer->Block().m_CaptureTopCategory = A.m_A; // 0..3
					pPlayer->Block().m_TopMessagesCount = 0;
				}
				PushPage(ClientId, SPage::LEADERBOARD_DETAIL, A.m_A);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				if(pGameContext)
				{
					if(A.m_A == 0 && pGameContext->Block().Accounts())
						pGameContext->Block().Accounts()->ShowTopLevel(ClientId);
					else if(A.m_A == 1 && pGameContext->Block().Accounts())
						pGameContext->Block().Accounts()->ShowTopBlockpoints(ClientId);
					else if(A.m_A == 2 && pGameContext->Block().Accounts())
						pGameContext->Block().Accounts()->ShowTopKillStreak(ClientId);
					else if(A.m_A == 3 && pGameContext->Block().Clans())
						pGameContext->Block().Clans()->ShowTopClans(ClientId);
				}
				return true;
			case EActionKind::OpenCosmeticsCategory:
				PushPage(ClientId, SPage::COSMETICS_CATEGORY, A.m_A);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::TogglePassive:
				if(pPlayer)
				{
					pPlayer->Block().TogglePassive();
					bool EnabledForMessage = pPlayer->Block().IsUsingPassiveProtection() || pPlayer->Block().IsPassivePendingEnable();
					pGameContext->SendChatTarget(ClientId, EnabledForMessage ? "Passive protection enabled." : "Passive protection disabled.");
					pGameContext->Block().ClearVotes(ClientId);
					if(IsAtRoot(ClientId))
						pGameContext->ProgressVoteOptions(ClientId, true);
					RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				}
				return true;
			case EActionKind::ToggleCosmeticItem:
				if(pGameContext)
				{
					// A=A(category), B=item
					const int Cat = A.m_A;
					const int Item = A.m_B;
					bool Changed = false;
					if(Cat == 0) // Skin Manipulations
						Changed = pGameContext->Block().Cosmetics()->ToggleSkinmani(ClientId, CCosmeticsHandler::ms_SkinmaniNames[Item]);
					else if(Cat == 1) // Gun Designs
						Changed = pGameContext->Block().Cosmetics()->ToggleGundesign(ClientId, CCosmeticsHandler::ms_GundesignNames[Item]);
					else if(Cat == 2) // Knockout Effects
						Changed = pGameContext->Block().Cosmetics()->ToggleKnockout(ClientId, CCosmeticsHandler::ms_KnockoutNames[Item]);
					else if(Cat == 3) // VIP Specials
					{
						// VIP specials indices: 0..3 mapped directly
						static const char *s_VipNames[] = {"Ball", "Crown", "Epic Circle", "Halo"};
						Changed = pGameContext->Block().Cosmetics()->ToggleSpecial(ClientId, s_VipNames[Item]);
					}
					if(Changed)
					{
						pGameContext->Block().ClearVotes(ClientId);
						RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
					}
				}
				return true;
			case EActionKind::ToggleHideCosmetics:
				if(pPlayer)
				{
					pPlayer->Block().m_HideCosmetics = !pPlayer->Block().m_HideCosmetics;
					pGameContext->SendChatTarget(ClientId, pPlayer->Block().m_HideCosmetics ? "Cosmetics hidden." : "Cosmetics visible.");
					pGameContext->Block().ClearVotes(ClientId);
					if(IsAtRoot(ClientId))
						pGameContext->ProgressVoteOptions(ClientId, true);
					RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				}
				return true;
			case EActionKind::SetScoreMode:
				if(pPlayer)
				{
					pPlayer->Block().m_ScoreDisplayMode = A.m_A;
					const char *apModes[] = {"Level", "Blockpoints"};
					char aMsg[128];
					str_format(aMsg, sizeof(aMsg), "Scoreboard now shows: %s", apModes[A.m_A]);
					pGameContext->SendChatTarget(ClientId, aMsg);
					pGameContext->Block().ClearVotes(ClientId);
					if(IsAtRoot(ClientId))
						pGameContext->ProgressVoteOptions(ClientId, true);
					RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				}
				return true;
			case EActionKind::OpenShop:
				PushPage(ClientId, SPage::SHOP);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenShopCategory:
				PushPage(ClientId, SPage::SHOP_CATEGORY, A.m_A);
				pGameContext->Block().ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::BuyShopItem:
				if(pPlayer && pGameContext)
				{
					if(!pPlayer->Block().IsLoggedIn())
					{
						pGameContext->SendChatTarget(ClientId, "You need to be logged in to make purchases.");
					}
					else
					{
						CShop::InstantPurchase(pGameContext, pPlayer, A.m_A, A.m_B);
						// Re-render the current shop category page so the item shows as "Owned"
						pGameContext->Block().ClearVotes(ClientId);
						RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
					}
				}
				return true;
			case EActionKind::DuelSetPoints:
			{
				auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto Match = Mgr ? Mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(Match && Match->IsInConfigPhase())
				{
					const bool Changed = Match->m_Config.m_PointsLimit != A.m_A;
					Match->m_Config.m_PointsLimit = A.m_A;
					if(Changed)
						Match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					// re-render for both players
					for(int Cid : {Match->m_Player1ID, Match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->Block().GetPlayer(Cid);
						if(pP)
						{
							pGameContext->Block().ClearVotes(Cid);
							RenderCurrentPage(pP, Cid, pGameContext->Server(), pGameContext);
						}
					}
				}
				return true;
			}
			case EActionKind::DuelToggleWeapon:
			{
				auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto Match = Mgr ? Mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(Match && Match->IsInConfigPhase())
				{
					bool Changed = false;
					int w = A.m_A;
					if(w >= 0 && w < 6)
					{
						const bool Before = Match->m_Config.m_aWeapons[w];
						Match->m_Config.m_aWeapons[w] = !Match->m_Config.m_aWeapons[w];
						Changed = Before != Match->m_Config.m_aWeapons[w];
					}
					if(Changed)
						Match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					for(int Cid : {Match->m_Player1ID, Match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->Block().GetPlayer(Cid);
						if(pP)
						{
							pGameContext->Block().ClearVotes(Cid);
							RenderCurrentPage(pP, Cid, pGameContext->Server(), pGameContext);
						}
					}
				}
				return true;
			}
			case EActionKind::DuelToggleEndlessHook:
			{
				auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto Match = Mgr ? Mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(Match && Match->IsInConfigPhase())
				{
					Match->m_Config.m_EndlessHook = !Match->m_Config.m_EndlessHook;
					Match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					for(int Cid : {Match->m_Player1ID, Match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->Block().GetPlayer(Cid);
						if(pP)
						{
							pGameContext->Block().ClearVotes(Cid);
							RenderCurrentPage(pP, Cid, pGameContext->Server(), pGameContext);
						}
					}
				}
				return true;
			}
			case EActionKind::DuelSetTimeLimit:
			{
				auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto Match = Mgr ? Mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(Match && Match->IsInConfigPhase())
				{
					const bool Changed = Match->m_Config.m_TimeLimit != A.m_A;
					Match->m_Config.m_TimeLimit = A.m_A;
					if(Changed)
						Match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					for(int Cid : {Match->m_Player1ID, Match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->Block().GetPlayer(Cid);
						if(pP)
						{
							pGameContext->Block().ClearVotes(Cid);
							RenderCurrentPage(pP, Cid, pGameContext->Server(), pGameContext);
						}
					}
				}
				return true;
			}
			case EActionKind::DuelToggleSpawnMode:
			{
				auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>();
				auto Match = Mgr ? Mgr->GetMatchForPlayer(ClientId) : nullptr;
				if(Match && Match->IsInConfigPhase())
				{
					const bool Changed = Match->m_Config.m_SpawnMode != A.m_A;
					Match->m_Config.m_SpawnMode = A.m_A; // 0=normal, 1=random
					if(Changed)
						Match->ResetDuelReadyVotes("[1on1] Settings changed - ready votes were reset.");
					for(int Cid : {Match->m_Player1ID, Match->m_Player2ID})
					{
						CPlayer *pP = pGameContext->Block().GetPlayer(Cid);
						if(pP)
						{
							pGameContext->Block().ClearVotes(Cid);
							RenderCurrentPage(pP, Cid, pGameContext->Server(), pGameContext);
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
	std::vector<SAction> Actions;
	Labels.reserve(64);
	Actions.reserve(64);

	const auto &Stack = GetStack(ClientID);
	SPage Current = Stack.empty() ? SPage{} : Stack.back();

	switch(Current.m_PageType)
	{
	case SPage::ROOT: BuildRoot(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case SPage::RULES: BuildRules(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case SPage::LEADERBOARDS: BuildLeaderboards(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case SPage::LEADERBOARD_DETAIL: BuildLeaderboardDetail(pPlayer, ClientID, pServer, pGameContext, Current.m_Data, Labels, Actions); break;
	case SPage::SERVER_INFOS: BuildServerInfos(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case SPage::SERVER_VOTES: BuildServerVotes(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case SPage::SERVER_INFOS_TOPIC: BuildServerInfosTopic(pPlayer, ClientID, pServer, pGameContext, Current.m_Data, Labels, Actions); break;
	case SPage::MAP_TRANSFERS: BuildMapTransfers(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case SPage::COSMETICS_ROOT: BuildCosmeticsRoot(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case SPage::COSMETICS_CATEGORY: BuildCosmeticsCategory(pPlayer, ClientID, pServer, pGameContext, Current.m_Data, Labels, Actions); break;
	case SPage::SHOP: BuildShop(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case SPage::SHOP_CATEGORY: BuildShopCategory(pPlayer, ClientID, pServer, pGameContext, Current.m_Data, Labels, Actions); break;
	case SPage::DUEL_CONFIG: BuildDuelConfig(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	}

	// add nav ctrls (skip for DUEL_CONFIG - players can't leave config phase)
	if(Current.m_PageType != SPage::ROOT && Current.m_PageType != SPage::DUEL_CONFIG)
	{
		Labels.insert(Labels.begin(), SmallCaps("« Back"));
		Actions.insert(Actions.begin(), SAction{EActionKind::Back, -1, -1});
	}
	if(Current.m_PageType != SPage::ROOT && Current.m_PageType != SPage::DUEL_CONFIG)
	{
		Labels.push_back(SmallCaps("× Main Page"));
		Actions.push_back(SAction{EActionKind::Close, -1, -1});
	}

	// beautify: wrap page with box-drawing header/footer
	{
		std::string Title;
		switch(Current.m_PageType)
		{
		case SPage::ROOT: Title = "Block Menu"; break;
		case SPage::LEADERBOARDS: Title = "Leaderboards"; break;
		case SPage::RULES: Title = "Rules"; break;
		case SPage::LEADERBOARD_DETAIL:
		{
			Title = "Leaderboard";
			break;
		}
		case SPage::SERVER_INFOS: Title = "Server Infos"; break;
		case SPage::SERVER_VOTES: Title = "Server Votes"; break;
		case SPage::SERVER_INFOS_TOPIC:
		{
			int Topic = Current.m_Data;
			if(Topic == 0)
				Title = "Account System";
			else if(Topic == 1)
				Title = "Clan System";
			else
				Title = "Info";
			break;
		}
		case SPage::MAP_TRANSFERS: Title = "Map Transfers"; break;
		case SPage::COSMETICS_ROOT: Title = "Cosmetics"; break;
		case SPage::COSMETICS_CATEGORY:
		{
			int Cat = Current.m_Data;
			if(Cat == 0)
				Title = "Skinmanis";
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
		case SPage::SHOP: Title = "Shop"; break;
		case SPage::SHOP_CATEGORY:
		{
			int Cat = Current.m_Data;
			if(Cat == CShop::CATEGORY_SKINMANI)
				Title = "Skinmani Shop";
			else if(Cat == CShop::CATEGORY_GUNDESIGN)
				Title = "Gundesign Shop";
			else if(Cat == CShop::CATEGORY_KNOCKOUT)
				Title = "Knockout Shop";
			else if(Cat == CShop::CATEGORY_UTILITY)
				Title = "Shop - Utilities";
			else
				Title = "Shop";
			break;
		}
		case SPage::DUEL_CONFIG:
			Title = "1on1 Config";
			break;
		}
		// a vote description carries VOTE_DESC_LENGTH *bytes*, and every box rune
		// and small-caps letter costs three of them, so the whole line is only
		// about twenty characters wide
		std::string Top, Bottom;
		BuildBoxBorders(Title, Top, Bottom);

		Labels.insert(Labels.begin(), Top);
		Actions.insert(Actions.begin(), SAction{EActionKind::None, -1, -1});

		// add vertical side bars to all inner lines (excluding top and bottom)
		for(size_t i = 0; i < Labels.size(); ++i)
		{
			if(i == 0)
				continue; // keep top border as-is
			// A real server vote must go out with exactly the description the
			// engine registered, otherwise the click cannot be matched back to it.
			if(i < Actions.size() && Actions[i].m_Kind == EActionKind::ServerVote)
				continue;
			Labels[i] = std::string("│ ") + Labels[i];
		}

		// append bottom
		Labels.push_back(Bottom);
		Actions.push_back(SAction{EActionKind::None, -1, -1});
	}

	// the vote description is capped at VOTE_DESC_LENGTH *bytes*, and every
	// small-caps glyph costs 2-3 of them, so a label that looks short can still
	// overflow. trim on a character boundary here, before the click map is built,
	// so what the client receives and what we match on are the same string.
	for(std::string &Label : Labels)
		Label = TruncateForVote(Label);

	// store mapping with a few safe aliases to handle client-side string differences
	std::vector<std::pair<std::string, SAction>> Map;
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
		int Idx = 0;
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
		while(Idx < 15 && Sent < Total)
		{
			SetVoteDescriptionAtIndex(Idx, Labels[Sent].c_str(), Msg);
			++Sent;
		}
		Msg.m_NumOptions = Idx;
		pServer->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
}

void CVoteManager::BuildRoot(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	if(pPlayer)
	{
		bool HasPassive = (pPlayer->Block().m_LocalPassiveDuration > 0) || (pPlayer->Block().IsLoggedIn() && pPlayer->Block().GetPlayerPassive() > 0);
		if(HasPassive)
		{
			std::string PassiveLine;
			if(pPlayer->Block().IsUsingPassiveProtection() || pPlayer->Block().IsPassivePendingEnable())
				PassiveLine = std::string("☑ ") + SmallCaps("Wayblock Protection");
			else
				PassiveLine = std::string("☐ ") + SmallCaps("Wayblock Protection");
			OutLabels.emplace_back(PassiveLine);
			OutActions.emplace_back(SAction{EActionKind::TogglePassive});
		}
	}

	// Hide cosmetics toggle
	if(pPlayer)
	{
		std::string HideLine;
		if(pPlayer->Block().m_HideCosmetics)
			HideLine = std::string("☑ ") + SmallCaps("Hide Cosmetics");
		else
			HideLine = std::string("☐ ") + SmallCaps("Hide Cosmetics");
		OutLabels.emplace_back(HideLine);
		OutActions.emplace_back(SAction{EActionKind::ToggleHideCosmetics});
	}

	// Score display mode (radio-button style checkboxes)
	if(pPlayer)
	{
		int Mode = pPlayer->Block().m_ScoreDisplayMode;
		if(Mode < 0 || Mode > 1)
			Mode = 0;

		OutLabels.emplace_back(SmallCaps("Score Display:"));
		OutActions.emplace_back(SAction{EActionKind::None});

		// Level option
		{
			std::string Line = std::string(Mode == 0 ? "  ☑ " : "  ☐ ") + SmallCaps("Level");
			OutLabels.emplace_back(Line);
			OutActions.emplace_back(SAction{EActionKind::SetScoreMode, 0});
		}
		// Blockpoints option
		{
			std::string Line = std::string(Mode == 1 ? "  ☑ " : "  ☐ ") + SmallCaps("Blockpoints");
			OutLabels.emplace_back(Line);
			OutActions.emplace_back(SAction{EActionKind::SetScoreMode, 1});
		}
	}

	// ─── separator ───
	OutLabels.emplace_back(SmallCaps("───────────"));
	OutActions.emplace_back(SAction{EActionKind::None});

	// rules (always available)
	{
		std::string Label = SmallCaps("Rules");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenRules});
	}

	// leaderboards (always available)
	{
		std::string Label = SmallCaps("Leaderboards");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenLeaderboards});
	}

	// server infos (always available)
	{
		std::string Label = SmallCaps("Server Infos");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenServerInfos});
	}

	{
		std::string Label = SmallCaps("Server Votes");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenServerVotes});
	}

	{
		std::string Label = SmallCaps("Map Transfers");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenMapTransfers});
	}

	// shop (requires login)
	if(pPlayer && pPlayer->Block().IsLoggedIn())
	{
		std::string Label = SmallCaps("Shop");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenShop});
	}

	if(pPlayer && pPlayer->Block().IsLoggedIn())
	{
		std::string Label = SmallCaps("Cosmetics");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenCosmetics});
	}
	else
	{
		OutLabels.emplace_back(SmallCaps("/login to use cosmetics!"));
		OutActions.emplace_back(SAction{EActionKind::None});
	}
	// the invite is server-owned, so it is configured rather than baked in
	if(g_Config.m_SvDiscordInvite[0])
	{
		char aInvite[96];
		str_format(aInvite, sizeof(aInvite), "discord: %s", g_Config.m_SvDiscordInvite);
		OutLabels.emplace_back(SmallCaps(aInvite));
		OutActions.emplace_back(SAction{EActionKind::None});
	}
}

void CVoteManager::BuildMapTransfers(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	bool Any = false;

	auto AddEntry = [&](int Port, const char *pName) {
		if(Port <= 0)
			return;
		Any = true;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s (:%d)", pName, Port);
		std::string Line = SmallCaps(aBuf);
		OutLabels.emplace_back(Line);
		OutActions.emplace_back(SAction{EActionKind::RedirectToPort, Port});
	};

	AddEntry(g_Config.m_SvBlmapV3RoyalPort, "BlmapV3Royal");
	AddEntry(g_Config.m_SvStorePort, "Store");
	AddEntry(g_Config.m_SvMulteasyPort, "Multeasymap");

	if(!Any)
	{
		OutLabels.emplace_back(SmallCaps("No map transfers configured."));
		OutActions.emplace_back(SAction{EActionKind::None});
	}
}

void CVoteManager::BuildRules(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
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
			OutActions.emplace_back(SAction{EActionKind::None});
			Any = true;
		}
	}

	if(!Any)
	{
		OutLabels.emplace_back(SmallCaps("Be nice."));
		OutActions.emplace_back(SAction{EActionKind::None});
	}
}

void CVoteManager::BuildLeaderboards(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	{
		std::string Label = SmallCaps("Top Level");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenLeaderboardCategory, 0});
	}

	{
		std::string Label = SmallCaps("Top Blockpoints");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenLeaderboardCategory, 1});
	}

	{
		std::string Label = SmallCaps("Top Killstreaks");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenLeaderboardCategory, 2});
	}

	{
		std::string Label = SmallCaps("Top Clans");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenLeaderboardCategory, 3});
	}
}

void CVoteManager::BuildServerInfos(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	// two subpages: Accounts, Clans
	{
		std::string Label = SmallCaps("Accounts");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenServerInfosTopic, 0});
	}
	{
		std::string Label = SmallCaps("Clans");
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenServerInfosTopic, 1});
	}
}

void CVoteManager::BuildServerVotes(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	(void)pPlayer;
	(void)ClientID;
	(void)pServer;

	int Count = 0;
	for(CVoteOptionServer *pOption = pGameContext->m_pVoteOptionFirst; pOption; pOption = pOption->m_pNext)
	{
		OutLabels.emplace_back(pOption->m_aDescription);
		OutActions.emplace_back(SAction{EActionKind::ServerVote});
		++Count;
	}

	if(Count == 0)
	{
		OutLabels.emplace_back(SmallCaps("No server votes configured"));
		OutActions.emplace_back(SAction{EActionKind::None});
	}
}

void CVoteManager::BuildServerInfosTopic(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int TopicIndex, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	if(TopicIndex == 0)
	{
		OutLabels.emplace_back("Accounts let you save your progress:");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("Level, EXP, Blockpoints, Cosmetics, Stats, and more.");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Chat commands:"));
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/register <name> <pass> - Create an account");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/login <name> <pass> - Log in");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/logout_account - Log out");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/password <old> <new> - Change password");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/exp - Show your EXP");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/profile [name] - View a profile");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/bp - Show your blockpoints");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/give_bp <player> <amount> - Offer BP transfer");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/accept_bp [player] | /decline_bp [player]");
		OutActions.emplace_back(SAction{EActionKind::None});
	}
	else if(TopicIndex == 1)
	{
		OutLabels.emplace_back("Clans let players team up and progress together:");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("Compete on leaderboards, share a name, and manage roles.");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Member Commands:"));
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_create <name> - Create a new clan");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_leave - Leave your clan");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_accept | /clan_decline - Respond to invite");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_exp - Show clan EXP progress");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_list - List clan members");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Co-Leader & Leader:"));
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_invite <player> - Invite a player");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_kick <player> - Kick a member");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Leader Only:"));
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_role <player> <member|coleader> - Set role");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_rename <newname> - Rename clan");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_transfer <player> - Transfer clan leadership");
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back("/clan_delete - Delete your clan");
		OutActions.emplace_back(SAction{EActionKind::None});
	}
}
void CVoteManager::BuildLeaderboardDetail(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	const char *pTitle;
	switch(CategoryIndex)
	{
	case 0: pTitle = "Top Level"; break;
	case 1: pTitle = "Top Blockpoints"; break;
	case 2: pTitle = "Top Killstreaks"; break;
	case 3: pTitle = "Top Clans"; break;
	default: pTitle = "Leaderboard"; break;
	}

	// generate a symmetric inner box around the content
	auto MakeInnerTop = [&](const char *pT) {
		return std::string("  ╭─[ ") + SmallCaps(pT) + " ]──────";
	};
	auto MakeInnerBottom = [&](const char *pT) {
		// match the number of runes between the two corners in the header:
		// header between corners is: "─[ " + SmallCaps(title) + " ]─" -> len = capsLen + 6 - might have fucked something up somewhere idk
		std::string Caps = SmallCaps(pT);
		int CapsLen = str_length(Caps.c_str());
		int DashCount = CapsLen + 6;
		if(DashCount < 1)
			DashCount = 1;
		std::string s = "  ╰";
		for(int i = 0; i < DashCount; ++i)
			s += "─";
		s += "╯";
		return s;
	};

	if(!pPlayer)
	{
		// spacer above inner box
		OutLabels.emplace_back("");
		OutActions.emplace_back(SAction{EActionKind::None});

		// inner boxed section header
		{
			std::string Top = MakeInnerTop(pTitle);
			OutLabels.emplace_back(Top);
			OutActions.emplace_back(SAction{EActionKind::None});
		}

		// content
		OutLabels.emplace_back(std::string("  │  ") + SmallCaps("Unavailable."));
		OutActions.emplace_back(SAction{EActionKind::None});

		// inner boxed section footer sized like the header
		OutLabels.emplace_back(MakeInnerBottom(pTitle));
		OutActions.emplace_back(SAction{EActionKind::None});

		// spacer below inner box
		OutLabels.emplace_back("");
		OutActions.emplace_back(SAction{EActionKind::None});
		return;
	}

	if(pPlayer->Block().m_TopMessagesCount <= 0 || pPlayer->Block().m_CaptureTopCategory != CategoryIndex)
	{
		// spacer above inner box
		OutLabels.emplace_back("");
		OutActions.emplace_back(SAction{EActionKind::None});

		// inner boxed section header
		{
			std::string Top = MakeInnerTop(pTitle);
			OutLabels.emplace_back(Top);
			OutActions.emplace_back(SAction{EActionKind::None});
		}

		// content placeholder
		OutLabels.emplace_back(std::string("  │  ") + SmallCaps("Loading..."));
		OutActions.emplace_back(SAction{EActionKind::None});

		// inner boxed section footer sized like the header
		OutLabels.emplace_back(MakeInnerBottom(pTitle));
		OutActions.emplace_back(SAction{EActionKind::None});

		// spacer below inner box
		OutLabels.emplace_back("");
		OutActions.emplace_back(SAction{EActionKind::None});
		return;
	}

	// spacer above inner box
	OutLabels.emplace_back("");
	OutActions.emplace_back(SAction{EActionKind::None});

	// inner boxed section header
	{
		std::string Top = MakeInnerTop(pTitle);
		OutLabels.emplace_back(Top);
		OutActions.emplace_back(SAction{EActionKind::None});
	}

	// content lines (skip decorative chat headers like "------------ Global Top ... ------------")
	auto IsDecorHeader = [&](const char *p) {
		if(!p)
			return false;
		std::string t = Trim(std::string(p));
		if(t.empty())
			return false;
		int n = (int)t.size();
		int Lead = 0;
		while(Lead < n && t[Lead] == '-')
			Lead++;
		int Trail = 0;
		while(Trail < n && t[n - 1 - Trail] == '-')
			Trail++;
		if(Lead >= 3 && Trail >= 3)
			return true; // looks like dashed banner
		// also tolerate explicit phrase check
		std::string Low = t;
		for(char &Ch : Low)
			Ch = (char)std::tolower((unsigned char)Ch);
		if(Low.find("global top") != std::string::npos)
			return true;
		return false;
	};

	for(int i = 0; i < pPlayer->Block().m_TopMessagesCount; ++i)
	{
		const char *pLine = pPlayer->Block().m_aTopMessages[i];
		if(!pLine || !pLine[0])
			break;
		if(IsDecorHeader(pLine))
			continue;
		OutLabels.emplace_back(std::string("  │  ") + pLine);
		OutActions.emplace_back(SAction{EActionKind::None});
	}

	// inner boxed section footer sized like the header
	OutLabels.emplace_back(MakeInnerBottom(pTitle));
	OutActions.emplace_back(SAction{EActionKind::None});

	// spacer below inner box
	OutLabels.emplace_back("");
	OutActions.emplace_back(SAction{EActionKind::None});
}

void CVoteManager::BuildCosmeticsRoot(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	// guard: only for logged-in players
	if(!(pPlayer && pPlayer->Block().IsLoggedIn()))
	{
		OutLabels.emplace_back(SmallCaps("/login to use cosmetics!"));
		OutActions.emplace_back(SAction{EActionKind::None});
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
		OutActions.emplace_back(SAction{EActionKind::None});

		for(int i : OwnedIndices)
		{
			const bool IsActive = (ActiveIndex == i);
			std::string Name = (ppNames && ppNames[i] ? ppNames[i] : "");
			std::string Line = std::string(IsActive ? "☑ " : "☐ ") + SmallCaps(Name);
			OutLabels.emplace_back(Line);
			OutActions.emplace_back(SAction{EActionKind::ToggleCosmeticItem, CategoryIndex, i});
		}
	};

	int Cid = pPlayer->GetCid();

	// Skin Manipulations (category 0)
	RenderSection("Skin Manipulations",
		CCosmeticsHandler::ms_SkinmaniNames, CCosmeticsHandler::NUM_SKINMANIS,
		0, pPlayer->Block().GetSkinMani(),
		[&](int i) { return pGameContext->Block().Cosmetics()->HasSkinmani(Cid, i); });

	// Gun Designs (category 1)
	RenderSection("Gun Designs",
		CCosmeticsHandler::ms_GundesignNames, CCosmeticsHandler::NUM_GUNDESIGNS,
		1, pPlayer->Block().GetGunDesign(),
		[&](int i) { return pGameContext->Block().Cosmetics()->HasGundesign(Cid, i); });

	// Knockout Effects (category 2)
	RenderSection("Knockout Effects",
		CCosmeticsHandler::ms_KnockoutNames, CCosmeticsHandler::NUM_KNOCKOUTS,
		2, pPlayer->Block().GetKnockout(),
		[&](int i) { return pGameContext->Block().Cosmetics()->HasKnockoutEffect(Cid, i); });

	// VIP Items (category 3) — only for VIP players
	if(pPlayer->Block().GetPlayerVip() || (pServer && pServer->GetAuthedState(Cid) != AUTHED_NO))
	{
		static const char *s_Vip[] = {"Ball", "Crown", "Epic Circle", "Halo"};
		RenderSection("VIP Items",
			s_Vip, 4,
			3, pPlayer->Block().GetCurrentSpecial(),
			[&](int i) { return pGameContext->Block().Cosmetics()->HasSpecial(Cid, i); });
	}

	if(!AnyOwned)
	{
		OutLabels.emplace_back(SmallCaps("No owned cosmetics."));
		OutActions.emplace_back(SAction{EActionKind::None});
		OutLabels.emplace_back(SmallCaps("Buy some in the shop!"));
		OutActions.emplace_back(SAction{EActionKind::None});
	}
}

void CVoteManager::BuildCosmeticsCategory(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	// always render a header
	const char **ppNames = nullptr;
	int Count = 0;

	if(!(pPlayer && pGameContext && pPlayer->Block().IsLoggedIn()))
	{
		OutLabels.emplace_back(SmallCaps("/login to use cosmetics!"));
		OutActions.emplace_back(SAction{EActionKind::None});
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
		if(!(pPlayer->Block().GetPlayerVip() || (pServer && pServer->GetAuthedState(pPlayer->GetCid()) != AUTHED_NO)))
		{
			OutLabels.emplace_back(SmallCaps("You are not VIP."));
			OutActions.emplace_back(SAction{EActionKind::None});
			OutLabels.emplace_back("Contact an admin if you're interested in contributing!");
			OutActions.emplace_back(SAction{EActionKind::None});
			if(g_Config.m_SvDiscordInvite[0])
			{
				char aInvite[96];
				str_format(aInvite, sizeof(aInvite), "discord: %s", g_Config.m_SvDiscordInvite);
				OutLabels.emplace_back(aInvite);
				OutActions.emplace_back(SAction{EActionKind::None});
			}
			return; // stop rendering items
		}
		static const char *s_Vip[] = {"Ball", "Crown", "Epic Circle", "Halo"};
		ppNames = s_Vip;
		Count = 4;
	}

	// collect owned items and render with selection checkbox
	int Active = -1;
	if(CategoryIndex == 0)
		Active = pPlayer->Block().GetSkinMani();
	else if(CategoryIndex == 1)
		Active = pPlayer->Block().GetGunDesign();
	else if(CategoryIndex == 2)
		Active = pPlayer->Block().GetKnockout();
	else if(CategoryIndex == 3)
		Active = pPlayer->Block().GetCurrentSpecial();

	std::unordered_set<int> Owned;
	for(int i = 0; i < Count; ++i)
	{
		bool Has = false;
		if(CategoryIndex == 0)
			Has = pGameContext->Block().Cosmetics()->HasSkinmani(pPlayer->GetCid(), i);
		else if(CategoryIndex == 1)
			Has = pGameContext->Block().Cosmetics()->HasGundesign(pPlayer->GetCid(), i);
		else if(CategoryIndex == 2)
			Has = pGameContext->Block().Cosmetics()->HasKnockoutEffect(pPlayer->GetCid(), i);
		else if(CategoryIndex == 3)
			Has = pGameContext->Block().Cosmetics()->HasSpecial(pPlayer->GetCid(), i);
		if(Has)
			Owned.insert(i);
	}

	if(Owned.empty())
	{
		OutLabels.emplace_back(SmallCaps("No owned items."));
		OutLabels.emplace_back(SmallCaps("Buy some in the store!"));
		OutActions.emplace_back(SAction{EActionKind::None});
		OutActions.emplace_back(SAction{EActionKind::None});
	}

	for(int i = 0; i < Count; ++i)
	{
		if(!Owned.contains(i))
			continue;
		const bool IsActive = (Active == i);
		std::string Name = (ppNames && ppNames[i] ? ppNames[i] : "");
		std::string Line = std::string(IsActive ? "☑ " : "☐ ") + SmallCaps(Name);
		OutLabels.emplace_back(Line);
		OutActions.emplace_back(SAction{EActionKind::ToggleCosmeticItem, CategoryIndex, i});
	}
}

void CVoteManager::BuildShop(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	if(!(pPlayer && pPlayer->Block().IsLoggedIn()))
	{
		OutLabels.emplace_back(SmallCaps("/login to use the shop!"));
		OutActions.emplace_back(SAction{EActionKind::None});
		return;
	}

	// show player's blockpoints balance
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Your BP: %d", pPlayer->Block().GetPlayerBlockpoints());
		OutLabels.emplace_back(aBuf);
		OutActions.emplace_back(SAction{EActionKind::None});

		OutLabels.emplace_back("───────────");
		OutActions.emplace_back(SAction{EActionKind::None});
	}

	// shop categories (excluding VIP)
	struct ShopCat
	{
		const char *m_Name;
		int m_CategoryIndex; // maps to CShop::CATEGORY_*
	} Cats[] = {
		{"Utilities", CShop::CATEGORY_UTILITY},
		{"Skin Manipulations", CShop::CATEGORY_SKINMANI},
		{"Gun Designs", CShop::CATEGORY_GUNDESIGN},
		{"Knockout Effects", CShop::CATEGORY_KNOCKOUT},
	};

	for(const auto &C : Cats)
	{
		std::string Label = C.m_Name;
		Label += " ›";
		OutLabels.emplace_back(Label);
		OutActions.emplace_back(SAction{EActionKind::OpenShopCategory, C.m_CategoryIndex});
	}
}

void CVoteManager::BuildShopCategory(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	if(!(pPlayer && pGameContext && pPlayer->Block().IsLoggedIn()))
	{
		OutLabels.emplace_back("/login to use the shop!");
		OutActions.emplace_back(SAction{EActionKind::None});
		return;
	}

	CCosmeticsHandler *pCos = pGameContext->Block().Cosmetics();
	if(!pCos)
		return;

	// show player's blockpoints balance at top
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Your BP: %d  |  Level: %d", pPlayer->Block().GetPlayerBlockpoints(), pPlayer->Block().GetPlayerLevel());
		OutLabels.emplace_back(aBuf);
		OutActions.emplace_back(SAction{EActionKind::None});

		OutLabels.emplace_back("───────────");
		OutActions.emplace_back(SAction{EActionKind::None});
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
		else if(CategoryIndex == CShop::CATEGORY_UTILITY && i == CCosmeticsHandler::UTILITY_VIP_WEEK)
			Owned = pPlayer->Block().HasVip();

		// get item name
		const char *pName = nullptr;
		if(CategoryIndex == CShop::CATEGORY_UTILITY)
			pName = CShop::UtilityName(i);
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
			OutActions.emplace_back(SAction{EActionKind::None}); // already owned, no action
		else
			OutActions.emplace_back(SAction{EActionKind::BuyShopItem, CategoryIndex, i});

		AnyItems = true;
	}

	if(!AnyItems)
	{
		OutLabels.emplace_back("No items available.");
		OutActions.emplace_back(SAction{EActionKind::None});
	}
}

void CVoteManager::PushPage(int ClientID, SPage::Type T, int Data)
{
	m_PageStack[ClientID].push_back(SPage{T, Data});
}

bool CVoteManager::PopPage(int ClientID)
{
	auto It = m_PageStack.find(ClientID);
	if(It == m_PageStack.end() || It->second.empty())
		return false;
	It->second.pop_back();
	if(It->second.empty())
		It->second.push_back(SPage{SPage::ROOT, -1});
	return true;
}

const std::vector<CVoteManager::SPage> &CVoteManager::GetStack(int ClientID)
{
	return m_PageStack[ClientID];
}

bool CVoteManager::IsAtRoot(int ClientId)
{
	auto It = m_PageStack.find(ClientId);
	if(It == m_PageStack.end() || It->second.empty())
		return true;
	return It->second.back().m_PageType == SPage::ROOT;
}

void CVoteManager::BuildDuelConfig(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<SAction> &OutActions)
{
	auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>();
	auto Match = Mgr ? Mgr->GetMatchForPlayer(ClientID) : nullptr;
	if(!Match || !Match->IsInConfigPhase())
	{
		OutLabels.emplace_back(SmallCaps("No active duel config."));
		OutActions.emplace_back(SAction{EActionKind::None});
		return;
	}

	const SMatchConfig &Cfg = Match->m_Config;

	// opponent name
	int OpponentId = (ClientID == Match->m_Player1ID) ? Match->m_Player2ID : Match->m_Player1ID;
	const char *pOpponentName = pServer->ClientName(OpponentId);
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "vs %s", pOpponentName);
		OutLabels.emplace_back(SmallCaps(aBuf));
		OutActions.emplace_back(SAction{EActionKind::None});
	}

	// warmup timer
	if(Match->m_ConfigStartTick > 0)
	{
		int Elapsed = ((pServer->Tick() - Match->m_ConfigStartTick) / pServer->TickSpeed());
		int Remaining = g_Config.m_Sv1on1WarmupSeconds - Elapsed;
		if(Remaining < 0)
			Remaining = 0;
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Warmup: %ds", Remaining);
		OutLabels.emplace_back(SmallCaps(aBuf));
		OutActions.emplace_back(SAction{EActionKind::None});
	}

	// separator
	OutLabels.emplace_back(SmallCaps("───────────"));
	OutActions.emplace_back(SAction{EActionKind::None});

	// ── Points Limit (radio checkboxes) ──
	{
		static const int s_PointOptions[] = {3, 10, 20, 30, 50};

		OutLabels.emplace_back(SmallCaps("Points:"));
		OutActions.emplace_back(SAction{EActionKind::None});
		for(int PointOption : s_PointOptions)
		{
			bool Selected = (Cfg.m_PointsLimit == PointOption);
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "  %s %d", Selected ? "\xe2\x98\x91" : "\xe2\x98\x90", PointOption);
			OutLabels.emplace_back(aBuf);
			OutActions.emplace_back(SAction{EActionKind::DuelSetPoints, PointOption});
		}
	}

	// ── Time Limit (radio checkboxes) ──
	{
		static const int s_TimeOptions[] = {0, 120, 300, 600};
		static const char *s_TimeNames[] = {"No Limit", "2 min", "5 min", "10 min"};
		static const int s_NumOptions = 4;

		OutLabels.emplace_back(SmallCaps("Time Limit:"));
		OutActions.emplace_back(SAction{EActionKind::None});
		for(int i = 0; i < s_NumOptions; i++)
		{
			bool Selected = (Cfg.m_TimeLimit == s_TimeOptions[i]);
			std::string Line = std::string(Selected ? "  \xe2\x98\x91 " : "  \xe2\x98\x90 ") + SmallCaps(s_TimeNames[i]);
			OutLabels.emplace_back(Line);
			OutActions.emplace_back(SAction{EActionKind::DuelSetTimeLimit, s_TimeOptions[i]});
		}
	}

	// ── Endless Hook ──
	{
		std::string Line = std::string(Cfg.m_EndlessHook ? "\xe2\x98\x91 " : "\xe2\x98\x90 ") + SmallCaps("Endless Hook");
		OutLabels.emplace_back(Line);
		OutActions.emplace_back(SAction{EActionKind::DuelToggleEndlessHook});
	}

	// ── Spawn Mode ──
	{
		OutLabels.emplace_back(SmallCaps("Spawn:"));
		OutActions.emplace_back(SAction{EActionKind::None});

		bool IsNormal = (Cfg.m_SpawnMode == 0);
		OutLabels.emplace_back(std::string(IsNormal ? "  \xe2\x98\x91 " : "  \xe2\x98\x90 ") + SmallCaps("Normal"));
		OutActions.emplace_back(IsNormal ? SAction{EActionKind::None} : SAction{EActionKind::DuelToggleSpawnMode, 0});

		bool IsRandom = (Cfg.m_SpawnMode == 1);
		OutLabels.emplace_back(std::string(IsRandom ? "  \xe2\x98\x91 " : "  \xe2\x98\x90 ") + SmallCaps("Random"));
		OutActions.emplace_back(IsRandom ? SAction{EActionKind::None} : SAction{EActionKind::DuelToggleSpawnMode, 1});
	}

	// separator
	OutLabels.emplace_back(SmallCaps("───────────"));
	OutActions.emplace_back(SAction{EActionKind::None});

	// ── Weapons ──
	OutLabels.emplace_back(SmallCaps("Weapons:"));
	OutActions.emplace_back(SAction{EActionKind::None});

	static const char *s_WeaponNames[] = {"Hammer", "Gun", "Shotgun", "Grenade", "Laser", "Ninja"};
	for(int w = 0; w < 6; w++)
	{
		std::string Line = std::string(Cfg.m_aWeapons[w] ? "  ☑ " : "  ☐ ") + SmallCaps(s_WeaponNames[w]);
		OutLabels.emplace_back(Line);
		OutActions.emplace_back(SAction{EActionKind::DuelToggleWeapon, w});
	}

	// separator
	OutLabels.emplace_back(SmallCaps("───────────"));
	OutActions.emplace_back(SAction{EActionKind::None});

	// ── Vote Status ──
	{
		const char *pP1Name = pServer->ClientName(Match->m_Player1ID);
		const char *pP2Name = pServer->ClientName(Match->m_Player2ID);

		auto VoteLabel = [](int v) -> const char * {
			if(v == 1)
				return "\xe2\x9c\x93 Start";
			if(v == -1)
				return "\xe2\x9c\x97 Cancel";
			return "Waiting...";
		};

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s: %s", pP1Name, VoteLabel(Match->m_aDuelVote[0]));
		OutLabels.emplace_back(SmallCaps(aBuf));
		OutActions.emplace_back(SAction{EActionKind::None});

		str_format(aBuf, sizeof(aBuf), "%s: %s", pP2Name, VoteLabel(Match->m_aDuelVote[1]));
		OutLabels.emplace_back(SmallCaps(aBuf));
		OutActions.emplace_back(SAction{EActionKind::None});

		OutLabels.emplace_back(SmallCaps("F3 = Start  |  F4 = Cancel"));
		OutActions.emplace_back(SAction{EActionKind::None});
	}
}

void CVoteManager::ForceDuelConfigPage(int ClientId, CPlayer *pPlayer, IServer *pServer, CGameContext *pGameContext)
{
	// replace entire page stack with DUEL_CONFIG
	auto &Stack = m_PageStack[ClientId];
	Stack.clear();
	Stack.push_back(SPage{SPage::DUEL_CONFIG, -1});

	// clear and re-render
	pGameContext->Block().ClearVotes(ClientId);
	RenderCurrentPage(pPlayer, ClientId, pServer, pGameContext);
}
