#include "teamdeathmatch.h"

#include <blockworlds/accounts.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/DDRace.h>
#include <game/server/player.h>

const bool showTeamIndicator = false;

CTeamDeathmatch::CTeamDeathmatch(CGameContext *pGameContext, int TileID1, int TileID2) :
	CEvent(pGameContext, CEvent::EVENT_TDM)
{
	m_pGameContext = pGameContext;
	m_TileID1 = TileID1;
	m_TileID2 = TileID2;
	m_StartTimer = -1;

	m_Score1 = 0;
	m_Score2 = 0;

	m_Clan1IDs = std::vector<int>();
	m_Clan2IDs = std::vector<int>();
	leftFrozenSince = -1;
	rightFrozenSince = -1;
	m_TeamName1 = (char *)"Blue";
	m_TeamName2 = (char *)"Red";
	m_Team = -1;
	m_Started = false;
}

void CTeamDeathmatch::ChatBroadcastLeftTeam(const char *pText)
{
	for(auto it = m_Clan1IDs.begin(); it != m_Clan1IDs.end(); ++it)
	{
		int pCurrent = *it;
		GameServer()->SendChatTarget(pCurrent, pText);
	}
}
void CTeamDeathmatch::ChatBroadcastRightTeam(const char *pText)
{
	for(auto it = m_Clan2IDs.begin(); it != m_Clan2IDs.end(); ++it)
	{
		int pCurrent = *it;
		GameServer()->SendChatTarget(pCurrent, pText);
	}
}
void CTeamDeathmatch::ChatBroadcast(const char *pText)
{
	ChatBroadcastLeftTeam(pText);
	ChatBroadcastRightTeam(pText);
}
void CTeamDeathmatch::BroadcastLeftTeam(const char *pText)
{
	for(auto it = m_Clan1IDs.begin(); it != m_Clan1IDs.end(); ++it)
	{
		int pCurrent = *it;
		GameServer()->SendBroadcast(pText, pCurrent);
	}
}
void CTeamDeathmatch::BroadcastRightTeam(const char *pText)
{
	for(auto it = m_Clan2IDs.begin(); it != m_Clan2IDs.end(); ++it)
	{
		int pCurrent = *it;
		GameServer()->SendBroadcast(pText, pCurrent);
	}
}
void CTeamDeathmatch::Broadcast(const char *pText)
{
	BroadcastLeftTeam(pText);
	BroadcastRightTeam(pText);
}

void CTeamDeathmatch::AddLeftTeam(int PlayerID, bool Silent)
{
	m_Clan1IDs.push_back(PlayerID);
}
void CTeamDeathmatch::AddRightTeam(int PlayerID, bool Silent)
{
	m_Clan2IDs.push_back(PlayerID);
}
void CTeamDeathmatch::allowDeath(bool allow)
{
	for(auto it = m_Clan1IDs.begin(); it != m_Clan1IDs.end(); ++it)
	{
		int pCurrent = *it;
		GameServer()->m_apPlayers[pCurrent]->m_allowDeath = allow;
	}
	for(auto it = m_Clan2IDs.begin(); it != m_Clan2IDs.end(); ++it)
	{
		int pCurrent = *it;
		GameServer()->m_apPlayers[pCurrent]->m_allowDeath = allow;
	}
}

void CTeamDeathmatch::Restart()
{
	updateScores();
	Start(m_Clan1IDs, m_Clan2IDs, false);
}
void CTeamDeathmatch::ResetPlayer(int pCurrent)
{
	if(!GameServer()->m_apPlayers[pCurrent])
		return;
	CGameControllerDDRace *pController = (CGameControllerDDRace *)m_pGameContext->m_pController;

	GameServer()->m_apPlayers[pCurrent]->s_TDM = 0;
	GameServer()->m_apPlayers[pCurrent]->s_TDM_start = 0;
	GameServer()->m_apPlayers[pCurrent]->s_TDM_team = 0;
	GameServer()->m_apPlayers[pCurrent]->m_Score = 0;
	GameServer()->m_apPlayers[pCurrent]->m_ShowLevel = true;
	GameServer()->m_apPlayers[pCurrent]->m_TeeInfos = GameServer()->m_apPlayers[pCurrent]->m_OldTeeInfos;

	GameServer()->m_World.RemoveEntitiesFromPlayer(pCurrent);
	pController->Teams().SetForceCharacterTeam(pCurrent, 0);
	GameServer()->m_apPlayers[pCurrent]->m_allowDeath = true;
	m_aRestorePos[pCurrent] = true;
	LoadPosition(GameServer()->m_apPlayers[pCurrent]);
}
void CTeamDeathmatch::StopTDM()
{
	for(auto it = m_Clan1IDs.begin(); it != m_Clan1IDs.end(); ++it)
	{
		int pCurrent = *it;
		ResetPlayer(pCurrent);
	}
	for(auto it = m_Clan2IDs.begin(); it != m_Clan2IDs.end(); ++it)
	{
		int pCurrent = *it;
		ResetPlayer(pCurrent);
	}
	allowDeath(true);
	m_Clan1IDs.clear();
	m_Clan2IDs.clear();
	m_Score1 = 0;
	m_Score2 = 0;
	m_Started = false;
	for(auto it = GameServer()->m_vEvents.begin(); it != GameServer()->m_vEvents.end(); ++it)
	{
		if(*it == this)
		{
			GameServer()->m_vEvents.erase(it);
			CleanupEvent();
			delete this;
			return;
		}
	}

	delete this;
}

int CTeamDeathmatch::GetTeam(int pID)
{
	for(auto it = m_Clan1IDs.begin(); it != m_Clan1IDs.end(); ++it)
	{
		int pCurrent = *it;
		if(pCurrent == pID)
			return 0;
	}
	for(auto it = m_Clan2IDs.begin(); it != m_Clan2IDs.end(); ++it)
	{
		int pCurrent = *it;
		if(pCurrent == pID)
			return 1;
	}
	return -1;
}

std::vector<int> *CTeamDeathmatch::GetTeamVector(int i)
{
	if(i == 0)
		return &m_Clan1IDs;
	if(i == 1)
		return &m_Clan2IDs;
	return nullptr;
}

std::vector<int> *CTeamDeathmatch::GetTeamVectorByPlayer(int PlayerID)
{
	int TeamID = GetTeam(PlayerID);
	if(TeamID == -1)
	{
		return nullptr;
	}
	return GetTeamVector(TeamID);
}

std::vector<CPlayer *> CTeamDeathmatch::vIDtovPlayer(std::vector<int> pIDs)
{
	std::vector<CPlayer *> players = std::vector<CPlayer *>();
	for(auto it = pIDs.begin(); it != pIDs.end(); ++it)
	{
		int pCurrent = *it;
		if(m_pGameContext->m_apPlayers[pCurrent])
			players.push_back(m_pGameContext->m_apPlayers[pCurrent]);
	}
	return players;
}

void CTeamDeathmatch::updateScore(int pTeamID)
{
	std::vector<int> *pIDs = GetTeamVector(pTeamID);
	if(pIDs == nullptr)
		return;
	std::vector<CPlayer *> pPlayers = vIDtovPlayer(*pIDs);
	for(CPlayer *pCurrent : pPlayers)
	{
		if(pCurrent != nullptr)
		{
			if(pTeamID == 0)
				pCurrent->m_Score = m_Score1;
			else if(pTeamID == 1)
				pCurrent->m_Score = m_Score2;
		}
	}
}

void CTeamDeathmatch::updateScores()
{
	updateScore(0);
	updateScore(1);
}

void CTeamDeathmatch::doScore(int pTeam)
{
	if(pTeam == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Team '%s' has scored!", m_TeamName1);
		ChatBroadcast(aBuf);
		rightFrozenSince = m_CurrentTick;
		m_Score1++;
		if(m_Score1 >= m_MaxRounds)
		{
			str_format(aBuf, sizeof(aBuf), "Team '%s' has won against Team '%s'! Score: '%s': %d - '%s': %d", m_TeamName1, m_TeamName2, m_TeamName1, m_Score1, m_TeamName2, m_Score2);
			GameServer()->SendChat(-1, -2, aBuf);
			allowDeath(true);
			updateScores();
			StopTDM();
			return;
		}
		else
		{
			allowDeath(false);
			Restart();
		}
	}
	else if(pTeam == 1)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Team '%s' has scored!", m_TeamName2);
		ChatBroadcast(aBuf);
		m_Score2++;
		leftFrozenSince = m_CurrentTick;
		if(m_Score2 >= m_MaxRounds)
		{
			str_format(aBuf, sizeof(aBuf), "Team '%s' has won against Team '%s'! Score: '%s': %d - '%s': %d", m_TeamName2, m_TeamName1, m_TeamName2, m_Score2, m_TeamName1, m_Score1);
			GameServer()->SendChat(-1, -2, aBuf);
			allowDeath(true);
			updateScores();
			StopTDM();
			return;
		}
		else
		{
			allowDeath(false);
			Restart();
		}
	}
}

int CTeamDeathmatch::handleSpectateTDM(CPlayer *pPlayer)
{
	std::vector<int> *pTeamIDs = GetTeamVectorByPlayer(pPlayer->GetCid());
	if(pTeamIDs == nullptr)
	{
		return 0;
	}
	std::vector<CPlayer *> pPlayers = vIDtovPlayer(*pTeamIDs);
	int foundID = -1;
	for(CPlayer *pTargetPlayer : pPlayers)
	{
		if(pTargetPlayer->GetCharacter() && pTargetPlayer->GetCid() != pPlayer->GetCid())
		{
			foundID = pTargetPlayer->GetCid();
			break;
		}
	}
	if(foundID == -1)
	{
		int pEnemyTeam = GetTeam(pPlayer->GetCid());
		if(pEnemyTeam == 0)
			pEnemyTeam = 1;
		else if(pEnemyTeam == 1)
			pEnemyTeam = 0;
		else
			pEnemyTeam = -1;
		doScore(pEnemyTeam);
		return 1;
	}
	else
	{
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 0", pPlayer->GetCid());
		GameServer()->Console()->ExecuteLine(aBuf);
		pPlayer->m_SpectatorId = foundID;
	}
	return 0;
}

void CTeamDeathmatch::OnTick()
{
	if((GameServer()->Server()->Tick() % 50) == 0 && m_StartTimer != -1)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Team '%s': %d\nTeam '%s': %d                                                                                                                                                                               ",
			m_TeamName1,
			m_Score1,
			m_TeamName2,
			m_Score2);

		Broadcast(aBuf);
	}
	if(m_StartTimer > 0)
	{
		m_StartTimer--;
		allowDeath(false);
		return;
	}
	else if(m_StartTimer == 0)
	{
		allowDeath(true);
	}

	if(!m_Started)
		return;
	bool leftFullyFrozen = true;
	bool rightFullyFrozen = true;
	for(auto it = m_Clan1IDs.begin(); it != m_Clan1IDs.end(); ++it)
	{
		int pCurrent = *it;
		if(GameServer()->m_apPlayers[pCurrent] && GameServer()->m_apPlayers[pCurrent]->GetCharacter())
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[pCurrent];
			if(!pPlayer)
				continue;
			pPlayer->m_Score = m_Score1;
			if(!pPlayer->GetCharacter() || !pPlayer->GetCharacter()->IsAlive())
				continue;
			if(pPlayer->m_spectateTDM == true)
			{
				if(handleSpectateTDM(pPlayer))
					return;

				continue;
			}
			if(pPlayer->m_TeeInfos.m_ColorBody != 10223467 && pPlayer->m_TeeInfos.m_ColorFeet != 10223467 && pPlayer->m_TeeInfos.m_UseCustomColor != 1)
			{
				pPlayer->m_OldTeeInfos = pPlayer->m_TeeInfos;
				pPlayer->m_TeeInfos.m_UseCustomColor = 1;
				pPlayer->m_TeeInfos.m_ColorBody = 10223467; // blue
				pPlayer->m_TeeInfos.m_ColorFeet = 10223467;
			}

			CCharacter *pChr = pPlayer->GetCharacter();
			pChr->SetSolo(false);
			/*
			pChr->Core()->m_CollisionDisabled = false;
			pChr->Core()->m_HookHitDisabled = false;
			pChr->Core()->m_LaserHitDisabled = false;
			pChr->Core()->m_HammerHitDisabled = false;
			pChr->Core()->m_GrenadeHitDisabled = false;
			pChr->Core()->m_ShotgunHitDisabled = false;
			*/
			if(pChr->m_FreezeTime > 0)
			{
			}
			else
			{
				leftFullyFrozen = false;
			}
		}
	}

	for(auto it = m_Clan2IDs.begin(); it != m_Clan2IDs.end(); ++it)
	{
		int pCurrent = *it;
		if(GameServer()->m_apPlayers[pCurrent] && GameServer()->m_apPlayers[pCurrent]->GetCharacter())
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[pCurrent];
			if(!pPlayer)
				continue;
			pPlayer->m_Score = m_Score2;
			if(!pPlayer->GetCharacter() || !pPlayer->GetCharacter()->IsAlive())
				continue;
			if(pPlayer->m_spectateTDM == true)
			{
				if(handleSpectateTDM(pPlayer))
					return;
				continue;
			}

			CCharacter *pChr = pPlayer->GetCharacter();
			pChr->SetSolo(false);
			/*
			pChr->Core()->m_CollisionDisabled = false;
			pChr->Core()->m_HookHitDisabled = false;
			pChr->Core()->m_LaserHitDisabled = false;
			pChr->Core()->m_HammerHitDisabled = false;
			pChr->Core()->m_GrenadeHitDisabled = false;
			pChr->Core()->m_ShotgunHitDisabled = false;
			*/
			if(pPlayer->m_TeeInfos.m_ColorBody != 65387 && pPlayer->m_TeeInfos.m_ColorFeet != 65387 && pPlayer->m_TeeInfos.m_UseCustomColor != 1)
			{
				pPlayer->m_OldTeeInfos = pPlayer->m_TeeInfos;
				pPlayer->m_TeeInfos.m_UseCustomColor = 1;
				pPlayer->m_TeeInfos.m_ColorBody = 65387; // red
				pPlayer->m_TeeInfos.m_ColorFeet = 65387;
			}
			if(pChr->m_FreezeTime > 0)
			{
			}
			else
			{
				rightFullyFrozen = false;
				continue;
			}
		}
	}
	if(leftFullyFrozen == true && rightFullyFrozen == true)
	{
		if((m_CurrentTick - leftFrozenSince) > 5 && (m_CurrentTick - rightFrozenSince) > 5)
		{
			ChatBroadcast("Draw!");
			leftFrozenSince = m_CurrentTick;
			rightFrozenSince = m_CurrentTick;

			Restart();
		}
	}
	else if(leftFullyFrozen == true)
	{
		if(leftFrozenSince == -1)
		{
			leftFrozenSince = m_CurrentTick;
		}
		else if((m_CurrentTick - leftFrozenSince) > 5)
		{
			doScore(1);
		}
	}
	else if(rightFullyFrozen == true)
	{
		if(rightFrozenSince == -1)
		{
			rightFrozenSince = m_CurrentTick;
		}
		else if((m_CurrentTick - rightFrozenSince) > 5)
		{
			doScore(0);
		}
	}
	else
	{
		leftFrozenSince = -1;
		rightFrozenSince = -1;
	}
}

void CTeamDeathmatch::Start(std::vector<int> Player1ID, std::vector<int> Player2ID, bool firstStart, int TileID1, int TileID2)
{
	m_StartTimer = 150;
	leftFrozenSince = -1;
	rightFrozenSince = -1;
	m_Started = true;
	std::vector<CPlayer *> players1 = vIDtovPlayer(Player1ID);
	std::vector<CPlayer *> players2 = vIDtovPlayer(Player2ID);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Team '%s': %d\nTeam '%s': %d                                                                                                                                                                               ",
		m_TeamName1,
		m_Score1,
		m_TeamName2,
		m_Score2);

	Broadcast(aBuf);
	Teleport(players1, players2, firstStart, TileID1, TileID2);
}

void CTeamDeathmatch::teleportTeamLoop(std::vector<CPlayer *> pPlayer1, std::vector<vec2> tilePositions, int foundIndex, int item, int savePos)
{
	CGameControllerDDRace *pController = (CGameControllerDDRace *)m_pGameContext->m_pController;
	if(m_Team == -1)
		m_Team = pController->Teams().GetFirstEmptyTeam();
	if(foundIndex == 0)
		return;
	int i = 0;
	for(auto it = pPlayer1.cbegin(); it != pPlayer1.cend(); ++it)
	{
		int randomIndex = rand() % tilePositions.size();
		if(pPlayer1.size() > tilePositions.size())
		{
			randomIndex = i;
			i++;
		}
		vec2 tilePosition = tilePositions[randomIndex];

		CPlayer *pCurrent = *it;

		if(pCurrent)
		{
			if(savePos)
			{
				SavePosition(pCurrent);
				if(GetTeam(pCurrent->GetCid()) == 0)
				{
					pCurrent->m_OldTeeInfos = pCurrent->m_TeeInfos;
					pCurrent->m_TeeInfos.m_UseCustomColor = 1;
					pCurrent->m_TeeInfos.m_ColorBody = 10223467; // blue
					pCurrent->m_TeeInfos.m_ColorFeet = 10223467;
				}
				else
				{
					pCurrent->m_OldTeeInfos = pCurrent->m_TeeInfos;
					pCurrent->m_TeeInfos.m_UseCustomColor = 1;
					pCurrent->m_TeeInfos.m_ColorBody = 65387; // red
					pCurrent->m_TeeInfos.m_ColorFeet = 65387;
				}
			}
			pCurrent->m_ShowLevel = false;
			pCurrent->SetTeam(0);
			pCurrent->m_spectateTDM = false;

			CCharacter *pChr = pCurrent->ForceSpawn(tilePosition, false);

			if(!pChr || !pChr->IsAlive())
			{
				printf("aborted!\n");
				continue;
			}
			pController->Teams().SetForceCharacterTeam(pCurrent->GetCid(), m_Team);

			if(pChr && pChr->IsAlive())
			{
				GameServer()->Teleport(pChr, tilePosition);
				//pChr->Core()->m_Vel = vec2(0, 0);
				vec2 _pos = pChr->m_Pos;
				_pos.y -= 75;

				if(pPlayer1.size() <= tilePositions.size())
					tilePositions.erase(std::remove(tilePositions.begin(), tilePositions.end(), tilePosition), tilePositions.end());

				pChr->Freeze(3);
				/*
				pChr->Core()->m_FreezeEnd = GameServer()->Server()->Tick() + GameServer()->Server()->TickSpeed() * 3;
				pChr->Core()->m_FreezeStart = GameServer()->Server()->Tick();
				*/
				pChr->m_FreezeTime = GameServer()->Server()->TickSpeed() * 3;
			}
		}
	}
	pController->Teams().SetTeamLock(m_Team, true);
}

void CTeamDeathmatch::Teleport(std::vector<CPlayer *> pPlayer1, std::vector<CPlayer *> pPlayer2, bool savePos, int TileIDLeft, int TileIDRight)
{
	if(!pPlayer1.size() || !pPlayer2.size())
	{
		printf("aborted!\n");
		return;
	}
	std::vector<vec2> tilePositionsLeft;
	std::vector<vec2> tilePositionsRight;
	int foundIndexLeft = CGameContext::getSwitchTilePositions(193, -1, -1, m_pGameContext, tilePositionsLeft);
	int foundIndexRight = CGameContext::getSwitchTilePositions(194, -1, -1, m_pGameContext, tilePositionsRight);
	if(foundIndexLeft > 0 && foundIndexRight > 0)
	{
		teleportTeamLoop(pPlayer1, tilePositionsLeft, foundIndexLeft, 5, savePos);
		teleportTeamLoop(pPlayer2, tilePositionsRight, foundIndexRight, 7, savePos);
	}
	else
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "No TDM tiles found. Red: %d, Blue: %d. NOTE: Needed tiles are 193 & 194 in the switch layer. (Please notify an admin)", foundIndexLeft, foundIndexRight);
		m_pGameContext->SendChat(-1, -2, aBuf);
	}
}

void CTeamDeathmatch::OnPlayerDisconnect(CPlayer *pPlayer, bool disconnect)
{
	if(GetTeam(pPlayer->GetCid()) == -1)
	{
		return;
	}
	if(!disconnect)
		return;
	int newLeaderTeam = 0;
	if(pPlayer->s_TDM == 2 || pPlayer->s_TDM == 3)
	{
		newLeaderTeam = GetTeam(pPlayer->GetCid());
	}
	ResetPlayer(pPlayer->GetCid());
	pPlayer->s_TDM = 0;
	pPlayer->s_TDM_start = 0;
	pPlayer->s_TDM_team = 0;
	pPlayer->TDM_invited_by = -1;
	pPlayer->s_TDM = 0;
	pPlayer->m_ShowLevel = true;

	if(pPlayer->IsLoggedIn() && pPlayer->m_ShowLevel)
		pPlayer->GetPlayerLevel();
	else
		pPlayer->m_Score = 0;
	for(auto it = m_Clan1IDs.begin(); it != m_Clan1IDs.end(); ++it)
	{
		int pCurrent = *it;
		if(pPlayer->GetCid() == pCurrent)
		{
			m_Clan1IDs.erase(it);
			break;
		}
	}
	for(auto it = m_Clan2IDs.begin(); it != m_Clan2IDs.end(); ++it)
	{
		int pCurrent = *it;
		if(pPlayer->GetCid() == pCurrent)
		{
			m_Clan2IDs.erase(it);
			break;
		}
	}

	if(m_Clan1IDs.size() == 0 || m_Clan2IDs.size() == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "The TDM was aborted due to one of the teams being empty. Final score: '%s': %d - '%s': %d", m_TeamName1, m_Score1, m_TeamName2, m_Score2);
		ChatBroadcast(aBuf);
		GameServer()->SendChatTarget(pPlayer->GetCid(), aBuf);
		StopTDM();
		return;
	}
	else
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Player '%s' has left the TDM.", GameServer()->Server()->ClientName(pPlayer->GetCid()));
		ChatBroadcast(aBuf);
		str_copy(aBuf, "You have left the TDM.", sizeof(aBuf));
		GameServer()->SendChatTarget(pPlayer->GetCid(), aBuf);
	}

	if(newLeaderTeam != 0 && newLeaderTeam != -1)
	{
		int pTargetID = -1;
		if(newLeaderTeam == 1)
		{
			pTargetID = m_Clan1IDs[0];
			GameServer()->m_apPlayers[pTargetID]->s_TDM = 2;
			GameServer()->m_apPlayers[pTargetID]->s_TDM_team = 1;
		}
		else if(newLeaderTeam == 2)
		{
			pTargetID = m_Clan2IDs[0];
			GameServer()->m_apPlayers[pTargetID]->s_TDM = 3;
			GameServer()->m_apPlayers[pTargetID]->s_TDM_team = 2;
		}
		else
		{
			return;
		}
		if(pTargetID != -1)
			GameServer()->SendChatTarget(pTargetID, "You have been promoted to the new team leader due to the old one leaving.");
	}
}

void CTeamDeathmatch::OnCharacterSpawn(class CCharacter *pVictim)
{
	bool found = false;
	for(auto it = m_Clan1IDs.begin(); it != m_Clan1IDs.end(); ++it)
	{
		int pCurrent = *it;
		if(pVictim->GetPlayer()->GetCid() == pCurrent)
			found = true;
	}

	for(auto it = m_Clan2IDs.begin(); it != m_Clan2IDs.end(); ++it)
	{
		int pCurrent = *it;
		if(pVictim->GetPlayer()->GetCid() == pCurrent)
			found = true;
	}
	if(!found)
		return;
	if(m_Team != -1 && pVictim->GetPlayer()->m_LastSetTeam < GameServer()->Server()->Tick())
	{
		if(pVictim->GetPlayer()->m_spectateTDM)
		{
			pVictim->GetPlayer()->m_spectateTDM = false;
			return;
		}
		CGameControllerDDRace *pController = (CGameControllerDDRace *)m_pGameContext->m_pController;
		pController->Teams().SetForceCharacterTeam(pVictim->GetPlayer()->GetCid(), m_Team);
		if(pVictim->GetPlayer()->GetCharacter() && m_Started && m_StartTimer == 0)
		{
			pVictim->GetPlayer()->m_spectateTDM = true;
		}
	}
}

bool CTeamDeathmatch::Leave(CPlayer *pPlayer)
{
	if(GetTeam(pPlayer->GetCid()) == -1)
	{
		return false;
	}
	OnPlayerDisconnect(pPlayer, true);
	return true;
}

bool CTeamDeathmatch::playersInclude(int pPlayerID)
{
	if(GetTeam(pPlayerID) == -1)
	{
		return false;
	}
	return true;
}
