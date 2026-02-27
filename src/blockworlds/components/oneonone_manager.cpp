#include "oneonone_manager.h"
#include <base/system.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/player.h>
#include <game/server/save.h>

#include <blockworlds/votes/votemanager.h>

#include <algorithm>

COneOnOneManager::COneOnOneManager(CGameContext *pGameServer) :
	CComponent(pGameServer)
{
}

std::shared_ptr<COneOnOneEvent> COneOnOneManager::CreateMatch(int Player1ID, int Player2ID, int Wager)
{
	if(!GameServer()->GetPlayer(Player1ID) || !GameServer()->GetPlayer(Player2ID))
	{
		dbg_msg("oneonone", "CreateMatch failed: one or both players not present (p1=%d p2=%d)", Player1ID, Player2ID);
		return nullptr;
	}

	if(GetMatchForPlayer(Player1ID) || GetMatchForPlayer(Player2ID))
	{
		dbg_msg("oneonone", "CreateMatch failed: one or both players already in an active match (p1=%d p2=%d)", Player1ID, Player2ID);
		return nullptr;
	}

	auto match = std::make_shared<COneOnOneEvent>(GameServer());
	// Insert into manager before initialization so the match is discoverable during Initialize/StartEvent
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Matches.push_back(match);
	}

	// Initialize will call StartEvent() immediately. If initialization fails (e.g., no free team), remove match and return null
	bool ok = match->Initialize(Player1ID, Player2ID, Wager);
	if(!ok)
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Matches.erase(std::remove_if(m_Matches.begin(), m_Matches.end(), [&](const std::shared_ptr<COneOnOneEvent> &m) { return m == match; }), m_Matches.end());
		dbg_msg("oneonone", "CreateMatch failed: Initialize returned false for players %d vs %d", Player1ID, Player2ID);
		return nullptr;
	}

	return match;
}

std::shared_ptr<COneOnOneEvent> COneOnOneManager::CreateMatchWithConfig(int Player1ID, int Player2ID, int Wager)
{
	if(!GameServer()->GetPlayer(Player1ID) || !GameServer()->GetPlayer(Player2ID))
	{
		dbg_msg("oneonone", "CreateMatchWithConfig failed: one or both players not present (p1=%d p2=%d)", Player1ID, Player2ID);
		return nullptr;
	}

	if(GetMatchForPlayer(Player1ID) || GetMatchForPlayer(Player2ID))
	{
		dbg_msg("oneonone", "CreateMatchWithConfig failed: one or both players already in an active match (p1=%d p2=%d)", Player1ID, Player2ID);
		return nullptr;
	}

	auto match = std::make_shared<COneOnOneEvent>(GameServer());
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Matches.push_back(match);
	}

	bool ok = match->InitializeConfigPhase(Player1ID, Player2ID, Wager);
	if(!ok)
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		m_Matches.erase(std::remove_if(m_Matches.begin(), m_Matches.end(), [&](const std::shared_ptr<COneOnOneEvent> &m) { return m == match; }), m_Matches.end());
		dbg_msg("oneonone", "CreateMatchWithConfig failed: InitializeConfigPhase returned false for players %d vs %d", Player1ID, Player2ID);
		return nullptr;
	}

	return match;
}

std::shared_ptr<COneOnOneEvent> COneOnOneManager::GetMatchForPlayer(int ClientId) const
{
	std::lock_guard<std::mutex> g(m_Mutex);
	for(const auto &m : m_Matches)
	{
		if(!m)
			continue;
		auto parts = m->Participants();
		if(std::find(parts.begin(), parts.end(), ClientId) != parts.end())
			return m;
	}
	return nullptr;
}

void COneOnOneManager::OnTick()
{
	// Copy matches under lock, then call OnTick() outside the lock to avoid
	// holding the mutex while running match logic (which may re-enter manager
	// code). Afterwards prune finished matches while holding the lock.
	std::vector<std::shared_ptr<COneOnOneEvent>> snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		snapshot = m_Matches;
	}

	for(const auto &m : snapshot)
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

	std::vector<std::shared_ptr<COneOnOneEvent>> snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		snapshot = m_Matches;
	}

	for(const auto &match : snapshot)
	{
		if(!match || match->GetState() == COneOnOneEvent::EEventState::Finished ||
			match->GetState() == COneOnOneEvent::EEventState::Created)
			continue;

		std::lock_guard<std::mutex> matchLock(match->m_Mutex);
		for(const auto &[Cid, pSavedTee] : match->m_pSavedPlayers)
		{
			if(Cid < 0 || !pSavedTee)
				continue;

			int SnapId = Cid;
			if(!Server()->Translate(SnapId, SnappingClient))
				continue;

			vec2 SavedPos = pSavedTee->GetPos();

			CNetObj_SpecChar *pSpecChar = Server()->SnapNewItem<CNetObj_SpecChar>(SnapId);
			if(!pSpecChar)
				continue;

			pSpecChar->m_X = (int)SavedPos.x;
			pSpecChar->m_Y = (int)SavedPos.y;
		}
	}
}

void COneOnOneManager::OnPlayerDropping(int ClientId)
{
	std::vector<std::shared_ptr<COneOnOneEvent>> snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		snapshot = m_Matches;
	}

	for(const auto &m : snapshot)
	{
		if(m && (m->GetState() == COneOnOneEvent::EEventState::Active || m->GetState() == COneOnOneEvent::EEventState::Preparation))
		{
			auto parts = m->Participants();
			if(std::find(parts.begin(), parts.end(), ClientId) != parts.end())
			{
				if(m->GetState() == COneOnOneEvent::EEventState::Preparation)
				{
					// player dropped during config phase — abort with full cleanup
					int otherCid = (ClientId == m->m_Player1ID) ? m->m_Player2ID : m->m_Player1ID;
					GameServer()->SendChatTarget(otherCid, "[1on1] Opponent disconnected during warmup. Match cancelled.");

					// clear duel config vote pages
					extern CVoteManager g_VoteManager;
					for(int cid : {m->m_Player1ID, m->m_Player2ID})
					{
						auto &Stack = g_VoteManager.GetPageStackMut(cid);
						Stack.clear();
						Stack.push_back(CVoteManager::Page{CVoteManager::Page::ROOT, -1});
						GameServer()->ClearVotes(cid);
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
	std::vector<std::shared_ptr<COneOnOneEvent>> snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		snapshot = m_Matches;
	}

	for(const auto &m : snapshot)
	{
		if(m && (m->GetState() == COneOnOneEvent::EEventState::Active || m->GetState() == COneOnOneEvent::EEventState::Preparation))
		{
			auto parts = m->Participants();
			if(std::find(parts.begin(), parts.end(), ClientId) != parts.end())
			{
				m->OnCharacterSpawn(ClientId, SpawnPos);
			}
		}
	}
}

void COneOnOneManager::OnCharacterDeath(int KillerId, int ClientId, int Weapon)
{
	std::vector<std::shared_ptr<COneOnOneEvent>> snapshot;
	{
		std::lock_guard<std::mutex> g(m_Mutex);
		snapshot = m_Matches;
	}

	for(const auto &m : snapshot)
	{
		if(m && m->GetState() == COneOnOneEvent::EEventState::Active)
		{
			auto parts = m->Participants();
			if(std::find(parts.begin(), parts.end(), ClientId) != parts.end() || std::find(parts.begin(), parts.end(), KillerId) != parts.end())
			{
				m->OnCharacterDeath(KillerId, ClientId, Weapon);
			}
		}
	}
}
