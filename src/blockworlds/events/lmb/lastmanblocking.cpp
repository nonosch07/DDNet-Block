#include "lastmanblocking.h"
#include "blockworlds/events/base/event_base.h"
#include "game/server/entities/character.h"
#include "game/server/gamemodes/DDRace.h"
#include "game/server/player.h"
#include <vector>

const int StartTimeInTicks = 3 * 50;

CLastManBlocking::CLastManBlocking(CGameContext *pGameContext) :
	CEvent(pGameContext, CEvent::EVENT_LMB)
{
	pPlayers = std::vector<CPlayer *>();
	m_Team = -1;
	m_Started = false;
	m_StartTick = -1;
}

void CLastManBlocking::Join(CPlayer *pPlayer, bool Silent)
{
	if(m_Started)
	{
		GameServer()->SendChatTarget(pPlayer->GetCid(), "The tournament has already started!");
		return;
	}
	for(CPlayer *_Player : pPlayers)
	{
		if(_Player == pPlayer)
		{
			GameServer()->SendChatTarget(pPlayer->GetCid(), "You are already in this tournament!");
			return;
		}
	}
	pPlayers.push_back(pPlayer);
	if(!Silent)
		GameServer()->SendChatTarget(pPlayer->GetCid(), "You have successfully entered the tournament!");
}

bool CLastManBlocking::Leave(CPlayer *pVictim)
{
	return Leave(pVictim, false);
}

bool CLastManBlocking::Leave(CPlayer *pVictim, bool disqualify)
{
	for(auto it = pPlayers.begin(); it != pPlayers.end(); ++it)
	{
		CPlayer *pPlayer = *it;
		if(pVictim == pPlayer)
		{
			pPlayers.erase(it);
			m_aRestorePos[pPlayer->GetCid()] = true;
			pPlayer->m_HideInfo = false;
			LoadPosition(pPlayer);
			if(m_Started)
			{
				if(pPlayers.size() == 1)
				{
					CPlayer *_pPlayer = GameServer()->m_apPlayers[pPlayers[0]->GetCid()];
					pPlayers.clear();
					EndTournament(_pPlayer);
					return true;
				}
				else if(pPlayers.size() > 1 && disqualify)
				{
					GameServer()->SendChatTarget(pPlayer->GetCid(), "You have been disqualified..");
					return true;
				}
			}
			if(!disqualify)
			{
				GameServer()->SendChatTarget(pPlayer->GetCid(), "You have successfully left the tournament.");
			}
			else
			{
				GameServer()->SendChatTarget(pPlayer->GetCid(), "You have been disqualified..");
			}
			return true;
		}
	}
	return false;
}

void CLastManBlocking::Teleport(CPlayer *pPlayer, vec2 pPos)
{
	if(pPlayer->GetCharacter())
		pPlayer->KillCharacter(-3, true);
	CCharacter *pChr1;

	if(!pPlayer->GetCharacter())
		pChr1 = pPlayer->ForceSpawn(pPos, false);
	else
		pChr1 = pPlayer->GetCharacter();
	if(!pChr1 || !pChr1->IsAlive())
	{
		return; // abort if not
	}
	pPlayer->m_HideInfo = true;
	if(pChr1 && pChr1->IsAlive())
	{
		pChr1->Freeze(StartTimeInTicks / GameServer()->Server()->TickSpeed());
		/*
		pChr1->Core()->m_FreezeEnd = GameServer()->Server()->Tick() + StartTimeInTicks;
		pChr1->Core()->m_FreezeStart = GameServer()->Server()->Tick();
		*/
		pChr1->m_FreezeTime = StartTimeInTicks;

		/*if(pChr1->Core())
		{
			pChr1->Core()->m_Vel = vec2(0, 0);
		}
		*/
	}

	CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
	if(m_Team == -1)
		m_Team = pController->Teams().GetFirstEmptyTeam();
	pController->Teams().SetForceCharacterTeam(pPlayer->GetCid(), m_Team);
	pController->Teams().SetTeamLock(m_Team, true);

	GameServer()->Teleport(pChr1, pPos);
}

void CLastManBlocking::Start()
{
	std::vector<vec2> tilePositions;
	// int foundIndex = CGameContext::GetTilePositions(TileID, GameServer(), tilePositions);
	int foundIndexLeft = CGameContext::GetTilePositions(193, m_pGameContext, tilePositions);

	if(foundIndexLeft == 0)
	{
		printf("Something went wrong; No spawn tiles for tournament found.");
		return;
	}
	int i = 0;
	for(CPlayer *pPlayer : pPlayers)
	{
		SavePosition(pPlayer);
		Teleport(pPlayer, tilePositions[i % foundIndexLeft]);

		i++;
	}
	m_Started = true;
	m_StartTick = GameServer()->Server()->Tick();
	GameServer()->SendBroadcast(" ", -1); // clear console
}

void CLastManBlocking::OnTick()
{
	if(pPlayers.size() == 0)
	{
		EndTournament(nullptr);
		return;
	}
	if(m_StartTick + (GameServer()->Server()->TickSpeed() * g_Config.m_SvLMBTournamentTime) <= GameServer()->Server()->Tick())
	{
		for(CPlayer *pPlayer : pPlayers)
		{
			Leave(pPlayer, true);
		}
		char aBuf[256];

		dbg_msg("Tournament", "event timed out after %d seconds.", g_Config.m_SvLMBTournamentTime);
		str_format(aBuf, sizeof(aBuf), "The Tournament has been cancelled due to the max time of %d seconds being exceeded.", g_Config.m_SvLMBTournamentTime);
		GameServer()->SendChat(-1, -2, aBuf);
		GameServer()->SendBroadcast(aBuf, -1);
		EndTournament(nullptr);
		return;
	}
	if((GameServer()->Server()->Tick() - m_StartTick) > StartTimeInTicks)
	{
		for(CPlayer *pPlayer : pPlayers)
		{
			if(!pPlayer || !pPlayer->GetCharacter())
				continue;
			CCharacter *pChr = pPlayer->GetCharacter();
			if(pChr->m_FrozenAndUntouchedSinceTick == -1)
				continue;

			int tick_diff = GameServer()->Server()->Tick() - pChr->m_FrozenAndUntouchedSinceTick;
			if((tick_diff / GameServer()->Server()->TickSpeed()) >= g_Config.m_SvLMBFreezeTime)
			{
				OnCharacterSpawn(pChr);
				break;
			}
		}
	}
}

void CLastManBlocking::EndTournament(CPlayer *pWinner)
{
	if(pWinner != nullptr)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "'%s' has won the Tournament 'LMB'!", GameServer()->Server()->ClientName(pWinner->GetCid()));
		GameServer()->SendChat(-1, -2, aBuf);
		GameServer()->SendBroadcast(aBuf, -1);
		pWinner->m_HideInfo = false;

		//flag
		GameServer()->m_apPlayers[pWinner->GetCid()]->m_EventWinner = true;
		GameServer()->m_apPlayers[pWinner->GetCid()]->m_EventWTick = GameServer()->Server()->TickSpeed() * 60 * g_Config.m_SvEventWinnerFlagDelay;
		GameServer()->SendChatTarget(pWinner->GetCid(), "Double exp enabled");

		m_aRestorePos[pWinner->GetCid()] = true;
		LoadPosition(pWinner);
	}
	else
	{
		char aBuf[256];
		str_copy(aBuf, "Nobody has won the tournament 'LMB'.", sizeof(aBuf));
		GameServer()->SendChat(-1, -2, aBuf);
		GameServer()->SendBroadcast(aBuf, -1);
	}
	Destroy();
}

void CLastManBlocking::OnCharacterSpawn(class CCharacter *pVictim)
{
	if(!m_Started)
		return;
	Leave(pVictim->GetPlayer(), true);
}

bool CLastManBlocking::playersInclude(int pPlayerID)
{
	for(CPlayer *pPlayer : pPlayers)
	{
		if(pPlayer && pPlayer->GetCid() == pPlayerID)
			return true;
	}
	return false;
}

void CLastManBlocking::OnPlayerDisconnect(CPlayer *pPlayer, bool disconnect)
{
	if(!m_Started && !disconnect)
	{
		return;
	}
	Leave(pPlayer, true);
}
