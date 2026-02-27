#include "requests.h"
#include <algorithm>
#include <base/system.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/components/oneonone_manager.h>
#include <blockworlds/discord/webhook.h>
#include <ctime>
#include <engine/shared/config.h>
#include <game/mapitems.h>
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

static int CurrentUtcYyyymmdd()
{
	time_t now = time(nullptr);
	struct tm utc
	{
	};
#if defined(__unix__) || defined(__APPLE__)
	gmtime_r(&now, &utc);
#else
	utc = *gmtime(&now);
#endif
	return (utc.tm_year + 1900) * 10000 + (utc.tm_mon + 1) * 100 + utc.tm_mday;
}

int CRequests::Create1on1Invite(int FromClient, int ToClient, int Wager, int ExpireSeconds)
{
	// disallow inviting players on the same IP address
	//if(GameServer()->Server()->IsClientsSameAddr(FromClient, ToClient))
	//{
	//	if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
	//		GameServer()->SendChatTarget(FromClient, "You cannot invite your dummy.");
	//	return -1;
	//}

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

	if(GameServer()->isInEvent(FromClient) || GameServer()->isInEvent(ToClient))
	{
		if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
			GameServer()->SendChatTarget(FromClient, "Cannot send 1on1 request: one of the players is in an event.");
		return -1;
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
			str_format(aBuf, sizeof(aBuf), "You already have %d outstanding 1on1 invite%s. Please wait.", outstanding, outstanding != 1 ? "s" : "");
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
	// basic sanity
	if(!CheckClientId(FromClient) || !CheckClientId(ToClient) || FromClient == ToClient)
		return -1;
	CPlayer *pFrom = GameServer()->m_apPlayers[FromClient];
	CPlayer *pTo = GameServer()->m_apPlayers[ToClient];
	if(!pFrom || !pTo)
		return -1;
	if(!pFrom->IsLoggedIn() || !pTo->IsLoggedIn())
	{
		if(pFrom)
			GameServer()->SendChatTarget(FromClient, "You and the target must both be logged in.");
		return -1;
	}
	if(Amount <= 0)
	{
		GameServer()->SendChatTarget(FromClient, "Amount must be greater than zero.");
		return -1;
	}
	if(Amount < g_Config.m_SvBpTransferAmountMin)
	{
		GameServer()->SendChatTarget(FromClient, "Amount below minimum transfer threshold.");
		return -1;
	}
	if(Amount > g_Config.m_SvBpTransferAmountCap)
	{
		GameServer()->SendChatTarget(FromClient, "Amount exceeds max cap.");
		return -1;
	}
	if(GameServer()->isInEvent(FromClient) || GameServer()->isInEvent(ToClient))
	{
		GameServer()->SendChatTarget(FromClient, "Transfers are not allowed while either player is in an event.");
		return -1;
	}
	int Cooldown = g_Config.m_SvBpTransferCooldown;
	if(Cooldown > 0 && pFrom->m_LastBpTransferOfferTick != 0 && Server()->Tick() - pFrom->m_LastBpTransferOfferTick < Cooldown * Server()->TickSpeed())
	{
		int Rem = (int)((Cooldown * Server()->TickSpeed() - (Server()->Tick() - pFrom->m_LastBpTransferOfferTick)) / Server()->TickSpeed());
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another transfer.", Rem, Rem != 1 ? "s" : "");
		GameServer()->SendChatTarget(FromClient, aBuf);
		return -1;
	}

	// cap outstanding offers per sender (only those from this sender to anyone)
	{
		int outstanding = 0;
		for(const auto &r : m_Requests)
		{
			if(r.m_Type == SRequest::EType::BlockpointTransfer && r.m_From == FromClient)
				outstanding++;
		}
		if(outstanding >= g_Config.m_SvBpTransferMaxOutstandingPerSender)
		{
			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), "You already have %d outstanding BP transfer offer%s.", outstanding, outstanding != 1 ? "s" : "");
			GameServer()->SendChatTarget(FromClient, aBuf);
			return -1;
		}
	}
	// sufficient funds at offer time to reduce failed accepts later
	if(pFrom->GetPlayerBlockpoints() < Amount)
	{
		GameServer()->SendChatTarget(FromClient, "You don't have enough blockpoints.");
		return -1;
	}

	// block if already at cap
	{
		const int AccId = pFrom->GetAccId();
		if(AccId > 0)
		{
			int today = CurrentUtcYyyymmdd();
			auto &ctr = m_BpDailyCounters[AccId];
			if(ctr.m_TodayDate != today)
			{
				ctr.m_TodayDate = today;
				ctr.m_TodayAmount = 0;
				ctr.m_TodayCount = 0;
			}
			if(g_Config.m_SvBpTransferDailyCountCap > 0 && ctr.m_TodayCount >= g_Config.m_SvBpTransferDailyCountCap)
			{
				GameServer()->SendChatTarget(FromClient, "Daily transfer count cap reached. Try again tomorrow.");
				return -1;
			}
			if(g_Config.m_SvBpTransferDailyAmountCap > 0 && ctr.m_TodayAmount >= g_Config.m_SvBpTransferDailyAmountCap)
			{
				GameServer()->SendChatTarget(FromClient, "Daily transfer amount cap reached. Try again tomorrow.");
				return -1;
			}
		}
	}

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

	// mark last offer for cooldown enforcement
	pFrom->m_LastBpTransferOfferTick = Server()->Tick();
	return r.m_Id;
}

int CRequests::CreateClanDeleteConfirm(int ClientId, int ClanId, int ExpireSeconds)
{
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::ClanDeleteConfirm;
	r.m_From = ClientId;
	r.m_To = ClientId; // self-confirmation
	r.m_ClanId = ClanId;
	{
		int expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + expiry * Server()->TickSpeed();
	}
	m_Requests.push_back(r);

	// notify
	const char *pClanName = "<clan>";
	if(GameServer()->Clans())
	{
		CClansData tmp;
		if(GameServer()->Clans()->GetClanSnapshotById(ClanId, tmp))
			pClanName = tmp.m_ClanName;
	}
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Are you sure you want to delete clan '%s'? Type /clan_yes or /clan_no.", pClanName);
	if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		GameServer()->SendChatTarget(ClientId, aBuf);
	return r.m_Id;
}

int CRequests::CreateClanKickConfirm(int ClientId, int ClanId, const char *pTargetAccountName, int ExpireSeconds)
{
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::ClanKickConfirm;
	r.m_From = ClientId;
	r.m_To = ClientId; // self-confirmation
	r.m_ClanId = ClanId;
	{
		int expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + expiry * Server()->TickSpeed();
	}
	r.m_aUsername[0] = '\0';
	if(pTargetAccountName)
		str_copy(r.m_aUsername, pTargetAccountName, sizeof(r.m_aUsername));
	m_Requests.push_back(r);

	const char *pClanName = "<clan>";
	if(GameServer()->Clans())
	{
		CClansData tmp;
		if(GameServer()->Clans()->GetClanSnapshotById(ClanId, tmp))
			pClanName = tmp.m_ClanName;
	}
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Are you sure you want to kick '%s' from clan '%s'? Type /clan_yes or /clan_no.", r.m_aUsername[0] ? r.m_aUsername : "<unknown>", pClanName);
	if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		GameServer()->SendChatTarget(ClientId, aBuf);
	return r.m_Id;
}

int CRequests::CreateClanTransferConfirm(int ClientId, int ClanId, const char *pTargetAccountName, int ExpireSeconds)
{
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::ClanTransferConfirm;
	r.m_From = ClientId;
	r.m_To = ClientId; // self-confirmation
	r.m_ClanId = ClanId;
	{
		int expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + expiry * Server()->TickSpeed();
	}
	r.m_aUsername[0] = '\0';
	if(pTargetAccountName)
		str_copy(r.m_aUsername, pTargetAccountName, sizeof(r.m_aUsername));
	m_Requests.push_back(r);

	const char *pClanName = "<clan>";
	if(GameServer()->Clans())
	{
		CClansData tmp;
		if(GameServer()->Clans()->GetClanSnapshotById(ClanId, tmp))
			pClanName = tmp.m_ClanName;
	}
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Are you sure you want to give ownership of clan '%s' to '%s'? Type /clan_yes or /clan_no.", pClanName, r.m_aUsername[0] ? r.m_aUsername : "<target>");
	if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		GameServer()->SendChatTarget(ClientId, aBuf);
	return r.m_Id;
}

int CRequests::CreateClanRenameConfirm(int ClientId, int ClanId, const char *pOldName, const char *pNewName, int ExpireSeconds)
{
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::ClanRenameConfirm;
	r.m_From = ClientId;
	r.m_To = ClientId;
	r.m_ClanId = ClanId;
	{
		int expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + expiry * Server()->TickSpeed();
	}
	r.m_aOldClanName[0] = '\0';
	r.m_aNewClanName[0] = '\0';
	if(pOldName)
		str_copy(r.m_aOldClanName, pOldName, sizeof(r.m_aOldClanName));
	if(pNewName)
		str_copy(r.m_aNewClanName, pNewName, sizeof(r.m_aNewClanName));
	m_Requests.push_back(r);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Are you sure you want to rename '%s' to '%s' (%d BP)? Type /clan_yes or /clan_no.", r.m_aOldClanName[0] ? r.m_aOldClanName : "<old>", r.m_aNewClanName[0] ? r.m_aNewClanName : "<new>", g_Config.m_SvClanRenamePrice);
	if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		GameServer()->SendChatTarget(ClientId, aBuf);
	return r.m_Id;
}

int CRequests::CreateClanCreateConfirm(int ClientId, const char *pNewClanName, int ExpireSeconds)
{
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::ClanCreateConfirm;
	r.m_From = ClientId;
	r.m_To = ClientId; // self-confirmation
	r.m_ClanId = 0; // not created yet
	{
		int expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + expiry * Server()->TickSpeed();
	}
	r.m_aNewClanName[0] = '\0';
	if(pNewClanName)
		str_copy(r.m_aNewClanName, pNewClanName, sizeof(r.m_aNewClanName));
	m_Requests.push_back(r);

	char aBuf[256];
	const int price = g_Config.m_SvClanCreatePrice;
	if(price > 0)
		str_format(aBuf, sizeof(aBuf), "Are you sure you want to create clan '%s' for %d BP? Type /clan_yes or /clan_no.", r.m_aNewClanName[0] ? r.m_aNewClanName : "<name>", price);
	else
		str_format(aBuf, sizeof(aBuf), "Are you sure you want to create clan '%s'? Type /clan_yes or /clan_no.", r.m_aNewClanName[0] ? r.m_aNewClanName : "<name>");
	if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		GameServer()->SendChatTarget(ClientId, aBuf);
	return r.m_Id;
}

int CRequests::CreateClanRenameNotice(int FromClient, int ToClient, int ClanId, const char *pOldName, const char *pNewName, int ExpireSeconds)
{
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::ClanRenameNotice;
	r.m_From = ToClient;
	r.m_To = ToClient;
	r.m_ClanId = ClanId;
	{
		int expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + expiry * Server()->TickSpeed();
	}
	r.m_aOldClanName[0] = '\0';
	r.m_aNewClanName[0] = '\0';
	if(pOldName)
		str_copy(r.m_aOldClanName, pOldName, sizeof(r.m_aOldClanName));
	if(pNewName)
		str_copy(r.m_aNewClanName, pNewName, sizeof(r.m_aNewClanName));
	m_Requests.push_back(r);

	// immediate notify if online
	if(CheckClientId(ToClient) && GameServer()->m_apPlayers[ToClient])
	{
		char aBuf[192];
		const char *pOld = r.m_aOldClanName[0] ? r.m_aOldClanName : "<old>";
		const char *pNew = r.m_aNewClanName[0] ? r.m_aNewClanName : "<new>";
		str_format(aBuf, sizeof(aBuf), "Clan renamed: '%s' -> '%s'", pOld, pNew);
		GameServer()->SendChatClan(ClanId, aBuf);
	}
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

		CPlayer *pFrom = GameServer()->m_apPlayers[from];
		CPlayer *pTo = GameServer()->m_apPlayers[to];
		if(!pFrom || !pTo)
			return false;
		if(wager > 0)
		{
			if(!pFrom->IsLoggedIn() || !pTo->IsLoggedIn())
			{
				GameServer()->SendChatTarget(to, "Both players must be logged in to play with a wager.");
				GameServer()->SendChatTarget(from, "Both players must be logged in to play with a wager.");
				int id = it->m_Id;
				auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
				if(eraseIt != m_Requests.end())
					m_Requests.erase(eraseIt);
				return false;
			}
			if(pFrom->GetPlayerBlockpoints() < wager || pTo->GetPlayerBlockpoints() < wager)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "Cannot start 1on1: %s doesn't have enough blockpoints for the wager.", pFrom->GetPlayerBlockpoints() < wager ? SafeClientName(GameServer(), from) : SafeClientName(GameServer(), to));
				GameServer()->SendChatTarget(from, aBuf);
				GameServer()->SendChatTarget(to, aBuf);
				int id = it->m_Id;
				auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
				if(eraseIt != m_Requests.end())
					m_Requests.erase(eraseIt);
				return false;
			}
		}

		{
			std::vector<vec2> startPositions;
			GetTilePositions(TILE_BW_1ON1_START_POS, GameServer(), startPositions);
			if(startPositions.empty())
			{
				GameServer()->SendChatTarget(from, "Cannot start 1on1: map missing 1on1 start positions.");
				GameServer()->SendChatTarget(to, "Cannot start 1on1: map missing 1on1 start positions.");
				int id = it->m_Id;
				auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
				if(eraseIt != m_Requests.end())
					m_Requests.erase(eraseIt);
				return false;
			}
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
		if(auto mgr = g_ComponentRegistry.Get<COneOnOneManager>(); mgr)
		{
			// create a new independent 1on1 match via manager with config phase
			auto match = mgr->CreateMatchWithConfig(from, to, wager);
			if(!match)
			{
				if(CheckClientId(from) && GameServer()->m_apPlayers[from])
					GameServer()->SendChatTarget(from, "Failed to start 1on1: opponent busy or no free teams available.");
				if(CheckClientId(to) && GameServer()->m_apPlayers[to])
					GameServer()->SendChatTarget(to, "Failed to start 1on1: opponent busy or no free teams available.");
			}
		}
		else
		{
			// fallback to broadcast if manager isn't available
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Starting 1on1 between %s and %s (wager %d)", SafeClientName(GameServer(), from), SafeClientName(GameServer(), to), wager);
			GameServer()->SendChatTarget(-1, aBuf);
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

	if(it->m_Type == SRequest::EType::ClanDeleteConfirm)
	{
		int clientId = it->m_From; // same as To
		int clanId = it->m_ClanId;
		CPlayer *pPl = CheckClientId(clientId) ? GameServer()->m_apPlayers[clientId] : nullptr;
		if(!pPl || !pPl->IsLoggedIn())
		{
			if(CheckClientId(clientId) && GameServer()->m_apPlayers[clientId])
				GameServer()->SendChatTarget(clientId, "You must be logged in to confirm clan deletion.");
			m_Requests.erase(it);
			return false;
		}
		if(pPl->GetClanId() != clanId || pPl->GetAuthLevel() != ClanAuthLevel::LEADER)
		{
			GameServer()->SendChatTarget(clientId, "You are no longer the leader of this clan.");
			m_Requests.erase(it);
			return false;
		}
		if(GameServer()->Clans())
		{
			GameServer()->Clans()->DeleteClan(clientId, clanId, pPl->GetAccId());
			GameServer()->SendChatTarget(clientId, "Clan deletion confirmed.");
		}
		m_Requests.erase(it);
		return true;
	}

	if(it->m_Type == SRequest::EType::ClanKickConfirm)
	{
		int clientId = it->m_From;
		int clanId = it->m_ClanId;
		CPlayer *pPl = CheckClientId(clientId) ? GameServer()->m_apPlayers[clientId] : nullptr;
		if(!pPl || !pPl->IsLoggedIn())
		{
			if(CheckClientId(clientId) && GameServer()->m_apPlayers[clientId])
				GameServer()->SendChatTarget(clientId, "You must be logged in to confirm clan kick.");
			m_Requests.erase(it);
			return false;
		}
		if(pPl->GetClanId() != clanId || pPl->GetAuthLevel() < ClanAuthLevel::COLEADER)
		{
			GameServer()->SendChatTarget(clientId, "You no longer have permission to kick from this clan.");
			m_Requests.erase(it);
			return false;
		}
		if(GameServer()->Clans())
		{
			GameServer()->Clans()->RemoveFromClan(clientId, it->m_aUsername, clanId);
			// char aBuf[192];
			// str_format(aBuf, sizeof(aBuf), "Clan kick confirmed: '%s' will be removed.", it->m_aUsername);
			// GameServer()->SendChatTarget(clientId, aBuf);
		}
		m_Requests.erase(it);
		return true;
	}

	if(it->m_Type == SRequest::EType::ClanTransferConfirm)
	{
		int clientId = it->m_From;
		int clanId = it->m_ClanId;
		CPlayer *pPl = CheckClientId(clientId) ? GameServer()->m_apPlayers[clientId] : nullptr;
		if(!pPl || !pPl->IsLoggedIn())
		{
			if(CheckClientId(clientId) && GameServer()->m_apPlayers[clientId])
				GameServer()->SendChatTarget(clientId, "You must be logged in to confirm clan transfer.");
			m_Requests.erase(it);
			return false;
		}
		if(pPl->GetClanId() != clanId || pPl->GetAuthLevel() != ClanAuthLevel::LEADER)
		{
			GameServer()->SendChatTarget(clientId, "You are no longer the leader of this clan.");
			m_Requests.erase(it);
			return false;
		}
		if(it->m_aUsername[0] == '\0')
		{
			GameServer()->SendChatTarget(clientId, "Invalid transfer target.");
			m_Requests.erase(it);
			return false;
		}
		if(GameServer()->Clans())
		{
			GameServer()->Clans()->TransferLeadership(clientId, it->m_aUsername, clanId);
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Clan transfer requested: '%s' will be made leader.", it->m_aUsername);
			GameServer()->SendChatTarget(clientId, aBuf);
		}
		m_Requests.erase(it);
		return true;
	}

	if(it->m_Type == SRequest::EType::ClanRenameConfirm)
	{
		int clientId = it->m_From;
		int clanId = it->m_ClanId;
		CPlayer *pPl = CheckClientId(clientId) ? GameServer()->m_apPlayers[clientId] : nullptr;
		if(!pPl || !pPl->IsLoggedIn())
		{
			if(CheckClientId(clientId) && GameServer()->m_apPlayers[clientId])
				GameServer()->SendChatTarget(clientId, "You must be logged in to confirm clan rename.");
			m_Requests.erase(it);
			return false;
		}
		if(pPl->GetClanId() != clanId || pPl->GetAuthLevel() != ClanAuthLevel::LEADER)
		{
			GameServer()->SendChatTarget(clientId, "You are no longer the leader of this clan.");
			m_Requests.erase(it);
			return false;
		}
		// re-validate price at accept time
		if(g_Config.m_SvClanRenamePrice > 0 && pPl->GetPlayerBlockpoints() < g_Config.m_SvClanRenamePrice)
		{
			GameServer()->SendChatTarget(clientId, "Insufficient blockpoints to complete rename.");
			m_Requests.erase(it);
			return false;
		}

		if(GameServer()->Clans())
		{
			GameServer()->Clans()->RenameClan(clientId, clanId, it->m_aNewClanName[0] ? it->m_aNewClanName : "");
			GameServer()->SendChatTarget(clientId, "Clan rename confirmed.");
		}
		m_Requests.erase(it);
		return true;
	}

	if(it->m_Type == SRequest::EType::ClanCreateConfirm)
	{
		int clientId = it->m_From;
		CPlayer *pPl = CheckClientId(clientId) ? GameServer()->m_apPlayers[clientId] : nullptr;
		if(!pPl || !pPl->IsLoggedIn())
		{
			if(CheckClientId(clientId) && GameServer()->m_apPlayers[clientId])
				GameServer()->SendChatTarget(clientId, "You must be logged in to create a clan.");
			m_Requests.erase(it);
			return false;
		}
		// re-validate level requirement and BP price
		if(pPl->GetPlayerLevel() < g_Config.m_SvClanMinLevel)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "You must be at least level %d to create a clan!", g_Config.m_SvClanMinLevel);
			GameServer()->SendChatTarget(clientId, aBuf);
			m_Requests.erase(it);
			return false;
		}
		if(g_Config.m_SvClanCreatePrice > 0 && pPl->GetPlayerBlockpoints() < g_Config.m_SvClanCreatePrice)
		{
			GameServer()->SendChatTarget(clientId, "Insufficient blockpoints to create a clan.");
			m_Requests.erase(it);
			return false;
		}
		if(GameServer()->Clans())
		{
			GameServer()->Clans()->CreateClan(clientId, it->m_aNewClanName[0] ? it->m_aNewClanName : "", pPl->GetAccId());
			GameServer()->SendChatTarget(clientId, "Clan creation confirmed.");
		}
		m_Requests.erase(it);
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

	if(it->m_Type == SRequest::EType::ClanRenameNotice)
	{
		// simple notification request; no accept flow needed. Remove it if accepted generically.
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
		if(from == to)
		{
			// shouldn't happen, but guard against self-accept
			if(toPresent)
				GameServer()->SendChatTarget(to, "You cannot accept your own transfer offer.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}
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
		// disallow accept if either is in an event
		if(GameServer()->isInEvent(from) || GameServer()->isInEvent(to))
		{
			if(toPresent)
				GameServer()->SendChatTarget(to, "Transfers are not allowed while either player is in an event.");
			if(fromPresent)
				GameServer()->SendChatTarget(from, "Your blockpoint transfer was cancelled: event restriction.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}
		// check bounds and current funds again at acceptance time
		if(amount <= 0 || amount < g_Config.m_SvBpTransferAmountMin || amount > g_Config.m_SvBpTransferAmountCap || pFrom->GetPlayerBlockpoints() < amount)
		{
			GameServer()->SendChatTarget(to, "Transfer cancelled: invalid amount or insufficient sender funds.");
			if(fromPresent)
				GameServer()->SendChatTarget(from, "Your blockpoint transfer was cancelled due to invalid amount or insufficient funds.");
			int id = it->m_Id;
			auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
			if(eraseIt != m_Requests.end())
				m_Requests.erase(eraseIt);
			return false;
		}
		// enforce daily caps at accept time (UTC day, per sender account)
		{
			const int fromAcc = pFrom->GetAccId();
			int today = CurrentUtcYyyymmdd();
			auto &ctr = m_BpDailyCounters[fromAcc];
			if(ctr.m_TodayDate != today)
			{
				ctr.m_TodayDate = today;
				ctr.m_TodayAmount = 0;
				ctr.m_TodayCount = 0;
			}
			if(g_Config.m_SvBpTransferDailyCountCap > 0 && ctr.m_TodayCount + 1 > g_Config.m_SvBpTransferDailyCountCap)
			{
				GameServer()->SendChatTarget(to, "Transfer cancelled: sender reached daily transfer count cap.");
				if(fromPresent)
					GameServer()->SendChatTarget(from, "Blockpoint transfer cancelled: daily transfer count cap reached.");
				int id2 = it->m_Id;
				auto eraseIt2 = std::find_if(m_Requests.begin(), m_Requests.end(), [id2](const SRequest &r) { return r.m_Id == id2; });
				if(eraseIt2 != m_Requests.end())
					m_Requests.erase(eraseIt2);
				return false;
			}
			if(g_Config.m_SvBpTransferDailyAmountCap > 0 && ctr.m_TodayAmount + amount > g_Config.m_SvBpTransferDailyAmountCap)
			{
				GameServer()->SendChatTarget(to, "Transfer cancelled: sender would exceed daily transfer amount cap.");
				if(fromPresent)
					GameServer()->SendChatTarget(from, "Blockpoint transfer cancelled: daily transfer amount cap would be exceeded.");
				int id2 = it->m_Id;
				auto eraseIt2 = std::find_if(m_Requests.begin(), m_Requests.end(), [id2](const SRequest &r) { return r.m_Id == id2; });
				if(eraseIt2 != m_Requests.end())
					m_Requests.erase(eraseIt2);
				return false;
			}
		}

		// remove request first to avoid any chance of re-entrancy duplicate application
		int id = it->m_Id;
		auto eraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [id](const SRequest &r) { return r.m_Id == id; });
		if(eraseIt != m_Requests.end())
			m_Requests.erase(eraseIt);

		// apply transfer atomically after removal
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

		// update daily counters for sender
		{
			const int fromAcc = pFrom->GetAccId();
			int today = CurrentUtcYyyymmdd();
			auto &ctr = m_BpDailyCounters[fromAcc];
			if(ctr.m_TodayDate != today)
			{
				ctr.m_TodayDate = today;
				ctr.m_TodayAmount = 0;
				ctr.m_TodayCount = 0;
			}
			ctr.m_TodayAmount += amount;
			ctr.m_TodayCount += 1;
		}

		// discord log for accepted transfer
		{
			CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
			const char *pUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
			if(Discord.IsConfigured(pUrl))
			{
				char aMsg[512];
				str_format(aMsg, sizeof(aMsg), "[BP] Transfer accepted: %s -> %s : %d BP | sender now %d, receiver now %d",
					SafeClientName(GameServer(), from), SafeClientName(GameServer(), to), amount, pFrom->GetPlayerBlockpoints(), pTo->GetPlayerBlockpoints());
				CDiscordWebhook::SSendOptions Opt;
				Opt.m_pWebhookUrl = pUrl;
				Discord.Send(aMsg, Opt);
			}
		}
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
		int to = it->m_To; // declining player
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

	if(it->m_Type == SRequest::EType::BlockpointTransfer)
	{
		int from = it->m_From;
		int to = it->m_To;
		if(CheckClientId(to) && GameServer()->m_apPlayers[to])
			GameServer()->SendChatTarget(to, "You declined the blockpoint transfer.");
		if(CheckClientId(from) && GameServer()->m_apPlayers[from])
			GameServer()->SendChatTarget(from, "Your blockpoint transfer was declined.");

		// discord log for decline
		{
			CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
			const char *pUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
			if(Discord.IsConfigured(pUrl))
			{
				char aMsg[512];
				str_format(aMsg, sizeof(aMsg), "[BP] Transfer declined: %s -> %s",
					SafeClientName(GameServer(), from), SafeClientName(GameServer(), to));
				CDiscordWebhook::SSendOptions Opt;
				Opt.m_pWebhookUrl = pUrl;
				Discord.Send(aMsg, Opt);
			}
		}
		m_Requests.erase(it);
		return true;
	}

	if(it->m_Type == SRequest::EType::ClanDeleteConfirm)
	{
		int clientId = it->m_From;
		if(CheckClientId(clientId) && GameServer()->m_apPlayers[clientId])
			GameServer()->SendChatTarget(clientId, "Cancelled clan deletion.");
		m_Requests.erase(it);
		return true;
	}

	if(it->m_Type == SRequest::EType::ClanKickConfirm)
	{
		int clientId = it->m_From;
		if(CheckClientId(clientId) && GameServer()->m_apPlayers[clientId])
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "Cancelled kicking '%s' from clan.", it->m_aUsername);
			GameServer()->SendChatTarget(clientId, aBuf);
		}
		m_Requests.erase(it);
		return true;
	}

	if(it->m_Type == SRequest::EType::ClanRenameNotice)
	{
		int to = it->m_To;
		if(CheckClientId(to) && GameServer()->m_apPlayers[to])
		{
			char aBuf[192];
			const char *pOld = it->m_aOldClanName[0] ? it->m_aOldClanName : "<old>";
			const char *pNew = it->m_aNewClanName[0] ? it->m_aNewClanName : "<new>";
			str_format(aBuf, sizeof(aBuf), "Dismissed: Clan renamed '%s' -> '%s'", pOld, pNew);
			GameServer()->SendChatTarget(to, aBuf);
		}
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

			// discord log for expiry
			{
				CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Http());
				const char *pUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
				if(Discord.IsConfigured(pUrl))
				{
					char aMsg[512];
					str_format(aMsg, sizeof(aMsg), "[BP] Transfer expired: %s -> %s : %d BP",
						SafeClientName(GameServer(), req.m_From), SafeClientName(GameServer(), req.m_To), req.m_Wager);
					CDiscordWebhook::SSendOptions Opt;
					Opt.m_pWebhookUrl = pUrl;
					Discord.Send(aMsg, Opt);
				}
			}
		}
		else if(req.m_Type == SRequest::EType::Clan)
		{
			const char *pToName = SafeClientName(GameServer(), req.m_To);
			const char *pFromName = SafeClientName(GameServer(), req.m_From);
			str_format(aBufFrom, sizeof(aBufFrom), "Your clan invite to '%s' has expired.", pToName);
			str_format(aBufTo, sizeof(aBufTo), "The clan invite from '%s' has expired.", pFromName);
			notifyTo = true;
		}
		else if(req.m_Type == SRequest::EType::ClanDeleteConfirm)
		{
			str_copy(aBufFrom, "Your clan deletion confirmation expired.", sizeof(aBufFrom));
		}
		else if(req.m_Type == SRequest::EType::ClanKickConfirm)
		{
			char aTmp[64];
			str_format(aTmp, sizeof(aTmp), "%s", req.m_aUsername[0] ? req.m_aUsername : "target");
			str_format(aBufFrom, sizeof(aBufFrom), "Your confirmation to kick '%s' expired.", aTmp);
		}
		else if(req.m_Type == SRequest::EType::ClanRenameConfirm)
		{
			str_copy(aBufFrom, "Clan rename confirmation expired.", sizeof(aBufFrom));
		}
		else if(req.m_Type == SRequest::EType::ClanCreateConfirm)
		{
			str_copy(aBufFrom, "Clan creation confirmation expired.", sizeof(aBufFrom));
		}
		else if(req.m_Type == SRequest::EType::ClanRenameNotice)
		{
			const char *pOld = req.m_aOldClanName[0] ? req.m_aOldClanName : "<old>";
			const char *pNew = req.m_aNewClanName[0] ? req.m_aNewClanName : "<new>";
			str_format(aBufFrom, sizeof(aBufFrom), "Clan rename notice expired: '%s' -> '%s'", pOld, pNew);
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
				str_format(aBuf, sizeof(aBuf), "A pending %s request was cancelled: %s", it->m_Type == SRequest::EType::BlockpointTransfer ? "blockpoint transfer" : it->m_Type == SRequest::EType::OneOnOne ? "1on1" : it->m_Type == SRequest::EType::Clan ? "clan" : "request",
					pReason);
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
