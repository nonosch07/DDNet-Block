#include "votemanager.h"
#include "cosmetics.h"
#include "extras.h"
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

// local copy of normalization used by cosmetics votes (keeps module independent)
static std::string NormalizeForCompareLocal(const std::string &s)
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
	// to lower ascii
	std::string ret;
	ret.reserve(out.size());
	for(unsigned char c : out)
		ret.push_back(static_cast<char>(std::tolower(c)));
	return ret;
}

class CVoteCosmetics : public IVoteModule
{
public:
	CVoteCosmetics() = default;
	void EnsureInitialized() override { m_Cosmetics.EnsureCategoriesInitialized(); }
	void SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext) override
	{
		m_Cosmetics.SendOptions(pPlayer, ClientID, pServer, pGameContext);
	}
	bool HandleVote(CPlayer *pPlayer, const std::string &voteInput, int ClientId, CGameContext *pGameContext) override
	{
		return m_Cosmetics.HandleVote(pPlayer, voteInput, ClientId, pGameContext);
	}

private:
	CosmeticsVoteManager m_Cosmetics;
};

CVoteManager g_VoteManager;

CVoteManager::CVoteManager()
{
	// register default modules: extras first so it appears above cosmetics
	m_vModules.push_back(std::make_unique<CVoteExtras>());
	m_vModules.push_back(std::make_unique<CVoteCosmetics>());
}

void CVoteManager::RegisterModule(std::unique_ptr<IVoteModule> pModule)
{
	m_vModules.push_back(std::move(pModule));
}

void CVoteManager::EnsureInitialized()
{
	if(m_Initialized)
		return;
	for(auto &m : m_vModules)
		m->EnsureInitialized();
	m_Initialized = true;
}

void CVoteManager::SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext)
{
	EnsureInitialized();
	for(auto &m : m_vModules)
		m->SendOptions(pPlayer, ClientID, pServer, pGameContext);
}

bool CVoteManager::HandleVote(CPlayer *pPlayer, const std::string &voteInput, int ClientId, CGameContext *pGameContext)
{
	EnsureInitialized();
	for(auto &m : m_vModules)
	{
		if(m->HandleVote(pPlayer, voteInput, ClientId, pGameContext))
			return true;
	}
	return false;
}
