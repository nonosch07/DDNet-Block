#include "1on1.h"
#include "base/system.h"
#include "game/mapitems.h"
#include "blockworlds/accounts.h"
#include "game/server/entities/character.h"
#include "game/server/gamecontext.h"
#include "game/server/gamemodes/DDRace.h"
#include "game/server/player.h"
#include "game/server/save.h"

// thanks to nudelschaft for the help

C1on1::C1on1(CGameContext *pGameContext, int Player1ID, int Player2ID, int Wager) :
	CEvent(pGameContext, CEvent::EVENT_1on1),
	m_Player1ID(Player1ID),
	m_Player2ID(Player2ID),
	m_Score1(0),
	m_Score2(0),
	m_Wager(Wager),
	m_Team(-1),
	m_Unfrozen(false),
	m_FrozenSince1(-1),
	m_FrozenSince2(-1),
	m_TileFreezeSince1(-1),
	m_TileFreezeSince2(-1),
	m_StartTimer(0),
	m_oldChar1(nullptr),
	m_oldChar2(nullptr),
	m_CurrentTick(GameServer()->Server()->Tick())
{
	dbg_msg("1on1", "Initialized 1on1 event: Player1ID=%d, Player2ID=%d, Wager=%d",
		m_Player1ID, m_Player2ID, m_Wager);
}
int GetTilePositions(int TileID, CGameContext *pSelf, std::vector<vec2> &result)
{ // use a vector reference as a parameter
	// std::vector<vec2> result; // no need to declare a local vector
	if(TileID < 0 || TileID > 255)
	{
		return 0;
	}
	int Length = pSelf->Collision()->GetWidth() * pSelf->Collision()->GetHeight(); // get the length of the pointer array
	int foundIndex = 0;
	for(int i = 0; i < Length; i++)
	{ // loop through all indices
		// int Index = (pLayer->m_Data + i);
		if(pSelf->Collision()->GetTileIndex(i) == TileID)
		{ // check if it matches the tile id

			int X = pSelf->Collision()->GetPos(i).x; // get the x coordinate from the index
			int Y = pSelf->Collision()->GetPos(i).y; // get the y coordinate from the index
			result.push_back(vec2(X, Y)); // use push_back to add elements to the vector
			foundIndex++;
		}
	}

	// *_result = result; // no need to assign the vector to another pointer
	return foundIndex;
	// return 0;
}

void C1on1::Start1v1(int Player1ID, int Player2ID)
{
	m_Player1ID = Player1ID;
	m_Player2ID = Player2ID;

	CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
	m_Team = pController->Teams().GetFirstEmptyTeam();
	pController->Teams().SetForceCharacterTeam(Player1ID, m_Team);
	pController->Teams().SetForceCharacterTeam(Player2ID, m_Team);
	pController->Teams().SetTeamLock(m_Team, true);

	// reset the scores
	m_Score1 = 0;
	m_Score2 = 0;

	// get the pointers to the players and characters
	CPlayer *pPlayer1 = GameServer()->m_apPlayers[m_Player1ID];
	CPlayer *pPlayer2 = GameServer()->m_apPlayers[m_Player2ID];

	if(!pPlayer1 || !pPlayer2)
	{
		GameServer()->SendChatTarget(Player1ID, "Something went wrong with the 1v1. Please try again.");
		GameServer()->SendChatTarget(Player2ID, "Something went wrong with the 1v1. Please try again.");
		this->End1vs1(m_Player2ID, false);
		return;
	}

	pPlayer1->m_Score = m_Score1;
	pPlayer2->m_Score = m_Score2;

	CCharacter *pChr1 = pPlayer1->GetCharacter();
	CCharacter *pChr2 = pPlayer2->GetCharacter();

	std::vector<vec2> spawnPosition;
	int spawncount = GetTilePositions(BW_1ON1_START_POS, GameServer(), spawnPosition);

	if(!pChr1 || !pChr2)
	{
		GameServer()->SendChatTarget(Player1ID, "Something went wrong with the 1v1. Please try again.");
		GameServer()->SendChatTarget(Player2ID, "Something went wrong with the 1v1. Please try again.");
		this->End1vs1(m_Player2ID, false);
		return;
	}

	if(!pChr1)
	{
		if(spawncount == 0)
			pChr1 = pPlayer1->ForceSpawn(vec2(0, 0), false);
		else
			pChr1 = pPlayer1->ForceSpawn(spawnPosition[0], false);
	}
	if(!pChr2)
	{
		if(spawncount == 0)
			pChr2 = pPlayer2->ForceSpawn(vec2(0, 0), false);
		else if(spawncount > 1)
			pChr2 = pPlayer2->ForceSpawn(spawnPosition[1], false);
		else
			pChr2 = pPlayer2->ForceSpawn(spawnPosition[0], false);
	}

	SavePosition(pPlayer1);
	SavePosition(pPlayer2);
	pPlayer1->KillCharacter();
	pPlayer2->KillCharacter();

	// get the vector of tile positions from the game context
	Teleport(pPlayer1, pPlayer2);
}

void C1on1::Teleport(CCharacter *pChr, vec2 Pos)
{
	GameServer()->Teleport(pChr, Pos);
}
void C1on1::Teleport(CPlayer *pPlayer1, CPlayer *pPlayer2)
{
	// check if they are valid
	if(!pPlayer1 || !pPlayer2)
	{
		return; // abort if not
	}

	CCharacter *pChr1 = pPlayer1->GetCharacter();
	CCharacter *pChr2 = pPlayer2->GetCharacter();
	if(pChr1)
		pChr1->GetPlayer()->KillCharacter(-3, false);
	if(pChr2)
		pChr2->GetPlayer()->KillCharacter(-3, false);
	// if (!pChr1)
	pChr1 = pPlayer1->ForceSpawn(vec2(0, 0), false);
	// if (!pChr2)
	pChr2 = pPlayer2->ForceSpawn(vec2(0, 0), false);

	if(!pChr1 || !pChr2 || !pChr1->IsAlive() || !pChr2->IsAlive())
	{
		return; // abort if not
	}

	if(pChr1 && pChr1->IsAlive())
	{
		pChr1->Freeze(3);
		pChr1->Core()->m_FreezeEnd = GameServer()->Server()->Tick() + GameServer()->Server()->TickSpeed() * 3;
		pChr1->Core()->m_FreezeStart = GameServer()->Server()->Tick();
		pChr1->m_FreezeTime = GameServer()->Server()->TickSpeed() * 3;
	}

	if(pChr2 && pChr2->IsAlive())
	{
		pChr2->Freeze(3);
		pChr2->Core()->m_FreezeEnd = GameServer()->Server()->Tick() + GameServer()->Server()->TickSpeed() * 3;
		pChr2->Core()->m_FreezeStart = GameServer()->Server()->Tick();
		pChr2->m_FreezeTime = GameServer()->Server()->TickSpeed() * 3;
	}
	if(pChr1->Core() && pChr2->Core())
	{
		pChr1->Core()->m_Vel = vec2(0, 0);
		pChr2->Core()->m_Vel = vec2(0, 0);
	}
	CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;

	pController->Teams().SetForceCharacterTeam(m_Player1ID, m_Team);
	pController->Teams().SetForceCharacterTeam(m_Player2ID, m_Team);
	pController->Teams().SetTeamLock(m_Team, true);

	std::vector<vec2> tilePositions;
	int foundIndex = CGameContext::GetTilePositions(BW_1ON1_START_POS, GameServer(), tilePositions);
	// check if there is at least one tile position
	if(foundIndex > 0)
	{
		// pick a random tile position from the vector
		int randomIndex = rand() % 2;
		// teleport both players to that position
		if(foundIndex >= 2)
		{
			if(randomIndex == 1)
			{
				Teleport(pChr1, tilePositions[1]);
				Teleport(pChr2, tilePositions[0]);
			}
			else
			{
				Teleport(pChr1, tilePositions[0]);
				Teleport(pChr2, tilePositions[1]);
			}
		}
		else
		{
			Teleport(pChr1, tilePositions[0]);
			Teleport(pChr2, tilePositions[0]);
		}

		m_FrozenSince1 = -1;
		m_FrozenSince2 = -1;
	}
}

void C1on1::OnTick()
{
	m_CurrentTick = GameServer()->Server()->Tick();

	if(!GameServer()->m_apPlayers[m_Player1ID])
		C1on1::End1vs1(m_Player1ID, true);
	else if(!GameServer()->m_apPlayers[m_Player2ID])
		C1on1::End1vs1(m_Player2ID, true);
	if((GameServer()->Server()->Tick() % 50) == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s: %d\n%s: %d                                                                                                                                                                               ", GameServer()->Server()->ClientName(m_Player1ID), m_Score1, GameServer()->Server()->ClientName(m_Player2ID), m_Score2);
		GameServer()->SendBroadcast(aBuf, m_Player1ID, false);

		GameServer()->SendBroadcast(aBuf, m_Player2ID, false);
	}

	if(m_StartTimer > 0)
	{
		m_StartTimer--;
		GameServer()->m_apPlayers[m_Player1ID]->m_allowDeath = false;
		GameServer()->m_apPlayers[m_Player2ID]->m_allowDeath = false;
		return;
	}
	else if(m_StartTimer == 0)
	{
		GameServer()->m_apPlayers[m_Player1ID]->m_allowDeath = true;
		GameServer()->m_apPlayers[m_Player2ID]->m_allowDeath = true;
	}
	CCharacter *pChr1 = GameServer()->GetPlayerChar(m_Player1ID);
	CCharacter *pChr2 = GameServer()->GetPlayerChar(m_Player2ID);

	if(!pChr1 || !pChr2 || !pChr1->IsAlive() || !pChr2->IsAlive())
		return;

	const int CurrTick = GameServer()->Server()->Tick();
	const int TickSpeed = GameServer()->Server()->TickSpeed();
	const int FreezeKillDelay = 3 * TickSpeed; // 3 seconds for TILE_FREEZE
	const int FrozenMaxTime = 8 * TickSpeed; // 8 seconds for max time
	if(m_StartTimer != 0)
		return;

	if(pChr1->m_FreezeTime == 0)
	{
		m_FrozenSince1 = -1;
	}
	else if(m_FrozenSince1 == -1 && pChr1->m_FreezeTime > 0)
	{
		m_FrozenSince1 = CurrTick;
	}

	if(pChr2->m_FreezeTime == 0)
	{
		m_FrozenSince2 = -1;
	}
	else if(m_FrozenSince2 == -1 && pChr2->m_FreezeTime > 0)
	{
		m_FrozenSince2 = CurrTick;
	}

	if(pChr1->m_TileIndex == TILE_FREEZE)
	{
		if(m_TileFreezeSince1 == -1)
			m_TileFreezeSince1 = CurrTick;
	}
	else
	{
		m_TileFreezeSince1 = -1;
	}

	if(pChr2->m_TileIndex == TILE_FREEZE)
	{
		if(m_TileFreezeSince2 == -1)
			m_TileFreezeSince2 = CurrTick;
	}
	else
	{
		m_TileFreezeSince2 = -1;
	}

	if(pChr1->m_TileIndex == TILE_FREEZE && pChr2->m_TileIndex == TILE_FREEZE)
	{
		if(m_TileFreezeSince1 != -1 && m_TileFreezeSince2 != -1 &&
			(CurrTick - m_TileFreezeSince1) >= FreezeKillDelay &&
			(CurrTick - m_TileFreezeSince2) >= FreezeKillDelay)
		{
			GameServer()->SendChatTeam(m_Team, "Draw!");
			Teleport(pChr1->GetPlayer(), pChr2->GetPlayer());
			m_TileFreezeSince1 = -1;
			m_TileFreezeSince2 = -1;
		}
		return;
	}

	if(pChr1->m_TileIndex == TILE_FREEZE && m_TileFreezeSince1 != -1)
	{
		if((CurrTick - m_TileFreezeSince1) >= FreezeKillDelay)
		{
			m_TileFreezeSince1 = -1;
			pChr1->Die(m_Player2ID, WEAPON_HAMMER);
			return;
		}
	}

	if(pChr2->m_TileIndex == TILE_FREEZE && m_TileFreezeSince2 != -1)
	{
		if((CurrTick - m_TileFreezeSince2) >= FreezeKillDelay)
		{
			m_TileFreezeSince2 = -1;
			pChr2->Die(m_Player1ID, WEAPON_HAMMER);
			return;
		}
	}

	if(m_FrozenSince1 != -1 && pChr1->m_FreezeTime > 0)
	{
		if((CurrTick - m_FrozenSince1) >= FrozenMaxTime)
		{
			m_FrozenSince1 = -1;
			pChr1->Die(m_Player2ID, WEAPON_HAMMER);
			return;
		}
	}

	if(m_FrozenSince2 != -1 && pChr2->m_FreezeTime > 0)
	{
		if((CurrTick - m_FrozenSince2) >= FrozenMaxTime)
		{
			m_FrozenSince2 = -1;
			pChr2->Die(m_Player1ID, WEAPON_HAMMER);
			return;
		}
	}
}

void C1on1::OnCharacterSpawn(class CCharacter *pVictim)
{
	m_Unfrozen = false;
	char aBuffer[256];
	if(pVictim->GetPlayer()->GetCid() == m_Player1ID)
	{
		str_format(aBuffer, sizeof(aBuffer), "Score for '%s'", GameServer()->Server()->ClientName(m_Player2ID));
		GameServer()->SendChatTarget(m_Player1ID, aBuffer);
		GameServer()->SendChatTarget(m_Player2ID, aBuffer);
		m_Score2++;
	}
	else if(pVictim->GetPlayer()->GetCid() == m_Player2ID)
	{
		str_format(aBuffer, sizeof(aBuffer), "Score for '%s'", GameServer()->Server()->ClientName(m_Player1ID));
		GameServer()->SendChatTarget(m_Player1ID, aBuffer);
		GameServer()->SendChatTarget(m_Player2ID, aBuffer);
		m_Score1++;
	}
	else
	{
		// player not in 1v1 -> skip
		return;
	}
	GameServer()->m_apPlayers[m_Player1ID]->m_Score = m_Score1;
	GameServer()->m_apPlayers[m_Player2ID]->m_Score = m_Score2;

	CCharacter *pChr1 = GameServer()->m_apPlayers[m_Player1ID]->GetCharacter();
	CCharacter *pChr2 = GameServer()->m_apPlayers[m_Player2ID]->GetCharacter();

	char aBuf[256];
	if(m_Score1 == 10 || m_Score2 == 10)
	{
		GameServer()->m_apPlayers[m_Player1ID]->m_Score = 0;
		GameServer()->m_apPlayers[m_Player2ID]->m_Score = 0;

		if(pChr1)
			pChr1->UnFreeze();
		if(pChr2)
			pChr2->UnFreeze();

		if(m_Score1 == 10)
			this->End1vs1(m_Player1ID, false);
		else
			this->End1vs1(m_Player2ID, false);

		return;
	}

	GameServer()->m_apPlayers[m_Player1ID]->m_allowDeath = false;
	GameServer()->m_apPlayers[m_Player2ID]->m_allowDeath = false;

	// Align the broadcast to the left of the screen.
	str_format(aBuf, sizeof(aBuf), "%s: %d\n%s: %d                                                                                                                                                                               ", GameServer()->Server()->ClientName(m_Player1ID), m_Score1, GameServer()->Server()->ClientName(m_Player2ID), m_Score2);
	GameServer()->SendBroadcast(aBuf, m_Player1ID, false);
	GameServer()->SendBroadcast(aBuf, m_Player2ID, false);

	Teleport(GameServer()->m_apPlayers[m_Player1ID], GameServer()->m_apPlayers[m_Player2ID]);
}

void C1on1::End1vs1(int PlayerID, bool Aborted)
{
	for(auto Event = GameServer()->m_vEvents.begin(); Event != GameServer()->m_vEvents.end(); ++Event)
	{
		CEvent *pEvent = *Event;
		if(pEvent->pGetGametype() != CEvent::EVENT_1on1)
			continue;

		C1on1 *pCurrent = (C1on1 *)pEvent;

		if(!(PlayerID == pCurrent->m_Player1ID || PlayerID == pCurrent->m_Player2ID))
			continue;

		CPlayer *pWinner;
		CPlayer *pLoser;

		if((PlayerID != pCurrent->m_Player1ID && Aborted) || pCurrent->m_Score1 > pCurrent->m_Score2)
		{
			pWinner = GameServer()->m_apPlayers[pCurrent->m_Player1ID];
			pLoser = GameServer()->m_apPlayers[pCurrent->m_Player2ID];
		}
		else
		{
			pWinner = GameServer()->m_apPlayers[pCurrent->m_Player2ID];
			pLoser = GameServer()->m_apPlayers[pCurrent->m_Player1ID];
		}

		char aBuf[256];

		if(Aborted)
		{
			str_format(aBuf, sizeof(aBuf), "[1on1] - '%s' has won! ('%s' left the match..)",
				GameServer()->Server()->ClientName(pWinner->GetCid()),
				GameServer()->Server()->ClientName(pLoser->GetCid()));
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "[1on1] - '%s' has won! (Result: '%s': %d - '%s': %d)",
				GameServer()->Server()->ClientName(pWinner->GetCid()),
				GameServer()->Server()->ClientName(pWinner->GetCid()),
				pWinner->GetCid() == pCurrent->m_Player1ID ? pCurrent->m_Score1 : pCurrent->m_Score2,
				GameServer()->Server()->ClientName(pLoser->GetCid()),
				pWinner->GetCid() == pCurrent->m_Player1ID ? pCurrent->m_Score2 : pCurrent->m_Score1);
		}

		if(pCurrent->m_Wager > 0)
		{
			int winnerBp = pWinner->GetPlayerBlockpoints();
			int loserBp = pLoser->GetPlayerBlockpoints();

			if(winnerBp == 0 || loserBp == 0)
			{
				GameServer()->SendChatTarget(-1, "something went VERY wrong, please contact an Admin on discord.");
				return;
			}

			pWinner->SetPlayerBlockpoints(winnerBp + pCurrent->m_Wager);
			pLoser->SetPlayerBlockpoints(loserBp - pCurrent->m_Wager);
		}

		GameServer()->SendChatTarget(-1, aBuf);

		GameServer()->m_vEvents.erase(Event);

		ResetPlayer(pWinner, pCurrent);
		ResetPlayer(pLoser, pCurrent);

		CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
		pController->Teams().SetTeamLock(pCurrent->m_Team, false);
		CleanupEvent();
		delete this;
		pCurrent = nullptr;
		break;
	}
}

void C1on1::ResetPlayer(CPlayer *pPlayer, C1on1 *pCurrent)
{
	if(!pPlayer)
		return;
	pPlayer->m_Score = 0;
	pPlayer->sent1on1InviteTo = '\0';
	pPlayer->m_allowDeath = true;
	GameServer()->SendBroadcast("", pPlayer->GetCid()); // reset broadcast

	m_aRestorePos[pPlayer->GetCid()] = true;
	pCurrent->LoadPosition(pPlayer);
}

bool C1on1::Leave(CPlayer *pPlayer)
{
	if(!pPlayer)
		return false;
	if(playersInclude(pPlayer->GetCid()))
	{
		End1vs1(pPlayer->GetCid(), true);
		return true;
	}
	return false;
}
bool C1on1::playersInclude(int pPlayerID)
{
	if(m_Player1ID == pPlayerID || m_Player2ID == pPlayerID)
	{
		return true;
	}
	return false;
}
