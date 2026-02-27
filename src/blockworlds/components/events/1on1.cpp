#include "1on1.h"
#include "1on1_utils.h"
#include <base/system.h>
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
	m_aReady[0] = false;
	m_aReady[1] = false;
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

	std::vector<vec2> spawnPosition;
	int spawncount = GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), spawnPosition);

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

	// kill & respawn at arena positions (no freeze during warmup)
	if(p1 && p1->GetCharacter())
		p1->KillCharacter(WEAPON_WORLD, false);
	if(p2 && p2->GetCharacter())
		p2->KillCharacter(WEAPON_WORLD, false);

	if(p1 && m_SpawnReservation.pos1Idx >= 0 && m_SpawnReservation.pos1Idx < (int)spawnPosition.size())
	{
		p1->ForceSpawn(spawnPosition[m_SpawnReservation.pos1Idx], false);
		p1->SetSkinMani(-1);
		if(p1->GetCurrentSpecial() != -1)
			p1->ToggleSpecial(p1->GetCurrentSpecial());
	}
	if(p2 && m_SpawnReservation.pos2Idx >= 0 && m_SpawnReservation.pos2Idx < (int)spawnPosition.size())
	{
		p2->ForceSpawn(spawnPosition[m_SpawnReservation.pos2Idx], false);
		p2->SetSkinMani(-1);
		if(p2->GetCurrentSpecial() != -1)
			p2->ToggleSpecial(p2->GetCurrentSpecial());
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
	GameServer()->SendChatTarget(m_Player1ID, "[1on1] 30s warmup — configure settings in vote menu (F3/F4) or type /ready.");
	GameServer()->SendChatTarget(m_Player2ID, "[1on1] 30s warmup — configure settings in vote menu (F3/F4) or type /ready.");

	dbg_msg("1on1", "InitializeConfigPhase: P1=%d P2=%d wager=%d team=%d", m_Player1ID, m_Player2ID, m_Wager, m_Team);
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
			const char *pMap = Server()->GetMapName();
			str_format(aMsg, sizeof(aMsg), "1on1 wager collected on %s: %s vs %s | Wager: %d BP", pMap ? pMap : "<map>", Server()->ClientName(m_Player1ID), Server()->ClientName(m_Player2ID), m_Wager);
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
	int spawncount = GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), spawnPosition);

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

	// apply config settings: endless hook, weapon restrictions
	for(int cid : {m_Player1ID, m_Player2ID})
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(cid);
		if(!pChr)
			continue;

		if(m_Config.m_EndlessHook)
			pChr->SetEndlessHook(true);

		for(int w = 0; w < 6; w++)
		{
			if(!m_Config.m_aWeapons[w])
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
		// no free team — fail gracefully so caller can notify players instead of emergency-shutdown
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
			const char *pMap = Server()->GetMapName();
			str_format(aMsg, sizeof(aMsg), "1on1 wager collected on %s: %s vs %s | Wager: %d BP", pMap ? pMap : "<map>", Server()->ClientName(m_Player1ID), Server()->ClientName(m_Player2ID), m_Wager);
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = pLogsUrl;
			Discord.Send(aMsg, Opt);

			str_format(aMsg, sizeof(aMsg), "1on1 wager locked for transfer on %s: %s <-> %s | Amount: %d BP", pMap ? pMap : "<map>", Server()->ClientName(m_Player1ID), Server()->ClientName(m_Player2ID), m_Wager);
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
	int spawncount = GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), spawnPosition);

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

	// directly spawn players at their reserved 1on1 positions
	if(p1 && m_SpawnReservation.pos1Idx >= 0 && m_SpawnReservation.pos1Idx < (int)spawnPosition.size())
	{
		p1->ForceSpawn(spawnPosition[m_SpawnReservation.pos1Idx], false);
		p1->SetSkinMani(-1);
		if(p1->GetCurrentSpecial() != -1)
			p1->ToggleSpecial(p1->GetCurrentSpecial());
		if(p1->GetCharacter())
			p1->GetCharacter()->FreezeForce(3);
	}
	if(p2 && m_SpawnReservation.pos2Idx >= 0 && m_SpawnReservation.pos2Idx < (int)spawnPosition.size())
	{
		p2->ForceSpawn(spawnPosition[m_SpawnReservation.pos2Idx], false);
		p2->SetSkinMani(-1);
		if(p2->GetCurrentSpecial() != -1)
			p2->ToggleSpecial(p2->GetCurrentSpecial());
		if(p2->GetCharacter())
			p2->GetCharacter()->FreezeForce(3);
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
	m_MatchStartTick = Server()->Tick();

	// apply config settings: endless hook, weapon restrictions
	for(int cid : {m_Player1ID, m_Player2ID})
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(cid);
		if(!pChr)
			continue;

		// endless hook
		if(m_Config.m_EndlessHook)
			pChr->SetEndlessHook(true);

		// weapon restrictions: remove weapons the config disables
		for(int w = 0; w < 6; w++)
		{
			if(!m_Config.m_aWeapons[w])
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
		SetState(EEventState::Finished);
		return;
	}
	if(m_Player1ID < 0 || m_Player2ID < 0)
		return;

	if(!GameServer()->GetPlayer(m_Player1ID) || !GameServer()->GetPlayer(m_Player2ID))
	{
		if(GetState() == EEventState::Preparation)
		{
			// during warmup, just abort gracefully
			extern CVoteManager g_VoteManager;
			for(int cid : {m_Player1ID, m_Player2ID})
			{
				auto &Stack = g_VoteManager.GetPageStackMut(cid);
				Stack.clear();
				Stack.push_back(CVoteManager::Page{CVoteManager::Page::ROOT, -1});
				GameServer()->ClearVotes(cid);
			}
			int otherCid = !GameServer()->GetPlayer(m_Player1ID) ? m_Player2ID : m_Player1ID;
			GameServer()->SendChatTarget(otherCid, "[1on1] Opponent disconnected during warmup. Match cancelled.");
			AbortAndRefund(nullptr);
		}
		else
		{
			FinishEvent();
		}
		return;
	}

	if(GetState() == EEventState::Active && Config()->m_Sv1on1BroadcastRate > 0 && (Server()->Tick() % Config()->m_Sv1on1BroadcastRate) == 0)
	{
		static constexpr const char *s_padding = "                                                                                     "
							 "                                                                                     "
							 "                                                                                     ";

		GameServer()->SendBroadcast(m_Player1ID, "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1.load(), Server()->ClientName(m_Player2ID), m_Score2.load(), s_padding);
		GameServer()->SendBroadcast(m_Player2ID, "%s: %d\n%s: %d\n%s", Server()->ClientName(m_Player1ID), m_Score1.load(), Server()->ClientName(m_Player2ID), m_Score2.load(), s_padding);
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
			// time's up — player with more points wins; tie = draw/refund
			int s1 = m_Score1.load();
			int s2 = m_Score2.load();
			if(s1 > s2)
				m_ForcedWinnerCid = m_Player1ID;
			else if(s2 > s1)
				m_ForcedWinnerCid = m_Player2ID;
			else
			{
				// true draw — suppress normal finish broadcast and handle manually
				m_SuppressFinishBroadcast = true;
				GameServer()->SendChatTarget(-1, "[1on1] Time limit reached — match ended in a draw!");
				if(m_Wager > 0)
					RefundEscrow();
			}

			GameServer()->SendChatTarget(m_Player1ID, "[1on1] Time limit reached!");
			GameServer()->SendChatTarget(m_Player2ID, "[1on1] Time limit reached!");
			FinishEvent();
		}
	}

	} // end of if(GetState() == EEventState::Active)

	// config phase timeout: 30 seconds to configure, then auto-start with current settings
	if(GetState() == EEventState::Preparation && m_ConfigStartTick > 0)
	{
		int elapsed = (int)((m_CurrentTick - m_ConfigStartTick) / Server()->TickSpeed());
		int remaining = 30 - elapsed;

		// broadcast countdown every 5 seconds (and at 3, 2, 1)
		if(remaining > 0 && (remaining <= 3 || (remaining % 5 == 0 && (m_CurrentTick - m_ConfigStartTick) % Server()->TickSpeed() == 0)))
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "[1on1] Match starts in %d second%s...", remaining, remaining == 1 ? "" : "s");
			GameServer()->SendChatTarget(m_Player1ID, aBuf);
			GameServer()->SendChatTarget(m_Player2ID, aBuf);
		}

		if(elapsed >= 30)
		{
			GameServer()->SendChatTarget(m_Player1ID, "[1on1] Warmup over — starting with current settings.");
			GameServer()->SendChatTarget(m_Player2ID, "[1on1] Warmup over — starting with current settings.");

			// clear duel config vote pages before match start
			extern CVoteManager g_VoteManager;
			for(int cid : {m_Player1ID, m_Player2ID})
			{
				auto &Stack = g_VoteManager.GetPageStackMut(cid);
				Stack.clear();
				Stack.push_back(CVoteManager::Page{CVoteManager::Page::ROOT, -1});
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

	// during Active phase, freeze on spawn; during warmup, let them move freely
	if(GetState() == EEventState::Active)
	{
		CCharacter *pSpawned = GameServer()->GetPlayerChar(ClientId);
		if(pSpawned)
		{
			pSpawned->ResetVelocity();
			pSpawned->FreezeForce(3);
		}
	}

	// reapply config settings after respawn (only during Active — warmup has default loadout)
	if(GetState() == EEventState::Active)
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
		if(pChr)
		{
			if(m_Config.m_EndlessHook)
				pChr->SetEndlessHook(true);

			for(int w = 0; w < 6; w++)
			{
				if(!m_Config.m_aWeapons[w])
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
	GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), startPositions);

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
		str_format(aBuf, sizeof(aBuf), "[1on1] - %s vs %s — %s won! (Result: %d - %d)", pName1, pName2, pWinnerName, m_Score1.load(), m_Score2.load());
		GameServer()->SendChatTarget(-1, aBuf);

		// post to Discord webhook
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		const char *p1on1Url = g_Config.m_SvDiscordWebhookUrl1on1[0] ? g_Config.m_SvDiscordWebhookUrl1on1 : nullptr;
		if(Discord.IsConfigured(p1on1Url))
		{
			char aMsg[512];
			const char *pMap = Server()->GetMapName();
			int winnerScore = (winnerCid == m_Player1ID) ? m_Score1.load() : m_Score2.load();
			int loserScore = (winnerCid == m_Player1ID) ? m_Score2.load() : m_Score1.load();
			int bp = m_Wager > 0 ? m_Wager : 0;
			const char *pWinnerBracket = pWinnerName;
			const char *pLoserName = (winnerCid == m_Player1ID) ? pName2 : pName1;
			str_format(aMsg, sizeof(aMsg), "[**%s**]  %d : %d  [%s]  ->  BP: %d (**%s**)", pWinnerBracket, winnerScore, loserScore, pLoserName, bp, pMap ? pMap : "<invalid>");
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = p1on1Url;
			Discord.Send(aMsg, Opt);
		}
	}

	// safe payout via escrow
	if(m_Wager > 0 && pWinner)
	{
		PayoutWinner(pWinner, pLoser);
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		const char *pLogsUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
		if(Discord.IsConfigured(pLogsUrl))
		{
			char aMsg[256];
			const char *pMap = Server()->GetMapName();
			int loserId = (m_ForcedWinnerCid == m_Player1ID) ? m_Player2ID : m_Player1ID;
			int winnerId = m_ForcedWinnerCid >= 0 ? m_ForcedWinnerCid : (m_Score1.load() > m_Score2.load() ? m_Player1ID : m_Player2ID);
			str_format(aMsg, sizeof(aMsg), "1on1 wager transferred on %s: %s → %s | Amount: %d BP", pMap ? pMap : "<map>", Server()->ClientName(loserId), Server()->ClientName(winnerId), m_Wager);
			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = pLogsUrl;
			Discord.Send(aMsg, Opt);
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

		// during warmup/config phase — just abort gracefully, no ragequit
		if(GetState() == EEventState::Preparation)
		{
			GameServer()->SendChatTarget(leaver, "[1on1] You left the warmup. Match cancelled.");
			GameServer()->SendChatTarget(opponent, "[1on1] Your opponent left during warmup. Match cancelled.");

			// clear duel config vote pages
			extern CVoteManager g_VoteManager;
			for(int cid : {m_Player1ID, m_Player2ID})
			{
				auto &Stack = g_VoteManager.GetPageStackMut(cid);
				Stack.clear();
				Stack.push_back(CVoteManager::Page{CVoteManager::Page::ROOT, -1});
				GameServer()->ClearVotes(cid);
			}

			AbortAndRefund(nullptr);
			return true;
		}

		// Active phase — ragequit
		m_PendingAwardTo = 0;
		m_PendingAwardTick = -1;
		m_ForcedWinnerCid = opponent;

		// static const char *s_RagequitMsgs[] = {
		// 	"[1on1] - %s ragequited the match vs %s! What a dramatic exit.",
		// 	"[1on1] - %s has abandoned the duel against %s — coward move!",
		// 	"[1on1] - %s disconnected mid-fight vs %s. GG, we saw nothing...",
		// 	"[1on1] - %s choked under pressure and fled from %s. Shame.",
		// 	"[1on1] - %s decided running was the best strategy against %s. Classic."};
		// const int NumMsgs = sizeof(s_RagequitMsgs) / sizeof(s_RagequitMsgs[0]);
		// int idx = (int)((Server()->Tick() + leaver) % NumMsgs);
		// char aBuf[256];
		// str_format(aBuf, sizeof(aBuf), s_RagequitMsgs[idx], pLeaverName, pOpponentName);
		// GameServer()->SendChatTarget(-1, aBuf);

		m_SuppressFinishBroadcast = true;

		// announce ragequit (no score shown)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "[1on1] - %s ragequited the match vs %s!", pLeaverName, pOpponentName);
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
			char aMsg[256];
			str_format(aMsg, sizeof(aMsg), "1on1 ragequit: %s left vs %s (score so far %d-%d)", pLeaverName, pOpponentName, m_Score1.load(), m_Score2.load());
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
	bool hadEscrow = false;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		if(m_EscrowCollected && m_EscrowBalance == m_Wager * 2)
		{
			balance = m_EscrowBalance;
			m_EscrowBalance = 0;
			m_EscrowCollected = false;
			hadEscrow = true;
		}
	}

	if(hadEscrow)
	{
		pWinner->SetPlayerBlockpoints(pWinner->GetPlayerBlockpoints() + balance);
		GameServer()->Accounts()->Save(pWinner->GetCid(), &pWinner->m_Account);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[1on1] - %s won the pot of %d blockpoints!", Server()->ClientName(pWinner->GetCid()), balance);
		GameServer()->SendChatTarget(-1, aBuf);
		return;
	}
	// Fallback
	if(m_Wager > 0 && pLoser && pLoser->GetPlayerBlockpoints() >= m_Wager)
	{
		pLoser->SetPlayerBlockpoints(pLoser->GetPlayerBlockpoints() - m_Wager);
		pWinner->SetPlayerBlockpoints(pWinner->GetPlayerBlockpoints() + m_Wager);
		GameServer()->Accounts()->Save(pLoser->GetCid(), &pLoser->m_Account);
		GameServer()->Accounts()->Save(pWinner->GetCid(), &pWinner->m_Account);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[1on1] - %s won %d blockpoints from %s!", Server()->ClientName(pWinner->GetCid()), m_Wager, Server()->ClientName(pLoser->GetCid()));
		GameServer()->SendChatTarget(-1, aBuf);
	}
	else if(pLoser)
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

int COneOnOneEvent::PlayerHookedGroundFor(int ClientId) const
{
	return PlayerHookedGroundForHelper(GameServer(), ClientId);
}
