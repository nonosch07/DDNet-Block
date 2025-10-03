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
			m_Requests.erase(it);
			return false;
		}

		if(GameServer()->isInEvent(from) || GameServer()->isInEvent(to))
		{
			GameServer()->SendChatTarget(to, "Cannot start 1on1: one of the players is already in an event.");
			GameServer()->SendChatTarget(from, "Your 1on1 could not be started because a player is in another event.");
			m_Requests.erase(it);
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
					GameServer()->SendChatTarget(-1, "Starting 1on1 via component system");
				}
			}
			else
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Starting 1on1 between %s and %s (wager %d)", SafeClientName(GameServer(), from), SafeClientName(GameServer(), to), wager);
				GameServer()->SendChatTarget(-1, aBuf);
			}
		}
	}

	m_Requests.erase(it);
	return true;
}

bool CRequests::DeclineRequest(int RequestId)
{
	auto it = std::find_if(m_Requests.begin(), m_Requests.end(), [RequestId](const SRequest &r) { return r.m_Id == RequestId; });
	if(it == m_Requests.end())
		return false;

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
	auto it = m_Requests.begin();
	while(it != m_Requests.end())
	{
		if(it->m_ExpireTick <= Tick)
		{
			char aBuf[256];
			const char *pToName = SafeClientName(GameServer(), it->m_To);
			str_format(aBuf, sizeof(aBuf), "Your %s invite to '%s' has expired.", it->m_Type == SRequest::OneOnOne ? "1on1" : "shop", pToName);
			if(CheckClientId(it->m_From) && GameServer()->m_apPlayers[it->m_From])
				GameServer()->SendChatTarget(it->m_From, aBuf);
			auto cur = it++;
			m_Requests.erase(cur);
		}
		else
			++it;
	}
}
