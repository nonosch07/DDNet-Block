#include "tdm.h"

#include "event_helpers.h"
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>

#include <algorithm>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/discord/webhook.h>
#include <random>

CTeamDeathmatchEvent::CTeamDeathmatchEvent(CGameContext *pGameContext) :
	CEventComponent(pGameContext), m_Rng((unsigned)std::random_device{}() ^ (unsigned)pGameContext->Server()->Tick())
{
	// preload shared event spawn positions from gamezone quads
	m_EventStartPositions = pGameContext->ZoneManager()->GetNamedQuadCenters("tdm_spawn");
}

// ===== Helpers =====

void CTeamDeathmatchEvent::AssignTeamsShuffled()
{
	m_ClientTeam.clear();
	if(m_Participants.empty())
		return;

	std::shuffle(m_Participants.begin(), m_Participants.end(), m_Rng);
	const int split = (int)m_Participants.size() / 2;
	for(size_t i = 0; i < m_Participants.size(); ++i)
	{
		int side = (int)i >= split ? 1 : 0; // 0 = blue, 1 = red
		m_ClientTeam[m_Participants[i]] = side;
	}
}

void CTeamDeathmatchEvent::ApplyParticipantVisuals(int ClientId, int Side)
{
	if(auto *p = GameServer()->GetPlayer(ClientId))
	{
		p->m_OldTeeInfos = p->m_TeeInfos;
		str_copy(p->m_TeeInfos.m_aSkinName, "default", sizeof(p->m_TeeInfos.m_aSkinName));
		p->m_TeeInfos.m_UseCustomColor = 1;
		if(Side == 0)
		{
			p->m_TeeInfos.m_ColorBody = 10223467; // blue
			p->m_TeeInfos.m_ColorFeet = 10223467;
		}
		else
		{
			p->m_TeeInfos.m_ColorBody = 65387; // red
			p->m_TeeInfos.m_ColorFeet = 65387;
		}
		p->m_HideInfo = true;
		p->m_HideInfoInScoreboard = true;

		// strip cosmetics
		p->ClearCosmetics();
		p->SetSkinMani(-1);
		p->SetGunDesign(-1);
		p->SetKnockout(-1);
		if(p->GetCurrentSpecial() != -1)
			p->ToggleSpecial(p->GetCurrentSpecial());
	}
}

void CTeamDeathmatchEvent::RestoreParticipantVisuals(int ClientId)
{
	if(auto *p = GameServer()->GetPlayer(ClientId))
	{
		p->m_TeeInfos = p->m_OldTeeInfos;
		p->m_HideInfo = false;
		p->m_HideInfoInScoreboard = false;
	}
}

void CTeamDeathmatchEvent::SaveAndPrepareParticipants()
{
	for(int ClientId : m_Participants)
	{
		// save cosmetics snapshot before ApplyParticipantVisuals strips them
		SaveAndClearCosmetics(ClientId);

		SaveWeapons(ClientId);
		SavePosition(ClientId);

		auto it = m_ClientTeam.find(ClientId);
		if(it != m_ClientTeam.end())
			ApplyParticipantVisuals(ClientId, it->second);

		if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
		{
			// save transient solo/collision, then ensure normal interaction in the event
			bool wasSolo = pChar->Core()->m_Solo;
			bool wasCollisionDisabled = pChar->Core()->m_CollisionDisabled;
			m_PrevSoloState[ClientId] = {wasSolo, wasCollisionDisabled};
			if(wasSolo)
				pChar->SetSolo(false);
			if(wasCollisionDisabled)
				pChar->Core()->m_CollisionDisabled = false;
		}
	}
}

void CTeamDeathmatchEvent::RestoreParticipants()
{
	// restore visuals first
	for(int ClientId : m_Participants)
		RestoreParticipantVisuals(ClientId);

	// restore solo/collision
	for(const auto &kv : m_PrevSoloState)
	{
		if(auto *pChar = GameServer()->GetPlayerChar(kv.first))
		{
			if(kv.second.solo)
				pChar->SetSolo(true);
			pChar->Core()->m_CollisionDisabled = kv.second.collision;
		}
	}
	m_PrevSoloState.clear();

	// restore saved position & weapons (deferred processed right after)
	std::vector<int> ids;
	ids.reserve(m_pSavedPlayers.size());
	for(const auto &kv : m_pSavedPlayers)
		ids.push_back(kv.first);
	for(int ClientId : ids)
	{
		LoadPosition(ClientId);
		LoadWeapons(ClientId);
	}
	CEventComponent::OnTick();
}

std::optional<vec2> CTeamDeathmatchEvent::ChooseSpawnFor(int ClientId)
{
	if(!m_EventStartPositions.empty())
	{
		auto it = m_AssignedSpawnIndex.find(ClientId);
		if(it != m_AssignedSpawnIndex.end())
		{
			int idx = it->second;
			m_AssignedSpawnIndex.erase(it); // consume reservation so later respawns use least-crowded logic
			if(idx >= 0 && idx < (int)m_EventStartPositions.size())
				return m_EventStartPositions[(size_t)idx];
		}

		// choose the least-crowded start position
		int bestIdx = -1;
		int bestCount = 0x7fffffff;
		const float R = 100.0f;
		for(int i = 0; i < (int)m_EventStartPositions.size(); ++i)
		{
			vec2 pos = m_EventStartPositions[(size_t)i];
			int nearby = 0;
			for(int pid : m_Participants)
			{
				if(pid == ClientId)
					continue;
				auto *pChr = GameServer()->GetPlayerChar(pid);
				if(!pChr || !pChr->IsAlive())
					continue;
				if(distance(pChr->m_Pos, pos) <= R)
					nearby++;
			}
			if(nearby < bestCount)
			{
				bestCount = nearby;
				bestIdx = i;
			}
		}
		if(bestIdx >= 0)
			return m_EventStartPositions[(size_t)bestIdx];
	}
	// no tdm_spawn gamezone quads: do not teleport (fallback is saved position)
	(void)ClientId;
	return std::nullopt;
}

void CTeamDeathmatchEvent::TeleportToSpawn(int ClientId)
{
	if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
	{
		if(auto pos = ChooseSpawnFor(ClientId))
			GameServer()->Teleport(pChar, *pos);
		pChar->ResetVelocity();
	}
}

void CTeamDeathmatchEvent::BroadcastStatus()
{
	int secs = (int)((m_ActiveEndTick - Server()->Tick()) / Server()->TickSpeed());
	if(secs < 0)
		secs = 0;
	char aTimeLeft[32];
	FormatTimeLeft(aTimeLeft, sizeof(aTimeLeft), secs);

	for(int ClientId : m_Participants)
	{
		CPlayer *pPlayer = GameServer()->GetPlayer(ClientId);
		if(!pPlayer)
			continue;

		int side = GetSideOf(ClientId);
		if(side == 0)
		{
			pPlayer->SendBroadcastAlignedLeft("Team Blue\n"
							  "Blue %d / %d\n"
							  "Red %d / %d\n"
							  "Time left: %s",
				m_ScoreTeam[0], m_TargetScore, m_ScoreTeam[1], m_TargetScore, aTimeLeft);
		}
		else if(side == 1)
		{
			pPlayer->SendBroadcastAlignedLeft("Team Red\n"
							  "Red %d / %d\n"
							  "Blue %d / %d\n"
							  "Time left: %s",
				m_ScoreTeam[1], m_TargetScore, m_ScoreTeam[0], m_TargetScore, aTimeLeft);
		}
		else
		{
			pPlayer->SendBroadcastAlignedLeft("%s\n"
							  "Blue %d / %d\n"
							  "Red %d / %d\n"
							  "Time left: %s",
				GetEventName(), m_ScoreTeam[0], m_TargetScore, m_ScoreTeam[1], m_TargetScore, aTimeLeft);
		}
	}
}

void CTeamDeathmatchEvent::EnsureForcedTeamForAll()
{
	auto &Teams = GameServer()->m_pController->Teams();
	for(int ClientId : m_Participants)
		Teams.SetForceCharacterTeam(ClientId, m_DDRaceTeam);
}

void CTeamDeathmatchEvent::TrackFreezeAndAutokill()
{
	const int RequiredFreezeTicks = Config()->m_SvTDMFreezeTimeKill * Server()->TickSpeed();
	for(int ClientId : m_Participants)
	{
		auto *pChar = GameServer()->GetPlayerChar(ClientId);
		if(!pChar || !pChar->IsAlive())
			continue;

		// track freeze start/stop
		const bool isFrozen = pChar->m_FreezeTime;
		int since = GetFrozenSince(ClientId);
		if(isFrozen && since == 0)
			SetFrozenSince(ClientId, Server()->Tick());
		if(!isFrozen && since != 0)
			SetFrozenSince(ClientId, 0);

		if(isFrozen)
		{
			int &lastVal = m_LastFreezeTimeValue[ClientId];
			if(lastVal == 0)
				lastVal = pChar->m_FreezeTime; // initialize

			if(pChar->m_FreezeTime < lastVal)
			{
				// countdown progressed -> not perma-frozen; restart delay
				m_PermanentFreezeSince.erase(ClientId);
			}
			else // equal or increased: not decreasing
			{
				int &st = m_PermanentFreezeSince[ClientId];
				if(st == 0)
					st = Server()->Tick();
				if(Server()->Tick() - st >= RequiredFreezeTicks)
				{
					// autokill due to permanent freeze should not emit a kill message
					pChar->Die(-1, WEAPON_WORLD, false);
					SetFrozenSince(ClientId, 0);
					m_PermanentFreezeSince.erase(ClientId);
					GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);
					if(CheckEndCondition())
					{
						FinishEvent();
						return;
					}
				}
			}
			lastVal = pChar->m_FreezeTime;
		}
		else
		{
			// not frozen -> clear trackers
			m_LastFreezeTimeValue.erase(ClientId);
			m_PermanentFreezeSince.erase(ClientId);
		}
	}
}

void CTeamDeathmatchEvent::ApplyGroundHookPenalty(int ClientId)
{
	const int GroundHookDelayTicks = Config()->m_SvGroundHookPenaltyDelay * Server()->TickSpeed();
	if(PlayerHookedGroundFor(ClientId) > GroundHookDelayTicks)
	{
		if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
			pChar->FreezeForce(Config()->m_SvGroundHookPenalty);
	}
}

void CTeamDeathmatchEvent::UpdatePerPlayerScores()
{
	for(int pid : m_Participants)
	{
		if(auto *p = GameServer()->GetPlayer(pid))
		{
			int side = GetSideOf(pid);
			p->m_Score = side == 0 ? m_ScoreTeam[0] : m_ScoreTeam[1];
		}
	}
}

void CTeamDeathmatchEvent::ResetTransientState()
{
	m_ClientTeam.clear();
	m_PrevSoloState.clear();
	m_FrozenSince.clear();
	m_RespawnAtTick.clear();
	m_LastRespawnSeconds.clear();
	m_AssignedSpawnIndex.clear();
	m_SetSpecAtTick.clear();
	m_LastDeathHandledTick.clear();
	m_LastImpactByVictim.clear();
	m_LastImpactTick.clear();
	m_LastFreezeTimeValue.clear();
	m_PermanentFreezeSince.clear();
	m_ScoreTeam[0] = m_ScoreTeam[1] = 0;
	ResetStats();
}

int CTeamDeathmatchEvent::GetSideOf(int ClientId) const
{
	auto it = m_ClientTeam.find(ClientId);
	return it == m_ClientTeam.end() ? -1 : it->second;
}

void CTeamDeathmatchEvent::SetFrozenSince(int ClientId, int Tick)
{
	m_FrozenSince[ClientId] = Tick;
}

int CTeamDeathmatchEvent::GetFrozenSince(int ClientId) const
{
	auto it = m_FrozenSince.find(ClientId);
	return it == m_FrozenSince.end() ? 0 : it->second;
}

void CTeamDeathmatchEvent::UpdateRespawns()
{
	if(m_RespawnAtTick.empty())
		return;

	std::vector<int> ready;
	ready.reserve(m_RespawnAtTick.size());
	for(auto &kv : m_RespawnAtTick)
	{
		int cid = kv.first;
		int when = kv.second;
		int ticksLeft = when - Server()->Tick();
		int secsLeft = ticksLeft > 0 ? (ticksLeft + Server()->TickSpeed() - 1) / Server()->TickSpeed() : 0; // ceil
		int &lastShown = m_LastRespawnSeconds[cid];
		if(secsLeft != lastShown)
		{
			lastShown = secsLeft;
			if(secsLeft > 0)
			{
				CPlayer *pPlayer = GameServer()->GetPlayer(cid);
				if(pPlayer)
					pPlayer->SendBroadcastAlignedLeft("You will respawn in %d %s", secsLeft, secsLeft == 1 ? "sec" : "secs");
			}
		}
		if(Server()->Tick() >= when)
			ready.push_back(cid);
	}

	for(int cid : ready)
	{
		m_RespawnAtTick.erase(cid);
		m_LastRespawnSeconds.erase(cid);
		// respawn the player at a chosen spawn position
		if(IsParticipant(cid))
		{
			if(CPlayer *p = GameServer()->GetPlayer(cid))
			{
				// reinforce event team and force-spawn at chosen spot if available
				GameServer()->m_pController->Teams().SetForceCharacterTeam(cid, m_DDRaceTeam);
				if(auto pos = ChooseSpawnFor(cid))
				{
					m_SkipTeleportOnSpawn[cid] = true; // avoid duplicate teleportation in OnCharacterSpawn
					p->ForceSpawn(*pos, false);
				}
				else
				{
					m_SkipTeleportOnSpawn[cid] = true; // still skip freeze path in OnCharacterSpawn
					p->Respawn(false);
				}
				GameServer()->SendBroadcast(" ", cid, false); // clear countdown
			}
		}
	}
}

void CTeamDeathmatchEvent::UpdateSetSpectators()
{
	if(m_SetSpecAtTick.empty())
		return;
	std::vector<int> done;
	done.reserve(m_SetSpecAtTick.size());
	for(const auto &kv : m_SetSpecAtTick)
	{
		int cid = kv.first;
		int when = kv.second;
		if(Server()->Tick() < when)
			continue;
		CPlayer *p = GameServer()->GetPlayer(cid);
		if(!p)
		{
			done.push_back(cid);
			continue;
		}
		if(!p->GetCharacter())
		{
			p->SetTeam(TEAM_SPECTATORS, false);
			done.push_back(cid);
		}
	}
	for(int cid : done)
		m_SetSpecAtTick.erase(cid);
}

void CTeamDeathmatchEvent::AssignUniqueStartSpawns()
{
	m_AssignedSpawnIndex.clear();
	if(m_EventStartPositions.empty())
		return;

	// Greedy: assign each participant a unique spawn if enough positions exist
	std::vector<int> indices(m_EventStartPositions.size());
	for(size_t i = 0; i < indices.size(); ++i)
		indices[i] = (int)i;
	std::shuffle(indices.begin(), indices.end(), m_Rng);

	const int uniqueCount = std::min((int)m_Participants.size(), (int)indices.size());
	for(int i = 0; i < uniqueCount; ++i)
		m_AssignedSpawnIndex[m_Participants[(size_t)i]] = indices[(size_t)i];
}

std::optional<int> CTeamDeathmatchEvent::GetTeamIndexFor(int ClientId) const
{
	auto it = m_ClientTeam.find(ClientId);
	if(it == m_ClientTeam.end())
		return std::nullopt;
	// internal mapping is 0=blue,1=red; caller will translate to TEAM_BLUE/TEAM_RED
	return it->second;
}

// ===== Event lifecycle =====

void CTeamDeathmatchEvent::OnTick()
{
	if(CEventComponent::EmergencyShutdown())
	{
		FinishEvent();
		return;
	}

	if(GetState() == CEventComponent::EEventState::Registration)
	{
		if(Server()->Tick() >= m_RegistrationEndTick)
		{
			CloseRegistration();
			return;
		}

		CEventComponent::OnTick(); // test-mode dummies
		for(int i = 0; i < Server()->MaxClients(); ++i)
		{
			CPlayer *pPlayer = GameServer()->GetPlayer(i);
			if(!pPlayer)
				continue;

			char aTimeLeft[32];
			FormatTimeLeft(aTimeLeft, sizeof(aTimeLeft), (int)((m_RegistrationEndTick - Server()->Tick()) / Server()->TickSpeed()));
			pPlayer->SendBroadcastAlignedLeft("%s is about to start!\nRegister with /join\nTime left: %s\n\nParticipants: %zd",
				GetEventName(), aTimeLeft, Candidates().size());
		}
	}
	else if(GetState() == CEventComponent::EEventState::Active)
	{
		for(int id : Participants())
		{
			// keep cosmetics disabled and apply ground hook penalty
			ApplyGroundHookPenalty(id);
			if(auto *p = GameServer()->GetPlayer(id))
			{
				p->m_HideInfo = true;
				p->m_HideInfoInScoreboard = true;
			}
		}

		BroadcastStatus();

		// after initial fixed freeze window, start tracking long freezes for autokill
		if(m_ActiveStartTick != -1 && Server()->Tick() > m_ActiveStartTick + Config()->m_SvTDMFreezeTime * Server()->TickSpeed())
			TrackFreezeAndAutokill();

		// move freshly killed participants to spectators (deferred)
		UpdateSetSpectators();

		// handle spectate-on-death respawn countdowns and spawns
		UpdateRespawns();

		if(CheckEndCondition())
			FinishEvent();
	}
}

void CTeamDeathmatchEvent::OnSnapClientInfo(int ClientId, int SnappingClient, struct CNetObj_ClientInfo *pClientInfo)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(IsParticipant(ClientId) && SnappingClient != ClientId)
	{
		if(Server()->ClientAuthed(SnappingClient))
			return;

		StrToInts(&pClientInfo->m_Name0, 4, " ");
		StrToInts(&pClientInfo->m_Clan0, 3, " ");
		StrToInts(&pClientInfo->m_Skin0, 6, "default");
		pClientInfo->m_Country = 0;
	}
}

void CTeamDeathmatchEvent::OnCharacterSpawn(int ClientId, vec2 /*SpawnPos*/)
{
	if(GetState() != CEventComponent::EEventState::Active || !IsParticipant(ClientId))
		return;

	GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);

	if(auto it = m_SkipTeleportOnSpawn.find(ClientId); it != m_SkipTeleportOnSpawn.end() && it->second)
	{
		m_SkipTeleportOnSpawn.erase(it);
		return;
	}
	TeleportToSpawn(ClientId);

	if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
	{
		if(m_ActiveStartTick != -1 && Server()->Tick() < m_ActiveStartTick + Config()->m_SvTDMFreezeTime * Server()->TickSpeed())
		{
			pChar->FreezeForce(Config()->m_SvTDMFreezeTime);
			SetFrozenSince(ClientId, Server()->Tick());
		}
	}
}

void CTeamDeathmatchEvent::OnEventPlayerDropping(int ClientId)
{
	if(GetState() == CEventComponent::EEventState::Active)
	{
		if(IsParticipant(ClientId))
			Leave(ClientId);
	}
	else if(GetState() == CEventComponent::EEventState::Registration)
	{
		if(IsCandidate(ClientId))
			DeRegister(ClientId);
	}
}

void CTeamDeathmatchEvent::OpenRegistration()
{
	m_Candidates.clear();
	m_RegistrationEndTick = Server()->Tick() + Config()->m_SvTDMRegistrationTime * Server()->TickSpeed();
	SetState(CEventComponent::EEventState::Registration);
}

void CTeamDeathmatchEvent::CloseRegistration()
{
	SetState(CEventComponent::EEventState::Preparation);
	GameServer()->SendBroadcast(-1, " ", false); // clear registration broadcast for all

	if((int)m_Candidates.size() < Config()->m_SvTDMMinimumCandidates)
	{
		GameServer()->SendChatTarget(-1, "Not enough candidates joined %s", GetEventName());
		GameServer()->SendBroadcast(-1, "Not enough candidates joined %s", GetEventName());
		SetState(CEventComponent::EEventState::Finished);
		return;
	}

	if((int)m_Candidates.size() > Config()->m_SvTDMMaximumCandidates)
		m_Candidates.resize(Config()->m_SvTDMMaximumCandidates);

	m_Participants = m_Candidates;
	m_Candidates.clear();
	StartEvent();
}

void CTeamDeathmatchEvent::StartEvent()
{
	ResetTransientState();

	// create isolated DDNet team for the event
	auto &Teams = GameServer()->m_pController->Teams();
	// choose an empty team that is not currently used by another event
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
	Teams.SetTeamLock(m_DDRaceTeam, true);
	Teams.SetTeamInvitesOpen(m_DDRaceTeam, false);

	AssignTeamsShuffled();
	SaveAndPrepareParticipants();
	AssignUniqueStartSpawns();

	m_ActiveStartTick = Server()->Tick();
	m_ActiveEndTick = Server()->Tick() + Config()->m_SvTDMActiveTime * Server()->TickSpeed();

	// force all participants into the event team and spawn/freeze
	for(int ClientId : m_Participants)
	{
		Teams.SetForceCharacterTeam(ClientId, m_DDRaceTeam);
		TeleportToSpawn(ClientId);
		if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
			pChar->FreezeForce(Config()->m_SvTDMFreezeTime);
	}

	// adaptive target score based on the larger team size
	int countBlue = 0, countRed = 0;
	for(const auto &kv : m_ClientTeam)
		(kv.second == 0 ? countBlue : countRed)++;
	m_TargetScore = std::max(countBlue, countRed) * 10;

	SetState(CEventComponent::EEventState::Active);
}

void CTeamDeathmatchEvent::OnCharacterDeath(int KillerId, int ClientId, int /*Weapon*/)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	// prevent duplicate processing in the same tick for the same victim
	{
		int now = Server()->Tick();
		int &last = m_LastDeathHandledTick[ClientId];
		if(last == now)
			return;
		last = now;
	}

	// update temporary player stats (does not affect account K/D)
	const bool victimIsParticipant = IsParticipant(ClientId);
	if(victimIsParticipant)
		m_PlayerStats[ClientId].Deaths++;

	int vSide = GetSideOf(ClientId);
	bool killCredited = false;
	if(vSide != -1)
	{
		// primary: credit the actual killer if it's a valid opposing participant
		if(KillerId >= 0 && KillerId != ClientId && IsParticipant(KillerId))
		{
			int kSide = GetSideOf(KillerId);
			if(kSide != -1 && kSide != vSide)
			{
				m_PlayerStats[KillerId].Kills++;
				killCredited = true;
			}
		}

		if(!killCredited)
		{
			auto itA = m_LastImpactByVictim.find(ClientId);
			auto itT = m_LastImpactTick.find(ClientId);
			if(itA != m_LastImpactByVictim.end() && itT != m_LastImpactTick.end())
			{
				const int attacker = itA->second;
				const int impactTick = itT->second;
				const int windowTicks = 10 * Server()->TickSpeed(); // 10s window for last-impact attribution
				if(attacker >= 0 && attacker != ClientId && Server()->Tick() - impactTick <= windowTicks)
				{
					if(IsParticipant(attacker))
					{
						int aSide = GetSideOf(attacker);
						if(aSide != -1 && aSide != vSide)
						{
							m_PlayerStats[attacker].Kills++;
							killCredited = true;
						}
					}
				}
			}
		}
	}

	auto itV = m_ClientTeam.find(ClientId);
	if(itV != m_ClientTeam.end())
	{
		const int victimSide = itV->second;
		m_ScoreTeam[Opposite(victimSide)] += m_PointsPerKill;
	}

	// keep event team enforced
	auto &Teams = GameServer()->m_pController->Teams();
	if(IsParticipant(ClientId))
		Teams.SetForceCharacterTeam(ClientId, m_DDRaceTeam);
	if(KillerId >= 0 && IsParticipant(KillerId))
		Teams.SetForceCharacterTeam(KillerId, m_DDRaceTeam);

	UpdatePerPlayerScores();
	if(CheckEndCondition())
		FinishEvent();

	// schedule spectate-on-death respawn after 3 seconds for participants
	if(IsParticipant(ClientId))
	{
		m_RespawnAtTick[ClientId] = Server()->Tick() + 3 * Server()->TickSpeed();
		m_LastRespawnSeconds[ClientId] = -1; // force immediate broadcast update
		// defer switching to spectators by a tick to avoid re-entrant Die loops
		m_SetSpecAtTick[ClientId] = Server()->Tick() + 1;
	}
}

void CTeamDeathmatchEvent::OnPlayerImpacted(int ClientId, int InitiatorId)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;
	if(!IsParticipant(ClientId) || !IsParticipant(InitiatorId))
		return;
	if(ClientId == InitiatorId)
		return;
	const int vSide = GetSideOf(ClientId);
	const int aSide = GetSideOf(InitiatorId);
	if(vSide == -1 || aSide == -1 || vSide == aSide)
		return;
	m_LastImpactByVictim[ClientId] = InitiatorId;
	m_LastImpactTick[ClientId] = Server()->Tick();
}

int CTeamDeathmatchEvent::GetMinCandidates() const
{
	return Config()->m_SvTDMMinimumCandidates;
}

bool CTeamDeathmatchEvent::AllowKillCommandFor(int ClientId) const
{
	if(GetState() != CEventComponent::EEventState::Active)
		return false;
	if(!IsParticipant(ClientId))
		return false;
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(!pChr)
		return false;
	if(pChr->m_FreezeTime <= 0)
		return false;
	int since = GetFrozenSince(ClientId);
	if(since == 0)
		return false;
	const int required = 2 * Server()->TickSpeed(); // 2 seconds
	return Server()->Tick() - since >= required;
}
void CTeamDeathmatchEvent::FinishEvent()
{
	if(GetState() == CEventComponent::EEventState::Ending || GetState() == CEventComponent::EEventState::Finished)
		return;

	SetState(CEventComponent::EEventState::Ending);

	// announce winner
	if(m_ScoreTeam[0] > m_ScoreTeam[1])
		GameServer()->SendBroadcast(-1, "Team Blue wins %d - %d", m_ScoreTeam[0], m_ScoreTeam[1]);
	else if(m_ScoreTeam[1] > m_ScoreTeam[0])
		GameServer()->SendBroadcast(-1, "Team Red wins %d - %d", m_ScoreTeam[1], m_ScoreTeam[0]);
	else
		GameServer()->SendBroadcast(-1, "TDM ended in a tie %d - %d", m_ScoreTeam[0], m_ScoreTeam[1]);

	struct Ranked
	{
		int ClientId;
		int Kills;
	};
	std::vector<Ranked> blue, red;
	for(int pid : m_Participants)
	{
		int side = GetSideOf(pid);
		int kills = 0;
		auto it = m_PlayerStats.find(pid);
		if(it != m_PlayerStats.end())
			kills = it->second.Kills;
		Ranked r{pid, kills};
		if(side == 0)
			blue.push_back(r);
		else if(side == 1)
			red.push_back(r);
	}
	auto cmp = [](const Ranked &a, const Ranked &b) { return a.Kills > b.Kills; };
	std::sort(blue.begin(), blue.end(), cmp);
	std::sort(red.begin(), red.end(), cmp);

	int winBP[3] = {Config()->m_SvTDMWinBP1, Config()->m_SvTDMWinBP2, Config()->m_SvTDMWinBP3};
	int loseBP[3] = {Config()->m_SvTDMLoseBP1, Config()->m_SvTDMLoseBP2, Config()->m_SvTDMLoseBP3};
	bool blueWin = m_ScoreTeam[0] > m_ScoreTeam[1];
	bool redWin = m_ScoreTeam[1] > m_ScoreTeam[0];

	auto award = [&](const std::vector<Ranked> &team, bool isWinner) {
		for(int i = 0; i < 3 && i < (int)team.size(); ++i)
		{
			int bp = isWinner ? winBP[i] : loseBP[i];
			if(bp > 0)
			{
				if(auto *pPlayer = GameServer()->GetPlayer(team[i].ClientId))
				{
					if(!pPlayer->IsLoggedIn())
					{
						GameServer()->SendChatTarget(team[i].ClientId, "You must be logged in to receive rewards.");
						continue;
					}
					pPlayer->SetPlayerBlockpoints(pPlayer->GetPlayerBlockpoints() + bp);
					GameServer()->Accounts()->Save(team[i].ClientId, &pPlayer->m_Account);
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "You received %d BP for your performance in TDM.", bp);
					GameServer()->SendChatTarget(team[i].ClientId, aBuf);
				}
			}
		}
	};
	if(blueWin)
	{
		award(blue, true);
		award(red, false);
	}
	else if(redWin)
	{
		award(red, true);
		award(blue, false);
	}

	// announce per-team best players and summary stats in chat
	AnnounceResults();

	RestoreParticipants();

	m_Participants.clear();
	if(m_DDRaceTeam != -1)
	{
		auto &Teams = GameServer()->m_pController->Teams();
		Teams.SetTeamEvent(m_DDRaceTeam, false);
		Teams.SetTeamLock(m_DDRaceTeam, false);
		Teams.ResetRoundState(m_DDRaceTeam);
	}

	SetState(CEventComponent::EEventState::Finished);
}

void CTeamDeathmatchEvent::ResetStats()
{
	m_PlayerStats.clear();
}

void CTeamDeathmatchEvent::AnnounceResults()
{
	struct Ranked
	{
		int ClientId;
		int K;
		int D;
		float KD;
		int Side;
	};
	std::vector<Ranked> blue, red;
	blue.reserve(m_Participants.size());
	red.reserve(m_Participants.size());
	for(int pid : m_Participants)
	{
		const auto it = m_PlayerStats.find(pid);
		int k = 0, d = 0;
		if(it != m_PlayerStats.end())
		{
			k = it->second.Kills;
			d = it->second.Deaths;
		}
		float kd = d > 0 ? (float)k / (float)d : (k > 0 ? (float)k : 0.0f);
		int side = GetSideOf(pid);
		Ranked r{pid, k, d, kd, side};
		if(side == 0)
			blue.push_back(r);
		else if(side == 1)
			red.push_back(r);
	}
	auto cmp = [](const Ranked &a, const Ranked &b) {
		if(a.K != b.K)
			return a.K > b.K; // more kills first
		if(a.KD != b.KD)
			return a.KD > b.KD; // better KD
		return a.ClientId < b.ClientId; // stable
	};
	std::sort(blue.begin(), blue.end(), cmp);
	std::sort(red.begin(), red.end(), cmp);

	char aBuf[256];
	// overall summary
	str_format(aBuf, sizeof(aBuf), "TDM results: Blue %d - %d Red (Target %d)", m_ScoreTeam[0], m_ScoreTeam[1], m_TargetScore);
	GameServer()->SendChatTarget(-1, aBuf);

	// show the top3 best players of each team
	auto announceTop = [&](const char *pTeamName, const std::vector<Ranked> &v) {
		for(int i = 0; i < 3; ++i)
		{
			if(i < (int)v.size())
			{
				const auto &r = v[i];
				const char *pName = GameServer()->Server()->ClientName(r.ClientId);
				int kd_int = (int)(r.KD * 100.0f + 0.5f);
				str_format(aBuf, sizeof(aBuf), "%s #%d: %s - K %d / D %d (K/D %d.%02d)",
					pTeamName, i + 1, pName, r.K, r.D, kd_int / 100, kd_int % 100);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%s #%d: —", pTeamName, i + 1);
			}
			GameServer()->SendChatTarget(-1, aBuf);
		}
	};

	announceTop("Blue", blue);
	announceTop("Red", red);

	// discord webhook: post TDM result with teams, scores and top-3 per side
	{
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
		const char *pTdmUrl = g_Config.m_SvDiscordWebhookUrlTdm[0] ? g_Config.m_SvDiscordWebhookUrlTdm : nullptr;
		if(Discord.IsConfigured(pTdmUrl))
		{
			bool BlueWins = m_ScoreTeam[0] > m_ScoreTeam[1];
			bool RedWins = m_ScoreTeam[1] > m_ScoreTeam[0];

			// strip emoji/non-ASCII bytes from a name so monospace table columns don't shift
			// %-16.16s counts BYTES, but emoji can be 2-4 bytes wide yet only ~1 visual char
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

			char aDiscord[2000];
			char aTmp[128];

			if(BlueWins)
				str_format(aDiscord, sizeof(aDiscord),
					"**TDM Result**\n"
					"**Winner: Team Blue** | Blue **%d** - Red **%d** | Target: %d\n"
					"```\n",
					m_ScoreTeam[0], m_ScoreTeam[1], m_TargetScore);
			else if(RedWins)
				str_format(aDiscord, sizeof(aDiscord),
					"**TDM Result**\n"
					"**Winner: Team Red** | Blue **%d** - Red **%d** | Target: %d\n"
					"```\n",
					m_ScoreTeam[0], m_ScoreTeam[1], m_TargetScore);
			else
				str_format(aDiscord, sizeof(aDiscord),
					"**TDM Result**\n"
					"Tie | Blue **%d** - Red **%d** | Target: %d\n"
					"```\n",
					m_ScoreTeam[0], m_ScoreTeam[1], m_TargetScore);

			str_append(aDiscord, "Team Blue           K    D    K/D\n");
			for(int i = 0; i < 3; ++i)
			{
				if(i < (int)blue.size())
				{
					const char *pName = Server()->ClientName(blue[i].ClientId);
					char aSafeName[17];
					SanitizeName(pName ? pName : "?", aSafeName, sizeof(aSafeName));
					int kdi = (int)(blue[i].KD * 100.0f + 0.5f);
					str_format(aTmp, sizeof(aTmp), "#%d  %-16.16s %3d  %3d  %d.%02d\n",
						i + 1, aSafeName, blue[i].K, blue[i].D, kdi / 100, kdi % 100);
				}
				else
					str_format(aTmp, sizeof(aTmp), "#%d  -\n", i + 1);
				str_append(aDiscord, aTmp);
			}

			str_append(aDiscord, "\nTeam Red            K    D    K/D\n");
			for(int i = 0; i < 3; ++i)
			{
				if(i < (int)red.size())
				{
					const char *pName = Server()->ClientName(red[i].ClientId);
					char aSafeName[17];
					SanitizeName(pName ? pName : "?", aSafeName, sizeof(aSafeName));
					int kdi = (int)(red[i].KD * 100.0f + 0.5f);
					str_format(aTmp, sizeof(aTmp), "#%d  %-16.16s %3d  %3d  %d.%02d\n",
						i + 1, aSafeName, red[i].K, red[i].D, kdi / 100, kdi % 100);
				}
				else
					str_format(aTmp, sizeof(aTmp), "#%d  -\n", i + 1);
				str_append(aDiscord, aTmp);
			}

			str_append(aDiscord, "```");

			CDiscordWebhook::SSendOptions Opt;
			Opt.m_pWebhookUrl = pTdmUrl;
			Discord.Send(aDiscord, Opt);
		}
	}
}

void CTeamDeathmatchEvent::ForceNextStage()
{
	if(GetState() == CEventComponent::EEventState::Registration)
		CloseRegistration();
	else if(GetState() == CEventComponent::EEventState::Active)
		FinishEvent();
}

bool CTeamDeathmatchEvent::CheckEndCondition()
{
	if(m_ScoreTeam[0] >= m_TargetScore || m_ScoreTeam[1] >= m_TargetScore)
		return true;
	if(Server()->Tick() > m_ActiveEndTick)
		return true;
	return false;
}

bool CTeamDeathmatchEvent::Register(int ClientId)
{
	if(!CheckClientId(ClientId))
		return false;
	if(GetState() != CEventComponent::EEventState::Registration)
	{
		GameServer()->SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	if(!GameServer()->GetPlayer(ClientId))
		return false;
	if(IsCandidate(ClientId))
	{
		GameServer()->SendChatTarget(ClientId, "You already registered to participate.");
		return false;
	}

	// When the server allows multiple clients per IP (dummies), prevent registering
	// with more than one account from the same connection.
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

	if((int)m_Candidates.size() >= Config()->m_SvTDMMaximumCandidates)
	{
		GameServer()->SendChatTarget(ClientId, "Registration is full (max %d players).", Config()->m_SvTDMMaximumCandidates);
		return false;
	}

	m_Candidates.push_back(ClientId);
	GameServer()->SendChatTarget(ClientId, "You successfully joined %s!", GetEventName());
	return true;
}

bool CTeamDeathmatchEvent::DeRegister(int ClientId)
{
	if(GetState() != CEventComponent::EEventState::Registration)
	{
		GameServer()->SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	auto it = std::find(Candidates().begin(), Candidates().end(), ClientId);
	if(it == Candidates().end())
	{
		GameServer()->SendChatTarget(ClientId, "You aren't registered to participate.");
		return false;
	}
	m_Candidates.erase(it);
	GameServer()->SendChatTarget(ClientId, "You successfully left %s.", GetEventName());
	return true;
}

bool CTeamDeathmatchEvent::Join(int ClientId)
{
	SavePosition(ClientId);
	SaveWeapons(ClientId);
	m_Participants.push_back(ClientId);

	if(auto *pPlayer = GameServer()->GetPlayer(ClientId))
	{
		SaveAndClearCosmetics(ClientId);
		pPlayer->ClearCosmetics();

		if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
			pChar->ResetVelocity();
	}
	return true;
}

bool CTeamDeathmatchEvent::Leave(int ClientId)
{
	auto itIn = std::find(Participants().begin(), Participants().end(), ClientId);
	if(itIn == Participants().end())
		return false;
	m_Participants.erase(itIn);

	if(GetState() == CEventComponent::EEventState::Active)
	{
		int penalty = Config()->m_SvTDMLeavePenaltyBP;
		if(penalty > 0)
		{
			if(auto *pPlayer = GameServer()->GetPlayer(ClientId))
			{
				if(pPlayer->IsLoggedIn())
				{
					int bp = pPlayer->GetPlayerBlockpoints();
					pPlayer->SetPlayerBlockpoints(std::max(0, bp - penalty));
					GameServer()->Accounts()->Save(ClientId, &pPlayer->m_Account);
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "You lost %d BP for leaving TDM early.", penalty);
					GameServer()->SendChatTarget(ClientId, aBuf);
				}
			}
		}
	}

	// reset team mapping and visuals
	GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, 0);
	m_ClientTeam.erase(ClientId);
	RestoreParticipantVisuals(ClientId);
	RestoreCosmetics(ClientId);

	// restore solo/collision for this player
	if(auto itSolo = m_PrevSoloState.find(ClientId); itSolo != m_PrevSoloState.end())
	{
		if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
		{
			if(itSolo->second.solo)
				pChar->SetSolo(true);
			pChar->Core()->m_CollisionDisabled = itSolo->second.collision;
		}
		m_PrevSoloState.erase(itSolo);
	}

	// return immediately
	LoadPosition(ClientId);
	LoadWeapons(ClientId);
	CEventComponent::OnTick();

	// if one side becomes empty or overall < 2, end event
	if(GetState() == CEventComponent::EEventState::Active)
	{
		int team0 = 0, team1 = 0;
		for(int pid : m_Participants)
		{
			int side = GetSideOf(pid);
			if(side == 0)
				++team0;
			else if(side == 1)
				++team1;
		}
		if(team0 == 0 || team1 == 0 || (int)m_Participants.size() < 2)
		{
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "events",
				"TDM: ending event because a team became empty or too few participants");
			FinishEvent();
		}
	}
	return true;
}

bool CTeamDeathmatchEvent::IsCandidate(int ClientId) const
{
	return std::find(m_Candidates.begin(), m_Candidates.end(), ClientId) != m_Candidates.end();
}

bool CTeamDeathmatchEvent::IsParticipant(int ClientId) const
{
	return std::find(m_Participants.begin(), m_Participants.end(), ClientId) != m_Participants.end();
}
