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
		if(existing.m_Type == SRequest::EType::OneOnOne && existing.m_From == FromClient && existing.m_To == ToClient)
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
			if(r.m_Type == SRequest::EType::OneOnOne && r.m_From == FromClient)
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

	int expiryCfg = g_Config.m_Sv1on1InviteExpiry > 0 ? g_Config.m_Sv1on1InviteExpiry : ExpireSeconds;
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::OneOnOne;
	r.m_From = FromClient;
	r.m_To = ToClient;
	r.m_Wager = Wager;
	r.m_ExpireTick = Server()->Tick() + expiryCfg * Server()->TickSpeed();
	m_Requests.push_back(r);

	// record last invite tick on the sender for cooldown enforcement
	if(pFrom)
		pFrom->m_Last1on1InviteTick = Server()->Tick();

	char aBuf[256];
	const char *pFromName = SafeClientName(GameServer(), FromClient);
	str_format(aBuf, sizeof(aBuf), "%s challenged you for a 1on1! Use /accept or /decline to respond. (%d BP)", pFromName, Wager);
	if(CheckClientId(ToClient) && GameServer()->m_apPlayers[ToClient])
		GameServer()->SendChatTarget(ToClient, aBuf);
	return r.m_Id;
}

int CRequests::CreateShopRequest(int OwnerClient, int Category, int ItemId, int Price, int ExpireSeconds)
{
	int expiryCfg = g_Config.m_SvShopRequestExpiry > 0 ? g_Config.m_SvShopRequestExpiry : ExpireSeconds;
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::Shop;
	r.m_From = OwnerClient;
	r.m_To = OwnerClient; // owner/operator
	r.m_Category = Category;
	r.m_Item = ItemId;
	r.m_ExpireTick = Server()->Tick() + expiryCfg * Server()->TickSpeed();
	m_Requests.push_back(r);
	return r.m_Id;
}

int CRequests::CreateClanInvite(int FromClient, int ToClient, int ClanId, int ExpireSeconds)
{
	for(const auto &existing : m_Requests)
	{
		if(existing.m_Type == SRequest::EType::Clan && existing.m_From == FromClient && existing.m_To == ToClient && existing.m_ClanId == ClanId)
		{
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
			{
				GameServer()->SendChatTarget(FromClient, "You already have a pending clan invite to this player.");
			}
			return -1;
		}
	}

	int expiryCfg = g_Config.m_SvClanInviteExpiry > 0 ? g_Config.m_SvClanInviteExpiry : ExpireSeconds;
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::Clan;
	r.m_From = FromClient; // issuer (clan leader/co-leader)
	r.m_To = ToClient; // target player
	r.m_ClanId = ClanId;
	r.m_ExpireTick = Server()->Tick() + expiryCfg * Server()->TickSpeed();
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

int CRequests::CreateBlockpointTransfer(int FromClient, int ToClient, int Amount, int ExpireSeconds)
{
	// prevent duplicate pending transfer (same pair, same amount) to avoid spam
	for(const auto &existing : m_Requests)
	{
		if(existing.m_Type == SRequest::EType::BlockpointTransfer && existing.m_From == FromClient && existing.m_To == ToClient && existing.m_Wager == Amount)
		{
			int64_t ticksLeft = existing.m_ExpireTick - Server()->Tick();
			int secondsLeft = ticksLeft > 0 ? (int)(ticksLeft / Server()->TickSpeed()) : 0;
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "You already have a pending BP transfer to this player (%d BP). Expires in %d second%s.", Amount, secondsLeft, secondsLeft != 1 ? "s" : "");
				GameServer()->SendChatTarget(FromClient, aBuf);
			}
			return -1;
		}
	}

	int expiryCfg = g_Config.m_SvBpTransferExpiry > 0 ? g_Config.m_SvBpTransferExpiry : ExpireSeconds;
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::BlockpointTransfer;
	r.m_From = FromClient;
	r.m_To = ToClient;
	r.m_Wager = Amount; // reuse field
	r.m_ExpireTick = Server()->Tick() + expiryCfg * Server()->TickSpeed();
	m_Requests.push_back(r);

	const char *pFromName = SafeClientName(GameServer(), FromClient);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s wants to send you %d blockpoints. Use /accept_bp or /decline_bp.", pFromName, Amount);
	if(CheckClientId(ToClient) && GameServer()->m_apPlayers[ToClient])
		GameServer()->SendChatTarget(ToClient, aBuf);
	return r.m_Id;
}

bool CRequests::AcceptRequest(int RequestId)
{
	auto it = std::find_if(m_Requests.begin(), m_Requests.end(), [RequestId](const SRequest &r) { return r.m_Id == RequestId; });
	if(it == m_Requests.end())
		return false;
	if(it->m_Type == SRequest::EType::OneOnOne)
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

		// notify both parties of acceptance before starting the event
		if(CheckClientId(from) && GameServer()->m_apPlayers[from])
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "%s accepted your 1on1 challenge%s%s", SafeClientName(GameServer(), to), wager > 0 ? " (wager " : "", wager > 0 ? std::to_string(wager).c_str() : "");
			if(wager > 0)
			{
				int len = str_length(aBuf);
				if(len < (int)sizeof(aBuf) - 2)
					str_append(aBuf, ")", sizeof(aBuf));
			}
			GameServer()->SendChatTarget(from, aBuf);
		}
		if(CheckClientId(to) && GameServer()->m_apPlayers[to])
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "You accepted the 1on1 challenge from %s%s%s", SafeClientName(GameServer(), from), wager > 0 ? " (wager " : "", wager > 0 ? std::to_string(wager).c_str() : "");
			if(wager > 0)
			{
				int len = str_length(aBuf);
				if(len < (int)sizeof(aBuf) - 2)
					str_append(aBuf, ")", sizeof(aBuf));
			}
			GameServer()->SendChatTarget(to, aBuf);
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

	if(it->m_Type == SRequest::EType::Clan)
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

		if(pFrom->GetClanId() != clanId || pFrom->GetAuthLevel() < ClanAuthLevel::COLEADER)
		{
			if(CheckClientId(to) && GameServer()->m_apPlayers[to])
				GameServer()->SendChatTarget(to, "Invite no longer valid: inviter lost sufficient clan rights.");
			if(CheckClientId(from) && GameServer()->m_apPlayers[from])
				GameServer()->SendChatTarget(from, "Your pending clan invite was invalidated due to insufficient rights.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}

		if(!GameServer()->Clans())
		{
			dbg_msg("clan", "AcceptRequest: Clans() subsystem unavailable while accepting invite (from=%d to=%d clan=%d)", from, to, clanId);
			if(CheckClientId(to) && GameServer()->m_apPlayers[to])
				GameServer()->SendChatTarget(to, "Clan system unavailable. Try again later.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}

		GameServer()->Clans()->AssignClan(from, pTo->m_Account.m_aName, clanId, pTo->GetAccId());

		if(CheckClientId(from) && GameServer()->m_apPlayers[from])
			GameServer()->SendChatTarget(from, "Your clan invite was accepted. Assigning player...");

		int id = it->m_Id;
		auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
		if(eraseIt != m_Requests.end())
			m_Requests.erase(eraseIt);

		return true;
	}

	if(it->m_Type == SRequest::EType::BlockpointTransfer)
	{
		int from = it->m_From;
		int to = it->m_To;
		int amount = it->m_Wager;
		bool fromPresent = CheckClientId(from) && GameServer()->m_apPlayers[from];
		bool toPresent = CheckClientId(to) && GameServer()->m_apPlayers[to];
		CPlayer *pFrom = fromPresent ? GameServer()->m_apPlayers[from] : nullptr;
		CPlayer *pTo = toPresent ? GameServer()->m_apPlayers[to] : nullptr;
		if(!fromPresent || !toPresent || !pFrom || !pTo || !pFrom->IsLoggedIn() || !pTo->IsLoggedIn())
		{
			if(toPresent)
				GameServer()->SendChatTarget(to, "Transfer failed: one of the players disconnected or is not logged in.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}
		if(pFrom->GetPlayerBlockpoints() < amount || amount <= 0)
		{
			GameServer()->SendChatTarget(to, "Transfer cancelled: sender no longer has sufficient blockpoints.");
			if(fromPresent)
				GameServer()->SendChatTarget(from, "Your blockpoint transfer was cancelled due to insufficient funds.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}
		pFrom->SetPlayerBlockpoints(pFrom->GetPlayerBlockpoints() - amount);
		pTo->SetPlayerBlockpoints(pTo->GetPlayerBlockpoints() + amount);
		GameServer()->Accounts()->Save(from, &pFrom->m_Account);
		GameServer()->Accounts()->Save(to, &pTo->m_Account);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You sent %d blockpoints to %s (now %d)", amount, SafeClientName(GameServer(), to), pFrom->GetPlayerBlockpoints());
		GameServer()->SendChatTarget(from, aBuf);
		str_format(aBuf, sizeof(aBuf), "You received %d blockpoints from %s (now %d)", amount, SafeClientName(GameServer(), from), pTo->GetPlayerBlockpoints());
		GameServer()->SendChatTarget(to, aBuf);
		dbg_msg("bp_transfer", "transfer id=%d from=%d to=%d amount=%d", it->m_Id, from, to, amount);
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

	if(it->m_Type == SRequest::EType::OneOnOne)
	{
		int from = it->m_From; // inviter
		int to = it->m_To;   // declining player
		if(CheckClientId(to) && GameServer()->m_apPlayers[to])
			GameServer()->SendChatTarget(to, "You declined the 1on1 challenge.");
		if(CheckClientId(from) && GameServer()->m_apPlayers[from])
		{
			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), "Your 1on1 challenge to %s was declined.", SafeClientName(GameServer(), to));
			GameServer()->SendChatTarget(from, aBuf);
		}
		m_Requests.erase(it);
		return true;
	}

	if(it->m_Type == SRequest::EType::Clan)
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

std::vector<int> CRequests::GetRequestsFor(int ClientId, std::optional<SRequest::EType> typeFilter) const
{
	std::vector<int> out;
	for(const auto &r : m_Requests)
	{
		if(r.m_To == ClientId || r.m_From == ClientId)
		{
			if(!typeFilter.has_value() || r.m_Type == *typeFilter)
				out.push_back(r.m_Id);
		}
	}
	return out;
}

std::vector<int> CRequests::GetRequestIdsTo(int ToClient, std::optional<SRequest::EType> typeFilter) const
{
	std::vector<int> out;
	for(const auto &r : m_Requests)
	{
		if(r.m_To == ToClient)
		{
			if(!typeFilter.has_value() || r.m_Type == *typeFilter)
				out.push_back(r.m_Id);
		}
	}
	return out;
}

std::vector<int> CRequests::GetRequestIdsFromTo(int FromClient, int ToClient, std::optional<SRequest::EType> typeFilter) const
{
	std::vector<int> out;
	for(const auto &r : m_Requests)
	{
		if(r.m_To == ToClient && r.m_From == FromClient)
		{
			if(!typeFilter.has_value() || r.m_Type == *typeFilter)
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

		char aBufFrom[256];
		char aBufTo[256];
		bool notifyTo = false;

		if(req.m_Type == SRequest::EType::OneOnOne)
		{
			const char *pToName = SafeClientName(GameServer(), req.m_To);
			const char *pFromName = SafeClientName(GameServer(), req.m_From);
			str_format(aBufFrom, sizeof(aBufFrom), "Your 1on1 invite to '%s' has expired.", pToName);
			str_format(aBufTo, sizeof(aBufTo), "The 1on1 invite from '%s' has expired.", pFromName);
			notifyTo = true;
		}
		else if(req.m_Type == SRequest::EType::BlockpointTransfer)
		{
			const char *pToName = SafeClientName(GameServer(), req.m_To);
			const char *pFromName = SafeClientName(GameServer(), req.m_From);
			str_format(aBufFrom, sizeof(aBufFrom), "Your blockpoint transfer to '%s' (%d BP) has expired.", pToName, req.m_Wager);
			str_format(aBufTo, sizeof(aBufTo), "The blockpoint transfer from '%s' (%d BP) has expired.", pFromName, req.m_Wager);
			notifyTo = true;
		}
		else if(req.m_Type == SRequest::EType::Clan)
		{
			const char *pToName = SafeClientName(GameServer(), req.m_To);
			const char *pFromName = SafeClientName(GameServer(), req.m_From);
			str_format(aBufFrom, sizeof(aBufFrom), "Your clan invite to '%s' has expired.", pToName);
			str_format(aBufTo, sizeof(aBufTo), "The clan invite from '%s' has expired.", pFromName);
			notifyTo = true;
		}
		else
		{
			str_copy(aBufFrom, "Your shop request has expired.", sizeof(aBufFrom));
		}

		if(CheckClientId(req.m_From) && GameServer()->m_apPlayers[req.m_From])
			GameServer()->SendChatTarget(req.m_From, aBufFrom);
		if(notifyTo && req.m_From != req.m_To && CheckClientId(req.m_To) && GameServer()->m_apPlayers[req.m_To])
			GameServer()->SendChatTarget(req.m_To, aBufTo);

		auto it = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
		if(it != m_Requests.end())
			m_Requests.erase(it);
	}
}

int CRequests::CancelRequestsInvolving(int ClientId, std::optional<SRequest::EType> typeFilter, const char *pReason)
{
	int cancelled = 0;
	// collect first to avoid iterator invalidation complexity
	std::vector<int> ids;
	for(const auto &r : m_Requests)
	{
		if((r.m_From == ClientId || r.m_To == ClientId) && (!typeFilter.has_value() || r.m_Type == *typeFilter))
			ids.push_back(r.m_Id);
	}
	for(int id : ids)
	{
		auto it = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
		if(it == m_Requests.end())
			continue;
		// notify counterpart if online
		int other = (it->m_From == ClientId) ? it->m_To : it->m_From;
		if(CheckClientId(other) && GameServer()->m_apPlayers[other])
		{
			char aBuf[256];
			if(pReason)
				str_format(aBuf, sizeof(aBuf), "A pending %s request was cancelled: %s", it->m_Type == SRequest::EType::BlockpointTransfer ? "blockpoint transfer" : it->m_Type == SRequest::EType::OneOnOne ? "1on1" : it->m_Type == SRequest::EType::Clan ? "clan" : "request", pReason);
			else
				str_format(aBuf, sizeof(aBuf), "A pending %s request was cancelled.", it->m_Type == SRequest::EType::BlockpointTransfer ? "blockpoint transfer" : it->m_Type == SRequest::EType::OneOnOne ? "1on1" : it->m_Type == SRequest::EType::Clan ? "clan" : "request");
			GameServer()->SendChatTarget(other, aBuf);
		}
		m_Requests.erase(it);
		cancelled++;
	}
	if(cancelled > 0)
		dbg_msg("requests", "cancelled %d request(s) involving client %d", cancelled, ClientId);
	return cancelled;
}
