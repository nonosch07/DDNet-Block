#include "oneonone_manager.h"

#include <engine/server.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/player.h>
#include <game/server/save.h>

#include <block/base.h>
#include <block/components/core/component_registry.h>
#include <block/components/events.h>
#include <block/context.h>
#include <block/votes/votemanager.h>

#include <algorithm>

COneOnOneManager::COneOnOneManager(CGameContext *pGameServer) :
	CComponent(pGameServer)
{
}

std::shared_ptr<COneOnOneEvent> COneOnOneManager::CreateMatch(int Player1ID, int Player2ID, int Wager)
{
	if(!GameServer()->Block().GetPlayer(Player1ID) || !GameServer()->Block().GetPlayer(Player2ID))
	{
		dbg_msg("oneonone", "CreateMatch failed: one or both players not present (p1=%d p2=%d)", Player1ID, Player2ID);
		return nullptr;
	}

	if(GetMatchForPlayer(Player1ID) || GetMatchForPlayer(Player2ID))
	{
		dbg_msg("oneonone", "CreateMatch failed: one or both players already in an active match (p1=%d p2=%d)", Player1ID, Player2ID);
		return nullptr;
	}

	auto Match = std::make_shared<COneOnOneEvent>(GameServer());
	// Insert into manager before initialization so the match is discoverable during Initialize/StartEvent
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Matches.push_back(Match);
	}

	// Initialize will call StartEvent() immediately. If initialization fails (e.g., no free team), remove match and return null
	bool Ok = Match->Initialize(Player1ID, Player2ID, Wager);
	if(!Ok)
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Matches.erase(std::remove_if(m_Matches.begin(), m_Matches.end(), [&](const std::shared_ptr<COneOnOneEvent> &m) { return m == Match; }), m_Matches.end());
		dbg_msg("oneonone", "CreateMatch failed: Initialize returned false for players %d vs %d", Player1ID, Player2ID);
		return nullptr;
	}

	LeaveEventRegistration(Player1ID);
	LeaveEventRegistration(Player2ID);

	return Match;
}

std::shared_ptr<COneOnOneEvent> COneOnOneManager::CreateMatchWithConfig(int Player1ID, int Player2ID, int Wager)
{
	if(!GameServer()->Block().GetPlayer(Player1ID) || !GameServer()->Block().GetPlayer(Player2ID))
	{
		dbg_msg("oneonone", "CreateMatchWithConfig failed: one or both players not present (p1=%d p2=%d)", Player1ID, Player2ID);
		return nullptr;
	}

	if(GetMatchForPlayer(Player1ID) || GetMatchForPlayer(Player2ID))
	{
		dbg_msg("oneonone", "CreateMatchWithConfig failed: one or both players already in an active match (p1=%d p2=%d)", Player1ID, Player2ID);
		return nullptr;
	}

	auto Match = std::make_shared<COneOnOneEvent>(GameServer());
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Matches.push_back(Match);
	}

	bool Ok = Match->InitializeConfigPhase(Player1ID, Player2ID, Wager);
	if(!Ok)
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Matches.erase(std::remove_if(m_Matches.begin(), m_Matches.end(), [&](const std::shared_ptr<COneOnOneEvent> &m) { return m == Match; }), m_Matches.end());
		dbg_msg("oneonone", "CreateMatchWithConfig failed: InitializeConfigPhase returned false for players %d vs %d", Player1ID, Player2ID);
		return nullptr;
	}

	LeaveEventRegistration(Player1ID);
	LeaveEventRegistration(Player2ID);

	return Match;
}

void COneOnOneManager::LeaveEventRegistration(int ClientId)
{
	auto Events = g_ComponentRegistry.Get<CEvents>();
	if(!Events || !Events->DropRegistration(ClientId))
		return;

	GameServer()->Block().SendChatTarget(ClientId, "You were taken out of the event queue because your 1on1 is starting.");
}

std::shared_ptr<COneOnOneEvent> COneOnOneManager::GetMatchForPlayer(int ClientId) const
{
	std::lock_guard<std::mutex> g(m_Mutex);
	for(const auto &m : m_Matches)
	{
		if(!m)
			continue;
		auto Parts = m->Participants();
		if(std::find(Parts.begin(), Parts.end(), ClientId) != Parts.end())
			return m;
	}
	return nullptr;
}

void COneOnOneManager::OnTick()
{
	// Copy matches under lock, then call OnTick() outside the lock to avoid
	// holding the mutex while running match logic (which may re-enter manager
	// code). Afterwards prune finished matches while holding the lock.
	std::vector<std::shared_ptr<COneOnOneEvent>> Snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		Snapshot = m_Matches;
	}

	for(const auto &m : Snapshot)
	{
		if(m && m->GetState() != COneOnOneEvent::EEventState::Finished)
			m->OnTick();
	}

	// remove finished matches
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Matches.erase(std::remove_if(m_Matches.begin(), m_Matches.end(), [](const std::shared_ptr<COneOnOneEvent> &m) {
			return !m || m->GetState() == COneOnOneEvent::EEventState::Finished;
		}),
			m_Matches.end());
	}
}

void COneOnOneManager::OnSnap(int SnappingClient)
{
	std::vector<std::shared_ptr<COneOnOneEvent>> Snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		Snapshot = m_Matches;
	}

	for(const auto &Match : Snapshot)
	{
		if(!Match || Match->GetState() == COneOnOneEvent::EEventState::Finished ||
			Match->GetState() == COneOnOneEvent::EEventState::Created)
			continue;

		std::lock_guard<std::mutex> MatchLock(Match->m_Mutex);
		for(const auto &[Cid, pSavedTee] : Match->m_pSavedPlayers)
		{
			if(Cid < 0 || !pSavedTee)
				continue;

			int SnapId = Cid;
			if(!Server()->Translate(SnapId, SnappingClient))
				continue;

			vec2 SavedPos = pSavedTee->GetPos();

			CNetObj_SpecChar SpecChar = {};
			SpecChar.m_X = (int)SavedPos.x;
			SpecChar.m_Y = (int)SavedPos.y;
			Server()->IServer::SnapNewItem(SnapId, SpecChar);
		}
	}
}

void COneOnOneManager::OnPlayerDropping(int ClientId)
{
	std::vector<std::shared_ptr<COneOnOneEvent>> Snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		Snapshot = m_Matches;
	}

	for(const auto &m : Snapshot)
	{
		if(m && (m->GetState() == COneOnOneEvent::EEventState::Active || m->GetState() == COneOnOneEvent::EEventState::Preparation))
		{
			auto Parts = m->Participants();
			if(std::find(Parts.begin(), Parts.end(), ClientId) != Parts.end())
			{
				if(m->GetState() == COneOnOneEvent::EEventState::Preparation)
				{
					// player dropped during config phase - abort with full cleanup
					int OtherCid = (ClientId == m->m_Player1ID) ? m->m_Player2ID : m->m_Player1ID;
					GameServer()->Block().SendChatTarget(OtherCid, "[1on1] Opponent disconnected during warmup. Match cancelled.");

					// clear duel config vote pages

					for(int Cid : {m->m_Player1ID, m->m_Player2ID})
					{
						g_VoteManager.NavigateToRoot(Cid);
						GameServer()->Block().ClearVotes(Cid);
					}

					m->AbortAndRefund(nullptr);
				}
				else
				{
					m->OnPlayerDropping(ClientId);
				}
			}
		}
	}
}

void COneOnOneManager::OnCharacterSpawn(int ClientId, vec2 SpawnPos)
{
	std::vector<std::shared_ptr<COneOnOneEvent>> Snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		Snapshot = m_Matches;
	}

	for(const auto &m : Snapshot)
	{
		if(m && (m->GetState() == COneOnOneEvent::EEventState::Active || m->GetState() == COneOnOneEvent::EEventState::Preparation))
		{
			auto Parts = m->Participants();
			if(std::find(Parts.begin(), Parts.end(), ClientId) != Parts.end())
			{
				m->OnCharacterSpawn(ClientId, SpawnPos);
			}
		}
	}
}

void COneOnOneManager::OnCharacterDeath(int KillerId, int ClientId, int Weapon)
{
	std::vector<std::shared_ptr<COneOnOneEvent>> Snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		Snapshot = m_Matches;
	}

	for(const auto &m : Snapshot)
	{
		if(m && m->GetState() == COneOnOneEvent::EEventState::Active)
		{
			auto Parts = m->Participants();
			if(std::find(Parts.begin(), Parts.end(), ClientId) != Parts.end() || std::find(Parts.begin(), Parts.end(), KillerId) != Parts.end())
			{
				m->OnCharacterDeath(KillerId, ClientId, Weapon);
			}
		}
	}
}
