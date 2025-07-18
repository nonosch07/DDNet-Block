#include "eventhandler.h"
#include "../tdm/teamdeathmatch.h"
#include "event_base.h"
#include "game/server/gamecontext.h"
#include "game/server/player.h"

#include "engine/shared/config.h"

BW_CEventHandler::BW_CEventHandler(CGameContext *pGameContext, CPlayer *pInviteFrom, int _pEvent, CPlayer *(*pInvited)[MAX_CLIENTS]) :
	CEvent(pGameContext, CEvent::EVENT_INVITE)
{
	m_pGameContext = pGameContext;
	m_pExpireTick = GameServer()->Server()->Tick() + g_Config.m_SvLMBRegistrationTime * GameServer()->Server()->TickSpeed();
	m_pInviteFrom = pInviteFrom;
	m_pEvent = _pEvent;

	char aBuf[256];
	const char *pEventString;

	if(_pEvent == CEvent::EVENT_TDM)
	{
		pEventString = "TDM";
	}
	else
	{
		GameServer()->SendChat(-1, -2, "Error: Invalid event type. stopping.");
		Destroy();
		return;
	}
	m_pEventString = (char *)pEventString;

	for(CEvent *pEvent : pGameContext->m_vEvents)
	{
		if(pEvent == this)
			continue;
		if(pEvent->pGetGametype() == CEvent::EVENT_INVITE)
		{
			GameServer()->SendChat(-1, -2, "There already is an ongoing public event.");
			Destroy();
			return;
		}
		else if(pEvent->isPublic())
		{
			str_format(aBuf, sizeof(aBuf), "There already is an ongoing public event (%s).", pEvent->getEventString());
			GameServer()->SendChat(-1, -2, aBuf);
			Destroy();
			return;
		}
	}

	if(pInvited == nullptr)
	{
		pInvited = &pGameContext->m_apPlayers;
	}

	CPlayer **pArray = *pInvited;

	str_format(aBuf, sizeof(aBuf), "Event '%s' has started . Join with '/sub'.", pEventString);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pArray[i] != nullptr)
			GameServer()->SendChatTarget(i, aBuf);
	}
}

void BW_CEventHandler::OnTick()
{
	if(GameServer()->Server()->Tick() >= m_pExpireTick)
	{
		if(m_pJoined.size() >= static_cast<std::vector<CPlayer *>::size_type>(g_Config.m_SvLMBMinimumCandidates))
		{
			StartEvent();
			Destroy();
		}
		else
		{
			Expire();
		}
		return;
	}

	if((GameServer()->Server()->Tick() % GameServer()->Server()->TickSpeed()) * 2 != 0)
		return;

	int m_StartTimer = (int)((m_pExpireTick - GameServer()->Server()->Tick()) / GameServer()->Server()->TickSpeed());
	int leftMinutes = (int)0;
	int leftSeconds = m_StartTimer % 60;

	char aBuf[256];
	char firstLine[128];
	if(leftMinutes == 0)
	{
		if(leftSeconds == 0)
		{
			if(m_pJoined.size() >= static_cast<std::vector<CPlayer *>::size_type>(g_Config.m_SvLMBMinimumCandidates))
			{
				str_format(firstLine, sizeof(firstLine), "Event '%s' is starting..", m_pEventString);
			}
			else
			{
				return; // chat message sent
			}
		}
		else if(leftSeconds == 1)
		{
			str_format(firstLine, sizeof(firstLine), "Event '%s' starts in %d second", m_pEventString, leftSeconds);
		}
		else
		{
			str_format(firstLine, sizeof(firstLine), "Event '%s' starts in %d seconds", m_pEventString, leftSeconds);
		}
	}
	else
	{
		if(leftSeconds == 0)
		{
			if(leftMinutes == 1)
				str_format(firstLine, sizeof(firstLine), "Event '%s' starts in %d minute", m_pEventString, leftMinutes);
			else
				str_format(firstLine, sizeof(firstLine), "Event '%s' starts in %d minutes", m_pEventString, leftMinutes);
		}
		else if(leftMinutes == 1)
		{
			if(leftSeconds == 1)
				str_format(firstLine, sizeof(firstLine), "Event '%s' starts in %d minute and %d second", m_pEventString, leftMinutes, leftSeconds);
			else
				str_format(firstLine, sizeof(firstLine), "Event '%s' starts in %d minute and %d seconds", m_pEventString, leftMinutes, leftSeconds);
		}
		else
		{
			if(leftSeconds == 1)
				str_format(firstLine, sizeof(firstLine), "Event '%s' starts in %d minutes and %d second", m_pEventString, leftMinutes, leftSeconds);
			else
				str_format(firstLine, sizeof(firstLine), "Event '%s' starts in %d minutes and %d seconds", m_pEventString, leftMinutes, leftSeconds);
		}
	}

	str_format(aBuf, sizeof(aBuf), "%s\n"
				       "%d participant%s (min %d)\n"
				       "Write '/sub' to take part!\n"
				       "                                                                                                                                                                               ",
		firstLine, (int)m_pJoined.size(), m_pJoined.size() == 1 ? "" : "s", g_Config.m_SvLMBMinimumCandidates);
	GameServer()->SendBroadcast(aBuf, -1, true);

	m_StartTimer--;
}

void BW_CEventHandler::StartEvent()
{
	if(m_pEvent == CEvent::EVENT_TDM)
	{
		CTeamDeathmatch *pTDM = new CTeamDeathmatch(GameServer());
		int i = 0;
		int switchTeamAtIndex = (int)(m_pJoined.size() / 2);
		for(CPlayer *pPlayer : m_pJoined)
		{
			if(i >= switchTeamAtIndex)
			{
				pTDM->AddRightTeam(pPlayer->GetCid(), true);
				GameServer()->SendChatTarget(pPlayer->GetCid(), "You have been automatically assigned to the Team Red.");
			}
			else
			{
				pTDM->AddLeftTeam(pPlayer->GetCid(), true);
				GameServer()->SendChatTarget(pPlayer->GetCid(), "You have been automatically assigned to the Team Blue.");
			}
			i++;
		}
		pTDM->m_MaxRounds = 10;
		pTDM->m_isPublic = true;
		pTDM->Start(pTDM->m_Clan1IDs, pTDM->m_Clan2IDs, true);
	}
	else
	{
		Destroy();
	}
}
void BW_CEventHandler::Expire()
{
	GameServer()->SendBroadcast(" ", -1); // Clear console
	char aBuffer[256];
	str_copy(aBuffer, "Not enough participants to start the event.", sizeof(aBuffer));
	GameServer()->SendChat(-1, -2, aBuffer);
	Destroy();
}

bool BW_CEventHandler::playersInclude(int pPlayerID)
{
	for(CPlayer *pPlayer : m_pJoined)
	{
		if(pPlayer && pPlayer->GetCid() == pPlayerID)
			return true;
	}
	return false;
}

void BW_CEventHandler::Accept(CPlayer *pAccepter)
{
	if(destroy)
		return;
	if(!pAccepter)
		return;
	char aBuf[256];
	for(CPlayer *pPlayer : m_pJoined)
	{
		if(pPlayer == pAccepter)
		{
			str_copy(aBuf, "You have already entered the registrations. Use '/leave' if you want to leave.", sizeof(aBuf));
			GameServer()->SendChatTarget(pPlayer->GetCid(), aBuf);
			return;
		}
	}
	m_pJoined.push_back(pAccepter);
	str_format(aBuf, sizeof(aBuf), "You have successfully joined the %s.", m_pEventString);
	GameServer()->SendChatTarget(pAccepter->GetCid(), aBuf);
}

bool BW_CEventHandler::Leave(CPlayer *pPlayer)
{
	for(auto it = m_pJoined.begin(); it != m_pJoined.end(); ++it)
	{
		CPlayer *pJoinedPlayer = *it;
		if(pPlayer == pJoinedPlayer)
		{
			m_pJoined.erase(it);
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "You have successfully left the waiting for the %s.", m_pEventString);
			GameServer()->SendChatTarget(pJoinedPlayer->GetCid(), aBuf);

			return true;
		}
	}
	return false;
}
void BW_CEventHandler::OnPlayerDisconnect(CPlayer *pPlayer, bool disconnect)
{
	if(!disconnect)
		return;
	Leave(pPlayer);
}
