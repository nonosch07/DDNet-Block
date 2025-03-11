#include "requests.h"
#include <game/server/blockworlds/clans.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CClanRequests::CClanRequests(CGameContext *pGameContext, CPlayer *pClanSeeker, CPlayer *pClanOwner, int pClanId, int ExpireInS) :
	m_pGameContext(pGameContext), m_pClanSeeker(pClanSeeker), m_pClanOwner(pClanOwner), m_pClanId(pClanId)
{
	m_pExpireTick = GameServer()->Server()->Tick() + ExpireInS * GameServer()->Server()->TickSpeed();

	if(!pClanSeeker->IsLoggedIn())
	{
		GameServer()->SendChatTarget(pClanSeeker->GetCid(), "You need to be logged in to join a clan.");
		Destroy(true);
		return;
	}

	if(pClanSeeker->GetClanId() != 0)
	{
		GameServer()->SendChatTarget(pClanSeeker->GetCid(), "You are already in a clan.");
		Destroy(true);
		return;
	}

	if(pClanSeeker->GetCharacter())
	{
		if(pClanSeeker->GetCharacter()->m_PendingClanRequests)
		{
			pClanSeeker->GetCharacter()->m_PendingClanRequests->Destroy(false);
		}
		pClanSeeker->GetCharacter()->m_PendingClanRequests = this;

		char aBuf[256];
		const char *pClanName = m_pGameContext->Clans()->GetClanName(pClanId);
		str_format(aBuf, sizeof(aBuf),
			"You have been invited to join the clan '%s'. Confirm with /clan_accept or decline with /clan_decline.",
			pClanName);

		GameServer()->SendChatTarget(pClanSeeker->GetCid(), aBuf);
	}
	else
	{
		Destroy(true);
	}
}

void CClanRequests::OnTick()
{
	if(GameServer()->Server()->Tick() >= m_pExpireTick)
	{
		Expire();
	}
}

void CClanRequests::Expire()
{
	char aBuf[256];
	const char *pClanName = m_pGameContext->Clans()->GetClanName(m_pClanId);
	str_format(aBuf, sizeof(aBuf), "Clan invitation for '%s' expired!", pClanName);
	GameServer()->SendChatTarget(m_pClanSeeker->GetCid(), aBuf);
	GameServer()->SendChatTarget(m_pClanOwner->GetCid(), "Your clan invitation expired.");
	Destroy(true);
}

void CClanRequests::Destroy(bool Silent)
{
	dbg_msg("clan-requests", "destroying clan request from %s",
		GameServer()->Server()->ClientName(m_pClanSeeker->GetCid()));

	if(!Silent)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Clan invitation has been aborted.");
		GameServer()->SendChatTarget(m_pClanSeeker->GetCid(), aBuf);
		GameServer()->SendChatTarget(m_pClanOwner->GetCid(), aBuf);
	}

	if(m_pClanSeeker && m_pClanSeeker->GetCharacter())
		m_pClanSeeker->GetCharacter()->m_PendingClanRequests = nullptr;

	delete this;
}

void CClanRequests::Accept()
{
	if(!m_pClanSeeker->IsLoggedIn() || !m_pClanOwner->IsLoggedIn())
	{
		GameServer()->SendChatTarget(m_pClanSeeker->GetCid(), "You both need to be logged in!");
		Destroy(true);
		return;
	}
	if(m_pClanSeeker->GetClanId() != 0)
	{
		GameServer()->SendChatTarget(m_pClanSeeker->GetCid(), "You are already in a clan.");
		Destroy(true);
		return;
	}

	if(!m_pClanOwner->GetClanId())
	{
		GameServer()->SendChatTarget(m_pClanSeeker->GetCid(), "Clan is no longer joinable. If you think this is a bug, contact an admin.");
		Destroy(true);
		return;
	}

	m_pGameContext->Clans()->AssignClan(m_pClanSeeker->GetCid(),
		m_pClanSeeker->GetPlayerName(),
		m_pClanId,
		m_pClanSeeker->GetAccId());

	m_pClanSeeker->SetAuthLevel(1);

	char aBuf[256];
	const char *pClanName = m_pGameContext->Clans()->GetClanName(m_pClanId);
	str_format(aBuf, sizeof(aBuf), "You have successfully joined the clan '%s'.", pClanName);
	GameServer()->SendChatTarget(m_pClanSeeker->GetCid(), aBuf);
	GameServer()->SendChatTarget(m_pClanOwner->GetCid(), "The invited player has accepted your clan invitation.");
	Destroy(true);
}

void CClanRequests::Decline()
{
	GameServer()->SendChatTarget(m_pClanSeeker->GetCid(), "You have declined the clan invitation.");
	GameServer()->SendChatTarget(m_pClanOwner->GetCid(), "The clan invitation was declined.");
	Destroy(false);
}
