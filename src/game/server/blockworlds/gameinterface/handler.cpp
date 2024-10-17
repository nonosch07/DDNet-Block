#include <game/server/gamecontext.h>

#include "renderers/vote.h"

#include "handler.h"

void CGameInterfaceHandler::Init(CGameContext *pGameServer)
{
	m_aRenderers[GAMEINT_VOTE] = new CVotes;
	m_aRenderers[GAMEINT_VOTE]->Init(pGameServer);
}

void CGameInterfaceHandler::Tick()
{
	if(m_aRenderers[GAMEINT_VOTE])
	{
		m_aRenderers[GAMEINT_VOTE]->Tick();
	}
}

void ShitYourself(int ClientID, IGameInterfaceRenderer *pRenderer, void *pUserData)
{
	auto *pSelf = (CGameContext *)pUserData;

	pSelf->SendBroadcast("\n\n\n\n\n\nSHIT YOURSELF\n\n\n\n\n       NOW!", ClientID);

	pRenderer->Deactivate(ClientID);
}

void ShrimpYourself(int ClientID, IGameInterfaceRenderer *pRenderer, void *pUserData)
{
	auto *pSelf = (CGameContext *)pUserData;

	pSelf->SendBroadcast("\n\n\n\n\n\nKILL YOURSELF\n\n\n\n\n       NOW!", ClientID);

	pRenderer->Deactivate(ClientID);
}

void CGameInterfaceHandler::OnClientEnter(int ClientID)
{
	auto *pRenderer = m_aRenderers[GAMEINT_VOTE];

	if(pRenderer)
	{
		auto *Tree = pRenderer->InterfaceTree(ClientID);

		Tree->SetValue({"Welcome Menu"});
		Tree->AddChild({"Hello!"});
		Tree->AddChild({"You can see these lines! I swear..."});

		Tree->AddChild({"What can you do there?",
			{
				{{ShrimpYourself, "Block"}},
				{{ShrimpYourself, "Shittalk"}},
				{{ShitYourself, "Shit yourself"}},
			}});

		pRenderer->Activate(ClientID);
		pRenderer->Render(ClientID);
	}
}

void CGameInterfaceHandler::OnClientDrop(int ClientID)
{
	auto *pRenderer = m_aRenderers[GAMEINT_VOTE];

	if(pRenderer)
	{
		pRenderer->InterfaceTree(ClientID)->Clear();
		pRenderer->Deactivate(ClientID);
	}

	dbg_msg("gameinterface", "deactivated voting interface for %d", ClientID);
}

void CGameInterfaceHandler::OnClientDirectInput(int ClientID, void *pInput)
{
}

bool CGameInterfaceHandler::OnVoteNetMessage(int ClientID, void *pInput)
{
	return false;
}

bool CGameInterfaceHandler::OnCallVote(int ClientID, const char *pDesc, const char *pReason)
{
	auto *pRenderer = m_aRenderers[GAMEINT_VOTE];

	if(pRenderer && pRenderer->IsActive(ClientID))
	{
		SVoteInput Context = {pDesc, pReason};
		return pRenderer->OnClientInput(ClientID, (void *)(&Context));
	}

	return false;
}
