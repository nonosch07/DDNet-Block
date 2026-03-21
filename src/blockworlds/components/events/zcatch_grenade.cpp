#include "zcatch_grenade.h"

#include <algorithm>

#include <engine/shared/config.h>

#include "event_helpers.h"
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/discord/webhook.h>

CZCatchGrenadeEvent::CZCatchGrenadeEvent(CGameContext *pGameServer) :
	CEventComponent(pGameServer)
{
	m_SpawnPositions.clear();

	m_SpawnPositions = GameServer()->ZoneManager()->GetNamedQuadCenters("zcg_spawn");
	if(m_SpawnPositions.empty())
	{
		EmergencyShutdown("Map has no zcatch grenade spawns!");
		return;
	}
}

void CZCatchGrenadeEvent::OpenRegistration()
{
	m_Candidates.clear();
	m_RegistrationEndTick = Server()->Tick() + Config()->m_SvZCatchRegistrationTime * Server()->TickSpeed();
	SetState(EEventState::Registration);
}

void CZCatchGrenadeEvent::CloseRegistration()
{
	SetState(EEventState::Preparation);
	GameServer()->SendBroadcast(-1, " ", false);
	if((int)m_Candidates.size() < Config()->m_SvZCatchMinimumCandidates)
	{
		FinishEvent(NOT_ENOUGH_CANDIDATES);
		return;
	}
	m_Participants = m_Candidates;
	m_Candidates.clear();
	StartEvent();
}

void CZCatchGrenadeEvent::StartEvent()
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
		if(Teams.GetTeamState(t) == CGameTeams::TEAMSTATE_EMPTY && !Teams.IsTeamEvent(t))
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
	m_PrevSoloState.clear();
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
	str_format(aBuf, sizeof(aBuf), "zCatch Grenade started! First to %d kills wins!", Config()->m_SvZCatchGrenadeKillsToWin);
	GameServer()->SendChatTarget(-1, aBuf);

	SetState(EEventState::Active);
}

void CZCatchGrenadeEvent::FinishEvent()
{
	SetState(EEventState::Ending);

	if(m_FinishReason == NATURAL)
	{
		if(m_Winner != -1)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "'%s' has won zCatch Grenade!", Server()->ClientName(m_Winner));
			GameServer()->SendChatTarget(-1, aBuf);
			GameServer()->SendBroadcast(-1, aBuf, false);

			// give winner exp multiplier and flag
			if(auto *pWinner = GameServer()->GetPlayer(m_Winner))
			{
				pWinner->AddExpMultiplier(Config()->m_SvZCatchGrenadeWinnerExpMultiplier, Config()->m_SvZCatchGrenadeWinnerExpDuration);
				pWinner->GiveFlag(Config()->m_SvZCatchGrenadeWinnerExpDuration);
				char aBonusBuf[256];
				str_format(aBonusBuf, sizeof(aBonusBuf), "%d%% experience bonus enabled for %d minutes!", Config()->m_SvZCatchGrenadeWinnerExpMultiplier, Config()->m_SvZCatchGrenadeWinnerExpDuration);
				GameServer()->SendChatTarget(m_Winner, aBonusBuf);
			}
			// webhook
			{
				CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
				const char *pZCatchUrl = g_Config.m_SvDiscordWebhookUrlZCatch[0] ? g_Config.m_SvDiscordWebhookUrlZCatch : nullptr;
				if(Discord.IsConfigured(pZCatchUrl))
				{
					const char *pWinnerName = Server()->ClientName(m_Winner);
					int WinScore = m_Scores.count(m_Winner) ? m_Scores.at(m_Winner) : 0;

					std::vector<std::pair<int, int>> vSorted(m_Scores.begin(), m_Scores.end());
					std::sort(vSorted.begin(), vSorted.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

					char aDiscord[1024];
					char aTmp[128];

					str_format(aDiscord, sizeof(aDiscord),
						"**zCatch Grenade Result**\n"
						"**Winner: %s** | %d kills\n"
						"```\n"
						"Rank  Player               Kills\n",
						pWinnerName ? pWinnerName : "?", WinScore);

					for(int i = 0; i < 3 && i < (int)vSorted.size(); ++i)
					{
						const char *pName = Server()->ClientName(vSorted[i].first);
						str_format(aTmp, sizeof(aTmp), "#%d    %-20.20s  %d\n",
							i + 1, pName ? pName : "?", vSorted[i].second);
						str_append(aDiscord, aTmp);
					}

					str_append(aDiscord, "```");

					CDiscordWebhook::SSendOptions Opt;
					Opt.m_pWebhookUrl = pZCatchUrl;
					Discord.Send(aDiscord, Opt);
				}
			}
		}
		else
		{
			const char *pMsg = "zCatch Grenade ended with no winner (time limit).";
			GameServer()->SendChatTarget(-1, pMsg);
			GameServer()->SendBroadcast(-1, pMsg, false);
		}
	}
	else if(m_FinishReason == NOT_ENOUGH_CANDIDATES)
	{
		GameServer()->SendChatTarget(-1, "Not enough players joined zCatch Grenade.");
	}
	else if(m_FinishReason == EMERGENCY)
	{
		GameServer()->SendChatTarget(-1, "zCatch Grenade ended prematurely.");
	}

	// release all caught players and return everyone to normal
	m_CaughtBy.clear();
	m_Captives.clear();

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
			pChar->Core()->m_CollisionDisabled = entry.second.collision;
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

void CZCatchGrenadeEvent::ForceNextStage()
{
	if(GetState() == EEventState::Registration)
		CloseRegistration();
	else if(GetState() == EEventState::Active)
		FinishEvent(NATURAL);
}

bool CZCatchGrenadeEvent::CheckEndCondition()
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
		if(score >= Config()->m_SvZCatchGrenadeKillsToWin)
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

bool CZCatchGrenadeEvent::Register(int ClientId)
{
	if(!CheckClientId(ClientId))
		return false;
	if(GetState() != EEventState::Registration)
	{
		GameServer()->SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	if(!GameServer()->GetPlayer(ClientId))
		return false;
	if(IsCandidate(ClientId))
	{
		GameServer()->SendChatTarget(ClientId, "You are already registered for zCatch Grenade.");
		return false;
	}
	if(g_Config.m_SvMaxClientsPerIp > 1 && g_Config.m_SvEventsTestMode == 0)
	{
		for(int CandId : m_Candidates)
		{
			if(GameServer()->Server()->IsClientsSameAddr(ClientId, CandId))
			{
				GameServer()->SendChatTarget(ClientId, "You cannot register for this event (Already registered).");
				return false;
			}
		}
	}

	m_Candidates.push_back(ClientId);
	GameServer()->SendChatTarget(ClientId, "You joined zCatch Grenade registration!");
	return true;
}

bool CZCatchGrenadeEvent::DeRegister(int ClientId)
{
	if(GetState() != EEventState::Registration)
	{
		GameServer()->SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	auto it = std::find(m_Candidates.begin(), m_Candidates.end(), ClientId);
	if(it == m_Candidates.end())
	{
		GameServer()->SendChatTarget(ClientId, "You are not registered for zCatch Grenade.");
		return false;
	}
	m_Candidates.erase(it);
	GameServer()->SendChatTarget(ClientId, "You left zCatch Grenade registration.");
	return true;
}

bool CZCatchGrenadeEvent::Join(int ClientId)
{
	SaveWeapons(ClientId); // saves current weapons, clears all, gives hammer + gun
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
			pChar->Core()->m_CollisionDisabled = false;

		GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);
		pChar->ResetVelocity();
		pChar->FreezeForce(Config()->m_SvZCatchInitialFreezeTime);
		GameServer()->Teleport(pChar, NextSpawnPos());

		// Replace hammer+gun (given by SaveWeapons) with grenade only
		ArmWithGrenade(pChar);
	}

	if(auto *pPlayer = GameServer()->GetPlayer(ClientId))
	{
		SaveAndClearCosmetics(ClientId);
		pPlayer->ClearCosmetics();
	}

	return true;
}

bool CZCatchGrenadeEvent::Leave(int ClientId)
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
	if(auto *pPlayer = GameServer()->GetPlayer(ClientId))
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
			pChar->Core()->m_CollisionDisabled = soloIt->second.collision;
			m_PrevSoloState.erase(soloIt);
		}
	}

	m_Scores.erase(ClientId);
	RestoreCosmetics(ClientId);
	return true;
}

void CZCatchGrenadeEvent::EmergencyShutdown(const char *pMsg)
{
	CEventComponent::EmergencyShutdown(pMsg);
	if(GetState() != EEventState::Finished)
		FinishEvent(EMERGENCY);
}

void CZCatchGrenadeEvent::OnCharacterTakeDamage(vec2 Force, vec2 Source, int Dmg, int From, int ClientId, int Weapon)
{
	if(GetState() != EEventState::Active)
		return;
	if(Weapon != WEAPON_GRENADE)
		return;

	// force the explosion to be closer, why 3? because claude said so :)
	if(Dmg < 3)
		return;

	// Both must be free (uncaught) participants
	if(!IsParticipant(ClientId) || IsCaught(ClientId))
		return;
	if(From == ClientId || !IsParticipant(From) || IsCaught(From))
		return;

	// Kill the victim — OnCharacterDeath will handle the catch/release logic.
	// Guard IsAlive() in case two explosions hit the same frame.
	auto *pChar = GameServer()->GetPlayerChar(ClientId);
	if(!pChar || !pChar->IsAlive())
		return;

	// if(GameServer()->Collision()->IntersectLine(Source, pChar->m_Pos, nullptr, nullptr))
	// 	return;

	pChar->Die(From, WEAPON_GRENADE);
}

void CZCatchGrenadeEvent::OnCharacterDeath(int KillerId, int ClientId, int /*Weapon*/)
{
	if(GetState() != EEventState::Active)
		return;
	if(!IsParticipant(ClientId))
		return;
	if(IsCaught(ClientId))
		return;

	ReleaseCaptives(ClientId);

	if(KillerId >= 0 && KillerId != ClientId && IsParticipant(KillerId) && !IsCaught(KillerId))
	{
		m_CaughtBy[ClientId] = KillerId;
		m_Captives[KillerId].insert(ClientId);
		m_Scores[KillerId]++;

		if(auto *pKillerChar = GameServer()->GetPlayerChar(KillerId))
			pKillerChar->SetEmote(EMOTE_HAPPY, Server()->Tick() + 3 * Server()->TickSpeed());
		GameServer()->CreateSoundGlobal(SOUND_HIT, KillerId);
	}
}

void CZCatchGrenadeEvent::OnCharacterSpawn(int ClientId, vec2 /*SpawnPos*/)
{
	if(GetState() != EEventState::Active)
		return;

	if(IsCaught(ClientId))
	{
		// caught player just respawned - safely lock them to spectate their catcher.
		LockToSpectator(ClientId, m_CaughtBy.at(ClientId));
		return;
	}

	if(IsParticipant(ClientId))
	{
		// free participant respawned mid-event: pick the next random unique spawn.
		auto *pChar = GameServer()->GetPlayerChar(ClientId);
		if(pChar)
		{
			GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);
			GameServer()->Teleport(pChar, NextSpawnPos());
			ArmWithGrenade(pChar);
		}
	}
}

void CZCatchGrenadeEvent::OnEventPlayerDropping(int ClientId)
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

void CZCatchGrenadeEvent::OnSnapPlayerInfo(int ClientId, int /*SnappingClient*/, CNetObj_PlayerInfo *pPlayerInfo)
{
	if(GetState() != EEventState::Active)
		return;
	if(!IsParticipant(ClientId))
		return;
	auto it = m_Scores.find(ClientId);
	if(it != m_Scores.end())
		pPlayerInfo->m_Score = it->second;
}

std::optional<int> CZCatchGrenadeEvent::GetScoreOf(int ClientId) const
{
	if(!IsParticipant(ClientId))
		return std::nullopt;
	auto it = m_Scores.find(ClientId);
	return (it != m_Scores.end()) ? std::optional<int>{it->second} : std::optional<int>{0};
}

void CZCatchGrenadeEvent::OnTick()
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

			CPlayer *pPlayer = GameServer()->GetPlayer(i);
			if(!pPlayer)
				continue;

			pPlayer->SendBroadcastAlignedLeft("zCatch Grenade is about to start!\n"
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
		// enforce spectate lock for caught players each tick
		for(const auto &[victim, catcher] : m_CaughtBy)
		{
			auto *pVictimPlayer = GameServer()->GetPlayer(victim);
			if(pVictimPlayer && pVictimPlayer->m_SpectatorId != catcher)
				pVictimPlayer->m_SpectatorId = catcher;
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
			CPlayer *pPlayer = GameServer()->GetPlayer(ClientId);
			if(!pPlayer)
				continue;

			pPlayer->SendBroadcastAlignedLeft("%d / %d\n"
							  "Currently caught: %d / %d\n",
				leadScore, Config()->m_SvZCatchGrenadeKillsToWin,
				caughtCount, (int)m_Participants.size() - 1);
		}

		CEventComponent::OnTick(); // process deferred position/weapon restores

		if(CheckEndCondition())
			FinishEvent(NATURAL);
	}
}

bool CZCatchGrenadeEvent::AllowZoomFor(int ClientId) const
{
	if(GetState() != EEventState::Active)
		return true;
	if(!IsParticipant(ClientId))
		return true;

	if(IsCaught(ClientId))
		return true;

	return false;
}

int CZCatchGrenadeEvent::GetMinCandidates() const
{
	return Config()->m_SvZCatchMinimumCandidates;
}

bool CZCatchGrenadeEvent::IsCandidate(int ClientId) const
{
	return std::find(m_Candidates.begin(), m_Candidates.end(), ClientId) != m_Candidates.end();
}

bool CZCatchGrenadeEvent::IsParticipant(int ClientId) const
{
	return std::find(m_Participants.begin(), m_Participants.end(), ClientId) != m_Participants.end();
}

bool CZCatchGrenadeEvent::IsCaught(int ClientId) const
{
	return m_CaughtBy.count(ClientId) > 0;
}

bool CZCatchGrenadeEvent::IsFighting(int ClientId) const
{
	return IsParticipant(ClientId) && !IsCaught(ClientId);
}

void CZCatchGrenadeEvent::ReleaseCaptives(int CatcherId)
{
	auto capIt = m_Captives.find(CatcherId);
	if(capIt == m_Captives.end())
		return;

	std::set<int> toRelease = capIt->second;
	m_Captives.erase(capIt);

	for(int victim : toRelease)
	{
		m_CaughtBy.erase(victim);

		auto *pVictimPlayer = GameServer()->GetPlayer(victim);
		if(pVictimPlayer)
		{
			if(pVictimPlayer->GetTeam() == TEAM_SPECTATORS)
				pVictimPlayer->SetTeam(0, false);
			GameServer()->m_pController->Teams().SetForceCharacterTeam(victim, m_DDRaceTeam);
			pVictimPlayer->Respawn();
		}
	}
}

void CZCatchGrenadeEvent::LockToSpectator(int VictimId, int WatchedId)
{
	auto *pVictimPlayer = GameServer()->GetPlayer(VictimId);
	if(!pVictimPlayer)
		return;
	if(pVictimPlayer->GetTeam() != TEAM_SPECTATORS)
		pVictimPlayer->SetTeam(TEAM_SPECTATORS, false);
	pVictimPlayer->m_SpectatorId = WatchedId;
}

vec2 CZCatchGrenadeEvent::NextSpawnPos()
{
	return RandomSpawnPos(m_SpawnPositions, m_UsedSpawnIndices);
}

void CZCatchGrenadeEvent::ArmWithGrenade(CCharacter *pChar)
{
	if(!pChar)
		return;
	// remove hammer and gun (given by SaveWeaponsHelper), give grenade only
	pChar->GiveWeapon(WEAPON_HAMMER, true);
	pChar->GiveWeapon(WEAPON_GUN, true);
	pChar->GiveWeapon(WEAPON_GRENADE);
	pChar->SetActiveWeapon(WEAPON_GRENADE);
}
