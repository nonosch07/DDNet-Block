#include "tdm.h"

#include "event_helpers.h"

#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>

#include <blockworlds/bw_context.h>
#include <blockworlds/bw_util.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/discord/webhook.h>

#include <algorithm>
#include <random>

CTeamDeathmatchEvent::CTeamDeathmatchEvent(CGameContext *pGameContext) :
	CEventComponent(pGameContext), m_Rng((unsigned)std::random_device{}() ^ (unsigned)pGameContext->Server()->Tick())
{
	// preload shared event spawn positions from gamezone quads
	m_EventStartPositions = pGameContext->Bw().ZoneManager()->GetNamedQuadCenters("tdm_spawn");
}

// ===== Helpers =====

void CTeamDeathmatchEvent::AssignTeamsShuffled()
{
	m_ClientTeam.clear();
	if(m_Participants.empty())
		return;

	std::shuffle(m_Participants.begin(), m_Participants.end(), m_Rng);
	const int Split = (int)m_Participants.size() / 2;
	for(size_t i = 0; i < m_Participants.size(); ++i)
	{
		int Side = (int)i >= Split ? 1 : 0; // 0 = blue, 1 = red
		m_ClientTeam[m_Participants[i]] = Side;
	}
}

void CTeamDeathmatchEvent::ApplyParticipantVisuals(int ClientId, int Side)
{
	if(auto *p = GameServer()->Bw().GetPlayer(ClientId))
	{
		p->Bw().m_OldTeeInfos = p->TeeInfos();
		CTeeInfo TeeInfo = p->TeeInfos();
		str_copy(TeeInfo.m_aSkinName, "default", sizeof(TeeInfo.m_aSkinName));
		TeeInfo.m_UseCustomColor = true;
		if(Side == 0)
		{
			TeeInfo.m_ColorBody = 10223467; // blue
			TeeInfo.m_ColorFeet = 10223467;
		}
		else
		{
			TeeInfo.m_ColorBody = 65387; // red
			TeeInfo.m_ColorFeet = 65387;
		}
		p->SetTeeInfos(TeeInfo);
		p->Bw().m_HideInfo = true;
		p->Bw().m_HideInfoInScoreboard = true;

		// strip cosmetics
		p->Bw().ClearCosmetics();
		p->Bw().SetSkinMani(-1);
		p->Bw().SetGunDesign(-1);
		p->Bw().SetKnockout(-1);
		if(p->Bw().GetCurrentSpecial() != -1)
			p->Bw().ToggleSpecial(p->Bw().GetCurrentSpecial());
	}
}

void CTeamDeathmatchEvent::RestoreParticipantVisuals(int ClientId)
{
	if(auto *p = GameServer()->Bw().GetPlayer(ClientId))
	{
		p->SetTeeInfos(p->Bw().m_OldTeeInfos);
		p->Bw().m_HideInfo = false;
		p->Bw().m_HideInfoInScoreboard = false;
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

		auto It = m_ClientTeam.find(ClientId);
		if(It != m_ClientTeam.end())
			ApplyParticipantVisuals(ClientId, It->second);

		if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
		{
			// save transient solo/collision, then ensure normal interaction in the event
			bool WasSolo = pChar->Core()->m_Solo;
			bool WasCollisionDisabled = pChar->Core()->m_CollisionDisabled;
			m_PrevSoloState[ClientId] = {WasSolo, WasCollisionDisabled};
			if(WasSolo)
				pChar->SetSolo(false);
			if(WasCollisionDisabled)
				pChar->Bw().Core().m_CollisionDisabled = false;
			pChar->GetPlayer()->Pause(CPlayer::PAUSE_NONE, false);
			pChar->SetDeepFrozen(false);
		}
	}
}

void CTeamDeathmatchEvent::RestoreParticipants()
{
	// restore visuals first
	for(int ClientId : m_Participants)
		RestoreParticipantVisuals(ClientId);

	// restore solo/collision
	for(const auto &Kv : m_PrevSoloState)
	{
		if(auto *pChar = GameServer()->GetPlayerChar(Kv.first))
		{
			if(Kv.second.m_Solo)
				pChar->SetSolo(true);
			pChar->Bw().Core().m_CollisionDisabled = Kv.second.m_Collision;
		}
	}
	m_PrevSoloState.clear();

	// restore saved position & weapons (deferred processed right after)
	std::vector<int> Ids;
	Ids.reserve(m_pSavedPlayers.size());
	for(const auto &Kv : m_pSavedPlayers)
		Ids.push_back(Kv.first);
	for(int ClientId : Ids)
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
		auto It = m_AssignedSpawnIndex.find(ClientId);
		if(It != m_AssignedSpawnIndex.end())
		{
			int Idx = It->second;
			m_AssignedSpawnIndex.erase(It); // consume reservation
			if(Idx >= 0 && Idx < (int)m_EventStartPositions.size())
			{
				m_UsedSpawnIndices.insert(Idx);
				return m_EventStartPositions[(size_t)Idx];
			}
		}

		return RandomSpawnPos(m_EventStartPositions, m_UsedSpawnIndices);
	}
	// no tdm_spawn gamezone quads: do not teleport (fallback is saved position)
	(void)ClientId;
	return std::nullopt;
}

void CTeamDeathmatchEvent::TeleportToSpawn(int ClientId)
{
	if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
	{
		if(auto Pos = ChooseSpawnFor(ClientId))
			GameServer()->Bw().Teleport(pChar, *Pos);
		pChar->ResetVelocity();
	}
}

void CTeamDeathmatchEvent::BroadcastStatus()
{
	int Secs = ((m_ActiveEndTick - Server()->Tick()) / Server()->TickSpeed());
	if(Secs < 0)
		Secs = 0;
	char aTimeLeft[32];
	FormatTimeLeft(aTimeLeft, sizeof(aTimeLeft), Secs);

	for(int ClientId : m_Participants)
	{
		CPlayer *pPlayer = GameServer()->Bw().GetPlayer(ClientId);
		if(!pPlayer)
			continue;

		int Side = GetSideOf(ClientId);
		if(Side == 0)
		{
			pPlayer->Bw().SendBroadcastAlignedLeft("Team Blue\n"
							       "Blue %d / %d\n"
							       "Red %d / %d\n"
							       "Time left: %s",
				m_ScoreTeam[0], m_TargetScore, m_ScoreTeam[1], m_TargetScore, aTimeLeft);
		}
		else if(Side == 1)
		{
			pPlayer->Bw().SendBroadcastAlignedLeft("Team Red\n"
							       "Red %d / %d\n"
							       "Blue %d / %d\n"
							       "Time left: %s",
				m_ScoreTeam[1], m_TargetScore, m_ScoreTeam[0], m_TargetScore, aTimeLeft);
		}
		else
		{
			pPlayer->Bw().SendBroadcastAlignedLeft("%s\n"
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
		const bool IsFrozen = pChar->m_FreezeTime;
		int Since = GetFrozenSince(ClientId);
		if(IsFrozen && Since == 0)
			SetFrozenSince(ClientId, Server()->Tick());
		if(!IsFrozen && Since != 0)
			SetFrozenSince(ClientId, 0);

		if(IsFrozen)
		{
			int &LastVal = m_LastFreezeTimeValue[ClientId];
			if(LastVal == 0)
				LastVal = pChar->m_FreezeTime; // initialize

			if(pChar->m_FreezeTime < LastVal)
			{
				// countdown progressed -> not perma-frozen; restart delay
				m_PermanentFreezeSince.erase(ClientId);
			}
			else // equal or increased: not decreasing
			{
				int &St = m_PermanentFreezeSince[ClientId];
				if(St == 0)
					St = Server()->Tick();
				if(Server()->Tick() - St >= RequiredFreezeTicks)
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
			LastVal = pChar->m_FreezeTime;
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
			pChar->Bw().FreezeForce(Config()->m_SvGroundHookPenalty);
	}
}

void CTeamDeathmatchEvent::UpdatePerPlayerScores()
{
	for(int Pid : m_Participants)
	{
		if(auto *p = GameServer()->Bw().GetPlayer(Pid))
		{
			int Side = GetSideOf(Pid);
			p->Bw().m_Score = Side == 0 ? m_ScoreTeam[0] : m_ScoreTeam[1];
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
	m_UsedSpawnIndices.clear();
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
	auto It = m_ClientTeam.find(ClientId);
	return It == m_ClientTeam.end() ? -1 : It->second;
}

void CTeamDeathmatchEvent::SetFrozenSince(int ClientId, int Tick)
{
	m_FrozenSince[ClientId] = Tick;
}

int CTeamDeathmatchEvent::GetFrozenSince(int ClientId) const
{
	auto It = m_FrozenSince.find(ClientId);
	return It == m_FrozenSince.end() ? 0 : It->second;
}

void CTeamDeathmatchEvent::UpdateRespawns()
{
	if(m_RespawnAtTick.empty())
		return;

	std::vector<int> Ready;
	Ready.reserve(m_RespawnAtTick.size());
	for(auto &Kv : m_RespawnAtTick)
	{
		int Cid = Kv.first;
		int When = Kv.second;
		int TicksLeft = When - Server()->Tick();
		int SecsLeft = TicksLeft > 0 ? (TicksLeft + Server()->TickSpeed() - 1) / Server()->TickSpeed() : 0; // ceil
		int &LastShown = m_LastRespawnSeconds[Cid];
		if(SecsLeft != LastShown)
		{
			LastShown = SecsLeft;
			if(SecsLeft > 0)
			{
				CPlayer *pPlayer = GameServer()->Bw().GetPlayer(Cid);
				if(pPlayer)
					pPlayer->Bw().SendBroadcastAlignedLeft("You will respawn in %d %s", SecsLeft, SecsLeft == 1 ? "sec" : "secs");
			}
		}
		if(Server()->Tick() >= When)
			Ready.push_back(Cid);
	}

	for(int Cid : Ready)
	{
		m_RespawnAtTick.erase(Cid);
		m_LastRespawnSeconds.erase(Cid);
		// respawn the player at a chosen spawn position
		if(IsParticipant(Cid))
		{
			if(CPlayer *p = GameServer()->Bw().GetPlayer(Cid))
			{
				// reinforce event team and force-spawn at chosen spot if available
				GameServer()->m_pController->Teams().SetForceCharacterTeam(Cid, m_DDRaceTeam);
				if(auto Pos = ChooseSpawnFor(Cid))
				{
					m_SkipTeleportOnSpawn[Cid] = true; // avoid duplicate teleportation in OnCharacterSpawn
					// A forced event spawn must not re-enter the event components.
					GameServer()->Bw().m_SuppressSpawnEvent = true;
					p->ForceSpawn(*Pos);
					GameServer()->Bw().m_SuppressSpawnEvent = false;
				}
				else
				{
					m_SkipTeleportOnSpawn[Cid] = true; // still skip freeze path in OnCharacterSpawn
					p->Respawn(false);
				}
				GameServer()->Bw().SendBroadcast(" ", Cid, false); // clear countdown
			}
		}
	}
}

void CTeamDeathmatchEvent::UpdateSetSpectators()
{
	if(m_SetSpecAtTick.empty())
		return;
	std::vector<int> Done;
	Done.reserve(m_SetSpecAtTick.size());
	for(const auto &Kv : m_SetSpecAtTick)
	{
		int Cid = Kv.first;
		int When = Kv.second;
		if(Server()->Tick() < When)
			continue;
		CPlayer *p = GameServer()->Bw().GetPlayer(Cid);
		if(!p)
		{
			Done.push_back(Cid);
			continue;
		}
		if(!p->GetCharacter())
		{
			p->SetTeam(TEAM_SPECTATORS, false);
			// clear stale input so held keys don't carry over on respawn
			mem_zero(&GameServer()->m_aLastPlayerInput[Cid], sizeof(GameServer()->m_aLastPlayerInput[Cid]));
			GameServer()->m_aPlayerHasInput[Cid] = false;
			Done.push_back(Cid);
		}
	}
	for(int Cid : Done)
		m_SetSpecAtTick.erase(Cid);
}

void CTeamDeathmatchEvent::AssignUniqueStartSpawns()
{
	m_AssignedSpawnIndex.clear();
	m_UsedSpawnIndices.clear();
	if(m_EventStartPositions.empty())
		return;

	// Greedy: assign each participant a unique spawn if enough positions exist
	std::vector<int> Indices(m_EventStartPositions.size());
	for(size_t i = 0; i < Indices.size(); ++i)
		Indices[i] = (int)i;
	std::shuffle(Indices.begin(), Indices.end(), m_Rng);

	const int UniqueCount = std::min((int)m_Participants.size(), (int)Indices.size());
	for(int i = 0; i < UniqueCount; ++i)
		m_AssignedSpawnIndex[m_Participants[(size_t)i]] = Indices[(size_t)i];
}

std::optional<int> CTeamDeathmatchEvent::GetTeamIndexFor(int ClientId) const
{
	auto It = m_ClientTeam.find(ClientId);
	if(It == m_ClientTeam.end())
		return std::nullopt;
	// internal mapping is 0=blue,1=red; caller will translate to TEAM_BLUE/TEAM_RED
	return It->second;
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
			CPlayer *pPlayer = GameServer()->Bw().GetPlayer(i);
			if(!pPlayer)
				continue;

			char aTimeLeft[32];
			FormatTimeLeft(aTimeLeft, sizeof(aTimeLeft), ((m_RegistrationEndTick - Server()->Tick()) / Server()->TickSpeed()));
			pPlayer->Bw().SendBroadcastAlignedLeft("%s is about to start!\nRegister with /join\nTime left: %s\n\nParticipants: %zd",
				GetEventName(), aTimeLeft, Candidates().size());
		}
	}
	else if(GetState() == CEventComponent::EEventState::Active)
	{
		for(int Id : Participants())
		{
			// keep cosmetics disabled and apply ground hook penalty
			ApplyGroundHookPenalty(Id);
			if(auto *p = GameServer()->Bw().GetPlayer(Id))
			{
				p->Bw().m_HideInfo = true;
				p->Bw().m_HideInfoInScoreboard = true;
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
		if(Server()->GetAuthedState(SnappingClient) != AUTHED_NO)
			return;

		StrToInts(pClientInfo->m_aName, std::size(pClientInfo->m_aName), " ");
		StrToInts(pClientInfo->m_aClan, std::size(pClientInfo->m_aClan), " ");
		StrToInts(pClientInfo->m_aSkin, std::size(pClientInfo->m_aSkin), "default");
		pClientInfo->m_Country = 0;
	}
}

void CTeamDeathmatchEvent::OnCharacterSpawn(int ClientId, vec2 /*SpawnPos*/)
{
	if(GetState() != CEventComponent::EEventState::Active || !IsParticipant(ClientId))
		return;

	GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);

	if(auto It = m_SkipTeleportOnSpawn.find(ClientId); It != m_SkipTeleportOnSpawn.end() && It->second)
	{
		m_SkipTeleportOnSpawn.erase(It);
		return;
	}
	TeleportToSpawn(ClientId);

	if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
	{
		if(m_ActiveStartTick != -1 && Server()->Tick() < m_ActiveStartTick + Config()->m_SvTDMFreezeTime * Server()->TickSpeed())
		{
			pChar->Bw().FreezeForce(Config()->m_SvTDMFreezeTime);
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
	GameServer()->Bw().SendBroadcast(-1, " ", false); // clear registration broadcast for all

	if((int)m_Candidates.size() < Config()->m_SvTDMMinimumCandidates)
	{
		GameServer()->Bw().SendChatTarget(-1, "Not enough candidates joined %s", GetEventName());
		GameServer()->Bw().SendBroadcast(-1, "Not enough candidates joined %s", GetEventName());
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
	int ChosenTeam = -1;
	for(int t = 1; t < NUM_DDRACE_TEAMS; ++t)
	{
		if(Teams.GetTeamState(t) == ETeamState::EMPTY && !Teams.IsTeamEvent(t))
		{
			ChosenTeam = t;
			break;
		}
	}
	m_DDRaceTeam = ChosenTeam;
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
			pChar->Bw().FreezeForce(Config()->m_SvTDMFreezeTime);
	}

	// adaptive target score based on the larger team size
	int CountBlue = 0, CountRed = 0;
	for(const auto &Kv : m_ClientTeam)
		(Kv.second == 0 ? CountBlue : CountRed)++;
	m_TargetScore = std::max(CountBlue, CountRed) * 10;

	SetState(CEventComponent::EEventState::Active);
}

void CTeamDeathmatchEvent::OnCharacterDeath(int KillerId, int ClientId, int /*Weapon*/)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	// prevent duplicate processing in the same tick for the same victim
	{
		int Now = Server()->Tick();
		int &Last = m_LastDeathHandledTick[ClientId];
		if(Last == Now)
			return;
		Last = Now;
	}

	// update temporary player stats (does not affect account K/D)
	const bool VictimIsParticipant = IsParticipant(ClientId);
	if(VictimIsParticipant)
		m_PlayerStats[ClientId].m_Deaths++;

	int vSide = GetSideOf(ClientId);
	bool KillCredited = false;
	if(vSide != -1)
	{
		// primary: credit the actual killer if it's a valid opposing participant
		if(KillerId >= 0 && KillerId != ClientId && IsParticipant(KillerId))
		{
			int KSide = GetSideOf(KillerId);
			if(KSide != -1 && KSide != vSide)
			{
				m_PlayerStats[KillerId].m_Kills++;
				KillCredited = true;
			}
		}

		if(!KillCredited)
		{
			auto ItA = m_LastImpactByVictim.find(ClientId);
			auto ItT = m_LastImpactTick.find(ClientId);
			if(ItA != m_LastImpactByVictim.end() && ItT != m_LastImpactTick.end())
			{
				const int Attacker = ItA->second;
				const int ImpactTick = ItT->second;
				const int WindowTicks = 10 * Server()->TickSpeed(); // 10s window for last-impact attribution
				if(Attacker >= 0 && Attacker != ClientId && Server()->Tick() - ImpactTick <= WindowTicks)
				{
					if(IsParticipant(Attacker))
					{
						int aSide = GetSideOf(Attacker);
						if(aSide != -1 && aSide != vSide)
						{
							m_PlayerStats[Attacker].m_Kills++;
						}
					}
				}
			}
		}
	}

	auto ItV = m_ClientTeam.find(ClientId);
	if(ItV != m_ClientTeam.end())
	{
		const int VictimSide = ItV->second;
		m_ScoreTeam[Opposite(VictimSide)] += m_PointsPerKill;
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
	int Since = GetFrozenSince(ClientId);
	if(Since == 0)
		return false;
	const int Required = 2 * Server()->TickSpeed(); // 2 seconds
	return Server()->Tick() - Since >= Required;
}
void CTeamDeathmatchEvent::FinishEvent()
{
	if(GetState() == CEventComponent::EEventState::Ending || GetState() == CEventComponent::EEventState::Finished)
		return;

	SetState(CEventComponent::EEventState::Ending);

	// announce winner
	if(m_ScoreTeam[0] > m_ScoreTeam[1])
		GameServer()->Bw().SendBroadcast(-1, "Team Blue wins %d - %d", m_ScoreTeam[0], m_ScoreTeam[1]);
	else if(m_ScoreTeam[1] > m_ScoreTeam[0])
		GameServer()->Bw().SendBroadcast(-1, "Team Red wins %d - %d", m_ScoreTeam[1], m_ScoreTeam[0]);
	else
		GameServer()->Bw().SendBroadcast(-1, "TDM ended in a tie %d - %d", m_ScoreTeam[0], m_ScoreTeam[1]);

	struct SRanked
	{
		int m_ClientId;
		int m_Kills;
	};
	std::vector<SRanked> Blue, Red;
	for(int Pid : m_Participants)
	{
		int Side = GetSideOf(Pid);
		int Kills = 0;
		auto It = m_PlayerStats.find(Pid);
		if(It != m_PlayerStats.end())
			Kills = It->second.m_Kills;
		SRanked r{Pid, Kills};
		if(Side == 0)
			Blue.push_back(r);
		else if(Side == 1)
			Red.push_back(r);
	}
	auto Cmp = [](const SRanked &a, const SRanked &b) { return a.m_Kills > b.m_Kills; };
	std::sort(Blue.begin(), Blue.end(), Cmp);
	std::sort(Red.begin(), Red.end(), Cmp);

	int WinBp[3] = {Config()->m_SvTDMWinBP1, Config()->m_SvTDMWinBP2, Config()->m_SvTDMWinBP3};
	int LoseBp[3] = {Config()->m_SvTDMLoseBP1, Config()->m_SvTDMLoseBP2, Config()->m_SvTDMLoseBP3};
	bool BlueWin = m_ScoreTeam[0] > m_ScoreTeam[1];
	bool RedWin = m_ScoreTeam[1] > m_ScoreTeam[0];

	auto Award = [&](const std::vector<SRanked> &Team, bool IsWinner) {
		for(int i = 0; i < 3 && i < (int)Team.size(); ++i)
		{
			int Bp = IsWinner ? WinBp[i] : LoseBp[i];
			if(Bp > 0)
			{
				if(auto *pPlayer = GameServer()->Bw().GetPlayer(Team[i].m_ClientId))
				{
					if(!pPlayer->Bw().IsLoggedIn())
					{
						GameServer()->Bw().SendChatTarget(Team[i].m_ClientId, "You must be logged in to receive rewards.");
						continue;
					}
					pPlayer->Bw().SetPlayerBlockpoints(pPlayer->Bw().GetPlayerBlockpoints() + Bp);
					GameServer()->Bw().Accounts()->Save(Team[i].m_ClientId, &pPlayer->Bw().m_Account);
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "You received %d BP for your performance in TDM.", Bp);
					GameServer()->Bw().SendChatTarget(Team[i].m_ClientId, aBuf);
				}
			}
		}
	};
	if(BlueWin)
	{
		Award(Blue, true);
		Award(Red, false);
	}
	else if(RedWin)
	{
		Award(Red, true);
		Award(Blue, false);
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
	struct SRanked
	{
		int m_ClientId;
		int m_K;
		int m_D;
		float m_Kd;
		int m_Side;
	};
	std::vector<SRanked> Blue, Red;
	Blue.reserve(m_Participants.size());
	Red.reserve(m_Participants.size());
	for(int Pid : m_Participants)
	{
		const auto It = m_PlayerStats.find(Pid);
		int k = 0, d = 0;
		if(It != m_PlayerStats.end())
		{
			k = It->second.m_Kills;
			d = It->second.m_Deaths;
		}
		float Kd = d > 0 ? (float)k / (float)d : (k > 0 ? (float)k : 0.0f);
		int Side = GetSideOf(Pid);
		SRanked r{Pid, k, d, Kd, Side};
		if(Side == 0)
			Blue.push_back(r);
		else if(Side == 1)
			Red.push_back(r);
	}
	auto Cmp = [](const SRanked &a, const SRanked &b) {
		if(a.m_K != b.m_K)
			return a.m_K > b.m_K; // more kills first
		if(a.m_Kd != b.m_Kd)
			return a.m_Kd > b.m_Kd; // better KD
		return a.m_ClientId < b.m_ClientId; // stable
	};
	std::sort(Blue.begin(), Blue.end(), Cmp);
	std::sort(Red.begin(), Red.end(), Cmp);

	char aBuf[256];
	// overall summary
	str_format(aBuf, sizeof(aBuf), "TDM results: Blue %d - %d Red (Target %d)", m_ScoreTeam[0], m_ScoreTeam[1], m_TargetScore);
	GameServer()->Bw().SendChatTarget(-1, aBuf);

	// show the top3 best players of each team
	auto AnnounceTop = [&](const char *pTeamName, const std::vector<SRanked> &v) {
		for(int i = 0; i < 3; ++i)
		{
			if(i < (int)v.size())
			{
				const auto &r = v[i];
				const char *pName = GameServer()->Server()->ClientName(r.m_ClientId);
				int KdInt = (int)(r.m_Kd * 100.0f + 0.5f);
				str_format(aBuf, sizeof(aBuf), "%s #%d: %s - K %d / D %d (K/D %d.%02d)",
					pTeamName, i + 1, pName, r.m_K, r.m_D, KdInt / 100, KdInt % 100);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%s #%d: —", pTeamName, i + 1);
			}
			GameServer()->Bw().SendChatTarget(-1, aBuf);
		}
	};

	AnnounceTop("Blue", Blue);
	AnnounceTop("Red", Red);

	// discord webhook: post TDM result with teams, scores and top-3 per side
	{
		CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Bw().Http());
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
				int Out = 0;
				for(int i = 0; pSrc[i] && Out < DstSize - 1; ++i)
				{
					unsigned char c = (unsigned char)pSrc[i];
					if(c >= 0x20 && c <= 0x7E) // printable ASCII only
						pDst[Out++] = pSrc[i];
				}
				pDst[Out] = '\0';
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
				if(i < (int)Blue.size())
				{
					const char *pName = Server()->ClientName(Blue[i].m_ClientId);
					char aSafeName[17];
					SanitizeName(pName ? pName : "?", aSafeName, sizeof(aSafeName));
					int Kdi = (int)(Blue[i].m_Kd * 100.0f + 0.5f);
					str_format(aTmp, sizeof(aTmp), "#%d  %-16.16s %3d  %3d  %d.%02d\n",
						i + 1, aSafeName, Blue[i].m_K, Blue[i].m_D, Kdi / 100, Kdi % 100);
				}
				else
					str_format(aTmp, sizeof(aTmp), "#%d  -\n", i + 1);
				str_append(aDiscord, aTmp);
			}

			str_append(aDiscord, "\nTeam Red            K    D    K/D\n");
			for(int i = 0; i < 3; ++i)
			{
				if(i < (int)Red.size())
				{
					const char *pName = Server()->ClientName(Red[i].m_ClientId);
					char aSafeName[17];
					SanitizeName(pName ? pName : "?", aSafeName, sizeof(aSafeName));
					int Kdi = (int)(Red[i].m_Kd * 100.0f + 0.5f);
					str_format(aTmp, sizeof(aTmp), "#%d  %-16.16s %3d  %3d  %d.%02d\n",
						i + 1, aSafeName, Red[i].m_K, Red[i].m_D, Kdi / 100, Kdi % 100);
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
		GameServer()->Bw().SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	if(!GameServer()->Bw().GetPlayer(ClientId))
		return false;
	if(IsCandidate(ClientId))
	{
		GameServer()->Bw().SendChatTarget(ClientId, "You already registered to participate.");
		return false;
	}

	// When the server allows multiple clients per IP (dummies), prevent registering
	// with more than one account from the same connection.
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

	if((int)m_Candidates.size() >= Config()->m_SvTDMMaximumCandidates)
	{
		GameServer()->Bw().SendChatTarget(ClientId, "Registration is full (max %d players).", Config()->m_SvTDMMaximumCandidates);
		return false;
	}

	m_Candidates.push_back(ClientId);
	GameServer()->Bw().SendChatTarget(ClientId, "You successfully joined %s!", GetEventName());
	return true;
}

bool CTeamDeathmatchEvent::DeRegister(int ClientId)
{
	if(GetState() != CEventComponent::EEventState::Registration)
	{
		GameServer()->Bw().SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	auto It = std::find(Candidates().begin(), Candidates().end(), ClientId);
	if(It == Candidates().end())
	{
		GameServer()->Bw().SendChatTarget(ClientId, "You aren't registered to participate.");
		return false;
	}
	m_Candidates.erase(It);
	GameServer()->Bw().SendChatTarget(ClientId, "You successfully left %s.", GetEventName());
	return true;
}

bool CTeamDeathmatchEvent::Join(int ClientId)
{
	SavePosition(ClientId);
	SaveWeapons(ClientId);
	m_Participants.push_back(ClientId);

	if(auto *pPlayer = GameServer()->Bw().GetPlayer(ClientId))
	{
		SaveAndClearCosmetics(ClientId);
		pPlayer->Bw().ClearCosmetics();

		if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
			pChar->ResetVelocity();
	}
	return true;
}

bool CTeamDeathmatchEvent::Leave(int ClientId)
{
	auto ItIn = std::find(Participants().begin(), Participants().end(), ClientId);
	if(ItIn == Participants().end())
		return false;
	m_Participants.erase(ItIn);

	if(GetState() == CEventComponent::EEventState::Active)
	{
		int Penalty = Config()->m_SvTDMLeavePenaltyBP;
		if(Penalty > 0)
		{
			if(auto *pPlayer = GameServer()->Bw().GetPlayer(ClientId))
			{
				if(pPlayer->Bw().IsLoggedIn())
				{
					int Bp = pPlayer->Bw().GetPlayerBlockpoints();
					pPlayer->Bw().SetPlayerBlockpoints(std::max(0, Bp - Penalty));
					GameServer()->Bw().Accounts()->Save(ClientId, &pPlayer->Bw().m_Account);
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "You lost %d BP for leaving TDM early.", Penalty);
					GameServer()->Bw().SendChatTarget(ClientId, aBuf);
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
	if(auto ItSolo = m_PrevSoloState.find(ClientId); ItSolo != m_PrevSoloState.end())
	{
		if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
		{
			if(ItSolo->second.m_Solo)
				pChar->SetSolo(true);
			pChar->Bw().Core().m_CollisionDisabled = ItSolo->second.m_Collision;
		}
		m_PrevSoloState.erase(ItSolo);
	}

	// return immediately
	LoadPosition(ClientId);
	LoadWeapons(ClientId);
	CEventComponent::OnTick();

	// if one side becomes empty or overall < 2, end event
	if(GetState() == CEventComponent::EEventState::Active)
	{
		int Team0 = 0, Team1 = 0;
		for(int Pid : m_Participants)
		{
			int Side = GetSideOf(Pid);
			if(Side == 0)
				++Team0;
			else if(Side == 1)
				++Team1;
		}
		if(Team0 == 0 || Team1 == 0 || (int)m_Participants.size() < 2)
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
