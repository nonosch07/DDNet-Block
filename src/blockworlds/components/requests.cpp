#include "requests.h"
#include <algorithm>
#include <base/system.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/components/events/1on1.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CRequests::CRequests(CGameContext *pGameServer) :
	CComponent(pGameServer) {}

int CRequests::NextId()
{
	return m_NextId++;
}

static const char *SafeClientName(CGameContext *pGameServer, int ClientId)
{
	if(!CheckClientId(ClientId))
		return "<invalid>";
	if(pGameServer->m_apPlayers[ClientId])
		return pGameServer->Server()->ClientName(ClientId);
	return "<disconnected>";
}

int CRequests::Create1on1Invite(int FromClient, int ToClient, int Wager, int ExpireSeconds)
{
	for(const auto &existing : m_Requests)
	{
		if(existing.m_Type == SRequest::OneOnOne && existing.m_From == FromClient && existing.m_To == ToClient)
		{
			int64_t ticksLeft = existing.m_ExpireTick - Server()->Tick();
			int secondsLeft = ticksLeft > 0 ? (int)(ticksLeft / Server()->TickSpeed()) : 0;
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "You already have a pending 1on1 invite to this player. Expires in %d second%s.", secondsLeft, secondsLeft != 1 ? "s" : "");
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
				GameServer()->SendChatTarget(FromClient, aBuf);
			return -1;
		}
	}

	// Anti-spam: limit how often a player can send any 1on1 invite and how many are outstanding.
	CPlayer *pFrom = GameServer()->m_apPlayers[FromClient];
	if(pFrom)
	{
		const int InviteCooldownSeconds = g_Config.m_Sv1on1InviteCooldown;
		int64_t now = Server()->Tick();
		if(pFrom->m_Last1on1InviteTick != 0 && now - pFrom->m_Last1on1InviteTick < InviteCooldownSeconds * Server()->TickSpeed())
		{
			int remaining = (int)((InviteCooldownSeconds * Server()->TickSpeed() - (now - pFrom->m_Last1on1InviteTick)) / Server()->TickSpeed());
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another 1on1 invite.", remaining, remaining != 1 ? "s" : "");
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
				GameServer()->SendChatTarget(FromClient, aBuf);
			return -1;
		}

		int outstanding = 0;
		for(const auto &r : m_Requests)
		{
			if(r.m_Type == SRequest::OneOnOne && r.m_From == FromClient)
				outstanding++;
		}
		const int MaxOutstanding = g_Config.m_Sv1on1MaxOutstandingInvitesPerSender;
		if(outstanding >= MaxOutstanding)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "You already have %d outstanding 1on1 invite%s. Please wait for them to be accepted, declined or expire.", outstanding, outstanding != 1 ? "s" : "");
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
				GameServer()->SendChatTarget(FromClient, aBuf);
			return -1;
		}
	}

	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::OneOnOne;
	r.m_From = FromClient;
	r.m_To = ToClient;
	r.m_Wager = Wager;
	r.m_ExpireTick = Server()->Tick() + ExpireSeconds * Server()->TickSpeed();
	m_Requests.push_back(r);

	// record last invite tick on the sender for cooldown enforcement
	if(pFrom)
		pFrom->m_Last1on1InviteTick = Server()->Tick();

	char aBuf[256];
	const char *pFromName = SafeClientName(GameServer(), FromClient);
	str_format(aBuf, sizeof(aBuf), "%s challenged you for a 1on1! Use /accept %s or /decline %s to respond. (%d BP)", pFromName, pFromName, pFromName, Wager);
	if(CheckClientId(ToClient) && GameServer()->m_apPlayers[ToClient])
		GameServer()->SendChatTarget(ToClient, aBuf);
	return r.m_Id;
}

int CRequests::CreateShopRequest(int OwnerClient, int Category, int ItemId, int Price, int ExpireSeconds)
{
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::Shop;
	r.m_From = OwnerClient;
	r.m_To = OwnerClient; // owner/operator
	r.m_Category = Category;
	r.m_Item = ItemId;
	r.m_ExpireTick = Server()->Tick() + ExpireSeconds * Server()->TickSpeed();
	m_Requests.push_back(r);
	return r.m_Id;
}

int CRequests::CreateClanInvite(int FromClient, int ToClient, int ClanId, int ExpireSeconds)
{
	for(const auto &existing : m_Requests)
	{
		if(existing.m_Type == SRequest::Clan && existing.m_From == FromClient && existing.m_To == ToClient && existing.m_ClanId == ClanId)
		{
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
			{
				GameServer()->SendChatTarget(FromClient, "You already have a pending clan invite to this player.");
			}
			return -1;
		}
	}

	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::Clan;
	r.m_From = FromClient; // issuer (clan leader/co-leader)
	r.m_To = ToClient; // target player
	r.m_ClanId = ClanId;
	r.m_ExpireTick = Server()->Tick() + ExpireSeconds * Server()->TickSpeed();
	m_Requests.push_back(r);

	const char *pFromName = SafeClientName(GameServer(), FromClient);
	char aBuf[256];
	const char *pClanName = "<clan>";
	if(GameServer()->Clans())
	{
		CClansData tmp;
		if(GameServer()->Clans()->GetClanSnapshotById(ClanId, tmp))
			pClanName = tmp.m_ClanName;
	}
	str_format(aBuf, sizeof(aBuf), "%s invited you to join clan '%s'. Use /clan_accept or /clan_decline.", pFromName, pClanName);
	if(CheckClientId(ToClient) && GameServer()->m_apPlayers[ToClient])
		GameServer()->SendChatTarget(ToClient, aBuf);

	return r.m_Id;
}

bool CRequests::AcceptRequest(int RequestId)
{
	auto it = std::find_if(m_Requests.begin(), m_Requests.end(), [RequestId](const SRequest &r) { return r.m_Id == RequestId; });
	if(it == m_Requests.end())
		return false;
	if(it->m_Type == SRequest::OneOnOne)
	{
		// start a 1on1 match via new component-based event system
		int from = it->m_From;
		int to = it->m_To;
		int wager = it->m_Wager;

		bool fromPresent = CheckClientId(from) && GameServer()->m_apPlayers[from];
		bool toPresent = CheckClientId(to) && GameServer()->m_apPlayers[to];
		if(!fromPresent || !toPresent)
		{
			if(fromPresent)
				GameServer()->SendChatTarget(from, "Your 1on1 could not be started because the other player disconnected.");
			if(toPresent)
				GameServer()->SendChatTarget(to, "The 1on1 you tried to accept could not be started because the other player disconnected.");
			// Erase by id after handling
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}

		if(GameServer()->isInEvent(from) || GameServer()->isInEvent(to))
		{
			GameServer()->SendChatTarget(to, "Cannot start 1on1: one of the players is already in an event.");
			GameServer()->SendChatTarget(from, "Your 1on1 could not be started because a player is in another event.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}

		if(auto events = g_ComponentRegistry.Get<CEvents>(); events)
		{
			auto ev = events->CreateEventByName("1on1");
			if(ev)
			{
				if(auto one = std::dynamic_pointer_cast<COneOnOneEvent>(ev))
				{
					one->Initialize(from, to, wager);
					events->SetActiveEvent(ev);
					// GameServer()->SendChatTarget(-1, "Starting 1on1 via component system");
				}
			}
			else
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Starting 1on1 between %s and %s (wager %d)", SafeClientName(GameServer(), from), SafeClientName(GameServer(), to), wager);
				GameServer()->SendChatTarget(-1, aBuf);
			}
		}

		// success path: remove the request by id
		{
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
		}
		return true;
	}

	if(it->m_Type == SRequest::Clan)
	{
		// clan invite accept flow
		int from = it->m_From; // issuer
		int to = it->m_To; // accepting player
		int clanId = it->m_ClanId;

		bool fromPresent = CheckClientId(from) && GameServer()->m_apPlayers[from];
		bool toPresent = CheckClientId(to) && GameServer()->m_apPlayers[to];
		if(!fromPresent || !toPresent)
		{
			if(fromPresent)
				GameServer()->SendChatTarget(from, "Your clan invite could not be accepted because the other player disconnected.");
			if(toPresent)
				GameServer()->SendChatTarget(to, "You could not accept the clan invite because the inviter disconnected.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}

		CPlayer *pTo = GameServer()->m_apPlayers[to];
		CPlayer *pFrom = GameServer()->m_apPlayers[from];
		if(!pTo || !pFrom)
		{
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}

		if(pTo->GetClanId() != 0)
		{
			GameServer()->SendChatTarget(to, "You are already in a clan.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}

		if(!GameServer()->Clans() || !GameServer()->Clans()->IsClanJoinable(clanId))
		{
			GameServer()->SendChatTarget(to, "Clan is no longer joinable.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}

		GameServer()->Clans()->AssignClan(pTo->GetCid(), pTo->GetPlayerName(), clanId, pTo->GetAccId());
		pTo->SetAuthLevel(1);
		if(CheckClientId(from) && GameServer()->m_apPlayers[from])
			GameServer()->SendChatTarget(from, "Your clan invite was accepted.");

		int id = it->m_Id;
		auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
		if(eraseIt != m_Requests.end())
			m_Requests.erase(eraseIt);

		return true;
	}

	// unknown/other request types: just erase and return false
	{
		int id = it->m_Id;
		auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
		if(eraseIt != m_Requests.end())
			m_Requests.erase(eraseIt);
	}
	return false;
}

bool CRequests::DeclineRequest(int RequestId)
{
	auto it = std::find_if(m_Requests.begin(), m_Requests.end(), [RequestId](const SRequest &r) { return r.m_Id == RequestId; });
	if(it == m_Requests.end())
		return false;

	if(it->m_Type == SRequest::Clan)
	{
		int from = it->m_From;
		int to = it->m_To;
		if(CheckClientId(to) && GameServer()->m_apPlayers[to])
			GameServer()->SendChatTarget(to, "You have declined the clan invitation.");
		if(CheckClientId(from) && GameServer()->m_apPlayers[from])
			GameServer()->SendChatTarget(from, "The clan invitation was declined.");
		m_Requests.erase(it);
		return true;
	}

	char aBuf[256];
	str_copy(aBuf, "Your invite has been declined.", sizeof(aBuf));
	if(CheckClientId(it->m_From) && GameServer()->m_apPlayers[it->m_From])
		GameServer()->SendChatTarget(it->m_From, aBuf);
	m_Requests.erase(it);
	return true;
}

bool CRequests::GetRequestInfo(int RequestId, SRequest &pOut) const
{
	auto it = std::find_if(m_Requests.begin(), m_Requests.end(), [RequestId](const SRequest &r) { return r.m_Id == RequestId; });
	if(it == m_Requests.end())
		return false;
	pOut = *it;
	return true;
}

std::vector<int> CRequests::GetRequestsFor(int ClientId, int TypeFilter) const
{
	std::vector<int> out;
	for(const auto &r : m_Requests)
	{
		if(r.m_To == ClientId || r.m_From == ClientId)
		{
			if(TypeFilter == -1 || (int)r.m_Type == TypeFilter)
				out.push_back(r.m_Id);
		}
	}
	return out;
}

std::vector<int> CRequests::GetRequestIdsTo(int ToClient, int TypeFilter) const
{
	std::vector<int> out;
	for(const auto &r : m_Requests)
	{
		if(r.m_To == ToClient)
		{
			if(TypeFilter == -1 || (int)r.m_Type == TypeFilter)
				out.push_back(r.m_Id);
		}
	}
	return out;
}

std::vector<int> CRequests::GetRequestIdsFromTo(int FromClient, int ToClient, int TypeFilter) const
{
	std::vector<int> out;
	for(const auto &r : m_Requests)
	{
		if(r.m_To == ToClient && r.m_From == FromClient)
		{
			if(TypeFilter == -1 || (int)r.m_Type == TypeFilter)
				out.push_back(r.m_Id);
		}
	}
	return out;
}

void CRequests::OnTick()
{
	int Tick = Server()->Tick();
	std::vector<int> ExpiredIds;
	for(const auto &r : m_Requests)
	{
		if(r.m_ExpireTick <= Tick)
			ExpiredIds.push_back(r.m_Id);
	}

	for(int Id : ExpiredIds)
	{
		SRequest req;
		if(!GetRequestInfo(Id, req))
			continue; // already handled/removed

		char aBuf[256];
		if(req.m_Type == SRequest::OneOnOne)
		{
			const char *pToName = SafeClientName(GameServer(), req.m_To);
			str_format(aBuf, sizeof(aBuf), "Your 1on1 invite to '%s' has expired.", pToName);
		}
		else
		{
			str_copy(aBuf, "Your shop request has expired.", sizeof(aBuf));
		}

		if(CheckClientId(req.m_From) && GameServer()->m_apPlayers[req.m_From])
			GameServer()->SendChatTarget(req.m_From, aBuf);

		auto it = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
		if(it != m_Requests.end())
			m_Requests.erase(it);
	}
}
