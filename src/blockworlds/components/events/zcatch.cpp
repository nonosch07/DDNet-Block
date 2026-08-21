#include "zcatch.h"

#include "event_helpers.h"

#include <engine/server/server.h>
#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <blockworlds/bw_context.h>
#include <blockworlds/bw_util.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/discord/webhook.h>

#include <algorithm>

CZCatchEvent::CZCatchEvent(CGameContext *pGameServer) :
	CEventComponent(pGameServer)
{
	m_SpawnPositions = GameServer()->Bw().ZoneManager()->GetNamedQuadCenters("zcb_spawn");
	if(m_SpawnPositions.empty())
	{
		EmergencyShutdown("Map has no zcatch block spawns!");
		return;
	}
}

void CZCatchEvent::OpenRegistration()
{
	m_Candidates.clear();
	m_RegistrationEndTick = Server()->Tick() + Config()->m_SvZCatchRegistrationTime * Server()->TickSpeed();
	SetState(EEventState::Registration);
}

void CZCatchEvent::CloseRegistration()
{
	SetState(EEventState::Preparation);
	GameServer()->Bw().SendBroadcast(-1, " ", false); // clear registration broadcast for all
	if((int)m_Candidates.size() < Config()->m_SvZCatchMinimumCandidates)
	{
		FinishEvent(NOT_ENOUGH_CANDIDATES);
		return;
	}
	m_Participants = m_Candidates;
	m_Candidates.clear();
	StartEvent();
}

void CZCatchEvent::StartEvent()
{
	// drop participants who no longer have a live character
	{
		auto ParticipantsCopy = m_Participants;
		for(int ClientId : ParticipantsCopy)
		{
			if(!GameServer()->GetPlayerChar(ClientId))
				Leave(ClientId);
		}
	}

	// find a free DDRace team
	auto &Teams = GameServer()->m_pController->Teams();
	int chosenTeam = -1;
	for(int t = 1; t < NUM_DDRACE_TEAMS; ++t)
	{
		if(Teams.GetTeamState(t) == ETeamState::EMPTY && !Teams.IsTeamEvent(t))
		{
			chosenTeam = t;
			break;
		}
	}
	m_DDRaceTeam = chosenTeam;
	if(m_DDRaceTeam == -1)
	{
		EmergencyShutdown("No free team was found");
		return;
	}
	Teams.SetTeamEvent(m_DDRaceTeam, true);
	Teams.SetTeamLock(m_DDRaceTeam, false);
	Teams.SetTeamInvitesOpen(m_DDRaceTeam, false);

	// clear state
	m_UsedSpawnIndices.clear();
	m_pSavedPlayers.clear();
	m_SavedWeapons.clear();
	m_Scores.clear();
	m_CaughtBy.clear();
	m_Captives.clear();
	m_FrozenSince.clear();
	m_PrevSoloState.clear();
	m_LastImpactorOf.clear();
	m_Winner = -1;

	// join all participants — RandomSpawnPos ensures each player gets a unique
	// random spawn position (no two land on the same quad).
	for(int ClientId : m_Participants)
	{
		m_Scores[ClientId] = 0;
		Join(ClientId);
	}

	m_ActiveStartTick = Server()->Tick();
	m_ActiveEndTick = (Config()->m_SvZCatchActiveTime > 0) ?
				  Server()->Tick() + Config()->m_SvZCatchActiveTime * Server()->TickSpeed() :
				  -1;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "zCatch started! First to %d kills wins!", Config()->m_SvZCatchKillsToWin);
	GameServer()->Bw().SendChatTarget(-1, aBuf);

	SetState(EEventState::Active);
}

void CZCatchEvent::FinishEvent()
{
	SetState(EEventState::Ending);

	if(m_FinishReason == NATURAL)
	{
		if(m_Winner != -1)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "'%s' has won zCatch!", Server()->ClientName(m_Winner));
			GameServer()->Bw().SendChatTarget(-1, aBuf);
			GameServer()->Bw().SendBroadcast(-1, aBuf, false);

			// discord webhook (+top 3)
			{
				CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Bw().Http());
				const char *pZCatchUrl = g_Config.m_SvDiscordWebhookUrlZCatch[0] ? g_Config.m_SvDiscordWebhookUrlZCatch : nullptr;
				if(Discord.IsConfigured(pZCatchUrl))
				{
					const char *pWinnerName = Server()->ClientName(m_Winner);
					int WinScore = m_Scores.count(m_Winner) ? m_Scores.at(m_Winner) : 0;

					// Sort all scores descending for the leaderboard
					std::vector<std::pair<int, int>> vSorted(m_Scores.begin(), m_Scores.end());
					std::sort(vSorted.begin(), vSorted.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

					// strip emoji/non-ASCII so the monospace table columns stay aligned
					auto SanitizeName = [](const char *pSrc, char *pDst, int DstSize) {
						if(!pSrc || DstSize <= 0)
						{
							if(DstSize > 0)
								pDst[0] = '\0';
							return;
						}
						int out = 0;
						for(int i = 0; pSrc[i] && out < DstSize - 1; ++i)
						{
							unsigned char c = (unsigned char)pSrc[i];
							if(c >= 0x20 && c <= 0x7E) // printable ASCII only
								pDst[out++] = pSrc[i];
						}
						pDst[out] = '\0';
					};

					char aSafeWinner[22];
					SanitizeName(pWinnerName ? pWinnerName : "?", aSafeWinner, sizeof(aSafeWinner));

					char aDiscord[1024];
					char aTmp[128];

					str_format(aDiscord, sizeof(aDiscord),
						"**zCatch Result**\n"
						"**Winner: %s** | %d kills\n"
						"```\n"
						"Rank  Player               Kills\n",
						aSafeWinner, WinScore);

					for(int i = 0; i < 3 && i < (int)vSorted.size(); ++i)
					{
						const char *pName = Server()->ClientName(vSorted[i].first);
						char aSafeName[21];
						SanitizeName(pName ? pName : "?", aSafeName, sizeof(aSafeName));
						str_format(aTmp, sizeof(aTmp), "#%-4d  %-20.20s  %d\n",
							i + 1, aSafeName, vSorted[i].second);
						str_append(aDiscord, aTmp);
					}

					str_append(aDiscord, "```");

					CDiscordWebhook::SSendOptions Opt;
					Opt.m_pWebhookUrl = pZCatchUrl;
					Discord.Send(aDiscord, Opt);
				}
			}

			// CPlayer *pWinner = GameServer()->GetPlayer(m_Winner);
			// if(pWinner && pWinner->IsLoggedIn())
			// {
			// 	int BPReward = Config()->m_SvZCatchBlockpointsReward;
			// 	int PagesReward = Config()->m_SvZCatchPagesReward;
			// 	if(BPReward > 0)
			// 		pWinner->SetPlayerBlockpoints(pWinner->GetPlayerBlockpoints() + BPReward);
			// 	if(PagesReward > 0)
			// 		pWinner->SetPlayerPages(pWinner->GetPlayerPages() + PagesReward);
			// 	pWinner->SetPlayerTourneyWins(pWinner->GetPlayerTourneyWin() + 1);
			// 	str_format(aBuf, sizeof(aBuf), "You received %d blockpoints and %d pages for winning zCatch!", BPReward, PagesReward);
			// 	GameServer()->Bw().SendChatTarget(m_Winner, aBuf);
			// }
			// else if(pWinner)
			// {
			// 	GameServer()->Bw().SendChatTarget(m_Winner, "You must be logged in to receive rewards.");
			// }
		}
		else
		{
			const char *pMsg = "zCatch ended with no winner (time limit).";
			GameServer()->Bw().SendChatTarget(-1, pMsg);
			GameServer()->Bw().SendBroadcast(-1, pMsg, false);
		}
	}
	else if(m_FinishReason == NOT_ENOUGH_CANDIDATES)
	{
		GameServer()->Bw().SendChatTarget(-1, "Not enough players joined zCatch.");
	}
	else if(m_FinishReason == EMERGENCY)
	{
		GameServer()->Bw().SendChatTarget(-1, "zCatch ended prematurely.");
	}

	// release all killed players and return everyone to normal
	// clear caught state first so Leave() + ReleaseCaptives() don't double-process
	m_CaughtBy.clear();
	m_Captives.clear();
	m_FrozenSince.clear();

	auto RemainingParticipants = m_Participants;
	for(int ClientId : RemainingParticipants)
		Leave(ClientId);

	m_Scores.clear();

	CEventComponent::OnTick(); // flush deferred position/weapon queues

	// restore solo/collision state
	for(const auto &entry : m_PrevSoloState)
	{
		if(auto *pChar = GameServer()->GetPlayerChar(entry.first))
		{
			if(entry.second.solo)
				pChar->SetSolo(true);
			pChar->Bw().Core().m_CollisionDisabled = entry.second.collision;
		}
	}
	m_PrevSoloState.clear();

	if(m_DDRaceTeam != -1)
	{
		GameServer()->m_pController->Teams().ResetRoundState(m_DDRaceTeam);
		GameServer()->m_pController->Teams().SetTeamEvent(m_DDRaceTeam, false);
		m_DDRaceTeam = -1;
	}

	SetState(EEventState::Finished);
}

void CZCatchEvent::ForceNextStage()
{
	if(GetState() == EEventState::Registration)
		CloseRegistration();
	else if(GetState() == EEventState::Active)
		FinishEvent(NATURAL);
}

bool CZCatchEvent::CheckEndCondition()
{
	// time limit
	if(m_ActiveEndTick > 0 && Server()->Tick() > m_ActiveEndTick)
	{
		m_Winner = -1;
		int best = -1;
		for(const auto &[id, score] : m_Scores)
		{
			if(score > best)
			{
				best = score;
				m_Winner = id;
			}
		}
		return true;
	}

	// win by reaching the kill-count goal
	for(const auto &[id, score] : m_Scores)
	{
		if(score >= Config()->m_SvZCatchKillsToWin)
		{
			m_Winner = id;
			return true;
		}
	}

	// last fighter standing (all others caught)
	if((int)m_Participants.size() > 1)
	{
		int fightingCount = 0;
		int lastFighter = -1;
		for(int id : m_Participants)
		{
			if(!IsCaught(id))
			{
				fightingCount++;
				lastFighter = id;
			}
		}
		if(fightingCount <= 1)
		{
			m_Winner = lastFighter;
			return true;
		}
	}

	return false;
}

bool CZCatchEvent::Register(int ClientId)
{
	if(!CheckClientId(ClientId))
		return false;
	if(GetState() != EEventState::Registration)
	{
		GameServer()->Bw().SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	if(!GameServer()->Bw().GetPlayer(ClientId))
		return false;
	if(IsCandidate(ClientId))
	{
		GameServer()->Bw().SendChatTarget(ClientId, "You are already registered for zCatch.");
		return false;
	}
	if(g_Config.m_SvMaxClientsPerIp > 1 && g_Config.m_SvEventsTestMode == 0)
	{
		for(int CandId : m_Candidates)
		{
			if(BwIsClientsSameAddr(GameServer()->Server(), ClientId, CandId))
			{
				GameServer()->Bw().SendChatTarget(ClientId, "You cannot register for this event (Already registered).");
				return false;
			}
		}
	}
	m_Candidates.push_back(ClientId);
	GameServer()->Bw().SendChatTarget(ClientId, "You joined zCatch registration!");
	return true;
}

bool CZCatchEvent::DeRegister(int ClientId)
{
	if(GetState() != EEventState::Registration)
	{
		GameServer()->Bw().SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	auto it = std::find(m_Candidates.begin(), m_Candidates.end(), ClientId);
	if(it == m_Candidates.end())
	{
		GameServer()->Bw().SendChatTarget(ClientId, "You are not registered for zCatch.");
		return false;
	}
	m_Candidates.erase(it);
	GameServer()->Bw().SendChatTarget(ClientId, "You left zCatch registration.");
	return true;
}

bool CZCatchEvent::Join(int ClientId)
{
	SaveWeapons(ClientId);
	SavePosition(ClientId);

	auto *pChar = GameServer()->GetPlayerChar(ClientId);
	if(pChar)
	{
		bool wasSolo = pChar->Core()->m_Solo;
		bool wasCollision = pChar->Core()->m_CollisionDisabled;
		m_PrevSoloState[ClientId] = {wasSolo, wasCollision};
		if(wasSolo)
			pChar->SetSolo(false);
		if(wasCollision)
			pChar->Bw().Core().m_CollisionDisabled = false;
		pChar->GetPlayer()->Pause(CPlayer::PAUSE_NONE, false);
		pChar->SetDeepFrozen(false);

		GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);
		pChar->ResetVelocity();
		pChar->Bw().FreezeForce(Config()->m_SvZCatchInitialFreezeTime);
		GameServer()->Bw().Teleport(pChar, NextSpawnPos());
	}

	if(auto *pPlayer = GameServer()->Bw().GetPlayer(ClientId))
	{
		SaveAndClearCosmetics(ClientId);
		pPlayer->Bw().ClearCosmetics();
	}

	return true;
}

bool CZCatchEvent::Leave(int ClientId)
{
	auto it = std::find(m_Participants.begin(), m_Participants.end(), ClientId);
	if(it == m_Participants.end())
		return false;
	m_Participants.erase(it);

	// release any captives this player was holding
	ReleaseCaptives(ClientId);

	// if this player was caught, remove from catcher's list
	{
		auto caughtIt = m_CaughtBy.find(ClientId);
		if(caughtIt != m_CaughtBy.end())
		{
			int catcherId = caughtIt->second;
			m_CaughtBy.erase(caughtIt);
			auto capIt = m_Captives.find(catcherId);
			if(capIt != m_Captives.end())
				capIt->second.erase(ClientId);
		}
	}

	// if player is still in spectator mode from being caught, restore game team first
	if(auto *pPlayer = GameServer()->Bw().GetPlayer(ClientId))
	{
		if(pPlayer->GetTeam() == TEAM_SPECTATORS)
			pPlayer->SetTeam(0, false);
	}

	LoadPositionHelper(GameServer(), m_pSavedPlayers, ClientId);
	LoadWeaponsHelper(GameServer(), m_SavedWeapons, ClientId);

	m_DeferredLoadQueue.erase(std::remove(m_DeferredLoadQueue.begin(), m_DeferredLoadQueue.end(), ClientId), m_DeferredLoadQueue.end());
	m_DeferredWeaponsQueue.erase(std::remove(m_DeferredWeaponsQueue.begin(), m_DeferredWeaponsQueue.end(), ClientId), m_DeferredWeaponsQueue.end());

	// restore solo/collision state
	if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
	{
		auto soloIt = m_PrevSoloState.find(ClientId);
		if(soloIt != m_PrevSoloState.end())
		{
			if(soloIt->second.solo)
				pChar->SetSolo(true);
			pChar->Bw().Core().m_CollisionDisabled = soloIt->second.collision;
			m_PrevSoloState.erase(soloIt);
		}
	}

	m_Scores.erase(ClientId);
	m_FrozenSince.erase(ClientId);
	m_LastImpactorOf.erase(ClientId);
	RestoreCosmetics(ClientId);
	return true;
}

void CZCatchEvent::EmergencyShutdown(const char *pMsg)
{
	CEventComponent::EmergencyShutdown(pMsg);
	if(GetState() != EEventState::Finished)
		FinishEvent(EMERGENCY);
}

void CZCatchEvent::OnBlockedKill(int VictimID, int KillerID)
{
	if(GetState() != EEventState::Active)
		return;
	if(!IsParticipant(VictimID) || !IsParticipant(KillerID))
		return;
	// killer must be a free (fighting) participant - caught players can't block others
	if(IsCaught(KillerID))
		return;
	// victim should not already be caught (they'd be a spectator)
	if(IsCaught(VictimID))
		return;

	// if victim had their own captives, release them first (chain reaction)
	ReleaseCaptives(VictimID);

	// register the catch.
	// LockToSpectator is intentionally NOT called here. this function may be
	// invoked from within CCharacter::Die() (via OnPlayerKill), and calling
	// SetTeam(TEAM_SPECTATORS) -> KillCharacter() -> Die() from inside Die()
	// would be re-entrant (we dont want dat). instead, the spectate lock is applied in
	// OnCharacterSpawn once the victim's new character has safely spawned
	m_CaughtBy[VictimID] = KillerID;
	m_Captives[KillerID].insert(VictimID);

	// award the kill
	m_Scores[KillerID]++;

	// char aBuf[256];
	// str_format(aBuf, sizeof(aBuf), "%s caught %s  (%d/%d)",
	// 	Server()->ClientName(KillerID),
	// 	Server()->ClientName(VictimID),
	// 	newScore,
	// 	Config()->m_SvZCatchKillsToWin);
	// GameServer()->Bw().SendChatTarget(-1, aBuf);
	// win-condition check is performed in OnTick to stay outside Die().
}

int CZCatchEvent::GetMinCandidates() const
{
	return Config()->m_SvZCatchMinimumCandidates;
}

bool CZCatchEvent::AllowKillCommandFor(int ClientId) const
{
	if(GetState() != EEventState::Active)
		return false;
	if(!IsParticipant(ClientId) || IsCaught(ClientId))
		return false;
	// block tracker still holds the live impactor (not yet cleared, player hasn't died)
	int impactor = GameServer()->Bw().BlockTracker().GetImpactorOf(ClientId);
	if(impactor >= 0 && IsParticipant(impactor) && !IsCaught(impactor))
		return true;
	// fall back to shadow-tracked impactor
	auto it = m_LastImpactorOf.find(ClientId);
	return it != m_LastImpactorOf.end() && IsParticipant(it->second) && !IsCaught(it->second);
}

void CZCatchEvent::OnPlayerImpacted(int VictimId, int InitiatorId)
{
	if(GetState() != EEventState::Active)
		return;
	if(!IsParticipant(VictimId) || IsCaught(VictimId))
		return;
	if(!IsParticipant(InitiatorId) || IsCaught(InitiatorId))
		return;
	m_LastImpactorOf[VictimId] = InitiatorId;
}

std::optional<int> CZCatchEvent::GetScoreOf(int ClientId) const
{
	if(!IsParticipant(ClientId))
		return std::nullopt;
	auto it = m_Scores.find(ClientId);
	return (it != m_Scores.end()) ? std::optional<int>{it->second} : std::optional<int>{0};
}

void CZCatchEvent::OnTick()
{
	if(CEventComponent::EmergencyShutdown())
	{
		FinishEvent(EMERGENCY);
		return;
	}

	if(GetState() == EEventState::Registration)
	{
		if(Server()->Tick() >= m_RegistrationEndTick)
		{
			CloseRegistration();
			return;
		}
		CEventComponent::OnTick(); // test-mode dummies
		char aTimeLeft[32];
		FormatTimeLeft(aTimeLeft, sizeof(aTimeLeft), (int)((m_RegistrationEndTick - Server()->Tick()) / Server()->TickSpeed()));

		for(int i = 0; i < Server()->MaxClients(); ++i)
		{
			if(!Server()->ClientIngame(i))
				continue;

			CPlayer *pPlayer = GameServer()->Bw().GetPlayer(i);
			if(!pPlayer)
				continue;

			pPlayer->Bw().SendBroadcastAlignedLeft("zCatch is about to start!\n"
							       "Register with /join\n"
							       "Time left: %s\n\n"
							       "Participants: %" PRIzu "\n"
							       "%s",
				aTimeLeft,
				m_Candidates.size(),
				(int)m_Candidates.size() < Config()->m_SvZCatchMinimumCandidates ? "Not enough participants!\n" : "");
		}
	}
	else if(GetState() == EEventState::Active)
	{
		// enforce spectate lock for caughts players each tick
		for(const auto &[victim, catcher] : m_CaughtBy)
		{
			auto *pVictimPlayer = GameServer()->Bw().GetPlayer(victim);
			if(pVictimPlayer && pVictimPlayer->SpectatorId() != catcher)
				pVictimPlayer->SetSpectatorId(catcher);
		}

		// freeze-kill detection: kill a participant once they have been
		// continuously frozen for SvZCatchFreezeTimeout seconds.
		// this triggers the normal Die() -> OnPlayerKill -> OnBlockedKill flow
		// so the catch is attributed to whoever froze them.
		// skip during the initial freeze window so the round-start freeze doesn't
		// trigger a kill (block tracker may hold stale pre-event impact state).
		{
			const int graceTicks = Config()->m_SvZCatchInitialFreezeTime * Server()->TickSpeed();
			if((Server()->Tick() - m_ActiveStartTick) < graceTicks)
			{
				m_FrozenSince.clear(); // discard any stale entries accumulated during grace period
			}
			else
			{
				std::vector<int> toKill;
				for(int id : m_Participants)
				{
					if(IsCaught(id))
					{
						m_FrozenSince.erase(id);
						continue;
					}
					auto *pChar = GameServer()->GetPlayerChar(id);
					if(!pChar)
					{
						m_FrozenSince.erase(id);
						continue;
					}
					bool isFrozen = pChar->m_FreezeTime > 0;
					auto fsIt = m_FrozenSince.find(id);
					if(isFrozen && fsIt == m_FrozenSince.end())
					{
						m_FrozenSince[id] = Server()->Tick();
					}
					else if(!isFrozen && fsIt != m_FrozenSince.end())
					{
						m_FrozenSince.erase(fsIt);
					}
					else if(isFrozen && fsIt != m_FrozenSince.end())
					{
						int frozenTicks = Server()->Tick() - fsIt->second;
						int limitTicks = Config()->m_SvZCatchFreezeTimeout * Server()->TickSpeed();
						if(frozenTicks >= limitTicks)
						{
							// verify there's a tracked impactor who is a free participant
							int impactor = GameServer()->Bw().BlockTracker().GetImpactorOf(id);
							if(impactor >= 0 && IsParticipant(impactor) && !IsCaught(impactor))
								toKill.push_back(id);

							m_FrozenSince.erase(fsIt); // reset so we don't retry every tick
						}
					}
				}
				for(int id : toKill)
				{
					if(auto *pChar = GameServer()->GetPlayerChar(id))
						pChar->Die(-1, WEAPON_GAME);
				}
			}
		}

		int caughtCount = 0;
		int leadScore = 0;
		for(int id : m_Participants)
		{
			if(IsCaught(id))
				caughtCount++;
			auto it = m_Scores.find(id);
			if(it != m_Scores.end() && it->second > leadScore)
				leadScore = it->second;
		}

		for(int ClientId : m_Participants)
		{
			CPlayer *pPlayer = GameServer()->Bw().GetPlayer(ClientId);
			if(!pPlayer)
				continue;

			pPlayer->Bw().SendBroadcastAlignedLeft("%d / %d\n"
							       "Currently caught: %d / %d\n",
				leadScore, Config()->m_SvZCatchKillsToWin,
				caughtCount, (int)m_Participants.size() - 1);
		}

		CEventComponent::OnTick(); // process deferred position/weapon restores

		if(CheckEndCondition())
			FinishEvent(NATURAL);
	}
}

void CZCatchEvent::OnCharacterSpawn(int ClientId, vec2 /*SpawnPos*/)
{
	if(GetState() != EEventState::Active)
		return;

	m_FrozenSince.erase(ClientId); // clear stale freeze-tracking from previous life

	if(IsCaught(ClientId))
	{
		// caught player just respawned - safely lock them to spectate their catcher.
		// this is the correct place to call LockToSpectator (we are NOT inside Die() here).
		LockToSpectator(ClientId, m_CaughtBy.at(ClientId));
		return;
	}

	if(IsParticipant(ClientId))
	{
		// free participant respawned mid-event: pick the next random unique spawn.
		auto *pChar = GameServer()->GetPlayerChar(ClientId);
		if(pChar && !m_SpawnPositions.empty())
		{
			GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);
			GameServer()->Bw().Teleport(pChar, NextSpawnPos());
			pChar->GetCore().Reset();
		}
	}
}

void CZCatchEvent::OnCharacterDeath(int /*KillerId*/, int ClientId, int /*Weapon*/)
{
	if(GetState() != EEventState::Active)
		return;
	if(!IsParticipant(ClientId))
		return;
	if(IsCaught(ClientId))
		return; // already handled by OnBlockedKill inside Die()

	// OnBlockedKill fires only when the block tracker had a valid tracked impactor at death
	// time. If tracking was inactive (e.g. first death after teleport, or tracker expired),
	// the catch is silently skipped and the player would respawn freely. Fall back to our
	// own shadow-tracked impactor which is set on every hammer/hook impact and outlives
	// the tracker's own lifetime.
	auto impactIt = m_LastImpactorOf.find(ClientId);
	if(impactIt != m_LastImpactorOf.end())
	{
		int impactor = impactIt->second;
		m_LastImpactorOf.erase(impactIt);
		if(impactor >= 0 && IsParticipant(impactor) && !IsCaught(impactor))
		{
			OnBlockedKill(ClientId, impactor);
			return;
		}
	}

	// no valid impactor to credit — release their captives so those players can rejoin
	ReleaseCaptives(ClientId);
}

void CZCatchEvent::OnEventPlayerDropping(int ClientId)
{
	if(GetState() == EEventState::Active)
	{
		if(IsParticipant(ClientId))
			Leave(ClientId);
	}
	else if(GetState() == EEventState::Registration)
	{
		if(IsCandidate(ClientId))
			DeRegister(ClientId);
	}
}

void CZCatchEvent::OnSnapPlayerInfo(int ClientId, int /*SnappingClient*/, CNetObj_PlayerInfo *pPlayerInfo)
{
	if(GetState() != EEventState::Active)
		return;
	if(!IsParticipant(ClientId))
		return;
	auto it = m_Scores.find(ClientId);
	if(it != m_Scores.end())
		pPlayerInfo->m_Score = it->second;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool CZCatchEvent::IsCandidate(int ClientId) const
{
	return std::find(m_Candidates.begin(), m_Candidates.end(), ClientId) != m_Candidates.end();
}

bool CZCatchEvent::IsParticipant(int ClientId) const
{
	return std::find(m_Participants.begin(), m_Participants.end(), ClientId) != m_Participants.end();
}

bool CZCatchEvent::IsCaught(int ClientId) const
{
	return m_CaughtBy.count(ClientId) > 0;
}

bool CZCatchEvent::IsFighting(int ClientId) const
{
	return IsParticipant(ClientId) && !IsCaught(ClientId);
}

void CZCatchEvent::ReleaseCaptives(int CatcherId)
{
	auto capIt = m_Captives.find(CatcherId);
	if(capIt == m_Captives.end())
		return;

	// Copy to avoid modification during iteration
	std::set<int> toRelease = capIt->second;
	m_Captives.erase(capIt);

	for(int victim : toRelease)
	{
		m_CaughtBy.erase(victim);

		auto *pVictimPlayer = GameServer()->Bw().GetPlayer(victim);
		if(pVictimPlayer)
		{
			// Restore game team so the player can respawn
			if(pVictimPlayer->GetTeam() == TEAM_SPECTATORS)
				pVictimPlayer->SetTeam(0, false);
			// Put them back into the event DDRace team
			GameServer()->m_pController->Teams().SetForceCharacterTeam(victim, m_DDRaceTeam);
			pVictimPlayer->Respawn();
		}

		// char aBuf[128];
		// str_format(aBuf, sizeof(aBuf), "%s has been freed!", Server()->ClientName(victim));
		// GameServer()->Bw().SendChatTarget(-1, aBuf);
	}
}

void CZCatchEvent::LockToSpectator(int VictimId, int WatchedId)
{
	auto *pVictimPlayer = GameServer()->Bw().GetPlayer(VictimId);
	if(!pVictimPlayer)
		return;
	// Only change team if not already in spectator (avoids double KillCharacter calls)
	if(pVictimPlayer->GetTeam() != TEAM_SPECTATORS)
		pVictimPlayer->SetTeam(TEAM_SPECTATORS, false);
	// clear stale input so held keys don't carry over on respawn
	mem_zero(&GameServer()->m_aLastPlayerInput[VictimId], sizeof(GameServer()->m_aLastPlayerInput[VictimId]));
	GameServer()->m_aPlayerHasInput[VictimId] = false;
	// Override the spectator target to watch the catcher
	pVictimPlayer->SetSpectatorId(WatchedId);
}

vec2 CZCatchEvent::NextSpawnPos()
{
	return RandomSpawnPos(m_SpawnPositions, m_UsedSpawnIndices);
}
