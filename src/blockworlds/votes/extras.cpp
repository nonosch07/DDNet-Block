#include "extras.h"
#include "cosmetics.h"

#include <game/server/gamecontext.h>

void CVoteExtras::EnsureInitialized()
{
	// nothing to initialize for now
	//ps: skibidi bop yes yes yes
}

void CVoteExtras::SendOptions(CPlayer *pPlayer, int ClientID, IServer *pServer, CGameContext *pGameContext)
{
	if(!pPlayer)
		return;
	bool extrasEligible = (pPlayer->m_LocalPassiveDuration > 0) || (pPlayer->IsLoggedIn() && pPlayer->GetPlayerPassive() > 0);
	if(!extrasEligible)
		return;

	char aHeader[128];
	std::string passiveLine = (pPlayer->IsUsingPassiveProtection() ? "\u2612 " : "\u2610 ");
	passiveLine += "Passive Protection";

	CNetMsg_Sv_VoteOptionListAdd SpacerMsg;
	SpacerMsg.m_pDescription0 = " ";
	SpacerMsg.m_pDescription1 = "";
	SpacerMsg.m_pDescription2 = "";
	SpacerMsg.m_pDescription3 = "";
	SpacerMsg.m_pDescription4 = "";
	SpacerMsg.m_pDescription5 = "";
	SpacerMsg.m_pDescription6 = "";
	SpacerMsg.m_pDescription7 = "";
	SpacerMsg.m_pDescription8 = "";
	SpacerMsg.m_pDescription9 = "";
	SpacerMsg.m_pDescription10 = "";
	SpacerMsg.m_pDescription11 = "";
	SpacerMsg.m_pDescription12 = "";
	SpacerMsg.m_pDescription13 = "";
	SpacerMsg.m_pDescription14 = "";
	SpacerMsg.m_NumOptions = 1;
	pServer->SendPackMsg(&SpacerMsg, MSGFLAG_VITAL, ClientID);

	CNetMsg_Sv_VoteOptionListAdd OptionMsg;
	OptionMsg.m_pDescription0 = aHeader;
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

	CosmeticsVoteManager::CreateStripline(aHeader, sizeof(aHeader), "Extras");
	OptionMsg.m_pDescription0 = aHeader;
	OptionMsg.m_NumOptions = 1;
	pServer->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);

	CNetMsg_Sv_VoteOptionListAdd OptionMsg2;
	OptionMsg2.m_pDescription0 = passiveLine.c_str();
	OptionMsg2.m_pDescription1 = "";
	OptionMsg2.m_pDescription2 = "";
	OptionMsg2.m_pDescription3 = "";
	OptionMsg2.m_pDescription4 = "";
	OptionMsg2.m_pDescription5 = "";
	OptionMsg2.m_pDescription6 = "";
	OptionMsg2.m_pDescription7 = "";
	OptionMsg2.m_pDescription8 = "";
	OptionMsg2.m_pDescription9 = "";
	OptionMsg2.m_pDescription10 = "";
	OptionMsg2.m_pDescription11 = "";
	OptionMsg2.m_pDescription12 = "";
	OptionMsg2.m_pDescription13 = "";
	OptionMsg2.m_pDescription14 = "";
	OptionMsg2.m_NumOptions = 1;
	pServer->SendPackMsg(&OptionMsg2, MSGFLAG_VITAL, ClientID);
}

bool CVoteExtras::HandleVote(CPlayer *pPlayer, const std::string &voteInput, int ClientId, CGameContext *pGameContext)
{
	if(!pPlayer || !pGameContext)
		return false;

	bool extrasEligible = (pPlayer->m_LocalPassiveDuration > 0) || (pPlayer->IsLoggedIn() && pPlayer->GetPlayerPassive() > 0);
	if(!extrasEligible)
		return false;

	std::string norm;
	norm.reserve(voteInput.size());
	for(unsigned char c : voteInput)
		norm.push_back(static_cast<char>(std::tolower(c)));

	if(norm.find("passive") != std::string::npos)
	{
		if(!pPlayer)
		{
			pGameContext->SendChatTarget(ClientId, "Unknown extras option selected.");
			return false;
		}

		pPlayer->TogglePassive();
		char aBuf[256];
		if(pPlayer->IsUsingPassiveProtection())
			str_format(aBuf, sizeof(aBuf), "Passive protection enabled.");
		else
			str_format(aBuf, sizeof(aBuf), "Passive protection disabled.");
		pGameContext->SendChatTarget(ClientId, aBuf);

		pGameContext->ClearVotes(ClientId);
		return true;
	}

	return false;
}
