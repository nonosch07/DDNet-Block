#include "1on1.h"
#include <base/system.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/DDRace.h>
#include <game/server/player.h>

#include <blockworlds/discord/webhook.h>

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
	CEventComponent(pGameServer), m_Player1ID(-1), m_Player2ID(-1), m_Score1(0), m_Score2(0), m_Wager(0), m_Team(-1), m_StartTimer(0), m_CurrentTick(0), m_SuppressFinishBroadcast(false)
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
	// mark this team as an event team so team-wide kills are suppressed
	pController->Teams().SetTeamEvent(m_Team, true);
	pController->Teams().SetTeamLock(m_Team, true);

	m_Score1 = 0;
	m_Score2 = 0;
	m_Player1DeathTick = -1;
	m_Player2DeathTick = -1;
	m_LastAwardedPlayer = 0;
	m_LastAwardedTick = -1;
	m_RoundStartTick = Server()->Tick();
	m_BothFrozenSinceTick = -1;
	m_P1InFreezeTile = false;
	m_P2InFreezeTile = false;
	m_P1InFreezeTileTick = -1;
	m_P2InFreezeTileTick = -1;

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

	// If there's a wager, collect escrow from both players up-front. Abort if collection fails.
	if(m_Wager > 0)
	{
		if(!CollectEscrow())
		{
			AbortAndRefund("[1on1] Failed to collect wager from both players. Event aborted.");
			return;
		}
	}

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

	// handle deferred finish restoration outside of death callbacks
	if(GetState() == CEventComponent::EEventState::Ending && m_DeferFinishRestore && m_RestoreAtTick <= m_CurrentTick)
	{
		// restore team lock
		auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
		if(m_Team >= 0)
		{
			pController->Teams().SetTeamEvent(m_Team, false);
			pController->Teams().SetTeamLock(m_Team, false);
		}

		// restore positions
		LoadPosition(m_Player1ID);
		LoadPosition(m_Player2ID);

		m_DeferFinishRestore = false;
		SetState(EEventState::Finished);
		return;
	}
	if(m_Player1ID < 0 || m_Player2ID < 0)
		return;

	if(!GameServer()->GetPlayer(m_Player1ID) || !GameServer()->GetPlayer(m_Player2ID))
	{
		FinishEvent();
		return;
	}

	if(Config()->m_Sv1on1BroadcastRate > 0 && (Server()->Tick() % Config()->m_Sv1on1BroadcastRate) == 0)
	{
		static constexpr const char *s_padding = "                                                                                     "
							 "                                                                                     "
							 "                                                                                     ";

		GameServer()->SendBroadcast(m_Player1ID, "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1, Server()->ClientName(m_Player2ID), m_Score2, s_padding);
		GameServer()->SendBroadcast(m_Player2ID, "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1, Server()->ClientName(m_Player2ID), m_Score2, s_padding);
	}

	const int GraceTicks = Config()->m_Sv1on1DrawFreezeGrace * Server()->TickSpeed();
	const int StalemateThresholdTicks = Config()->m_Sv1on1DrawFreezeStalemate * Server()->TickSpeed();
	CCharacter *pChr1 = GameServer()->GetPlayerChar(m_Player1ID);
	CCharacter *pChr2 = GameServer()->GetPlayerChar(m_Player2ID);
	bool bothChars = pChr1 && pChr2;
	bool bothInFreezeTile = bothChars && pChr1->Core()->m_IsInFreeze && pChr2->Core()->m_IsInFreeze;

	// track if each player is currently in a freeze tile (perma-freeze context)
	if(pChr1)
	{
		bool inFreeze = pChr1->Core()->m_IsInFreeze; // set in DDRaceTick
		if(inFreeze)
		{
			if(!m_P1InFreezeTile)
				m_P1InFreezeTileTick = m_CurrentTick;
			m_P1InFreezeTile = true;
		}
		else
		{
			m_P1InFreezeTile = false;
		}
	}
	if(pChr2)
	{
		bool inFreeze = pChr2->Core()->m_IsInFreeze;
		if(inFreeze)
		{
			if(!m_P2InFreezeTile)
				m_P2InFreezeTileTick = m_CurrentTick;
			m_P2InFreezeTile = true;
		}
		else
		{
			m_P2InFreezeTile = false;
		}
	}
	if(bothInFreezeTile)
	{
		if(m_BothFrozenSinceTick == -1)
		{
			// only start counting after grace period
			if(m_CurrentTick > m_RoundStartTick + GraceTicks)
				m_BothFrozenSinceTick = m_CurrentTick;
		}
		else if(m_CurrentTick - m_BothFrozenSinceTick >= StalemateThresholdTicks)
		{
			// announce draw due to stalemate
			GameServer()->SendChatTarget(m_Player1ID, "[1on1] Draw (stalemate: both frozen)! Round restarting...");
			GameServer()->SendChatTarget(m_Player2ID, "[1on1] Draw (stalemate: both frozen)! Round restarting...");
			char aDrawBuf[256];
			str_format(aDrawBuf, sizeof(aDrawBuf), "%s: %d\n%s: %d\nStalemate draw! restarting...", Server()->ClientName(m_Player1ID), m_Score1, Server()->ClientName(m_Player2ID), m_Score2);
			GameServer()->SendBroadcast(aDrawBuf, m_Player1ID, false);
			GameServer()->SendBroadcast(aDrawBuf, m_Player2ID, false);
			// reset freeze timers so they can't unfreeze before respawn
			m_Player1DeathTick = -1; // ensure normal death draw logic does not interfere because otherwise we're fucked
			m_Player2DeathTick = -1;
			RestartRoundAfterDraw();
			return;
		}
	}
	else
	{
		m_BothFrozenSinceTick = -1; // reset if condition breaks
	}
}

void COneOnOneEvent::OnCharacterSpawn(int ClientId, vec2 SpawnPos)
{
	// only manage spawns during the active phase of the event
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(ClientId < 0)
		return;
	// don't award points on spawn; scoring is handled on death

	auto p1 = GameServer()->GetPlayer(m_Player1ID);
	auto p2 = GameServer()->GetPlayer(m_Player2ID);
	if(p1)
		p1->m_allowDeath = false;
	if(p2)
		p2->m_allowDeath = false;

	CCharacter *pChr1 = GameServer()->GetPlayerChar(m_Player1ID);
	CCharacter *pChr2 = GameServer()->GetPlayerChar(m_Player2ID);
	if(pChr1 && pChr2)
	{
		std::vector<vec2> tilePositions;
		GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), tilePositions);
		if(tilePositions.size() >= 2)
		{
			GameServer()->Teleport(pChr1, tilePositions[0]);
			GameServer()->Teleport(pChr2, tilePositions[1]);
			pChr1->ResetVelocity();
			pChr2->ResetVelocity();
			pChr1->Freeze(3);
			pChr2->Freeze(3);
		}
	}
}

// award points on death: opponent gets one point (suicides count)
void COneOnOneEvent::OnCharacterDeath(int KillerId, int ClientId, int Weapon)
{
	const char *pVictimName = ClientId >= 0 ? Server()->ClientName(ClientId) : "<none>";

	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(m_DrawRestartInProgress)
		return;

	if(ClientId == m_Player1ID)
	{
		m_Player1DeathTick = Server()->Tick();
	}
	else if(ClientId == m_Player2ID)
	{
		m_Player2DeathTick = Server()->Tick();
	}

	CCharacter *pChr1 = GameServer()->GetPlayerChar(m_Player1ID);
	CCharacter *pChr2 = GameServer()->GetPlayerChar(m_Player2ID);
	bool p1InFreezeTileNow = pChr1 ? pChr1->Core()->m_IsInFreeze : m_P1InFreezeTile;
	bool p2InFreezeTileNow = pChr2 ? pChr2->Core()->m_IsInFreeze : m_P2InFreezeTile;
	if(p1InFreezeTileNow && p2InFreezeTileNow)
	{
		// undo any implicit scoring and restart round as draw
		if(m_Score1 > 0 || m_Score2 > 0)
		{
		}
		GameServer()->SendChatTarget(m_Player1ID, "[1on1] Draw! (both in freeze) Round restarting...");
		GameServer()->SendChatTarget(m_Player2ID, "[1on1] Draw! (both in freeze) Round restarting...");
		char aDrawBuf[256];
		str_format(aDrawBuf, sizeof(aDrawBuf), "%s: %d\n%s: %d\nRound draw! restarting...", Server()->ClientName(m_Player1ID), m_Score1, Server()->ClientName(m_Player2ID), m_Score2);
		GameServer()->SendBroadcast(aDrawBuf, m_Player1ID, false);
		GameServer()->SendBroadcast(aDrawBuf, m_Player2ID, false);
		RestartRoundAfterDraw();
		return;
	}

	if(ClientId == m_Player1ID)
	{
		m_Score2 += 1;
		m_LastAwardedPlayer = 2;
		m_LastAwardedTick = Server()->Tick();
		dbg_msg("1on1", "CharacterDeath: %s died -> %s awarded 1 point. Scores now %d-%d", pVictimName, Server()->ClientName(m_Player2ID), m_Score1, m_Score2);

		if(auto p = GameServer()->GetPlayer(m_Player2ID))
		{
			p->m_Score = m_Score2;
		}
	}
	else if(ClientId == m_Player2ID)
	{
		m_Score1 += 1;
		m_LastAwardedPlayer = 1;
		m_LastAwardedTick = Server()->Tick();
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

	bool DrawDetected = false;
	int tickTolerance = Config()->m_Sv1on1DrawDeathTickTolerance; // max tick diff for immediate dual-death draw
	if(m_Player1DeathTick != -1 && m_Player2DeathTick != -1 && absolute(m_Player1DeathTick - m_Player2DeathTick) <= tickTolerance)
	{
		DrawDetected = true;
	}
	else
	{
		// reuse earlier character pointers to avoid shadowing warnings
		bool Dead1 = !pChr1 || !pChr1->IsAlive();
		bool Dead2 = !pChr2 || !pChr2->IsAlive();
		int extendedWindow = Config()->m_Sv1on1DrawDeathExtendedWindow; // ticks
		if(extendedWindow > 0 && Dead1 && Dead2 && (m_Player1DeathTick != -1 && m_Player2DeathTick != -1) && absolute(m_Player1DeathTick - m_Player2DeathTick) <= extendedWindow)
		{
			DrawDetected = true;
		}
	}

	if(DrawDetected)
	{
		if(m_Score1 > 0)
		{
			m_Score1 -= 1;
			if(auto p = GameServer()->GetPlayer(m_Player1ID))
				p->m_Score = m_Score1;
		}
		if(m_Score2 > 0)
		{
			m_Score2 -= 1;
			if(auto p = GameServer()->GetPlayer(m_Player2ID))
				p->m_Score = m_Score2;
		}

		GameServer()->SendChatTarget(m_Player1ID, "[1on1] Draw! Round restarting...");
		GameServer()->SendChatTarget(m_Player2ID, "[1on1] Draw! Round restarting...");
		char aDrawBuf[256];
		str_format(aDrawBuf, sizeof(aDrawBuf), "%s: %d\n%s: %d\nRound draw! restarting...", Server()->ClientName(m_Player1ID), m_Score1, Server()->ClientName(m_Player2ID), m_Score2);
		GameServer()->SendBroadcast(aDrawBuf, m_Player1ID, false);
		GameServer()->SendBroadcast(aDrawBuf, m_Player2ID, false);

		RestartRoundAfterDraw();
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

void COneOnOneEvent::RestartRoundAfterDraw()
{
	m_DrawRestartInProgress = true;
	m_Player1DeathTick = -1;
	m_Player2DeathTick = -1;
	m_LastAwardedPlayer = 0;
	m_LastAwardedTick = -1;
	m_RoundStartTick = Server()->Tick();
	m_BothFrozenSinceTick = -1;
	m_P1InFreezeTile = false;
	m_P2InFreezeTile = false;
	m_P1InFreezeTileTick = -1;
	m_P2InFreezeTileTick = -1;

	std::vector<vec2> startPositions;
	GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), startPositions);
	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);
	if(p1)
	{
		p1->KillCharacter(WEAPON_WORLD, false);
		p1->ForceSpawn(vec2(0, 0), false);
		// restore event team
		auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
		pController->Teams().SetForceCharacterTeam(m_Player1ID, m_Team);
	}
	if(p2)
	{
		p2->KillCharacter(WEAPON_WORLD, false);
		p2->ForceSpawn(vec2(0, 0), false);
		auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
		pController->Teams().SetForceCharacterTeam(m_Player2ID, m_Team);
	}
	m_DrawRestartInProgress = false;
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
	if(!m_SuppressFinishBroadcast && pWinner && pLoser)
	{
		const char *pName1 = m_Player1ID >= 0 ? Server()->ClientName(m_Player1ID) : "<none>";
		const char *pName2 = m_Player2ID >= 0 ? Server()->ClientName(m_Player2ID) : "<none>";
		const char *pWinnerName = pWinner->GetCid() == m_Player1ID ? pName1 : pName2;
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[1on1] - %s vs %s — %s won! (Result: %d - %d)", pName1, pName2, pWinnerName, m_Score1, m_Score2);
		GameServer()->SendChatTarget(-1, aBuf);

		// post to Discord webhook
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		const char *p1on1Url = g_Config.m_SvDiscordWebhookUrl1on1[0] ? g_Config.m_SvDiscordWebhookUrl1on1 : nullptr;
		if(g_Config.m_SvDiscord1on1Enabled && Discord.IsConfigured(p1on1Url))
		{
			char aMsg[512];
			const char *pMap = Server()->GetMapName();
			str_format(aMsg, sizeof(aMsg), "1on1 finished on %s: %s (%d) vs %s (%d) → Winner: %s | Score %d-%d", pMap ? pMap : "<map>", pName1, m_Score1, pName2, m_Score2, pWinnerName, m_Score1, m_Score2);
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = p1on1Url;
			Discord.Send(aMsg, Opt);
		}
	}

	// safe payout via escrow
	if(m_Wager > 0 && pWinner && pLoser)
	{
		PayoutWinner(pWinner, pLoser);
	}

	// clear per-player broadcasts using non-format overload
	GameServer()->SendBroadcast(" ", m_Player1ID, false);
	GameServer()->SendBroadcast(" ", m_Player2ID, false);

	// defer team unlock and position restore to the next tick to avoid reentrant spawn during death handling
	m_DeferFinishRestore = true;
	m_RestoreAtTick = Server()->Tick() + 1;
	SetState(EEventState::Ending);
}

bool COneOnOneEvent::Leave(int ClientId)
{
	if(ClientId == m_Player1ID || ClientId == m_Player2ID)
	{
		int leaver = ClientId;
		int opponent = (leaver == m_Player1ID) ? m_Player2ID : m_Player1ID;

		const char *pLeaverName = leaver >= 0 ? Server()->ClientName(leaver) : "<unknown>";
		const char *pOpponentName = opponent >= 0 ? Server()->ClientName(opponent) : "<unknown>";

		// ifs core is tied (e.g., 0-0), consider leaver the loser: give opponent a point so FinishEvent picks them
		if(m_Score1 == m_Score2)
		{
			if(opponent == m_Player1ID)
				m_Score1 += 1;
			else
				m_Score2 += 1;
		}

		static const char *s_RagequitMsgs[] = {
			"[1on1] - %s ragequited the match vs %s! What a dramatic exit.",
			"[1on1] - %s has abandoned the duel against %s — coward move!",
			"[1on1] - %s disconnected mid-fight vs %s. GG, we saw nothing...",
			"[1on1] - %s choked under pressure and fled from %s. Shame.",
			"[1on1] - %s decided running was the best strategy against %s. Classic."};
		const int NumMsgs = sizeof(s_RagequitMsgs) / sizeof(s_RagequitMsgs[0]);
		int idx = (int)((Server()->Tick() + leaver) % NumMsgs);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), s_RagequitMsgs[idx], pLeaverName, pOpponentName);
		GameServer()->SendChatTarget(-1, aBuf);

		// make the opponent the winner immediately
		m_SuppressFinishBroadcast = false;
		if(opponent == m_Player1ID)
			m_Score1 = std::max(m_Score2 + 1, m_Score1 + 1);
		else
			m_Score2 = std::max(m_Score1 + 1, m_Score2 + 1);
		FinishEvent();

		// notify Discord about ragequit
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		const char *p1on1Url = g_Config.m_SvDiscordWebhookUrl1on1[0] ? g_Config.m_SvDiscordWebhookUrl1on1 : nullptr;
		if(g_Config.m_SvDiscord1on1Enabled && Discord.IsConfigured(p1on1Url))
		{
			char aMsg[256];
			str_format(aMsg, sizeof(aMsg), "1on1 ragequit: %s left vs %s (score so far %d-%d)", pLeaverName, pOpponentName, m_Score1, m_Score2);
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = p1on1Url;
			Discord.Send(aMsg, Opt);
		}
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

// ========= Wager/Escrow helpers =========

bool COneOnOneEvent::CollectEscrow()
{
	if(m_EscrowCollected || m_Wager <= 0)
		return true;
	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);
	if(!p1 || !p2)
		return false;
	if(!p1->IsLoggedIn() || !p2->IsLoggedIn())
	{
		GameServer()->SendChatTarget(-1, "[1on1] Wager requires both players to be logged in.");
		return false;
	}
	if(p1->GetPlayerBlockpoints() < m_Wager || p2->GetPlayerBlockpoints() < m_Wager)
	{
		char aBuf[256];
		const char *pWho = p1->GetPlayerBlockpoints() < m_Wager ? Server()->ClientName(m_Player1ID) : Server()->ClientName(m_Player2ID);
		str_format(aBuf, sizeof(aBuf), "[1on1] Wager collection failed: %s doesn't have enough blockpoints.", pWho);
		GameServer()->SendChatTarget(-1, aBuf);
		return false;
	}
	// Deduct to escrow
	p1->SetPlayerBlockpoints(p1->GetPlayerBlockpoints() - m_Wager);
	p2->SetPlayerBlockpoints(p2->GetPlayerBlockpoints() - m_Wager);
	GameServer()->Accounts()->Save(m_Player1ID, &p1->m_Account);
	GameServer()->Accounts()->Save(m_Player2ID, &p2->m_Account);
	m_EscrowCollected = true;
	m_EscrowBalance = m_Wager * 2;
	char aBuf[192];
	str_format(aBuf, sizeof(aBuf), "[1on1] Pot started with %d BP from each player.", m_Wager);
	GameServer()->SendChatTarget(m_Player1ID, aBuf);
	GameServer()->SendChatTarget(m_Player2ID, aBuf);
	return true;
}

void COneOnOneEvent::RefundEscrow()
{
	if(!m_EscrowCollected || m_EscrowBalance <= 0)
		return;
	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);
	if(p1)
	{
		p1->SetPlayerBlockpoints(p1->GetPlayerBlockpoints() + m_Wager);
		GameServer()->Accounts()->Save(m_Player1ID, &p1->m_Account);
	}
	if(p2)
	{
		p2->SetPlayerBlockpoints(p2->GetPlayerBlockpoints() + m_Wager);
		GameServer()->Accounts()->Save(m_Player2ID, &p2->m_Account);
	}
	m_EscrowBalance = 0;
	m_EscrowCollected = false;
	if(p1)
		GameServer()->SendChatTarget(m_Player1ID, "[1on1] Escrow refunded.");
	if(p2)
		GameServer()->SendChatTarget(m_Player2ID, "[1on1] Escrow refunded.");
}

void COneOnOneEvent::PayoutWinner(CPlayer *pWinner, CPlayer *pLoser)
{
	if(m_EscrowCollected && m_EscrowBalance == m_Wager * 2)
	{
		pWinner->SetPlayerBlockpoints(pWinner->GetPlayerBlockpoints() + m_EscrowBalance);
		GameServer()->Accounts()->Save(pWinner->GetCid(), &pWinner->m_Account);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[1on1] - %s won the pot of %d blockpoints!", Server()->ClientName(pWinner->GetCid()), m_EscrowBalance);
		GameServer()->SendChatTarget(-1, aBuf);
		m_EscrowBalance = 0;
		m_EscrowCollected = false;
		return;
	}
	// Fallback
	if(m_Wager > 0 && pLoser->GetPlayerBlockpoints() >= m_Wager)
	{
		pLoser->SetPlayerBlockpoints(pLoser->GetPlayerBlockpoints() - m_Wager);
		pWinner->SetPlayerBlockpoints(pWinner->GetPlayerBlockpoints() + m_Wager);
		GameServer()->Accounts()->Save(pLoser->GetCid(), &pLoser->m_Account);
		GameServer()->Accounts()->Save(pWinner->GetCid(), &pWinner->m_Account);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[1on1] - %s won %d blockpoints from %s!", Server()->ClientName(pWinner->GetCid()), m_Wager, Server()->ClientName(pLoser->GetCid()));
		GameServer()->SendChatTarget(-1, aBuf);
	}
	else
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[1on1] - %s could not pay the wager of %d blockpoints to %s!", Server()->ClientName(pLoser->GetCid()), m_Wager, Server()->ClientName(pWinner->GetCid()));
		GameServer()->SendChatTarget(-1, aBuf);
	}
}

void COneOnOneEvent::AbortAndRefund(const char *pReason)
{
	if(pReason && pReason[0])
	{
		GameServer()->SendChatTarget(m_Player1ID, pReason);
		GameServer()->SendChatTarget(m_Player2ID, pReason);
	}
	RefundEscrow();
	// unlock team and restore players immediately
	auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
	if(m_Team >= 0)
	{
		pController->Teams().SetTeamEvent(m_Team, false);
		pController->Teams().SetTeamLock(m_Team, false);
	}
	LoadPosition(m_Player1ID);
	LoadPosition(m_Player2ID);
	SetState(EEventState::Finished);
}

void COneOnOneEvent::EmergencyShutdown(const char *pMsg)
{
	// base handles flags
	CEventComponent::EmergencyShutdown(pMsg);
	// ensure escrow is returned lol
	RefundEscrow();
}
