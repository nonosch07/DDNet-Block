#include "cosmetics.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <locale>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
static std::string ToLowerAscii(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for(unsigned char c : s)
		out.push_back(static_cast<char>(std::tolower(c)));
	return out;
}
static std::string NormalizeForCompare(const std::string &s)
{
	std::string tmp;
	tmp.reserve(s.size());
	for(size_t i = 0; i < s.size(); ++i)
	{
		unsigned char c = s[i];
		if(c < 0x80)
		{
			if(std::isalnum(c) || std::isspace(c))
				tmp.push_back(c);
		}
	}
	std::string out;
	out.reserve(tmp.size());
	bool in_space = false;
	for(char ch : tmp)
	{
		if(std::isspace(static_cast<unsigned char>(ch)))
		{
			if(!in_space)
			{
				out.push_back(' ');
				in_space = true;
			}
		}
		else
		{
			out.push_back(ch);
			in_space = false;
		}
	}
	if(!out.empty() && out.front() == ' ')
		out.erase(out.begin());
	if(!out.empty() && out.back() == ' ')
		out.pop_back();
	return ToLowerAscii(out);
}
} // namespace

void CosmeticsVoteManager::SetVoteDescriptionAtIndex(int *pIndex, const char *pStr, CNetMsg_Sv_VoteOptionListAdd *pOptionMsg)
{
	switch(*pIndex)
	{
	case 0: pOptionMsg->m_pDescription0 = pStr; break;
	case 1: pOptionMsg->m_pDescription1 = pStr; break;
	case 2: pOptionMsg->m_pDescription2 = pStr; break;
	case 3: pOptionMsg->m_pDescription3 = pStr; break;
	case 4: pOptionMsg->m_pDescription4 = pStr; break;
	case 5: pOptionMsg->m_pDescription5 = pStr; break;
	case 6: pOptionMsg->m_pDescription6 = pStr; break;
	case 7: pOptionMsg->m_pDescription7 = pStr; break;
	case 8: pOptionMsg->m_pDescription8 = pStr; break;
	case 9: pOptionMsg->m_pDescription9 = pStr; break;
	case 10: pOptionMsg->m_pDescription10 = pStr; break;
	case 11: pOptionMsg->m_pDescription11 = pStr; break;
	case 12: pOptionMsg->m_pDescription12 = pStr; break;
	case 13: pOptionMsg->m_pDescription13 = pStr; break;
	case 14: pOptionMsg->m_pDescription14 = pStr; break;
	default: return;
	}
	(*pIndex)++;
}

void CosmeticsVoteManager::EnsureCategoriesInitialized()
{
	if(!m_CategoriesInitialized)
	{
		ClearOptions();
		AddCategory({"Skin Manipulations", CCosmeticsHandler::NUM_SKINMANIS, CCosmeticsHandler::ms_SkinmaniNames, &CPlayer::GetPlayerSkinmani, &CPlayer::GetSkinMani});
		AddCategory({"Gun Designs", CCosmeticsHandler::NUM_GUNDESIGNS, CCosmeticsHandler::ms_GundesignNames, &CPlayer::GetPlayerGundesign, &CPlayer::GetGunDesign});
		AddCategory({"Knockout Effects", CCosmeticsHandler::NUM_KNOCKOUTS, CCosmeticsHandler::ms_KnockoutNames, &CPlayer::GetPlayerKnockouts, &CPlayer::GetKnockout});
		m_CategoriesInitialized = true;
	}
}

CosmeticsVoteManager::CosmeticsVoteManager() :
	m_CategoriesInitialized(false) {}

void CosmeticsVoteManager::AddCategory(const CosmeticCategory &category) { m_Categories.push_back(category); }
void CosmeticsVoteManager::ClearOptions() { m_Options.clear(); }
const std::vector<std::string> &CosmeticsVoteManager::GetOptions() const { return m_Options; }

void CosmeticsVoteManager::CreateStripline(char *pDst, int DstSize, const char *pTitle)
{
	if(!pDst || DstSize <= 0)
		return;
	std::string title = pTitle ? pTitle : "";
	int TitleLen = static_cast<int>(title.length());
	int StripSideLen = std::min(15, std::max(0, (DstSize / 2) - TitleLen - 3));
	std::string s;
	s.reserve(DstSize);
	for(int i = 0; i < StripSideLen; ++i)
		s.push_back('#');
	s += ' ';
	s += title;
	s += ' ';
	for(int i = 0; i < StripSideLen; ++i)
		s.push_back('#');
	std::strncpy(pDst, s.c_str(), static_cast<size_t>(DstSize) - 1);
	pDst[DstSize - 1] = '\0';
}

void CosmeticsVoteManager::SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext)
{
	EnsureCategoriesInitialized();
	m_Options.clear();
	char aHeader[128];

	if(pGameContext && pGameContext->m_pVoteOptionFirst)
		m_Options.push_back(std::string(" "));

	std::unordered_set<std::string> seenNormalized;
	for(const auto &cat : m_Categories)
	{
		std::vector<std::string> lines;
		int active = pPlayer ? (pPlayer->*(cat.GetActive))() : -1;
		const char *owned = pPlayer ? (pPlayer->*(cat.GetOwned))() : nullptr;
		for(int i = 0; i < cat.NumItems; i++)
		{
			if(owned && owned[i] == '1')
			{
				std::string norm = NormalizeForCompare(cat.Names[i]);
				if(norm.empty() || seenNormalized.find(norm) != seenNormalized.end())
					continue;
				seenNormalized.insert(norm);
				std::string line = (active == i ? "\u2612 " : "\u2610 ");
				line += cat.Names[i];
				lines.push_back(line);
			}
		}
		if(!lines.empty())
		{
			CreateStripline(aHeader, sizeof(aHeader), cat.Header);
			m_Options.push_back(std::string(aHeader));
			for(const auto &s : lines)
				m_Options.push_back(s);
		}
	}

	if((!pPlayer || !pPlayer->IsLoggedIn()) && (m_Options.empty() || (m_Options.size() == 1 && m_Options[0] == " ")))
	{
		CreateStripline(aHeader, sizeof(aHeader), "Login to unlock cosmetics!");
		m_Options.push_back(std::string(aHeader));
	}

	int TotalOptions = static_cast<int>(m_Options.size());
	int OptionsSent = 0;
	while(OptionsSent < TotalOptions)
	{
		int index = 0;
		CNetMsg_Sv_VoteOptionListAdd OptionMsg;
		OptionMsg.m_pDescription0 = "";
		OptionMsg.m_pDescription1 = "";
		OptionMsg.m_pDescription2 = "";
		OptionMsg.m_pDescription3 = "";
		OptionMsg.m_pDescription4 = "";
		OptionMsg.m_pDescription5 = "";
		OptionMsg.m_pDescription6 = "";
		OptionMsg.m_pDescription7 = "";
		OptionMsg.m_pDescription8 = "";
		OptionMsg.m_pDescription9 = "";
		OptionMsg.m_pDescription10 = "";
		OptionMsg.m_pDescription11 = "";
		OptionMsg.m_pDescription12 = "";
		OptionMsg.m_pDescription13 = "";
		OptionMsg.m_pDescription14 = "";

		while(index < 15 && OptionsSent < TotalOptions)
		{
			const char *pOption = m_Options[OptionsSent].c_str();
			SetVoteDescriptionAtIndex(&index, pOption, &OptionMsg);
			OptionsSent++;
		}

		OptionMsg.m_NumOptions = index;
		pServer->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
	}
}

bool CosmeticsVoteManager::HandleVote(CPlayer *pPlayer, const std::string &voteInput, int ClientId, CGameContext *pGameContext)
{
	if(!pPlayer || !pGameContext)
		return false;
	EnsureCategoriesInitialized();
	std::string normalizedVote = NormalizeForCompare(voteInput);

	if(pGameContext && pGameContext->m_pVoteOptionFirst)
	{
		CVoteOptionServer *pOption = pGameContext->m_pVoteOptionFirst;
		while(pOption)
		{
			std::string optionDesc = NormalizeForCompare(pOption->m_aDescription);
			if(!optionDesc.empty() && (normalizedVote.find(optionDesc) != std::string::npos || optionDesc.find(normalizedVote) != std::string::npos))
			{
				pGameContext->CallVote(ClientId, pOption->m_aDescription, pOption->m_aCommand, "", "", nullptr);
				return true;
			}
			pOption = pOption->m_pNext;
		}
	}

	for(const auto &cat : m_Categories)
	{
		for(int i = 0; i < cat.NumItems; ++i)
		{
			std::string Name = cat.Names[i];
			std::string normalizedName = NormalizeForCompare(Name);
			if(normalizedName.empty())
				continue;
			if(normalizedVote.find(normalizedName) != std::string::npos)
			{
				bool toggled = false;
				std::string headerStr = std::string(cat.Header ? cat.Header : "");
				if(headerStr == "Skin Manipulations")
					toggled = pGameContext->Cosmetics()->ToggleSkinmani(ClientId, Name.c_str());
				else if(headerStr == "Gun Designs")
					toggled = pGameContext->Cosmetics()->ToggleGundesign(ClientId, Name.c_str());
				else if(headerStr == "Knockout Effects")
					toggled = pGameContext->Cosmetics()->ToggleKnockout(ClientId, Name.c_str());
				else
					toggled = pGameContext->Cosmetics()->ToggleGundesign(ClientId, Name.c_str()) || pGameContext->Cosmetics()->ToggleKnockout(ClientId, Name.c_str()) || pGameContext->Cosmetics()->ToggleSkinmani(ClientId, Name.c_str());
				if(toggled)
				{
					pGameContext->ClearVotes(ClientId);
					return true;
				}
				pGameContext->SendChatTarget(ClientId, "Unknown cosmetics option selected.");
				return false;
			}
		}
	}

	pGameContext->SendChatTarget(ClientId, "Unknown cosmetics option selected.");
	return false;
}
