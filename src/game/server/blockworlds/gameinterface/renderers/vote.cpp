#include <game/server/gamecontext.h>

#include "vote.h"

enum
{
	VOTE_READY_TO_SEND = 0,
	VOTE_IN_PROGRESS,
	VOTE_ALL_SENT,
};

void CVotes::Init(CGameContext *pGameServer)
{
	IGameInterfaceRenderer::Init(pGameServer);
}

void CVotes::Tick()
{
	for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
		if(IsActive(ClientID))
			ProgressVoteOptions(ClientID);
}

void CVotes::Activate(int ClientID)
{
	IGameInterfaceRenderer::Activate(ClientID);
}

void CVotes::Deactivate(int ClientID)
{
	IGameInterfaceRenderer::Deactivate(ClientID);

	InterfaceTree(ClientID)->Clear();
	m_ClientLines[ClientID].clear();
	m_Heap[ClientID].Reset();

	ClearVotes(ClientID);
}

constexpr const char *s_Gap = " ";

std::list<const char *> GenerateList(CHeap *Heap, CGenericTreeElement<CGameInterfaceObject> *Tree, int Depth = 0, bool IsFirstLine = false)
{
	std::list<const char *> Lines;

	auto *Children = Tree->GetChildren();

	int PrefixSize = 0;
	char aPrefix[64] = {0};

	// ─╭╰│ᐅ
	for(int i = 0; i < Depth; i++)
		PrefixSize += str_copy(aPrefix + PrefixSize, "│", sizeof(aPrefix) - PrefixSize);

	if(!Tree->GetChildren()->empty())
	{
		if(!IsFirstLine)
		{
			const char *Gap = Heap->StoreString(aPrefix);
			Lines.push_back(Gap);
		}

		{
			constexpr char s_Heading[] = "╭─ ";

			int HeadingSize = PrefixSize + sizeof(s_Heading) + str_length(Tree->GetValue()->GetText());

			char *Heading = (char *)Heap->Allocate(HeadingSize);

			str_format(Heading, HeadingSize, "%s%s%s", aPrefix, s_Heading, Tree->GetValue()->GetText());

			Lines.push_back(Heading);
		}

		for(CGenericTreeElement<CGameInterfaceObject> &Child : *Children)
		{
			auto ChildrenLines = GenerateList(Heap, &Child, Depth + 1);

			Lines.splice(Lines.end(), ChildrenLines);
		}

		{
			constexpr char s_Ending[] = "╰─────────────";

			int EndingSize = PrefixSize + sizeof(s_Ending);

			char *Ending = (char *)Heap->Allocate(EndingSize);

			str_format(Ending, EndingSize, "%s%s", aPrefix, s_Ending);

			Lines.push_back(Ending);
		}
	}
	else
	{
		if(Tree->GetValue()->GetCallback())
			PrefixSize += str_copy(aPrefix + PrefixSize, "ᐅ", sizeof(aPrefix) - PrefixSize);

		PrefixSize += str_copy(aPrefix + PrefixSize, " ", sizeof(aPrefix) - PrefixSize);

		int LineSize = PrefixSize + str_length(Tree->GetValue()->GetText()) + 1;

		char *Line = (char *)Heap->Allocate(LineSize);

		str_format(Line, LineSize, "%s%s", aPrefix, Tree->GetValue()->GetText());

		Lines.push_back(Line);
	}

	return Lines;
}

void CVotes::Render(int ClientID)
{
	if(!IsActive(ClientID))
		return;

	if(m_ClientState[ClientID] == VOTE_ALL_SENT)
		return;

	m_ClientLines[ClientID] = GenerateList(&m_Heap[ClientID], InterfaceTree(ClientID), 0, true);
	m_ClientLinesCursor[ClientID] = m_ClientLines[ClientID].begin();

	StartVoteGroup(ClientID);
}

bool CVotes::OnClientInput(int ClientID, void *pUserData)
{
	auto *pInput = (SVoteInput *)pUserData;

	auto *pTree = InterfaceTree(ClientID);

	auto *pFound = pTree->FindElement([pInput](CGameInterfaceObject &Obj) { return str_find(pInput->m_pDesc, Obj.GetText()) != nullptr; });

	if(!pFound)
		return false;

	auto Callback = pFound->GetCallback();

	if(Callback)
		Callback(ClientID, this, GameServer());

	return true;
}

void CVotes::StartVoteGroup(int ClientID)
{
	if(m_ClientState[ClientID] != VOTE_READY_TO_SEND)
		return;

	CNetMsg_Sv_VoteOptionGroupStart StartMsg;
	GameServer()->Server()->SendPackMsg(&StartMsg, MSGFLAG_VITAL, ClientID);

	m_ClientState[ClientID] = VOTE_IN_PROGRESS;

	dbg_msg("gameinterface/vote", "started sending group for %d id", ClientID);
}

void CVotes::SendVoteGroup(int ClientID)
{
	if(m_ClientState[ClientID] != VOTE_IN_PROGRESS)
		return;

	CNetMsg_Sv_VoteOptionListAdd OptionMsg;

	int ToSend = 0;

	const char **pDescStart = &OptionMsg.m_pDescription0;
	const char **pDescEnd = &OptionMsg.m_pDescription14 + 1;

	for(const char **pDesc = pDescStart; pDesc < pDescEnd; pDesc++)
		*pDesc = "";

	for(const char **pDesc = pDescStart; pDesc < pDescEnd && m_ClientLinesCursor[ClientID] != m_ClientLines[ClientID].end(); pDesc++, ToSend++, m_ClientLinesCursor[ClientID]++)
		*pDesc = *m_ClientLinesCursor[ClientID];

	OptionMsg.m_NumOptions = ToSend;
	GameServer()->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);

	dbg_msg("gameinterface/vote", "sent %d options for %d id", ToSend, ClientID);
}

void CVotes::EndVoteGroup(int ClientID)
{
	CNetMsg_Sv_VoteOptionGroupEnd EndMsg;
	GameServer()->Server()->SendPackMsg(&EndMsg, MSGFLAG_VITAL, ClientID);

	m_ClientState[ClientID] = VOTE_ALL_SENT;

	dbg_msg("gameinterface/vote", "sent all options for %d id", ClientID);
}

void CVotes::ClearVotes(int ClientID)
{
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	GameServer()->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, ClientID);

	m_ClientState[ClientID] = VOTE_READY_TO_SEND;

	dbg_msg("gameinterface/vote", "cleared votes for %d id", ClientID);
}

void CVotes::ProgressVoteOptions(int ClientID)
{
	if(m_ClientState[ClientID] != VOTE_IN_PROGRESS)
		return;

	SendVoteGroup(ClientID);

	if(m_ClientLinesCursor[ClientID] == m_ClientLines[ClientID].end())
	{
		m_Heap[ClientID].Reset();
		EndVoteGroup(ClientID);
	}
}
