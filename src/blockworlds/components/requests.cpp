#include "requests.h"
#include <algorithm>
#include <base/system.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/components/events/1on1.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CRequests::CRequests(CGameContext *pGameServer) :
	CComponent(pGameServer) {}

int CRequests::NextId()
{
	return m_NextId++;
}

int CRequests::Create1on1Invite(int FromClient, int ToClient, int Wager, int ExpireSeconds)
{
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::OneOnOne;
	r.m_From = FromClient;
	r.m_To = ToClient;
	r.m_Wager = Wager;
	r.m_ExpireTick = Server()->Tick() + ExpireSeconds * Server()->TickSpeed();
	m_Requests.push_back(r);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s challenged you for an 1on1! (/accept %d, /decline %d) (%d BP)", Server()->ClientName(FromClient), r.m_Id, r.m_Id, Wager);
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
		// delegate to Events component to create a 1on1 event (we'll implement a C1v1 component later)
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
				str_format(aBuf, sizeof(aBuf), "Starting 1on1 between %s and %s (wager %d)", Server()->ClientName(from), Server()->ClientName(to), wager);
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
	str_format(aBuf, sizeof(aBuf), "Your invite (id=%d) has been declined.", it->m_Id);
	GameServer()->SendChatTarget(it->m_From, aBuf);
	m_Requests.erase(it);
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
			str_format(aBuf, sizeof(aBuf), "Your %s invite to '%s' has expired.", it->m_Type == SRequest::OneOnOne ? "1on1" : "shop", Server()->ClientName(it->m_To));
			GameServer()->SendChatTarget(it->m_From, aBuf);
			auto cur = it++;
			m_Requests.erase(cur);
		}
		else
			++it;
	}
}
