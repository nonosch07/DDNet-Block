#include "1on1_invite.h"
#include "game/mapitems.h"
#include "blockworlds/events/1on1/1on1.h"
#include "game/server/player.h"

#include "game/server/gamecontext.h"

CInvite::CInvite(CGameContext *pGameContext, CPlayer *pInviteTo, CPlayer *pInviteFrom, int Event, int ExpireInS, int Wager)
{
	m_pGameContext = pGameContext;
	m_ExpireTick = GameServer()->Server()->Tick() + ExpireInS * GameServer()->Server()->TickSpeed();
	m_pInviteTo = pInviteTo;
	m_pInviteFrom = pInviteFrom;
	m_Event = Event;
	m_pInviteFrom->m_EventInvites.push_back(this);
	m_pInviteTo->m_EventInvites.push_back(this);
	GameServer()->m_vEventInvites.push_back(this);
	m_Wager = Wager;
	if(pInviteTo->m_IsDummy)
	{
		GameServer()->SendChatTarget(pInviteFrom->GetCid(), "This player is a bot.");
		Destroy();
		return;
	}
}

void CInvite::Accept()
{
	if(m_Event == CEvent::EVENT_1on1)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Block '%s' 10 times to win!", GameServer()->Server()->ClientName(m_pInviteFrom->GetCid()));

		GameServer()->SendChatTarget(m_pInviteTo->GetCid(), aBuf);
		str_format(aBuf, sizeof(aBuf), "Block '%s' 10 times to win!", GameServer()->Server()->ClientName(m_pInviteTo->GetCid()));
		GameServer()->SendChatTarget(m_pInviteFrom->GetCid(), aBuf);

		m_pInviteFrom->sent1on1InviteTo = m_pInviteTo->GetCid();
		m_pInviteTo->sent1on1InviteTo = m_pInviteFrom->GetCid();

		C1on1 *p1on1 = new C1on1(GameServer(), m_pInviteTo->GetCid(), m_pInviteFrom->GetCid(), m_Wager);
		p1on1->Start1v1(m_pInviteTo->GetCid(), m_pInviteFrom->GetCid());
		Destroy();
		return;
	}
}
void CInvite::Decline()
{
	char aBuf[256];
	str_copy(aBuf, "Match request declined.", sizeof(aBuf));
	GameServer()->SendChatTarget(m_pInviteTo->GetCid(), aBuf);
	str_format(aBuf, sizeof(aBuf), "'%s' has declined your match request.", GameServer()->Server()->ClientName(m_pInviteTo->GetCid()));
	GameServer()->SendChatTarget(m_pInviteFrom->GetCid(), aBuf);
	Destroy();
}

void CInvite::Destroy()
{
	for(auto it = m_pInviteFrom->m_EventInvites.begin(); it != m_pInviteFrom->m_EventInvites.end(); ++it)
	{
		CInvite *pCurrent = *it;
		if(pCurrent == this)
		{
			m_pInviteFrom->m_EventInvites.erase(it);
			break;
		}
	}
	for(auto it = m_pInviteTo->m_EventInvites.begin(); it != m_pInviteTo->m_EventInvites.end(); ++it)
	{
		CInvite *pCurrent = *it;
		if(pCurrent == this)
		{
			m_pInviteTo->m_EventInvites.erase(it);
			break;
		}
	}
	for(auto it = GameServer()->m_vEventInvites.begin(); it != GameServer()->m_vEventInvites.end(); ++it)
	{
		CInvite *pCurrent = *it;
		if(pCurrent == this)
		{
			GameServer()->m_vEventInvites.erase(it);
			break;
		}
	}
	delete this;
}
void CInvite::Expire()
{
	const char *EventString = m_Event == CEvent::EVENT_1on1 ? "1on1" : "invalid event!";

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "The %s invite by '%s' has expired.", EventString, GameServer()->Server()->ClientName(m_pInviteFrom->GetCid()));
	GameServer()->SendChatTarget(m_pInviteTo->GetCid(), aBuf);
	str_format(aBuf, sizeof(aBuf), "Your %s invite to '%s' has expired.", EventString, GameServer()->Server()->ClientName(m_pInviteTo->GetCid()));
	GameServer()->SendChatTarget(m_pInviteFrom->GetCid(), aBuf);
	Destroy();
}

void CInvite::OnTick()
{
	if(GameServer()->Server()->Tick() >= m_ExpireTick || !m_pInviteTo || !m_pInviteFrom)
	{
		Expire();
	}
}
