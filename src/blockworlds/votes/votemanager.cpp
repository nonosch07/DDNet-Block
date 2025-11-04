// page-based, safe voting manager implementation - Nouaa
#include "votemanager.h"

#include <blockworlds/cosmetics/cosmetics.h>
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

static void CreateStripline(char *pDst, int DstSize, const char *pTitle)
{
	if(!pDst || DstSize <= 0)
		return;
	int TitleLen = str_length(pTitle ? pTitle : "");
	int StripSideLen = fmin(15, (DstSize / 2) - TitleLen - 3);
	mem_zero(pDst, DstSize);
	for(int i = 0; i < StripSideLen; i++)
		str_append(pDst, "#", DstSize);
	str_append(pDst, " ", DstSize);
	str_append(pDst, pTitle ? pTitle : "", DstSize);
	str_append(pDst, " ", DstSize);
	for(int i = 0; i < StripSideLen; i++)
		str_append(pDst, "#", DstSize);
}

void CVoteManager::ClearClient(int ClientId)
{
	m_MapByClient.erase(ClientId);
	m_PageStack.erase(ClientId);
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
					RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
					return true;
				}
				return true;
			case EActionKind::Close:
			{
				auto &Stack = m_PageStack[ClientId];
				Stack.clear();
				Stack.push_back(Page{Page::ROOT, -1});
				m_MapByClient.erase(ClientId);
			}
				pGameContext->ClearVotes(ClientId);
				return true;
			case EActionKind::OpenExtras:
				PushPage(ClientId, Page::EXTRAS);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
				return true;
			case EActionKind::OpenCosmetics:
				PushPage(ClientId, Page::COSMETICS_ROOT);
				pGameContext->ClearVotes(ClientId);
				RenderCurrentPage(pPlayer, ClientId, pGameContext->Server(), pGameContext);
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
					pGameContext->SendChatTarget(ClientId, pPlayer->IsUsingPassiveProtection() ? "Passive protection enabled." : "Passive protection disabled.");
					pGameContext->ClearVotes(ClientId);
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
	case Page::EXTRAS: BuildExtras(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case Page::COSMETICS_ROOT: BuildCosmeticsRoot(pPlayer, ClientID, pServer, pGameContext, Labels, Actions); break;
	case Page::COSMETICS_CATEGORY: BuildCosmeticsCategory(pPlayer, ClientID, pServer, pGameContext, Current.Data, Labels, Actions); break;
	}

	// add nav ctrls
	if(Current.PageType != Page::ROOT)
	{
		Labels.insert(Labels.begin(), SmallCaps("« Back"));
		Actions.insert(Actions.begin(), Action{EActionKind::Back, -1, -1});
	}
	Labels.push_back(SmallCaps("× Close"));
	Actions.push_back(Action{EActionKind::Close, -1, -1});

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
	char aHeader[128];
	CreateStripline(aHeader, sizeof(aHeader), SmallCaps("Blockworlds Menu").c_str());
	OutLabels.emplace_back(aHeader);
	OutActions.emplace_back(Action{EActionKind::None});

	// extras page (if eligible)
	bool extrasEligible = pPlayer && ((pPlayer->m_LocalPassiveDuration > 0) || (pPlayer->IsLoggedIn() && pPlayer->GetPlayerPassive() > 0));
	if(extrasEligible)
	{
		std::string label = SmallCaps("Extras");
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenExtras});
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
}

void CVoteManager::BuildExtras(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	char aHeader[128];
	CreateStripline(aHeader, sizeof(aHeader), SmallCaps("Extras").c_str());
	OutLabels.emplace_back(aHeader);
	OutActions.emplace_back(Action{EActionKind::None});

	if(!pPlayer)
		return;

	bool extrasEligible = (pPlayer->m_LocalPassiveDuration > 0) || (pPlayer->IsLoggedIn() && pPlayer->GetPlayerPassive() > 0);
	if(!extrasEligible)
	{
		OutLabels.emplace_back(SmallCaps("No extras available."));
		OutActions.emplace_back(Action{EActionKind::None});
		return;
	}

	std::string PassiveLine = std::string(pPlayer->IsUsingPassiveProtection() ? "☑ " : "☐ ") + SmallCaps("Passive Protection");
	OutLabels.emplace_back(PassiveLine);
	OutActions.emplace_back(Action{EActionKind::TogglePassive});
}

void CVoteManager::BuildCosmeticsRoot(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	char aHeader[128];
	CreateStripline(aHeader, sizeof(aHeader), SmallCaps("Cosmetics").c_str());
	OutLabels.emplace_back(aHeader);
	OutActions.emplace_back(Action{EActionKind::None});

	// guard: only for logged-in players
	if(!(pPlayer && pPlayer->IsLoggedIn()))
	{
		OutLabels.emplace_back(SmallCaps("/login to use cosmetics!"));
		OutActions.emplace_back(Action{EActionKind::None});
		return;
	}

	// categories ==
	// 0=Skin Manipulations, 1=Gun Designs, 2=Knockout Effects, 3=VIP Items
	struct Cat
	{
		const char *Name;
		int Index;
	} Cats[] = {
		{"Skin Manipulations", 0}, {"Gun Designs", 1}, {"Knockout Effects", 2}, {"VIP Items", 3}};

	for(const auto &C : Cats)
	{
		std::string label = SmallCaps(C.Name);
		label += " ›";
		OutLabels.emplace_back(label);
		OutActions.emplace_back(Action{EActionKind::OpenCosmeticsCategory, C.Index});
	}
}

void CVoteManager::BuildCosmeticsCategory(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext, int CategoryIndex, std::vector<std::string> &OutLabels, std::vector<Action> &OutActions)
{
	// always render a header
	const char *pTitle = "";
	const char **ppNames = nullptr;
	int Count = 0;

	if(CategoryIndex == 0)
	{
		pTitle = "Skin Manipulations";
	}
	else if(CategoryIndex == 1)
	{
		pTitle = "Gun Designs";
	}
	else if(CategoryIndex == 2)
	{
		pTitle = "Knockout Effects";
	}
	else if(CategoryIndex == 3)
	{
		pTitle = "VIP Items";
	}

	char aHeader[128];
	{
		std::string fancy = SmallCaps(pTitle);
		CreateStripline(aHeader, sizeof(aHeader), fancy.c_str());
	}
	OutLabels.emplace_back(aHeader);
	OutActions.emplace_back(Action{EActionKind::None});

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
		OutLabels.emplace_back(SmallCaps("No owned items in this category."));
		OutActions.emplace_back(Action{EActionKind::None});
		return;
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
