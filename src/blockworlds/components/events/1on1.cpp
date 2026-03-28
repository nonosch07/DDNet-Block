#include "1on1.h"
#include "1on1_utils.h"
#include <base/system.h>
#include <engine/shared/config.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/DDRace.h>
#include <game/server/player.h>
#include <game/server/teams.h>

#include <blockworlds/discord/webhook.h>

#include <blockworlds/votes/votemanager.h>

COneOnOneEvent::COneOnOneEvent(CGameContext *pGameServer) :
	CComponent(pGameServer), m_Player1ID(-1), m_Player2ID(-1), m_Wager(0), m_Team(-1), m_StartTimer(0), m_CurrentTick(0), m_SuppressFinishBroadcast(false)
{
	m_State.store(EEventState::Created);
	m_EmergencyShutdown = false;
	m_EmergencyMessage[0] = '\0';
}

COneOnOneEvent::~COneOnOneEvent()
{
	// Clear saved tees and weapons; unique_ptr will free saved tees automatically
	m_pSavedPlayers.clear();
	m_SavedWeapons.clear();
}

bool COneOnOneEvent::Initialize(int Player1ID, int Player2ID, int Wager)
{
	m_Player1ID = Player1ID;
	m_Player2ID = Player2ID;
	m_Wager = Wager;
	return StartEvent();
}

bool COneOnOneEvent::InitializeConfigPhase(int Player1ID, int Player2ID, int Wager)
{
	m_Player1ID = Player1ID;
	m_Player2ID = Player2ID;
	m_Wager = Wager;
	m_Config = SMatchConfig{}; // reset to defaults (hammer+gun only)
	m_aDuelVote[0] = 0;
	m_aDuelVote[1] = 0;
	m_DuelVoteLastSendTick = -1;
	m_ConfigStartTick = Server()->Tick();

	// ── Arena setup (same as StartEvent but enters Preparation, no escrow) ──

	auto pController = (CGameControllerDDRace *)GameServer()->m_pController;

	// choose an empty team
	int chosenTeam = -1;
	for(int t = 1; t < NUM_DDRACE_TEAMS; ++t)
	{
		if(pController->Teams().GetTeamState(t) == CGameTeams::TEAMSTATE_EMPTY && !pController->Teams().IsTeamEvent(t))
		{
			chosenTeam = t;
			break;
		}
	}
	m_Team = chosenTeam;
	if(m_Team == -1)
	{
		dbg_msg("1on1", "InitializeConfigPhase failed: no free team available");
		return false;
	}
	pController->Teams().SetForceCharacterTeam(m_Player1ID, m_Team);
	pController->Teams().SetForceCharacterTeam(m_Player2ID, m_Team);
	pController->Teams().SetTeamEvent(m_Team, true);
	pController->Teams().SetTeamLock(m_Team, true);

	// save and disable solo & collision state
	auto &core = pController->Teams().m_Core;
	for(int cid : {m_Player1ID, m_Player2ID})
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(cid);
		if(pChr)
		{
			bool wasSolo = pChr->Core()->m_Solo;
			bool wasCollisionDisabled = pChr->Core()->m_CollisionDisabled;
			m_PrevSoloState[cid] = {wasSolo, wasCollisionDisabled};
			if(wasSolo)
				pChr->SetSolo(false);
			if(wasCollisionDisabled)
				pChr->Core()->m_CollisionDisabled = false;
		}
		else
		{
			bool wasSolo = core.GetSolo(cid);
			m_PrevSoloState[cid] = {wasSolo, false};
			core.SetSolo(cid, false);
		}
	}

	// reset scoring state
	m_Score1.store(0);
	m_Score2.store(0);
	m_Player1DeathTick = -1;
	m_Player2DeathTick = -1;
	m_LastAwardedPlayer = 0;
	m_LastAwardedTick = -1;
	m_ForcedWinnerCid = -1;
	m_PendingAwardTo = 0;
	m_PendingAwardTick = -1;
	m_RoundStartTick = Server()->Tick();
	m_BothFrozenSinceTick = -1;
	m_P1InFreezeTile = false;
	m_P2InFreezeTile = false;
	m_P1InFreezeTileTick = -1;
	m_P2InFreezeTileTick = -1;

	// save positions & weapons
	SaveWeapons(m_Player1ID);
	SaveWeapons(m_Player2ID);
	SavePosition(m_Player1ID);
	SavePosition(m_Player2ID);

	// populate participants
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Participants.clear();
		m_Participants.push_back(m_Player1ID);
		m_Participants.push_back(m_Player2ID);
	}

	// get spawn positions
	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);
	if(!p1 || !p2)
	{
		GameServer()->SendChatTarget(m_Player1ID, "Something went wrong with the 1v1. Please try again.");
		GameServer()->SendChatTarget(m_Player2ID, "Something went wrong with the 1v1. Please try again.");
		AbortAndRefund("[1on1] Setup failed.");
		return false;
	}

	std::vector<vec2> spawnPosition = GameServer()->ZoneManager()->Get1on1PrepPositions();
	int spawncount = (int)spawnPosition.size();

	if(spawncount <= 0)
	{
		m_SpawnReservation.pos1Idx = -1;
		m_SpawnReservation.pos2Idx = -1;
	}
	else if(spawncount == 1)
	{
		m_SpawnReservation.pos1Idx = 0;
		m_SpawnReservation.pos2Idx = 0;
	}
	else
	{
		int idx1 = secure_rand_below(spawncount);
		int idx2 = secure_rand_below(spawncount - 1);
		if(idx2 >= idx1)
			idx2++;
		m_SpawnReservation.pos1Idx = idx1;
		m_SpawnReservation.pos2Idx = idx2;
	}

	// kill & respawn at prep arena positions (no freeze during warmup)
	SaveAndClearCosmetics(m_Player1ID);
	SaveAndClearCosmetics(m_Player2ID);

	if(p1 && p1->GetCharacter())
		p1->KillCharacter(WEAPON_WORLD, false);
	if(p2 && p2->GetCharacter())
		p2->KillCharacter(WEAPON_WORLD, false);

	if(p1 && m_SpawnReservation.pos1Idx >= 0 && m_SpawnReservation.pos1Idx < (int)spawnPosition.size())
	{
		p1->ForceSpawn(spawnPosition[m_SpawnReservation.pos1Idx], false);
	}
	if(p2 && m_SpawnReservation.pos2Idx >= 0 && m_SpawnReservation.pos2Idx < (int)spawnPosition.size())
	{
		p2->ForceSpawn(spawnPosition[m_SpawnReservation.pos2Idx], false);
	}

	m_StartTimer = 0;

	// initialize player state for warmup
	if(p1)
	{
		p1->m_Score = 0;
		p1->m_allowDeath = false;
	}
	if(p2)
	{
		p2->m_Score = 0;
		p2->m_allowDeath = false;
	}

	// enter Preparation state (warmup/config phase)
	SetState(EEventState::Preparation);

	// push both players onto the DUEL_CONFIG vote page
	extern CVoteManager g_VoteManager;
	if(p1)
		g_VoteManager.ForceDuelConfigPage(m_Player1ID, p1, Server(), GameServer());
	if(p2)
		g_VoteManager.ForceDuelConfigPage(m_Player2ID, p2, Server(), GameServer());

	GameServer()->SendChatTarget(m_Player1ID, "[1on1] You have been teleported to the arena.");
	GameServer()->SendChatTarget(m_Player2ID, "[1on1] You have been teleported to the arena.");
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "[1on1] Use the vote menu to configure the match. F3=Start, F4=Cancel. Auto-starts in %ds.", g_Config.m_Sv1on1WarmupSeconds);
		GameServer()->SendChatTarget(m_Player1ID, aBuf);
		GameServer()->SendChatTarget(m_Player2ID, aBuf);
	}
	SendDuelVoteUi();
	dbg_msg("1on1", "InitializeConfigPhase: P1=%d P2=%d wager=%d team=%d", m_Player1ID, m_Player2ID, m_Wager, m_Team);
	return true;
}

void COneOnOneEvent::SendDuelVoteUi()
{
	if(GetState() != EEventState::Preparation || m_ConfigStartTick < 0)
		return;
	int WarmupSec = g_Config.m_Sv1on1WarmupSeconds;
	int elapsed = (int)((Server()->Tick() - m_ConfigStartTick) / Server()->TickSpeed());
	int remaining = WarmupSec - elapsed;
	if(remaining < 1)
		remaining = 1;

	::CNetMsg_Sv_VoteSet Msg;
	Msg.m_Timeout = remaining;
	Msg.m_pDescription = "1on1: F3 = Start  |  F4 = Cancel";
	Msg.m_pReason = "";
	if(m_Player1ID >= 0 && Server()->ClientIngame(m_Player1ID))
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, m_Player1ID);
	if(m_Player2ID >= 0 && Server()->ClientIngame(m_Player2ID))
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, m_Player2ID);

	int yes = (m_aDuelVote[0] == 1 ? 1 : 0) + (m_aDuelVote[1] == 1 ? 1 : 0);
	int no = (m_aDuelVote[0] == -1 ? 1 : 0) + (m_aDuelVote[1] == -1 ? 1 : 0);
	CNetMsg_Sv_VoteStatus Status;
	Status.m_Total = 2;
	Status.m_Yes = yes;
	Status.m_No = no;
	Status.m_Pass = 2 - yes - no;
	if(m_Player1ID >= 0 && Server()->ClientIngame(m_Player1ID))
		Server()->SendPackMsg(&Status, MSGFLAG_VITAL, m_Player1ID);
	if(m_Player2ID >= 0 && Server()->ClientIngame(m_Player2ID))
		Server()->SendPackMsg(&Status, MSGFLAG_VITAL, m_Player2ID);

	m_DuelVoteLastSendTick = Server()->Tick();
}

void COneOnOneEvent::ClearDuelVoteUi()
{
	::CNetMsg_Sv_VoteSet Msg;
	Msg.m_Timeout = 0;
	Msg.m_pDescription = "";
	Msg.m_pReason = "";
	if(m_Player1ID >= 0 && Server()->ClientIngame(m_Player1ID))
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, m_Player1ID);
	if(m_Player2ID >= 0 && Server()->ClientIngame(m_Player2ID))
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, m_Player2ID);
	m_DuelVoteLastSendTick = -1;
}

bool COneOnOneEvent::OnDuelVote(int ClientId, int Vote)
{
	if(GetState() != EEventState::Preparation)
		return false;
	if(ClientId != m_Player1ID && ClientId != m_Player2ID)
		return false;

	int idx = (ClientId == m_Player1ID) ? 0 : 1;
	m_aDuelVote[idx] = (Vote >= 1) ? 1 : -1;

	CNetMsg_Sv_YourVote Confirm = {m_aDuelVote[idx]};
	Server()->SendPackMsg(&Confirm, MSGFLAG_VITAL, ClientId);

	char aBuf[128];
	const char *pName = Server()->ClientName(ClientId);
	if(m_aDuelVote[idx] == 1)
		str_format(aBuf, sizeof(aBuf), "[1on1] %s voted to START.", pName);
	else
		str_format(aBuf, sizeof(aBuf), "[1on1] %s voted to CANCEL.", pName);
	GameServer()->SendChatTarget(m_Player1ID, aBuf);
	GameServer()->SendChatTarget(m_Player2ID, aBuf);

	int yes = (m_aDuelVote[0] == 1 ? 1 : 0) + (m_aDuelVote[1] == 1 ? 1 : 0);
	int no = (m_aDuelVote[0] == -1 ? 1 : 0) + (m_aDuelVote[1] == -1 ? 1 : 0);
	CNetMsg_Sv_VoteStatus Status;
	Status.m_Total = 2;
	Status.m_Yes = yes;
	Status.m_No = no;
	Status.m_Pass = 2 - yes - no;
	if(m_Player1ID >= 0 && Server()->ClientIngame(m_Player1ID))
		Server()->SendPackMsg(&Status, MSGFLAG_VITAL, m_Player1ID);
	if(m_Player2ID >= 0 && Server()->ClientIngame(m_Player2ID))
		Server()->SendPackMsg(&Status, MSGFLAG_VITAL, m_Player2ID);

	extern CVoteManager g_VoteManager;
	for(int cid : {m_Player1ID, m_Player2ID})
	{
		CPlayer *pP = GameServer()->GetPlayer(cid);
		if(pP)
		{
			GameServer()->ClearVotes(cid);
			g_VoteManager.RenderCurrentPage(pP, cid, Server(), GameServer());
		}
	}

	// both F3'ed -> start
	if(m_aDuelVote[0] == 1 && m_aDuelVote[1] == 1)
	{
		GameServer()->SendChatTarget(m_Player1ID, "[1on1] Both players ready - starting match!");
		GameServer()->SendChatTarget(m_Player2ID, "[1on1] Both players ready - starting match!");
		ClearDuelVoteUi();
		for(int cid : {m_Player1ID, m_Player2ID})
		{
			g_VoteManager.NavigateToRoot(cid);
			GameServer()->ClearVotes(cid);
		}
		StartMatchFromConfig();
		return true;
	}

	// both F4'ed -> abort
	if(m_aDuelVote[0] == -1 && m_aDuelVote[1] == -1)
	{
		GameServer()->SendChatTarget(m_Player1ID, "[1on1] Both players cancelled - match aborted.");
		GameServer()->SendChatTarget(m_Player2ID, "[1on1] Both players cancelled - match aborted.");
		ClearDuelVoteUi();
		for(int cid : {m_Player1ID, m_Player2ID})
		{
			g_VoteManager.NavigateToRoot(cid);
			GameServer()->ClearVotes(cid);
		}
		AbortAndRefund(nullptr);
		return true;
	}

	return true;
}

void COneOnOneEvent::StartMatchFromConfig()
{
	// transition from Preparation (warmup/config) to Active
	if(GetState() != EEventState::Preparation)
		return;

	// collect escrow now (deferred from config phase so /leave doesn't lose wager)
	if(m_Wager > 0)
	{
		if(!CollectEscrow())
		{
			AbortAndRefund("[1on1] Failed to collect wager from both players. Match aborted.");
			return;
		}
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		const char *pLogsUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
		if(Discord.IsConfigured(pLogsUrl))
		{
			char aMsg[256];
			str_format(aMsg, sizeof(aMsg), "1on1 wager collected: **%s** vs **%s** | Wager: %d BP", Server()->ClientName(m_Player1ID), Server()->ClientName(m_Player2ID), m_Wager);
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = pLogsUrl;
			Discord.Send(aMsg, Opt);
		}
	}

	// reset scores (warmup kills don't count)
	m_Score1.store(0);
	m_Score2.store(0);
	m_Player1DeathTick = -1;
	m_Player2DeathTick = -1;
	m_LastAwardedPlayer = 0;
	m_LastAwardedTick = -1;
	m_ForcedWinnerCid = -1;
	m_PendingAwardTo = 0;
	m_PendingAwardTick = -1;
	m_RoundStartTick = Server()->Tick();
	m_BothFrozenSinceTick = -1;
	m_P1InFreezeTile = false;
	m_P2InFreezeTile = false;
	m_P1InFreezeTileTick = -1;
	m_P2InFreezeTileTick = -1;

	CPlayer *pStart1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *pStart2 = GameServer()->GetPlayer(m_Player2ID);
	if(pStart1)
		pStart1->m_Score = 0;
	if(pStart2)
		pStart2->m_Score = 0;

	// re-pick spawn positions and respawn both players frozen
	std::vector<vec2> spawnPosition;
	if(m_Config.m_SpawnMode == 1)
		spawnPosition = GameServer()->ZoneManager()->Get1on1ArenaPositions(-1);
	else
		spawnPosition = GameServer()->ZoneManager()->GetNamedQuadCenters("1on1_spawn");
	int spawncount = (int)spawnPosition.size();

	if(spawncount >= 2)
	{
		int idx1 = secure_rand_below(spawncount);
		int idx2 = secure_rand_below(spawncount - 1);
		if(idx2 >= idx1)
			idx2++;
		m_SpawnReservation.pos1Idx = idx1;
		m_SpawnReservation.pos2Idx = idx2;
	}
	else if(spawncount == 1)
	{
		m_SpawnReservation.pos1Idx = 0;
		m_SpawnReservation.pos2Idx = 0;
	}

	auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);

	if(p1)
	{
		pController->Teams().SetForceCharacterTeam(m_Player1ID, m_Team);
		if(p1->GetCharacter())
			p1->KillCharacter(WEAPON_WORLD, false);
		int idx1 = m_SpawnReservation.pos1Idx;
		if(idx1 >= 0 && idx1 < (int)spawnPosition.size())
			p1->ForceSpawn(spawnPosition[idx1], false);
		if(p1->GetCharacter())
			p1->GetCharacter()->FreezeForce(3);
	}
	if(p2)
	{
		pController->Teams().SetForceCharacterTeam(m_Player2ID, m_Team);
		if(p2->GetCharacter())
			p2->KillCharacter(WEAPON_WORLD, false);
		int idx2 = m_SpawnReservation.pos2Idx;
		if(idx2 >= 0 && idx2 < (int)spawnPosition.size())
			p2->ForceSpawn(spawnPosition[idx2], false);
		if(p2->GetCharacter())
			p2->GetCharacter()->FreezeForce(3);
	}

	m_StartTimer = 0;
	m_MatchStartTick = Server()->Tick();

	// apply config settings: endless hook, weapon restrictions and grants
	for(int cid : {m_Player1ID, m_Player2ID})
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(cid);
		if(!pChr)
			continue;

		if(m_Config.m_EndlessHook)
			pChr->SetEndlessHook(true);

		for(int w = 0; w < 6; w++)
		{
			if(m_Config.m_aWeapons[w])
			{
				pChr->GiveWeapon(w);
			}
			else
			{
				pChr->SetWeaponGot(w, false);
				if(pChr->GetActiveWeapon() == w)
				{
					for(int alt = 0; alt < 6; alt++)
					{
						if(m_Config.m_aWeapons[alt])
						{
							pChr->SetActiveWeapon(alt);
							break;
						}
					}
				}
			}
		}
	}

	SetState(EEventState::Active);

	GameServer()->SendChatTarget(m_Player1ID, "[1on1] Match started! Good luck!");
	GameServer()->SendChatTarget(m_Player2ID, "[1on1] Match started! Good luck!");

	dbg_msg("1on1", "StartMatchFromConfig: P1=%d P2=%d wager=%d config(pts=%d time=%d hook=%d)", m_Player1ID, m_Player2ID, m_Wager, m_Config.m_PointsLimit, m_Config.m_TimeLimit, (int)m_Config.m_EndlessHook);
}

bool COneOnOneEvent::StartEvent()
{
	auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
	// choose an empty team that is not currently used by another event
	int chosenTeam = -1;
	for(int t = 1; t < NUM_DDRACE_TEAMS; ++t)
	{
		if(pController->Teams().GetTeamState(t) == CGameTeams::TEAMSTATE_EMPTY && !pController->Teams().IsTeamEvent(t))
		{
			chosenTeam = t;
			break;
		}
	}
	m_Team = chosenTeam;
	if(m_Team == -1)
	{
		// no free team - fail gracefully so caller can notify players
		dbg_msg("1on1", "StartEvent failed: no free team available");
		return false;
	}
	pController->Teams().SetForceCharacterTeam(m_Player1ID, m_Team);
	pController->Teams().SetForceCharacterTeam(m_Player2ID, m_Team);
	// mark this team as an event team so team-wide kills are suppressed
	pController->Teams().SetTeamEvent(m_Team, true);
	pController->Teams().SetTeamLock(m_Team, true);

	// save and disable solo & collision state for both participants
	auto &core = ((CGameControllerDDRace *)GameServer()->m_pController)->Teams().m_Core;
	for(int cid : {m_Player1ID, m_Player2ID})
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(cid);
		if(pChr)
		{
			bool wasSolo = pChr->Core()->m_Solo;
			bool wasCollisionDisabled = pChr->Core()->m_CollisionDisabled;
			m_PrevSoloState[cid] = {wasSolo, wasCollisionDisabled};
			if(wasSolo)
				pChr->SetSolo(false);
			if(wasCollisionDisabled)
				pChr->Core()->m_CollisionDisabled = false;
		}
		else
		{
			// fallback to teams core if no character is present
			bool wasSolo = core.GetSolo(cid);
			m_PrevSoloState[cid] = {wasSolo, false};
			core.SetSolo(cid, false);
		}
	}

	m_Score1.store(0);
	m_Score2.store(0);
	m_Player1DeathTick = -1;
	m_Player2DeathTick = -1;
	m_LastAwardedPlayer = 0;
	m_LastAwardedTick = -1;
	m_ForcedWinnerCid = -1;
	m_PendingAwardTo = 0;
	m_PendingAwardTick = -1;
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
			return false;
		}
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		const char *pLogsUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
		if(Discord.IsConfigured(pLogsUrl))
		{
			char aMsg[256];
			str_format(aMsg, sizeof(aMsg), "1on1 wager collected: **%s** vs **%s** | Wager: %d BP", Server()->ClientName(m_Player1ID), Server()->ClientName(m_Player2ID), m_Wager);
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = pLogsUrl;
			Discord.Send(aMsg, Opt);

			str_format(aMsg, sizeof(aMsg), "1on1 wager locked for transfer: **%s** <-> **%s** | Amount: %d BP", Server()->ClientName(m_Player1ID), Server()->ClientName(m_Player2ID), m_Wager);
			Discord.Send(aMsg, Opt);
		}
	}

	// save positions & teeinfos
	SaveWeapons(m_Player1ID);
	SaveWeapons(m_Player2ID);
	SavePosition(m_Player1ID);
	SavePosition(m_Player2ID);

	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Participants.clear();
		m_Participants.push_back(m_Player1ID);
		m_Participants.push_back(m_Player2ID);
	}

	// teleport/spawn
	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);
	if(!p1 || !p2)
	{
		GameServer()->SendChatTarget(m_Player1ID, "Something went wrong with the 1v1. Please try again.");
		GameServer()->SendChatTarget(m_Player2ID, "Something went wrong with the 1v1. Please try again.");
		FinishEvent();
		return false;
	}

	std::vector<vec2> spawnPosition;
	if(m_Config.m_SpawnMode == 1)
		spawnPosition = GameServer()->ZoneManager()->Get1on1ArenaPositions(-1);
	else
		spawnPosition = GameServer()->ZoneManager()->GetNamedQuadCenters("1on1_spawn");
	int spawncount = (int)spawnPosition.size();

	if(spawncount <= 0)
	{
		m_SpawnReservation.pos1Idx = -1;
		m_SpawnReservation.pos2Idx = -1;
	}
	else if(spawncount == 1)
	{
		m_SpawnReservation.pos1Idx = 0;
		m_SpawnReservation.pos2Idx = 0;
	}
	else
	{
		int idx1 = secure_rand_below(spawncount);
		int idx2 = secure_rand_below(spawncount - 1);
		if(idx2 >= idx1)
			idx2++;
		m_SpawnReservation.pos1Idx = idx1;
		m_SpawnReservation.pos2Idx = idx2;
	}

	if(p1 && p1->GetCharacter())
		p1->KillCharacter(WEAPON_WORLD, false);
	if(p2 && p2->GetCharacter())
		p2->KillCharacter(WEAPON_WORLD, false);

	// save & disable cosmetics before spawning (guard prevents double-save if InitializeConfigPhase ran first)
	SaveAndClearCosmetics(m_Player1ID);
	SaveAndClearCosmetics(m_Player2ID);

	// directly spawn players at their reserved 1on1 positions
	if(p1 && m_SpawnReservation.pos1Idx >= 0 && m_SpawnReservation.pos1Idx < (int)spawnPosition.size())
	{
		p1->ForceSpawn(spawnPosition[m_SpawnReservation.pos1Idx], false);
		if(p1->GetCharacter())
			p1->GetCharacter()->FreezeForce(3);
	}
	if(p2 && m_SpawnReservation.pos2Idx >= 0 && m_SpawnReservation.pos2Idx < (int)spawnPosition.size())
	{
		p2->ForceSpawn(spawnPosition[m_SpawnReservation.pos2Idx], false);
		if(p2->GetCharacter())
			p2->GetCharacter()->FreezeForce(3);
	}

	m_StartTimer = 0;
	m_MatchStartTick = Server()->Tick();

	// apply config settings: endless hook, weapon restrictions and grants
	for(int cid : {m_Player1ID, m_Player2ID})
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(cid);
		if(!pChr)
			continue;

		// endless hook
		if(m_Config.m_EndlessHook)
			pChr->SetEndlessHook(true);

		// give enabled weapons and remove disabled ones
		for(int w = 0; w < 6; w++)
		{
			if(m_Config.m_aWeapons[w])
			{
				pChr->GiveWeapon(w);
			}
			else
			{
				pChr->SetWeaponGot(w, false);
				// if active weapon was removed, switch to hammer or first available
				if(pChr->GetActiveWeapon() == w)
				{
					for(int alt = 0; alt < 6; alt++)
					{
						if(m_Config.m_aWeapons[alt])
						{
							pChr->SetActiveWeapon(alt);
							break;
						}
					}
				}
			}
		}
	}

	SetState(EEventState::Active);
	return true;
}

void COneOnOneEvent::OnTick()
{
	m_CurrentTick = Server()->Tick();

	// handle deferred finish restoration outside of death callbacks
	if(GetState() == EEventState::Ending && m_DeferFinishRestore && m_RestoreAtTick <= m_CurrentTick)
	{
		// restore team lock
		auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
		if(m_Team >= 0)
		{
			pController->Teams().SetTeamEvent(m_Team, false);
			pController->Teams().SetTeamLock(m_Team, false);
		}

		// clear forced event team for both participants so reconnect uses normal team
		pController->Teams().SetForceCharacterTeam(m_Player1ID, TEAM_FLOCK);
		pController->Teams().SetForceCharacterTeam(m_Player2ID, TEAM_FLOCK);

		// restore positions for all players we saved (avoid fallback kill for non-saved)
		std::vector<int> aSaved;
		aSaved.reserve(m_pSavedPlayers.size());
		for(const auto &kv : m_pSavedPlayers)
			aSaved.push_back(kv.first);
		for(const int Cid : aSaved)
		{
			pController->Teams().SetForceCharacterTeam(Cid, TEAM_FLOCK);
			LoadPosition(Cid);
			LoadWeapons(Cid);
			if(auto p = GameServer()->GetPlayer(Cid))
				p->m_allowDeath = true;
		}

		// restore solo & collision state for saved players
		auto &core = ((CGameControllerDDRace *)GameServer()->m_pController)->Teams().m_Core;
		for(const int Cid : aSaved)
		{
			auto it = m_PrevSoloState.find(Cid);
			if(it == m_PrevSoloState.end())
				continue;
			CCharacter *pChr = GameServer()->GetPlayerChar(Cid);
			if(pChr)
			{
				if(it->second.solo)
					pChr->SetSolo(true);
				pChr->Core()->m_CollisionDisabled = it->second.collision;
			}
			else
			{
				core.SetSolo(Cid, it->second.solo);
			}
			m_PrevSoloState.erase(it);
		}

		m_DeferFinishRestore = false;

		// restore cosmetics for both players now that they are back in the world
		RestoreCosmetics(m_Player1ID);
		RestoreCosmetics(m_Player2ID);

		SetState(EEventState::Finished);
		return;
	}
	if(m_Player1ID < 0 || m_Player2ID < 0)
		return;

	if(!GameServer()->GetPlayer(m_Player1ID) || !GameServer()->GetPlayer(m_Player2ID))
	{
		if(GetState() == EEventState::Preparation)
		{
			ClearDuelVoteUi();
			extern CVoteManager g_VoteManager;
			for(int cid : {m_Player1ID, m_Player2ID})
			{
				g_VoteManager.NavigateToRoot(cid);
				GameServer()->ClearVotes(cid);
			}
			int otherCid = !GameServer()->GetPlayer(m_Player1ID) ? m_Player2ID : m_Player1ID;
			GameServer()->SendChatTarget(otherCid, "[1on1] Opponent disconnected during the preparation phase. Match cancelled.");
			AbortAndRefund(nullptr);
		}
		else
		{
			FinishEvent();
		}
		return;
	}

	if(GetState() == EEventState::Active)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s: %d\n"
					       "%s: %d",
			Server()->ClientName(m_Player1ID),
			m_Score1.load(),
			Server()->ClientName(m_Player2ID),
			m_Score2.load());

		GameServer()->m_apPlayers[m_Player1ID]->SendBroadcastAlignedLeft(aBuf);
		GameServer()->m_apPlayers[m_Player2ID]->SendBroadcastAlignedLeft(aBuf);
	}

	// All freeze/stalemate/scoring logic only applies during Active phase
	if(GetState() == EEventState::Active)
	{
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

			if(inFreeze)
			{
				if(!m_P1Frozen)
					m_P1FrozenTick = m_CurrentTick;
				m_P1Frozen = true;
			}
			else
			{
				m_P1Frozen = false;
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

			if(inFreeze)
			{
				if(!m_P2Frozen)
					m_P2FrozenTick = m_CurrentTick;
				m_P2Frozen = true;
			}
			else
			{
				m_P2Frozen = false;
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
				GameServer()->SendChatTarget(m_Player1ID, "Draw!");
				GameServer()->SendChatTarget(m_Player2ID, "Draw!");
				char aDrawBuf[256];
				str_format(aDrawBuf, sizeof(aDrawBuf), "%s: %d\n%s: %d\nStalemate draw! restarting...", Server()->ClientName(m_Player1ID), m_Score1.load(), Server()->ClientName(m_Player2ID), m_Score2.load());
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

		if(GetState() == EEventState::Active)
		{
			const int GroundHookDelayTicks = Config()->m_SvGroundHookPenaltyDelay * Server()->TickSpeed();
			if(PlayerHookedGroundFor(m_Player1ID) > GroundHookDelayTicks)
			{
				GameServer()->GetPlayerChar(m_Player1ID)->FreezeForce(Config()->m_SvGroundHookPenalty);
			}
			if(PlayerHookedGroundFor(m_Player2ID) > GroundHookDelayTicks)
			{
				GameServer()->GetPlayerChar(m_Player2ID)->FreezeForce(Config()->m_SvGroundHookPenalty);
			}
			// adds the check so people can't hold 1on1's hostage.
			CheckFreezePenalties();
		}

		if(GetState() == EEventState::Active && CheckEndCondition())
		{
			static constexpr const char *s_padding = "                                                                                     "
								 "                                                                                     "
								 "                                                                                     ";
			char aFinalBroadcast[256];
			str_format(aFinalBroadcast, sizeof(aFinalBroadcast), "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1.load(), Server()->ClientName(m_Player2ID), m_Score2.load(), s_padding);
			GameServer()->SendBroadcast(aFinalBroadcast, m_Player1ID, false);
			GameServer()->SendBroadcast(aFinalBroadcast, m_Player2ID, false);

			FinishEvent();
		}

		// time limit enforcement
		if(GetState() == EEventState::Active && m_Config.m_TimeLimit > 0 && m_MatchStartTick > 0)
		{
			int elapsed = (int)((m_CurrentTick - m_MatchStartTick) / Server()->TickSpeed());
			if(elapsed >= m_Config.m_TimeLimit)
			{
				// time's up - player with more points wins; tie = draw/refund
				int s1 = m_Score1.load();
				int s2 = m_Score2.load();
				if(s1 > s2)
					m_ForcedWinnerCid = m_Player1ID;
				else if(s2 > s1)
					m_ForcedWinnerCid = m_Player2ID;
				else
				{
					// true draw - suppress normal finish broadcast and handle it here
					m_SuppressFinishBroadcast = true;
					GameServer()->SendChatTarget(-1, "[1on1] Time limit reached - match ended in a draw!");
					if(m_Wager > 0)
						RefundEscrow();
				}

				GameServer()->SendChatTarget(m_Player1ID, "[1on1] Time limit reached!");
				GameServer()->SendChatTarget(m_Player2ID, "[1on1] Time limit reached!");
				FinishEvent();
			}
		}

	} // end of if(GetState() == EEventState::Active)

	// config phase timeout - auto start when timer hits zero
	if(GetState() == EEventState::Preparation && m_ConfigStartTick > 0)
	{
		int WarmupSec = g_Config.m_Sv1on1WarmupSeconds;
		int elapsed = (int)((m_CurrentTick - m_ConfigStartTick) / Server()->TickSpeed());

		// Refresh vote overlay every 5s (global EndVote wipes it for all clients)
		if(m_DuelVoteLastSendTick >= 0 && (m_CurrentTick - m_DuelVoteLastSendTick) >= Server()->TickSpeed() * 5)
			SendDuelVoteUi();

		if(elapsed >= WarmupSec)
		{
			GameServer()->SendChatTarget(m_Player1ID, "[1on1] Preparation phase over - starting with current settings.");
			GameServer()->SendChatTarget(m_Player2ID, "[1on1] Preparation phase over - starting with current settings.");

			ClearDuelVoteUi();
			extern CVoteManager g_VoteManager;
			for(int cid : {m_Player1ID, m_Player2ID})
			{
				g_VoteManager.NavigateToRoot(cid);
				GameServer()->ClearVotes(cid);
			}

			StartMatchFromConfig();
		}
	}
}

void COneOnOneEvent::OnCharacterSpawn(int ClientId, vec2 SpawnPos)
{
	// manage spawns during Active and Preparation (warmup) phases
	if(GetState() != EEventState::Active && GetState() != EEventState::Preparation)
		return;

	if(ClientId < 0)
		return;

	if(ClientId != m_Player1ID && ClientId != m_Player2ID)
		return;

	auto p1 = GameServer()->GetPlayer(m_Player1ID);
	auto p2 = GameServer()->GetPlayer(m_Player2ID);
	if(p1)
		p1->m_allowDeath = false;
	if(p2)
		p2->m_allowDeath = false;

	if(GetState() == EEventState::Active)
	{
		std::vector<vec2> spawnPos;
		if(m_Config.m_SpawnMode == 1)
			spawnPos = GameServer()->ZoneManager()->Get1on1ArenaPositions(-1);
		else
			spawnPos = GameServer()->ZoneManager()->GetNamedQuadCenters("1on1_spawn");

		int reservedIdx = (ClientId == m_Player1ID) ? m_SpawnReservation.pos1Idx : m_SpawnReservation.pos2Idx;

		CCharacter *pSpawned = GameServer()->GetPlayerChar(ClientId);
		if(pSpawned)
		{
			pSpawned->ResetVelocity();
			pSpawned->FreezeForce(3);

			// teleport to reserved arena slot if we have a valid position
			if(reservedIdx >= 0 && reservedIdx < (int)spawnPos.size())
				GameServer()->Teleport(pSpawned, spawnPos[reservedIdx]);
		}
	}

	// reapply config settings after respawn (only during Active - warmup has default loadout)
	if(GetState() == EEventState::Active)
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
		if(pChr)
		{
			if(m_Config.m_EndlessHook)
				pChr->SetEndlessHook(true);

			// give enabled weapons and remove disabled ones
			for(int w = 0; w < 6; w++)
			{
				if(m_Config.m_aWeapons[w])
				{
					pChr->GiveWeapon(w);
				}
				else
				{
					pChr->SetWeaponGot(w, false);
					if(pChr->GetActiveWeapon() == w)
					{
						for(int alt = 0; alt < 6; alt++)
						{
							if(m_Config.m_aWeapons[alt])
							{
								pChr->SetActiveWeapon(alt);
								break;
							}
						}
					}
				}
			}
		}
	}
}

// award points on death: opponent gets one point (suicides count)
void COneOnOneEvent::OnCharacterDeath(int KillerId, int ClientId, int Weapon)
{
	if(GetState() != EEventState::Active)
		return;

	if(ClientId != m_Player1ID && ClientId != m_Player2ID)
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

	// Prevent duplicate awards from multiple rapid death callbacks (e.g. when
	// a freeze forces a Die() and another kill event is emitted in the same
	// tick). If we've already awarded a point this tick, ignore further
	// death callbacks.
	if(m_LastAwardedTick == Server()->Tick())
	{
		dbg_msg("1on1", "Ignoring duplicate OnCharacterDeath for cid=%d in tick=%d", ClientId, Server()->Tick());
		return;
	}

	CCharacter *pChr1 = GameServer()->GetPlayerChar(m_Player1ID);
	CCharacter *pChr2 = GameServer()->GetPlayerChar(m_Player2ID);
	bool p1InFreezeTileNow = pChr1 ? pChr1->Core()->m_IsInFreeze : m_P1InFreezeTile;
	bool p2InFreezeTileNow = pChr2 ? pChr2->Core()->m_IsInFreeze : m_P2InFreezeTile;
	if(p1InFreezeTileNow && p2InFreezeTileNow)
	{
		// Draw: no scoring, ensure no pending award remains
		m_PendingAwardTo = 0;
		m_PendingAwardTick = -1;

		GameServer()->SendChatTarget(m_Player1ID, "Draw!");
		GameServer()->SendChatTarget(m_Player2ID, "Draw!");
		char aDrawBuf[256];
		str_format(aDrawBuf, sizeof(aDrawBuf), "%s: %d\n%s: %d\nRound draw! restarting...", Server()->ClientName(m_Player1ID), m_Score1.load(), Server()->ClientName(m_Player2ID), m_Score2.load());
		GameServer()->SendBroadcast(aDrawBuf, m_Player1ID, false);
		GameServer()->SendBroadcast(aDrawBuf, m_Player2ID, false);
		RestartRoundAfterDraw();
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
		// Draw: clear any pending award; do not change scores
		m_PendingAwardTo = 0;
		m_PendingAwardTick = -1;

		GameServer()->SendChatTarget(m_Player1ID, "Draw!");
		GameServer()->SendChatTarget(m_Player2ID, "Draw!");
		char aDrawBuf[256];
		str_format(aDrawBuf, sizeof(aDrawBuf), "%s: %d\n%s: %d\nRound draw! restarting...", Server()->ClientName(m_Player1ID), m_Score1.load(), Server()->ClientName(m_Player2ID), m_Score2.load());
		GameServer()->SendBroadcast(aDrawBuf, m_Player1ID, false);
		GameServer()->SendBroadcast(aDrawBuf, m_Player2ID, false);

		RestartRoundAfterDraw();
		return;
	}
	if(ClientId == m_Player1ID)
	{
		m_Score2.fetch_add(1);
		m_LastAwardedPlayer = 2;
		m_LastAwardedTick = Server()->Tick();
		if(auto p = GameServer()->GetPlayer(m_Player2ID))
			p->m_Score = m_Score2.load();

		char aPointMsg[256];
		str_format(aPointMsg, sizeof(aPointMsg), "Score for %s!", Server()->ClientName(m_Player2ID));
		GameServer()->SendChatTarget(m_Player1ID, aPointMsg);
		GameServer()->SendChatTarget(m_Player2ID, aPointMsg);

		static constexpr const char *s_padding = "                                                                                     "
							 "                                                                                     "
							 "                                                                                     ";
		char aBroadcastMsg[256];
		str_format(aBroadcastMsg, sizeof(aBroadcastMsg), "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1.load(), Server()->ClientName(m_Player2ID), m_Score2.load(), s_padding);
		GameServer()->SendBroadcast(aBroadcastMsg, m_Player1ID, false);
		GameServer()->SendBroadcast(aBroadcastMsg, m_Player2ID, false);

		m_Player1DeathTick = -1;
		m_Player2DeathTick = -1;
		m_PendingAwardTo = 0;
		m_PendingAwardTick = -1;
		m_RoundStartTick = Server()->Tick();

		RestartRoundAfterDraw();
	}
	else if(ClientId == m_Player2ID)
	{
		m_Score1.fetch_add(1);
		m_LastAwardedPlayer = 1;
		m_LastAwardedTick = Server()->Tick();
		if(auto p = GameServer()->GetPlayer(m_Player1ID))
			p->m_Score = m_Score1.load();

		char aPointMsg[256];
		str_format(aPointMsg, sizeof(aPointMsg), "Score for %s!", Server()->ClientName(m_Player1ID));
		GameServer()->SendChatTarget(m_Player1ID, aPointMsg);
		GameServer()->SendChatTarget(m_Player2ID, aPointMsg);

		static constexpr const char *s_padding = "                                                                                     "
							 "                                                                                     "
							 "                                                                                     ";
		char aBroadcastMsg[256];
		str_format(aBroadcastMsg, sizeof(aBroadcastMsg), "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1.load(), Server()->ClientName(m_Player2ID), m_Score2.load(), s_padding);
		GameServer()->SendBroadcast(aBroadcastMsg, m_Player1ID, false);
		GameServer()->SendBroadcast(aBroadcastMsg, m_Player2ID, false);

		m_Player1DeathTick = -1;
		m_Player2DeathTick = -1;
		m_PendingAwardTo = 0;
		m_PendingAwardTick = -1;
		m_RoundStartTick = Server()->Tick();

		RestartRoundAfterDraw();
	}
}

bool COneOnOneEvent::CheckEndCondition()
{
	int limit = m_Config.m_PointsLimit;
	if(limit <= 0)
		limit = 10; // fallback
	return m_Score1.load() >= limit || m_Score2.load() >= limit;
}

void COneOnOneEvent::CheckFreezePenalties()
{
	CCharacter *pChr1 = GameServer()->GetPlayerChar(m_Player1ID);
	CCharacter *pChr2 = GameServer()->GetPlayerChar(m_Player2ID);

	if(!pChr1 || !pChr2)
		return;

	if(m_P1Frozen && pChr1->IsAlive() && pChr1->Core()->m_Vel.x != 0)
	{
		if(m_P1FrozenTick != -1 && m_CurrentTick - m_P1FrozenTick > 10 * Server()->TickSpeed())
		{
			pChr1->Die(m_Player2ID, WEAPON_WORLD);
		}
	}

	if(m_P2Frozen && pChr2->IsAlive() && pChr2->Core()->m_Vel.x != 0)
	{
		if(m_P2FrozenTick != -1 && m_CurrentTick - m_P2FrozenTick > 10 * Server()->TickSpeed())
		{
			pChr2->Die(m_Player1ID, WEAPON_WORLD);
		}
	}
}

void COneOnOneEvent::RestartRoundAfterDraw()
{
	m_DrawRestartInProgress = true;
	m_Player1DeathTick = -1;
	m_Player2DeathTick = -1;
	m_LastAwardedPlayer = 0;
	m_LastAwardedTick = -1;
	m_PendingAwardTo = 0;
	m_PendingAwardTick = -1;
	m_ForcedWinnerCid = -1;
	m_RoundStartTick = Server()->Tick();
	m_BothFrozenSinceTick = -1;
	m_P1InFreezeTile = false;
	m_P2InFreezeTile = false;
	m_P1InFreezeTileTick = -1;
	m_P2InFreezeTileTick = -1;

	std::vector<vec2> startPositions;
	if(m_Config.m_SpawnMode == 1)
		startPositions = GameServer()->ZoneManager()->Get1on1ArenaPositions(-1);
	else
		startPositions = GameServer()->ZoneManager()->GetNamedQuadCenters("1on1_spawn");

	int spawncount = (int)startPositions.size();
	if(spawncount >= 2)
	{
		int idx1 = secure_rand_below(spawncount);
		int idx2 = secure_rand_below(spawncount - 1);
		if(idx2 >= idx1)
			idx2++;
		m_SpawnReservation.pos1Idx = idx1;
		m_SpawnReservation.pos2Idx = idx2;
	}
	else if(spawncount == 1)
	{
		m_SpawnReservation.pos1Idx = 0;
		m_SpawnReservation.pos2Idx = 0;
	}

	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);

	auto pController = (CGameControllerDDRace *)GameServer()->m_pController;

	if(p1)
	{
		pController->Teams().SetForceCharacterTeam(m_Player1ID, m_Team);
		p1->KillCharacter(WEAPON_WORLD, false);
		int idx1 = m_SpawnReservation.pos1Idx;
		if(idx1 >= 0 && idx1 < (int)startPositions.size())
			p1->ForceSpawn(startPositions[idx1], false);
		else if(!startPositions.empty())
			p1->ForceSpawn(startPositions[0], false);
		else
			p1->ForceSpawn(vec2(0, 0), false);
	}
	if(p2)
	{
		pController->Teams().SetForceCharacterTeam(m_Player2ID, m_Team);
		p2->KillCharacter(WEAPON_WORLD, false);
		int idx2 = m_SpawnReservation.pos2Idx;
		if(idx2 >= 0 && idx2 < (int)startPositions.size())
			p2->ForceSpawn(startPositions[idx2], false);
		else if(!startPositions.empty())
			p2->ForceSpawn(startPositions[0], false);
		else
			p2->ForceSpawn(vec2(0, 0), false);
	}

	m_DrawRestartInProgress = false;
}

void COneOnOneEvent::FinishEvent()
{
	// announce winner and restore players
	CPlayer *pWinner = nullptr;
	CPlayer *pLoser = nullptr;
	int winnerCid = -1;
	int loserCid = -1;
	if(m_ForcedWinnerCid >= 0)
	{
		winnerCid = m_ForcedWinnerCid;
		loserCid = (winnerCid == m_Player1ID) ? m_Player2ID : m_Player1ID;
	}
	else if(m_Score1.load() > m_Score2.load())
	{
		winnerCid = m_Player1ID;
		loserCid = m_Player2ID;
	}
	else
	{
		winnerCid = m_Player2ID;
		loserCid = m_Player1ID;
	}
	pWinner = GameServer()->GetPlayer(winnerCid);
	pLoser = GameServer()->GetPlayer(loserCid);
	if(!m_SuppressFinishBroadcast && pWinner && pLoser)
	{
		const char *pName1 = m_Player1ID >= 0 ? Server()->ClientName(m_Player1ID) : "<none>";
		const char *pName2 = m_Player2ID >= 0 ? Server()->ClientName(m_Player2ID) : "<none>";
		const char *pWinnerName = (winnerCid == m_Player1ID) ? pName1 : pName2;
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[1on1] - %s vs %s - %s won! (Result: %d - %d)", pName1, pName2, pWinnerName, m_Score1.load(), m_Score2.load());
		GameServer()->SendChatTarget(-1, aBuf);

		// post to Discord webhook
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		const char *p1on1Url = g_Config.m_SvDiscordWebhookUrl1on1[0] ? g_Config.m_SvDiscordWebhookUrl1on1 : nullptr;
		if(Discord.IsConfigured(p1on1Url))
		{
			char aMsg[512];
			int winnerScore = (winnerCid == m_Player1ID) ? m_Score1.load() : m_Score2.load();
			int loserScore = (winnerCid == m_Player1ID) ? m_Score2.load() : m_Score1.load();
			int bp = m_Wager > 0 ? m_Wager : 0;
			const char *pWinnerBracket = pWinnerName;
			const char *pLoserName = (winnerCid == m_Player1ID) ? pName2 : pName1;
			str_format(aMsg, sizeof(aMsg), "[**%s**]  %d : %d  [%s]  ->  BP: %d", pWinnerBracket, winnerScore, loserScore, pLoserName, bp);
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = p1on1Url;
			Discord.Send(aMsg, Opt);
		}
	}

	// safe payout via escrow - only if escrow is actually still held (draw path already refunds before calling us)
	if(m_Wager > 0)
	{
		bool escrowHeld;
		{
			std::lock_guard<std::mutex> g(m_Mutex);
			escrowHeld = m_EscrowCollected;
		}
		if(escrowHeld)
		{
			if(pWinner)
			{
				PayoutWinner(pWinner, pLoser);
			}
			else
			{
				// both players gone - don't lose the BP, refund to accounts
				RefundEscrow();
			}
			CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
			const char *pLogsUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
			if(Discord.IsConfigured(pLogsUrl) && pWinner)
			{
				char aMsg[256];
				const char *pMap = Server()->GetMapName();
				int loserId = (m_ForcedWinnerCid == m_Player1ID) ? m_Player2ID : m_Player1ID;
				int winnerId = m_ForcedWinnerCid >= 0 ? m_ForcedWinnerCid : (m_Score1.load() > m_Score2.load() ? m_Player1ID : m_Player2ID);
				str_format(aMsg, sizeof(aMsg), "1on1 wager transferred on %s: %s -> %s | Amount: %d BP", pMap ? pMap : "<map>", Server()->ClientName(loserId), Server()->ClientName(winnerId), m_Wager);
				CDiscordWebhook::SSendOptions Opt;
				Opt.m_pWebhookUrl = pLogsUrl;
				Discord.Send(aMsg, Opt);
			}
		}
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

		// during warmup/config phase - just abort cleanly, no ragequit
		if(GetState() == EEventState::Preparation)
		{
			GameServer()->SendChatTarget(leaver, "[1on1] You left the preparation phase. Match cancelled.");
			GameServer()->SendChatTarget(opponent, "[1on1] Your opponent left during the preparation phase. Match cancelled.");

			// clear vote UI then vote pages
			ClearDuelVoteUi();
			extern CVoteManager g_VoteManager;
			for(int cid : {m_Player1ID, m_Player2ID})
			{
				g_VoteManager.NavigateToRoot(cid);
				GameServer()->ClearVotes(cid);
			}

			AbortAndRefund(nullptr);
			return true;
		}

		// Active phase - ragequit
		m_PendingAwardTo = 0;
		m_PendingAwardTick = -1;
		m_ForcedWinnerCid = opponent;

		int leaverScore = (leaver == m_Player1ID) ? m_Score1.load() : m_Score2.load();
		int opponentScore = (leaver == m_Player1ID) ? m_Score2.load() : m_Score1.load();

		static const char *s_RagequitPhrases[] = {
			"couldn't handle the smoke",
			"got absolutely shit on and dipped",
			"left like the little b*tch they are",
			"ran away crying like a baby",
			"got destroyed and rage alt+F4'd",
			"pissed their pants and disconnected",
			"got clapped so hard they uninstalled",
			"folded like a lawn chair",
			"is officially the biggest pussy in blockworlds",
			"couldn't take the L so they ran",
			"ragequit harder than their dad left them",
			"got their ass whooped and bounced",
			"left the match and probably broke their keyboard",
			"got humiliated and vanished like a coward",
			"got dumpstered and pulled the plug",
			"is crying in the corner right now",
			"went back to playing minecraft after that beating",
			"got farmed like a bot and disconnected",
			"quit faster than their will to live",
			"got rolled so hard they forgot how to play",
		};
		static const char *s_DiscordEmotes[] = {
			"<:kappa:1106645597169143898>",
			":horse:",
			"<:KEKW:712106151789199452>",
			"<:OMEGALUL:685906612447084604>",
			"<a:existentialdreadintensifies:980469156803674114>",
			"<:ghostmw2:1106645590659584050>",
			"<a:pepeenrage:1198206040093769808>",
			"<a:pepedespair:1106630400543035494>",
			"<a:pepetraumatized:1198206010087706727>",
			"<a:petercry:980464935597404310>",
			"<:yaw:980464943293956125>",
		};
		static const int NumPhrases = sizeof(s_RagequitPhrases) / sizeof(s_RagequitPhrases[0]);
		static const int NumEmotes = sizeof(s_DiscordEmotes) / sizeof(s_DiscordEmotes[0]);
		int phraseIdx = (int)((unsigned)(Server()->Tick() + leaver) % NumPhrases);
		int emoteIdx = (int)((unsigned)(Server()->Tick() + leaver + 7) % NumEmotes);

		m_SuppressFinishBroadcast = true;

		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "[1on1] - %s ragequited the match vs %s! %s %d : %d",
				pLeaverName, pOpponentName, pOpponentName, opponentScore, leaverScore);
			GameServer()->SendChatTarget(-1, aBuf);
		}

		// proactively clear forced team to avoid spawn issues when finishing
		auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
		pController->Teams().SetForceCharacterTeam(m_Player1ID, 0);
		pController->Teams().SetForceCharacterTeam(m_Player2ID, 0);

		// restore solo and collision state for both participants if it was changed for the event
		auto &core = ((CGameControllerDDRace *)GameServer()->m_pController)->Teams().m_Core;
		for(const auto &soloEntry : m_PrevSoloState)
		{
			int cid = soloEntry.first;
			CCharacter *pChr = GameServer()->GetPlayerChar(cid);
			if(pChr)
			{
				if(soloEntry.second.solo)
					pChr->SetSolo(true);
				pChr->Core()->m_CollisionDisabled = soloEntry.second.collision;
			}
			else
			{
				// fallback to teams core
				core.SetSolo(cid, soloEntry.second.solo);
			}
		}
		m_PrevSoloState.clear();

		FinishEvent();

		// notify Discord about ragequit
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		const char *p1on1Url = g_Config.m_SvDiscordWebhookUrl1on1[0] ? g_Config.m_SvDiscordWebhookUrl1on1 : nullptr;
		if(Discord.IsConfigured(p1on1Url))
		{
			char aMsg[512];
			str_format(aMsg, sizeof(aMsg), "%s ragequit! %s : **%s** %d : %d %s - %s",
				pLeaverName, s_DiscordEmotes[emoteIdx], pOpponentName, opponentScore, leaverScore, pLeaverName, s_RagequitPhrases[phraseIdx]);
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
	// always restore cosmetics for a disconnecting participant regardless of state
	RestoreCosmetics(ClientId);

	if(GetState() == EEventState::Active || GetState() == EEventState::Preparation)
	{
		if(ClientId == m_Player1ID || ClientId == m_Player2ID)
			Leave(ClientId);
	}
}

std::optional<int> COneOnOneEvent::GetScoreOf(int ClientId) const
{
	if(ClientId == m_Player1ID)
		return m_Score1.load();
	if(ClientId == m_Player2ID)
		return m_Score2.load();
	return std::nullopt;
}

// ========= Wager/Escrow helpers =========

bool COneOnOneEvent::CollectEscrow()
{
	int wager;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		if(m_EscrowCollected || m_Wager <= 0)
			return true;
		wager = m_Wager;
	}

	// Validate players and balances without holding match lock
	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);
	if(!p1 || !p2)
		return false;
	if(!p1->IsLoggedIn() || !p2->IsLoggedIn())
	{
		GameServer()->SendChatTarget(-1, "[1on1] Wager requires both players to be logged in.");
		return false;
	}
	if(p1->GetPlayerBlockpoints() < wager || p2->GetPlayerBlockpoints() < wager)
	{
		char aBuf[256];
		const char *pWho = p1->GetPlayerBlockpoints() < wager ? Server()->ClientName(m_Player1ID) : Server()->ClientName(m_Player2ID);
		str_format(aBuf, sizeof(aBuf), "[1on1] Wager collection failed: %s doesn't have enough blockpoints.", pWho);
		GameServer()->SendChatTarget(-1, aBuf);
		return false;
	}

	// Deduct to escrow (perform account saves immediately; update state under lock)
	p1->SetPlayerBlockpoints(p1->GetPlayerBlockpoints() - wager);
	p2->SetPlayerBlockpoints(p2->GetPlayerBlockpoints() - wager);
	GameServer()->Accounts()->Save(m_Player1ID, &p1->m_Account);
	GameServer()->Accounts()->Save(m_Player2ID, &p2->m_Account);

	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_EscrowCollected = true;
		m_EscrowBalance = wager * 2;
	}

	char aBuf[192];
	str_format(aBuf, sizeof(aBuf), "[1on1] Pot started with %d BP from each player.", wager);
	GameServer()->SendChatTarget(m_Player1ID, aBuf);
	GameServer()->SendChatTarget(m_Player2ID, aBuf);
	return true;
}

void COneOnOneEvent::RefundEscrow()
{
	int wager;
	bool doRefund = false;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		if(!m_EscrowCollected || m_EscrowBalance <= 0)
			return;
		// snapshot and clear escrow under lock
		wager = m_Wager;
		m_EscrowBalance = 0;
		m_EscrowCollected = false;
		doRefund = true;
	}

	if(!doRefund)
		return;

	CPlayer *p1 = GameServer()->GetPlayer(m_Player1ID);
	CPlayer *p2 = GameServer()->GetPlayer(m_Player2ID);
	if(p1)
	{
		p1->SetPlayerBlockpoints(p1->GetPlayerBlockpoints() + wager);
		GameServer()->Accounts()->Save(m_Player1ID, &p1->m_Account);
	}
	if(p2)
	{
		p2->SetPlayerBlockpoints(p2->GetPlayerBlockpoints() + wager);
		GameServer()->Accounts()->Save(m_Player2ID, &p2->m_Account);
	}
	if(p1)
		GameServer()->SendChatTarget(m_Player1ID, "[1on1] Pot refunded.");
	if(p2)
		GameServer()->SendChatTarget(m_Player2ID, "[1on1] Pot refunded.");
}

void COneOnOneEvent::PayoutWinner(CPlayer *pWinner, CPlayer *pLoser)
{
	int balance = 0;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		if(!m_EscrowCollected || m_EscrowBalance != m_Wager * 2)
			return; // escrow not in the expected state - do nothing
		balance = m_EscrowBalance;
		m_EscrowBalance = 0;
		m_EscrowCollected = false;
	}

	if(!pWinner)
		return; // caller should have called RefundEscrow instead

	pWinner->SetPlayerBlockpoints(pWinner->GetPlayerBlockpoints() + balance);
	GameServer()->Accounts()->Save(pWinner->GetCid(), &pWinner->m_Account);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "[1on1] - %s won the pot of %d blockpoints!", Server()->ClientName(pWinner->GetCid()), balance);
	GameServer()->SendChatTarget(-1, aBuf);
}

void COneOnOneEvent::AbortAndRefund(const char *pReason)
{
	if(pReason && pReason[0])
	{
		GameServer()->SendChatTarget(m_Player1ID, pReason);
		GameServer()->SendChatTarget(m_Player2ID, pReason);
	}
	RefundEscrow();

	auto pController = (CGameControllerDDRace *)GameServer()->m_pController;
	if(m_Team >= 0)
	{
		pController->Teams().SetTeamEvent(m_Team, false);
		pController->Teams().SetTeamLock(m_Team, false);
	}

	// clear force-team so players re-join the normal flock team
	pController->Teams().SetForceCharacterTeam(m_Player1ID, TEAM_FLOCK);
	pController->Teams().SetForceCharacterTeam(m_Player2ID, TEAM_FLOCK);

	// restore positions and weapons for both participants
	for(int cid : {m_Player1ID, m_Player2ID})
	{
		LoadPosition(cid);
		LoadWeapons(cid);
		if(auto p = GameServer()->GetPlayer(cid))
			p->m_allowDeath = true;
	}

	// restore solo/collision state
	auto &core = pController->Teams().m_Core;
	for(int cid : {m_Player1ID, m_Player2ID})
	{
		auto it = m_PrevSoloState.find(cid);
		if(it == m_PrevSoloState.end())
			continue;
		CCharacter *pChr = GameServer()->GetPlayerChar(cid);
		if(pChr)
		{
			if(it->second.solo)
				pChr->SetSolo(true);
			pChr->Core()->m_CollisionDisabled = it->second.collision;
		}
		else
		{
			core.SetSolo(cid, it->second.solo);
		}
		m_PrevSoloState.erase(it);
	}

	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Participants.clear();
	}

	// restore cosmetics before marking finished
	RestoreCosmetics(m_Player1ID);
	RestoreCosmetics(m_Player2ID);

	SetState(EEventState::Finished);
}

void COneOnOneEvent::EmergencyShutdown(const char *pMsg)
{
	// set emergency flag/message
	if(pMsg && pMsg[0])
	{
		str_copy(m_EmergencyMessage, pMsg);
		m_EmergencyShutdown = true;
	}
	// ensure escrow is returned
	RefundEscrow();
}

// ========== Minimal event-like helpers (adapted from CEventComponent) ==========

// Delegate shared helpers to centralized inline helpers
#include "event_helpers.h"

void COneOnOneEvent::SetState(COneOnOneEvent::EEventState NewState)
{
	EEventState OldState;
	std::function<void(EEventState, EEventState)> cb;
	{
		// Use atomic exchange to set state and obtain old value atomically
		OldState = m_State.exchange(NewState);
		if(OldState == NewState)
			return;
		// copy callback under lock to avoid races with setter
		std::lock_guard<std::mutex> g(m_Mutex);
		cb = m_pfnOnStateChange;
	}

	// Invoke callback outside the lock to avoid deadlocks.
	if(cb)
		cb(OldState, NewState);
}

const char *COneOnOneEvent::GetStateName() const
{
	return GetStateName(m_State.load());
}

const char *COneOnOneEvent::GetStateName(COneOnOneEvent::EEventState State)
{
	switch(State)
	{
	case EEventState::Created:
		return "created";
	case EEventState::Registration:
		return "registration";
	case EEventState::Preparation:
		return "preparation";
	case EEventState::Active:
		return "active";
	case EEventState::Ending:
		return "ending";
	case EEventState::Finished:
		return "finished";
	}
	return "";
}

void COneOnOneEvent::SavePosition(int ClientId)
{
	SavePositionHelper(GameServer(), m_pSavedPlayers, ClientId);
}

void COneOnOneEvent::LoadPosition(int ClientId)
{
	LoadPositionHelper(GameServer(), m_pSavedPlayers, ClientId);
}

void COneOnOneEvent::SaveWeapons(int ClientId)
{
	SaveWeaponsHelper(GameServer(), m_SavedWeapons, ClientId);
}

void COneOnOneEvent::LoadWeapons(int ClientId)
{
	LoadWeaponsHelper(GameServer(), m_SavedWeapons, ClientId);
}

void COneOnOneEvent::SaveAndClearCosmetics(int ClientId)
{
	// Guard: only save once per match to avoid overwriting with already-stripped state
	if(m_SavedCosmetics.count(ClientId) > 0)
		return;

	CPlayer *pPlayer = GameServer()->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	CEventComponent::SCosmeticsSnapshot Snap;
	Snap.m_Special = pPlayer->GetCurrentSpecial();
	Snap.m_SkinMani = pPlayer->GetSkinMani();
	Snap.m_GunDesign = pPlayer->GetGunDesign();
	Snap.m_Knockout = pPlayer->GetKnockout();
	Snap.m_FlagExpireTick = pPlayer->GetFlagExpireTick();
	m_SavedCosmetics[ClientId] = Snap;

	pPlayer->DisableCosmeticsForEvent();
}

void COneOnOneEvent::RestoreCosmetics(int ClientId)
{
	auto It = m_SavedCosmetics.find(ClientId);
	if(It == m_SavedCosmetics.end())
		return;

	const CEventComponent::SCosmeticsSnapshot &Snap = It->second;
	CPlayer *pPlayer = GameServer()->GetPlayer(ClientId);
	if(pPlayer)
	{
		pPlayer->SetSkinMani(Snap.m_SkinMani);
		pPlayer->SetGunDesign(Snap.m_GunDesign);
		pPlayer->SetKnockout(Snap.m_Knockout);
		if(Snap.m_Special >= 0)
			pPlayer->ToggleSpecial(Snap.m_Special);
		if(Snap.m_FlagExpireTick > Server()->Tick())
		{
			int RemainingTicks = Snap.m_FlagExpireTick - Server()->Tick();
			int RemainingMinutes = RemainingTicks / Server()->TickSpeed() / 60;
			if(RemainingMinutes > 0)
				pPlayer->GiveFlag(RemainingMinutes);
		}
	}

	m_SavedCosmetics.erase(It);
}

int COneOnOneEvent::PlayerHookedGroundFor(int ClientId) const
{
	return PlayerHookedGroundForHelper(GameServer(), ClientId);
}
