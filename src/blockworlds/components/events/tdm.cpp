#include "tdm.h"

#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>

#include <algorithm>
#include <array>
#include <blockworlds/components/core/component_registry.h>
#include <random>

CTeamDeathmatchEvent::CTeamDeathmatchEvent(CGameContext *pGameContext) :
	CEventComponent(pGameContext),
	m_RegistrationEndTick(-1),
	m_ActiveStartTick(-1),
	m_ActiveEndTick(-1),
	m_DDRaceTeam(-1),
	m_ScoreTeam1(0),
	m_ScoreTeam2(0),
	m_PointsPerKill(1),
	m_TargetScore(80),
	m_Rng((unsigned)std::random_device{}() ^ (unsigned)pGameContext->Server()->Tick())
{

	CGameContext::GetTilePositions(TILE_BW_EVENT_START_POS, pGameContext, m_EventStartPositions);
}

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

		if(Server()->Tick() % Config()->m_SvTDMBroadcastRate == 0)
		{
			static constexpr const char *s_padding = "                                                                                     "
								 "                                                                                     "
								 "                                                                                     ";

			GameServer()->SendBroadcast(-1, "%s is about to start!\nRegister with /join\nTime left: %d seconds\n\nCandidates: %zd\n%s",
				GetEventName(), (int)((m_RegistrationEndTick - Server()->Tick()) / Server()->TickSpeed()), Candidates().size(), s_padding);
		}
	}
	else if(GetState() == CEventComponent::EEventState::Active)
	{
		for(int participant : Participants())
		{
			if(PlayerHookedGroundFor(participant) > Config()->m_SvGroundHookPenaltyDelay)
			{
				CCharacter *pChar = GameServer()->GetPlayerChar(participant);
				if(pChar)
					pChar->FreezeForce(Config()->m_SvGroundHookPenalty);
			}

			CPlayer *pPlayer = GameServer()->GetPlayer(participant);
			if(pPlayer)
			{
				pPlayer->m_HideInfo = true;
				pPlayer->m_HideInfoInScoreboard = true;
				auto it = m_ClientTeam.find(participant);
				if(it != m_ClientTeam.end())
				{
					int side = it->second;
					str_copy(pPlayer->m_TeeInfos.m_aSkinName, "default", sizeof(pPlayer->m_TeeInfos.m_aSkinName));
					pPlayer->m_TeeInfos.m_UseCustomColor = 1;
					if(side == 0)
					{
						pPlayer->m_TeeInfos.m_ColorBody = 10223467;
						pPlayer->m_TeeInfos.m_ColorFeet = 10223467;
					}
					else
					{
						pPlayer->m_TeeInfos.m_ColorBody = 65387;
						pPlayer->m_TeeInfos.m_ColorFeet = 65387;
					}
				}
				// remove active cosmetics if any slipped through
				pPlayer->ClearCosmetics();
				pPlayer->SetSkinMani(-1);
				pPlayer->SetGunDesign(-1);
				pPlayer->SetKnockout(-1);
			}
		}

		if(Server()->Tick() % Config()->m_SvTDMBroadcastRate == 0)
		{
			int timeLeftSeconds = (int)((m_ActiveEndTick - Server()->Tick()) / Server()->TickSpeed());
			if(timeLeftSeconds < 0)
				timeLeftSeconds = 0;
			int minutes = timeLeftSeconds / 60;
			int seconds = timeLeftSeconds % 60;

			static constexpr const char *s_padding = "                                                                                     "
								 "                                                                                     "
								 "                                                                                     ";

			for(const auto &ClientId : Participants())
			{
				auto it = m_ClientTeam.find(ClientId);
				if(it == m_ClientTeam.end())
				{
					// fallback: send general info
					GameServer()->SendBroadcast(ClientId, "%s\nBlue %d / %d\nRed %d / %d\nTime left: %d:%02d\n%s",
						GetEventName(), m_ScoreTeam1, m_TargetScore, m_ScoreTeam2, m_TargetScore, minutes, seconds, s_padding);
					continue;
				}

				int side = it->second;
				if(side == 0)
				{
					GameServer()->SendBroadcast(ClientId, "Team Blue\nBlue %d / %d\nRed %d / %d\nTime left: %d:%02d\n%s",
						m_ScoreTeam1, m_TargetScore, m_ScoreTeam2, m_TargetScore, minutes, seconds, s_padding);
				}
				else
				{
					GameServer()->SendBroadcast(ClientId, "Team Red\nRed %d / %d\nBlue %d / %d\nTime left: %d:%02d\n%s",
						m_ScoreTeam2, m_TargetScore, m_ScoreTeam1, m_TargetScore, minutes, seconds, s_padding);
				}
			}
		}

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
		bool snappingAuthed = Server()->ClientAuthed(SnappingClient);

		if(snappingAuthed)
			return;

		StrToInts(&pClientInfo->m_Name0, 4, " ");
		StrToInts(&pClientInfo->m_Clan0, 3, " ");
		StrToInts(&pClientInfo->m_Skin0, 6, "default");

		pClientInfo->m_Country = 0;
		return;
	}
}

void CTeamDeathmatchEvent::OnCharacterSpawn(int ClientId, vec2 SpawnPos)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(IsParticipant(ClientId))
	{
		auto it = m_ClientTeam.find(ClientId);
		if(it == m_ClientTeam.end())
			return;
		int side = it->second;
		auto *pChar = GameServer()->GetPlayerChar(ClientId);
		if(!pChar)
			return;
		GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);
		// use general event start positions for spawns (shared across teams)
		if(!m_EventStartPositions.empty())
		{
			std::uniform_int_distribution<int> dist(0, (int)m_EventStartPositions.size() - 1);
			int idx = dist(m_Rng);
			GameServer()->Teleport(pChar, m_EventStartPositions[idx]);
		}
		else
		{
			auto &spawns = m_SpawnPositionsTeam[side];
			if(spawns.empty())
				return;
			std::uniform_int_distribution<int> dist(0, (int)spawns.size() - 1);
			int idx = dist(m_Rng);
			GameServer()->Teleport(pChar, spawns[idx]);
		}
		pChar->ResetVelocity();
		if(m_ActiveStartTick != -1 && Server()->Tick() < m_ActiveStartTick + Config()->m_SvTDMFreezeTime * Server()->TickSpeed())
		{
			pChar->FreezeForce(Config()->m_SvTDMFreezeTime);
		}

	}
}

void CTeamDeathmatchEvent::OnPlayerDropping(int ClientId)
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

	if((int)m_Candidates.size() < Config()->m_SvTDMMinimumCandidates)
	{
		GameServer()->SendChatTarget(-1, "Not enough candidates joined %s", GetEventName());
		GameServer()->SendBroadcast(-1, "Not enough candidates joined %s", GetEventName());
		SetState(CEventComponent::EEventState::Finished);
		return;
	}

	// First-come first-serve, limit to MAX_PLAYERS
	if((int)m_Candidates.size() > Config()->m_SvTDMMaximumCandidates)
		m_Candidates.resize(Config()->m_SvTDMMaximumCandidates);

	m_Participants = m_Candidates;
	m_Candidates.clear();
	StartEvent();
}

void CTeamDeathmatchEvent::StartEvent()
{
	// create a ddnet team (like LMB) and assign participants evenly
	auto &Teams = GameServer()->m_pController->Teams();
	m_DDRaceTeam = Teams.GetFirstEmptyTeam();
	if(m_DDRaceTeam == -1)
	{
		EmergencyShutdown("No free team was found");
		return;
	}
	// mark this team as an event team so team-wide kills are suppressed
	Teams.SetTeamEvent(m_DDRaceTeam, true);
	// lock the event team so players don't leave on death
	Teams.SetTeamLock(m_DDRaceTeam, true);
	Teams.SetTeamInvitesOpen(m_DDRaceTeam, false);

	for(const auto &ClientId : m_Participants)
	{
		SaveWeapons(ClientId);
		SavePosition(ClientId);
	}

	m_ClientTeam.clear();
	m_SpawnPositionsTeam[0].clear();
	m_SpawnPositionsTeam[1].clear();
	m_SpawnQuadsTeam[0].clear();
	m_SpawnQuadsTeam[1].clear();
	m_SpawnOffsetTeam[0] = 0;
	m_SpawnOffsetTeam[1] = 0;

	// shuffle participants before assigning teams to make teams random
	{
		std::shuffle(m_Participants.begin(), m_Participants.end(), m_Rng);
	}

	// split participants into two internal sides (0 = blue, 1 = red)
	int i = 0;
	int switchAt = (int)m_Participants.size() / 2;
	for(const auto &ClientId : m_Participants)
	{
		int side = (i >= switchAt) ? 1 : 0;
		m_ClientTeam[ClientId] = side;
		Teams.SetForceCharacterTeam(ClientId, m_DDRaceTeam);
		CPlayer *p = GameServer()->GetPlayer(ClientId);
		if(p)
		{
			p->m_OldTeeInfos = p->m_TeeInfos;
			str_copy(p->m_TeeInfos.m_aSkinName, "default", sizeof(p->m_TeeInfos.m_aSkinName));
			p->m_TeeInfos.m_UseCustomColor = 1;
			if(side == 0)
			{
				// blue
				p->m_TeeInfos.m_ColorBody = 10223467;
				p->m_TeeInfos.m_ColorFeet = 10223467;
			}
			else
			{
				// red
				p->m_TeeInfos.m_ColorBody = 65387;
				p->m_TeeInfos.m_ColorFeet = 65387;
			}
			p->m_HideInfo = true;
			p->m_HideInfoInScoreboard = true;
		}
		i++;
	}

	// If event start positions are provided on the map, use them and skip per-team quad loading.
	if(m_EventStartPositions.empty())
	{
		auto blueQuads = GameServer()->ZoneManager()->GetNamedQuads("tdm_blue");
		auto redQuads = GameServer()->ZoneManager()->GetNamedQuads("tdm_red");
		if(!blueQuads.empty() && !redQuads.empty())
		{
			m_SpawnQuadsTeam[0] = blueQuads;
			m_SpawnQuadsTeam[1] = redQuads;
		}
		else
		{
			// fallback to centers for older maps
			auto blueCenters = GameServer()->ZoneManager()->GetNamedQuadCenters("tdm_blue");
			auto redCenters = GameServer()->ZoneManager()->GetNamedQuadCenters("tdm_red");
			if(!blueCenters.empty() && !redCenters.empty())
			{
				m_SpawnPositionsTeam[0] = blueCenters;
				m_SpawnPositionsTeam[1] = redCenters;
			}
			else
			{
				// No per-team spawn quads/centers found. We'll not emergency-shutdown here —
				// m_EventStartPositions is empty as well, so StartEvent will attempt to
				// continue and per-player spawn logic will skip teleport if no positions
				// are available. Log a warning so map authors can fix it.
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "events", "TDM: no tdm_blue/tdm_red quads or centers found; falling back to saved positions");
			}
		}
	}

	m_ActiveStartTick = Server()->Tick();
	m_ActiveEndTick = Server()->Tick() + Config()->m_SvTDMActiveTime * Server()->TickSpeed();

	int playersTeam1 = 0, playersTeam2 = 0;
	for(const auto &p : m_ClientTeam)
	{
		if(p.second == 0)
			playersTeam1++;
		else if(p.second == 1)
			playersTeam2++;
	}
	int maxPlayers = std::max(playersTeam1, playersTeam2);
	m_TargetScore = maxPlayers * 10;

	SetState(CEventComponent::EEventState::Active);

	for(const auto &ClientId : m_Participants)
	{
		auto it = m_ClientTeam.find(ClientId);
		if(it == m_ClientTeam.end())
			continue;
		int side = it->second;
		CCharacter *pChar = GameServer()->GetPlayerChar(ClientId);
		if(!pChar)
			continue;

		// save previous solo and collision state, set not-solo and enable collision for event
		{
			bool wasSolo = pChar->Core()->m_Solo;
			bool wasCollisionDisabled = pChar->Core()->m_CollisionDisabled;
			m_PrevSoloState[ClientId] = {wasSolo, wasCollisionDisabled};
			if(wasSolo)
				pChar->SetSolo(false);
			if(wasCollisionDisabled)
				pChar->Core()->m_CollisionDisabled = false;
		}
		// spawn players at general event start positions (shared across teams)
		if(m_EventStartPositions.empty())
		{
			auto &spawns = m_SpawnPositionsTeam[side];
			if(spawns.empty())
				continue;
			std::uniform_int_distribution<int> dist(0, (int)spawns.size() - 1);
			int idx = dist(m_Rng);
			GameServer()->Teleport(pChar, spawns[idx]);
		}
		else
		{
			std::uniform_int_distribution<int> dist(0, (int)m_EventStartPositions.size() - 1);
			int idx = dist(m_Rng);
			GameServer()->Teleport(pChar, m_EventStartPositions[idx]);
		}
		// freeze player for initial TDM freeze time
		pChar->FreezeForce(Config()->m_SvTDMFreezeTime);
	}
}

void CTeamDeathmatchEvent::OnCharacterDeath(int KillerId, int ClientId, int Weapon)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	// award points: each kill awards m_PointsPerKill to the killer's team (m_PointsPerKill is set to 1)
	int killerTeam = -1;
	if(KillerId >= 0)
	{
		auto itK = m_ClientTeam.find(KillerId);
		if(itK != m_ClientTeam.end())
			killerTeam = itK->second;
	}

	if(killerTeam == -1)
		return;

	if(killerTeam == 0)
		m_ScoreTeam1 += m_PointsPerKill;
	else if(killerTeam == 1)
		m_ScoreTeam2 += m_PointsPerKill;

	// re-apply forced team for killer and victim to prevent leaving on death
	auto &Teams = GameServer()->m_pController->Teams();
	if(IsParticipant(ClientId))
		Teams.SetForceCharacterTeam(ClientId, m_DDRaceTeam);
	if(KillerId >= 0 && IsParticipant(KillerId))
		Teams.SetForceCharacterTeam(KillerId, m_DDRaceTeam);

	// update participant scores shown to players
	for(const auto &pid : m_Participants)
	{
		CPlayer *p = GameServer()->GetPlayer(pid);
		if(!p)
			continue;
		if(m_ClientTeam[pid] == 0)
			p->m_Score = m_ScoreTeam1;
		else
			p->m_Score = m_ScoreTeam2;
	}

	// check immediate end
	if(CheckEndCondition())
		FinishEvent();
}

void CTeamDeathmatchEvent::FinishEvent()
{
	SetState(CEventComponent::EEventState::Ending);

	// announce result
	if(m_ScoreTeam1 > m_ScoreTeam2)
	{
		GameServer()->SendBroadcast(-1, "Team Blue wins %d - %d", m_ScoreTeam1, m_ScoreTeam2);
		// reward blue team players
		int BlockpointsReward = 25;
		for(const auto &ClientId : m_Participants)
		{
			auto it = m_ClientTeam.find(ClientId);
			if(it == m_ClientTeam.end() || it->second != 0)
				continue;
			CPlayer *p = GameServer()->GetPlayer(ClientId);
			if(!p)
				continue;
			p->SetPlayerExperience(p->GetPlayerExperience() + 5);
			p->SetPlayerBlockpoints(p->GetPlayerBlockpoints() + BlockpointsReward);
			p->AddExpMultiplier(Config()->m_SvLMBWinnerExpMultiplier, Config()->m_SvLMBWinnerExpMultiplierDuration);
			GameServer()->SendChatTarget(ClientId, "You've received 5 experience and %d blockpoints for winning!", BlockpointsReward);
		}
	}
	else if(m_ScoreTeam2 > m_ScoreTeam1)
	{
		GameServer()->SendBroadcast(-1, "Team Red wins %d - %d", m_ScoreTeam2, m_ScoreTeam1);
		// reward red team players
		int BlockpointsReward = 100;
		for(const auto &ClientId : m_Participants)
		{
			auto it = m_ClientTeam.find(ClientId);
			if(it == m_ClientTeam.end() || it->second != 1)
				continue;
			CPlayer *p = GameServer()->GetPlayer(ClientId);
			if(!p)
				continue;
			p->SetPlayerLevel(p->GetPlayerLevel() + 1);
			p->SetPlayerBlockpoints(p->GetPlayerBlockpoints() + BlockpointsReward);
			p->AddExpMultiplier(Config()->m_SvLMBWinnerExpMultiplier, Config()->m_SvLMBWinnerExpMultiplierDuration);
			GameServer()->SendChatTarget(ClientId, "You've received 1 level and %d blockpoints for winning!", BlockpointsReward);
		}
	}
	else
	{
		GameServer()->SendBroadcast(-1, "TDM ended in a tie %d - %d", m_ScoreTeam1, m_ScoreTeam2);
	}

	// return participants to their saved positions
	// restore tee infos for participants first
	for(const auto &ClientId : m_Participants)
	{
		CPlayer *p = GameServer()->GetPlayer(ClientId);
		if(p)
		{
			p->m_TeeInfos = p->m_OldTeeInfos;
			p->m_HideInfo = false;
			p->m_HideInfoInScoreboard = false;
		}
	}

	// restore solo and collision state for participants
	for(const auto &soloEntry : m_PrevSoloState)
	{
		if(auto *pChar = GameServer()->GetPlayerChar(soloEntry.first))
		{
			if(soloEntry.second.solo)
				pChar->SetSolo(true);
			pChar->Core()->m_CollisionDisabled = soloEntry.second.collision;
		}
	}
	m_PrevSoloState.clear();

	std::vector<int> SavedClientIds;
	SavedClientIds.reserve(m_pSavedPlayers.size());
	for(const auto &kv : m_pSavedPlayers)
		SavedClientIds.push_back(kv.first);

	for(const auto ClientId : SavedClientIds)
	{
		LoadPosition(ClientId);
		LoadWeapons(ClientId);
	}

	// process deferred loads immediately so players (winners and everyone else)
	// are returned to their saved position/weapon state in the same tick.
	CEventComponent::OnTick();

	m_Participants.clear();
	if(m_DDRaceTeam != -1)
	{
		// unlock the team and reset round state
		GameServer()->m_pController->Teams().SetTeamEvent(m_DDRaceTeam, false);
		GameServer()->m_pController->Teams().SetTeamLock(m_DDRaceTeam, false);
		GameServer()->m_pController->Teams().ResetRoundState(m_DDRaceTeam);
	}

	SetState(CEventComponent::EEventState::Finished);
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
	// target score condition
	if(m_ScoreTeam1 >= m_TargetScore || m_ScoreTeam2 >= m_TargetScore)
		return true;

	// time limit
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
	// only logged-in players can join events
	CPlayer *pPlayer = GameServer()->GetPlayer(ClientId);
	if(!pPlayer || !pPlayer->IsLoggedIn())
	{
		GameServer()->SendChatTarget(ClientId, "You must be logged in to join this event.");
		return false;
	}
	if(IsCandidate(ClientId))
	{
		GameServer()->SendChatTarget(ClientId, "You already registered to participate.");
		return false;
	}

	if((int)m_Candidates.size() >= MAX_PLAYERS)
	{
		GameServer()->SendChatTarget(ClientId, "Registration is full (max %d players).", MAX_PLAYERS);
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
	auto ClientIdIt = std::find(Candidates().begin(), Candidates().end(), ClientId);
	if(ClientIdIt == Candidates().end())
	{
		GameServer()->SendChatTarget(ClientId, "You aren't registered to participate.");
		return false;
	}

	m_Candidates.erase(ClientIdIt);
	GameServer()->SendChatTarget(ClientId, "You successfully left %s.", GetEventName());
	return true;
}

bool CTeamDeathmatchEvent::Join(int ClientId)
{
	SavePosition(ClientId);
	m_Participants.push_back(ClientId);

	if(auto pPlayer = GameServer()->GetPlayer(ClientId))
	{
		// remove all cosmetics on join and rely on cosmetics module to block reactivation during event
		pPlayer->ClearCosmetics();
		pPlayer->SetSkinMani(-1);
		pPlayer->SetGunDesign(-1);
		pPlayer->SetKnockout(-1);
		if(pPlayer->GetCurrentSpecial() != -1)
			pPlayer->ToggleSpecial(pPlayer->GetCurrentSpecial());
	}

	return true;
}

bool CTeamDeathmatchEvent::Leave(int ClientId)
{
	auto ClientIdIt = std::find(Participants().begin(), Participants().end(), ClientId);
	if(ClientIdIt == Participants().end())
		return false;
	m_Participants.erase(ClientIdIt);
	LoadPosition(ClientId);
	// restore player's team to default
	GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, 0);
	// remove client team mapping if present
	auto itct = m_ClientTeam.find(ClientId);
	if(itct != m_ClientTeam.end())
		m_ClientTeam.erase(itct);
	// restore tee infos
	CPlayer *p = GameServer()->GetPlayer(ClientId);
	if(p)
	{
		p->m_TeeInfos = p->m_OldTeeInfos;
		p->m_HideInfo = false;
		p->m_HideInfoInScoreboard = false;
	}

	// restore solo/collision state for this player if present
	{
		auto it = m_PrevSoloState.find(ClientId);
		if(it != m_PrevSoloState.end())
		{
			if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
			{
				if(it->second.solo)
					pChar->SetSolo(true);
				pChar->Core()->m_CollisionDisabled = it->second.collision;
			}
			m_PrevSoloState.erase(it);
		}
	}
	// communicate ragequit flavor text for clarity
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "[TDM] - %s left the event.", Server()->ClientName(ClientId));
	GameServer()->SendChatTarget(-1, aBuf);

	// if event is active and one team has become empty, finish the event
	if(GetState() == CEventComponent::EEventState::Active)
	{
		int team0 = 0, team1 = 0;
		for(const auto &pid : m_Participants)
		{
			auto it = m_ClientTeam.find(pid);
			if(it == m_ClientTeam.end())
				continue;
			if(it->second == 0)
				++team0;
			else if(it->second == 1)
				++team1;
		}
		// if either team has no players left or total participants less than 2, end the event
		if(team0 == 0 || team1 == 0 || (int)m_Participants.size() < 2)
		{
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "events", "TDM: ending event because a team became empty or too few participants");
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
