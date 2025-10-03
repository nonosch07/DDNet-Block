#include "1on1.h"
#include <base/system.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/DDRace.h>
#include <game/server/player.h>

static int GetTilePositions(int TileID, CGameContext *pSelf, std::vector<vec2> &result)
{
	if(TileID < 0 || TileID > 255)
		return 0;
	int Length = pSelf->Collision()->GetWidth() * pSelf->Collision()->GetHeight();
	int foundIndex = 0;
	for(int i = 0; i < Length; i++)
	{
		if(pSelf->Collision()->GetTileIndex(i) == TileID)
		{
			int X = pSelf->Collision()->GetPos(i).x;
			int Y = pSelf->Collision()->GetPos(i).y;
			result.push_back(vec2(X, Y));
			foundIndex++;
		}
	}
	return foundIndex;
}

COneOnOneEvent::COneOnOneEvent(CGameContext *pGameServer) :
	CEventComponent(pGameServer), m_Player1ID(-1), m_Player2ID(-1), m_Score1(0), m_Score2(0), m_Wager(0), m_Team(-1), m_StartTimer(0), m_CurrentTick(0)
{
}

void COneOnOneEvent::Initialize(int Player1ID, int Player2ID, int Wager)
{
	m_Player1ID = Player1ID;
	m_Player2ID = Player2ID;
	m_Wager = Wager;
	StartEvent();
}

void COneOnOneEvent::StartEvent()
{
	auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
	m_Team = pController->Teams().GetFirstEmptyTeam();
	pController->Teams().SetForceCharacterTeam(m_Player1ID, m_Team);
	pController->Teams().SetForceCharacterTeam(m_Player2ID, m_Team);
	pController->Teams().SetTeamLock(m_Team, true);

	m_Score1 = 0;
	m_Score2 = 0;

	// initialize participants' visible score to 0
	CPlayer *pStart1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *pStart2 = GameServer()->GetPlayer(m_Player2ID);
	if(pStart1)
	{
		pStart1->m_Score = 0;
	}
	if(pStart2)
	{
		pStart2->m_Score = 0;
	}

	dbg_msg("1on1", "StartEvent: P1=%d P2=%d wager=%d team=%d", m_Player1ID, m_Player2ID, m_Wager, m_Team);

	// save positions & teeinfos
	SavePosition(m_Player1ID);
	SavePosition(m_Player2ID);

	m_Participants.clear();
	m_Participants.push_back(m_Player1ID);
	m_Participants.push_back(m_Player2ID);

	// teleport/spawn
	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);
	if(!p1 || !p2)
	{
		GameServer()->SendChatTarget(m_Player1ID, "Something went wrong with the 1v1. Please try again.");
		GameServer()->SendChatTarget(m_Player2ID, "Something went wrong with the 1v1. Please try again.");
		FinishEvent();
		return;
	}

	std::vector<vec2> spawnPosition;
	int spawncount = GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), spawnPosition);

	CCharacter *c1 = p1->GetCharacter();
	CCharacter *c2 = p2->GetCharacter();

	if(!c1)
	{
		if(spawncount == 0)
			p1->ForceSpawn(vec2(0, 0), false);
		else
			p1->ForceSpawn(spawnPosition[0], false);
	}
	if(!c2)
	{
		if(spawncount == 0)
			p2->ForceSpawn(vec2(0, 0), false);
		else if(spawncount > 1)
			p2->ForceSpawn(spawnPosition[1], false);
		else
			p2->ForceSpawn(spawnPosition[0], false);
	}

	std::vector<vec2> startPositions;
	GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), startPositions);
	CCharacter *chr1 = GameServer()->GetPlayerChar(m_Player1ID);
	CCharacter *chr2 = GameServer()->GetPlayerChar(m_Player2ID);
	if(chr1)
	{
		if(!startPositions.empty())
			GameServer()->Teleport(chr1, startPositions[0]);
		chr1->ResetVelocity();
		int freezeSec = 3;
		chr1->Freeze(freezeSec);
	}
	if(chr2)
	{
		if(startPositions.size() > 1)
			GameServer()->Teleport(chr2, startPositions[1]);
		else if(!startPositions.empty())
			GameServer()->Teleport(chr2, startPositions[0]);
		chr2->ResetVelocity();
		int freezeSec = 3;
		chr2->Freeze(freezeSec);
	}

	if(p1)
	{
		p1->SetSkinMani(-1);
		if(p1->GetCurrentSpecial() != -1)
			p1->ToggleSpecial(p1->GetCurrentSpecial());
	}
	if(p2)
	{
		p2->SetSkinMani(-1);
		if(p2->GetCurrentSpecial() != -1)
			p2->ToggleSpecial(p2->GetCurrentSpecial());
	}

	m_StartTimer = 0;
	SetState(EEventState::Active);
}

void COneOnOneEvent::OnTick()
{
	m_CurrentTick = Server()->Tick();
	if(m_Player1ID < 0 || m_Player2ID < 0)
		return;

	if(!GameServer()->GetPlayer(m_Player1ID) || !GameServer()->GetPlayer(m_Player2ID))
	{
		FinishEvent();
		return;
	}

	// update broadcasts every second
	if((Server()->Tick() % Server()->TickSpeed()) == 0)
	{
		static constexpr const char *s_padding = "                                                                                     "
							 "                                                                                     "
							 "                                                                                     ";

		GameServer()->SendBroadcast(m_Player1ID, "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1, Server()->ClientName(m_Player2ID), m_Score2, s_padding);
		GameServer()->SendBroadcast(m_Player2ID, "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1, Server()->ClientName(m_Player2ID), m_Score2, s_padding);
	}
}

void COneOnOneEvent::OnCharacterSpawn(int ClientId, vec2 SpawnPos)
{
	if(ClientId < 0)
		return;
	// don't award points on spawn; scoring is handled on death

	auto p1 = GameServer()->GetPlayer(m_Player1ID);
	auto p2 = GameServer()->GetPlayer(m_Player2ID);
	if(p1)
		p1->m_allowDeath = false;
	if(p2)
		p2->m_allowDeath = false;

	std::vector<vec2> tilePositions;
	GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), tilePositions);
	// teleport both to a spawn pair if exists
	if(tilePositions.size() >= 2)
	{
		GameServer()->Teleport(GameServer()->GetPlayerChar(m_Player1ID), tilePositions[0]);
		GameServer()->Teleport(GameServer()->GetPlayerChar(m_Player2ID), tilePositions[1]);

		GameServer()->GetPlayerChar(m_Player1ID)->Freeze(3);
		GameServer()->GetPlayerChar(m_Player2ID)->Freeze(3);
	}
}

// award points on death: opponent gets one point (suicides count)
void COneOnOneEvent::OnCharacterDeath(int KillerId, int ClientId, int Weapon)
{
	const char *pKillerName = KillerId >= 0 ? Server()->ClientName(KillerId) : "<none>";
	const char *pVictimName = ClientId >= 0 ? Server()->ClientName(ClientId) : "<none>";

	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(ClientId == m_Player1ID)
	{
		m_Score2 += 1;
		dbg_msg("1on1", "CharacterDeath: %s died -> %s awarded 1 point. Scores now %d-%d", pVictimName, Server()->ClientName(m_Player2ID), m_Score1, m_Score2);

		if(auto p = GameServer()->GetPlayer(m_Player2ID))
		{
			p->m_Score = m_Score2;
		}
	}
	else if(ClientId == m_Player2ID)
	{
		m_Score1 += 1;
		dbg_msg("1on1", "CharacterDeath: %s died -> %s awarded 1 point. Scores now %d-%d", pVictimName, Server()->ClientName(m_Player1ID), m_Score1, m_Score2);

		if(auto p = GameServer()->GetPlayer(m_Player1ID))
		{
			p->m_Score = m_Score1;
		}
	}
	else
	{
		return;
	}

	// broadcast updated score with padding
	static constexpr const char *s_padding = "                                                                                     "
						 "                                                                                     "
						 "                                                                                     ";

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1, Server()->ClientName(m_Player2ID), m_Score2, s_padding);
	GameServer()->SendBroadcast(aBuf, m_Player1ID, false);
	GameServer()->SendBroadcast(aBuf, m_Player2ID, false);

	if(CheckEndCondition())
		FinishEvent();
}

bool COneOnOneEvent::CheckEndCondition()
{
	return m_Score1 >= 10 || m_Score2 >= 10;
}

void COneOnOneEvent::FinishEvent()
{
	// announce winner and restore players
	CPlayer *pWinner = nullptr;
	CPlayer *pLoser = nullptr;
	if(m_Score1 > m_Score2)
	{
		pWinner = GameServer()->GetPlayer(m_Player1ID);
		pLoser = GameServer()->GetPlayer(m_Player2ID);
	}
	else
	{
		pWinner = GameServer()->GetPlayer(m_Player2ID);
		pLoser = GameServer()->GetPlayer(m_Player1ID);
	}
	if(pWinner && pLoser)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[1on1] - '%s' has won! (Result: %d - %d)", Server()->ClientName(pWinner->GetCid()), m_Score1, m_Score2);
		GameServer()->SendChatTarget(-1, aBuf);
	}

	// restore team lock
	auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
	if(m_Team >= 0)
	{
		pController->Teams().SetTeamLock(m_Team, false);
	}

	// restore positions
	LoadPosition(m_Player1ID);
	LoadPosition(m_Player2ID);

	SetState(EEventState::Finished);
}

bool COneOnOneEvent::Leave(int ClientId)
{
	if(ClientId == m_Player1ID || ClientId == m_Player2ID)
	{
		FinishEvent();
		return true;
	}
	return false;
}

void COneOnOneEvent::OnPlayerDropping(int ClientId)
{
	if(GetState() == CEventComponent::EEventState::Active)
	{
		if(ClientId == m_Player1ID || ClientId == m_Player2ID)
			Leave(ClientId);
	}
}

std::optional<int> COneOnOneEvent::GetScoreOf(int ClientId) const
{
	if(ClientId == m_Player1ID)
		return m_Score1;
	if(ClientId == m_Player2ID)
		return m_Score2;
	return std::nullopt;
}
