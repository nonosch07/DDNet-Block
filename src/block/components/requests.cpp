#include "requests.h"

#include <engine/server/server.h>
#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <block/base.h>
#include <block/components/core/component_registry.h>
#include <block/components/events.h>
#include <block/components/oneonone_manager.h>
#include <block/context.h>
#include <block/discord/webhook.h>
#include <block/util.h>

#include <algorithm>
#include <ctime>

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
	time_t Now = time(nullptr);
	struct tm Utc{};
#if defined(__unix__) || defined(__APPLE__)
	gmtime_r(&Now, &Utc);
#else
	utc = *gmtime(&now);
#endif
	return (Utc.tm_year + 1900) * 10000 + (Utc.tm_mon + 1) * 100 + Utc.tm_mday;
}

int CRequests::Create1on1Invite(int FromClient, int ToClient, int Wager, int ExpireSeconds)
{
	// dummy protection
	if(BlockIsClientsSameAddr(GameServer()->Server(), FromClient, ToClient) && g_Config.m_SvEventsTestMode == 0)
	{
		if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
			GameServer()->Block().SendChatTarget(FromClient, "You cannot invite your own dummy to a 1on1.");
		return -1;
	}

	for(const auto &Existing : m_Requests)
	{
		if(Existing.m_Type == SRequest::EType::OneOnOne && Existing.m_From == FromClient && Existing.m_To == ToClient)
		{
			int64_t TicksLeft = Existing.m_ExpireTick - Server()->Tick();
			int SecondsLeft = TicksLeft > 0 ? (int)(TicksLeft / Server()->TickSpeed()) : 0;
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "You already have a pending 1on1 invite to this player. Expires in %d second%s.", SecondsLeft, SecondsLeft != 1 ? "s" : "");
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
				GameServer()->Block().SendChatTarget(FromClient, aBuf);
			return -1;
		}
	}

	if(GameServer()->Block().isInEvent(FromClient) || GameServer()->Block().isInEvent(ToClient))
	{
		if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
			GameServer()->Block().SendChatTarget(FromClient, "Cannot send 1on1 request: one of the players is in an event.");
		return -1;
	}

	// Anti-spam: limit how often a player can send any 1on1 invite and how many are outstanding.
	CPlayer *pFrom = GameServer()->m_apPlayers[FromClient];
	if(pFrom)
	{
		const int InviteCooldownSeconds = g_Config.m_Sv1on1InviteCooldown;
		int64_t Now = Server()->Tick();
		if(pFrom->Block().m_Last1on1InviteTick != 0 && Now - pFrom->Block().m_Last1on1InviteTick < InviteCooldownSeconds * Server()->TickSpeed())
		{
			int Remaining = (int)((InviteCooldownSeconds * Server()->TickSpeed() - (Now - pFrom->Block().m_Last1on1InviteTick)) / Server()->TickSpeed());
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another 1on1 invite.", Remaining, Remaining != 1 ? "s" : "");
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
				GameServer()->Block().SendChatTarget(FromClient, aBuf);
			return -1;
		}

		int Outstanding = 0;
		for(const auto &r : m_Requests)
		{
			if(r.m_Type == SRequest::EType::OneOnOne && r.m_From == FromClient)
				Outstanding++;
		}
		const int MaxOutstanding = g_Config.m_Sv1on1MaxOutstandingInvitesPerSender;
		if(Outstanding >= MaxOutstanding)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "You already have %d outstanding 1on1 invite%s. Please wait.", Outstanding, Outstanding != 1 ? "s" : "");
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
				GameServer()->Block().SendChatTarget(FromClient, aBuf);
			return -1;
		}
	}

	int ExpiryCfg = g_Config.m_Sv1on1InviteExpiry > 0 ? g_Config.m_Sv1on1InviteExpiry : ExpireSeconds;
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::OneOnOne;
	r.m_From = FromClient;
	r.m_To = ToClient;
	r.m_Wager = Wager;
	r.m_ExpireTick = Server()->Tick() + ExpiryCfg * Server()->TickSpeed();
	m_Requests.push_back(r);

	// record last invite tick on the sender for cooldown enforcement
	if(pFrom)
		pFrom->Block().m_Last1on1InviteTick = Server()->Tick();

	char aBuf[256];
	const char *pFromName = SafeClientName(GameServer(), FromClient);
	str_format(aBuf, sizeof(aBuf), "%s challenged you for a 1on1! Use /accept or /decline to respond. (%d BP)", pFromName, Wager);
	if(CheckClientId(ToClient) && GameServer()->m_apPlayers[ToClient])
		GameServer()->Block().SendChatTarget(ToClient, aBuf);
	return r.m_Id;
}

int CRequests::CreateShopRequest(int OwnerClient, int Category, int ItemId, int Price, int ExpireSeconds)
{
	int ExpiryCfg = g_Config.m_SvShopRequestExpiry > 0 ? g_Config.m_SvShopRequestExpiry : ExpireSeconds;
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::Shop;
	r.m_From = OwnerClient;
	r.m_To = OwnerClient; // owner/operator
	r.m_Category = Category;
	r.m_Item = ItemId;
	r.m_ExpireTick = Server()->Tick() + ExpiryCfg * Server()->TickSpeed();
	m_Requests.push_back(r);
	return r.m_Id;
}

int CRequests::CreateClanInvite(int FromClient, int ToClient, int ClanId, int ExpireSeconds)
{
	for(const auto &Existing : m_Requests)
	{
		if(Existing.m_Type == SRequest::EType::Clan && Existing.m_From == FromClient && Existing.m_To == ToClient && Existing.m_ClanId == ClanId)
		{
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
			{
				GameServer()->Block().SendChatTarget(FromClient, "You already have a pending clan invite to this player.");
			}
			return -1;
		}
	}

	int ExpiryCfg = g_Config.m_SvClanInviteExpiry > 0 ? g_Config.m_SvClanInviteExpiry : ExpireSeconds;
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::Clan;
	r.m_From = FromClient; // issuer (clan leader/co-leader)
	r.m_To = ToClient; // target player
	r.m_ClanId = ClanId;
	r.m_ExpireTick = Server()->Tick() + ExpiryCfg * Server()->TickSpeed();
	m_Requests.push_back(r);

	const char *pFromName = SafeClientName(GameServer(), FromClient);
	char aBuf[256];
	const char *pClanName = "<clan>";
	if(GameServer()->Block().Clans())
	{
		CClansData Tmp;
		if(GameServer()->Block().Clans()->GetClanSnapshotById(ClanId, Tmp))
			pClanName = Tmp.m_ClanName;
	}
	str_format(aBuf, sizeof(aBuf), "%s invited you to join clan '%s'. Use /clan_accept or /clan_decline.", pFromName, pClanName);
	if(CheckClientId(ToClient) && GameServer()->m_apPlayers[ToClient])
		GameServer()->Block().SendChatTarget(ToClient, aBuf);

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
	if(!pFrom->Block().IsLoggedIn() || !pTo->Block().IsLoggedIn())
	{
		if(pFrom)
			GameServer()->Block().SendChatTarget(FromClient, "You and the target must both be logged in.");
		return -1;
	}
	if(Amount <= 0)
	{
		GameServer()->Block().SendChatTarget(FromClient, "Amount must be greater than zero.");
		return -1;
	}
	if(Amount < g_Config.m_SvBpTransferAmountMin)
	{
		GameServer()->Block().SendChatTarget(FromClient, "Amount below minimum transfer threshold.");
		return -1;
	}
	if(Amount > g_Config.m_SvBpTransferAmountCap)
	{
		GameServer()->Block().SendChatTarget(FromClient, "Amount exceeds max cap.");
		return -1;
	}
	if(GameServer()->Block().isInEvent(FromClient) || GameServer()->Block().isInEvent(ToClient))
	{
		GameServer()->Block().SendChatTarget(FromClient, "Transfers are not allowed while either player is in an event.");
		return -1;
	}
	int Cooldown = g_Config.m_SvBpTransferCooldown;
	if(Cooldown > 0 && pFrom->Block().m_LastBpTransferOfferTick != 0 && Server()->Tick() - pFrom->Block().m_LastBpTransferOfferTick < Cooldown * Server()->TickSpeed())
	{
		int Rem = (int)((Cooldown * Server()->TickSpeed() - (Server()->Tick() - pFrom->Block().m_LastBpTransferOfferTick)) / Server()->TickSpeed());
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another transfer.", Rem, Rem != 1 ? "s" : "");
		GameServer()->Block().SendChatTarget(FromClient, aBuf);
		return -1;
	}

	// cap outstanding offers per sender (only those from this sender to anyone)
	{
		int Outstanding = 0;
		for(const auto &r : m_Requests)
		{
			if(r.m_Type == SRequest::EType::BlockpointTransfer && r.m_From == FromClient)
				Outstanding++;
		}
		if(Outstanding >= g_Config.m_SvBpTransferMaxOutstandingPerSender)
		{
			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), "You already have %d outstanding BP transfer offer%s.", Outstanding, Outstanding != 1 ? "s" : "");
			GameServer()->Block().SendChatTarget(FromClient, aBuf);
			return -1;
		}
	}
	// sufficient funds at offer time to reduce failed accepts later
	if(pFrom->Block().GetPlayerBlockpoints() < Amount)
	{
		GameServer()->Block().SendChatTarget(FromClient, "You don't have enough blockpoints.");
		return -1;
	}

	// block if already at cap
	{
		const int AccId = pFrom->Block().GetAccId();
		if(AccId > 0)
		{
			int Today = CurrentUtcYyyymmdd();
			auto &Ctr = m_BpDailyCounters[AccId];
			if(Ctr.m_TodayDate != Today)
			{
				Ctr.m_TodayDate = Today;
				Ctr.m_TodayAmount = 0;
				Ctr.m_TodayCount = 0;
			}
			if(g_Config.m_SvBpTransferDailyCountCap > 0 && Ctr.m_TodayCount >= g_Config.m_SvBpTransferDailyCountCap)
			{
				GameServer()->Block().SendChatTarget(FromClient, "Daily transfer count cap reached. Try again tomorrow.");
				return -1;
			}
			if(g_Config.m_SvBpTransferDailyAmountCap > 0 && Ctr.m_TodayAmount >= g_Config.m_SvBpTransferDailyAmountCap)
			{
				GameServer()->Block().SendChatTarget(FromClient, "Daily transfer amount cap reached. Try again tomorrow.");
				return -1;
			}
		}
	}

	// prevent duplicate pending transfer (same pair, same amount) to avoid spam
	for(const auto &Existing : m_Requests)
	{
		if(Existing.m_Type == SRequest::EType::BlockpointTransfer && Existing.m_From == FromClient && Existing.m_To == ToClient && Existing.m_Wager == Amount)
		{
			int64_t TicksLeft = Existing.m_ExpireTick - Server()->Tick();
			int SecondsLeft = TicksLeft > 0 ? (int)(TicksLeft / Server()->TickSpeed()) : 0;
			if(CheckClientId(FromClient) && GameServer()->m_apPlayers[FromClient])
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "You already have a pending BP transfer to this player (%d BP). Expires in %d second%s.", Amount, SecondsLeft, SecondsLeft != 1 ? "s" : "");
				GameServer()->Block().SendChatTarget(FromClient, aBuf);
			}
			return -1;
		}
	}

	int ExpiryCfg = g_Config.m_SvBpTransferExpiry > 0 ? g_Config.m_SvBpTransferExpiry : ExpireSeconds;
	SRequest r;
	r.m_Id = NextId();
	r.m_Type = SRequest::EType::BlockpointTransfer;
	r.m_From = FromClient;
	r.m_To = ToClient;
	r.m_Wager = Amount; // reuse field
	r.m_ExpireTick = Server()->Tick() + ExpiryCfg * Server()->TickSpeed();
	m_Requests.push_back(r);

	const char *pFromName = SafeClientName(GameServer(), FromClient);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s wants to send you %d blockpoints. Use /accept_bp or /decline_bp.", pFromName, Amount);
	if(CheckClientId(ToClient) && GameServer()->m_apPlayers[ToClient])
		GameServer()->Block().SendChatTarget(ToClient, aBuf);

	// mark last offer for cooldown enforcement
	pFrom->Block().m_LastBpTransferOfferTick = Server()->Tick();
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
		int Expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + Expiry * Server()->TickSpeed();
	}
	m_Requests.push_back(r);

	// notify
	const char *pClanName = "<clan>";
	if(GameServer()->Block().Clans())
	{
		CClansData Tmp;
		if(GameServer()->Block().Clans()->GetClanSnapshotById(ClanId, Tmp))
			pClanName = Tmp.m_ClanName;
	}
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Are you sure you want to delete clan '%s'? Type /clan_yes or /clan_no.", pClanName);
	if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		GameServer()->Block().SendChatTarget(ClientId, aBuf);
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
		int Expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + Expiry * Server()->TickSpeed();
	}
	r.m_aUsername[0] = '\0';
	if(pTargetAccountName)
		str_copy(r.m_aUsername, pTargetAccountName, sizeof(r.m_aUsername));
	m_Requests.push_back(r);

	const char *pClanName = "<clan>";
	if(GameServer()->Block().Clans())
	{
		CClansData Tmp;
		if(GameServer()->Block().Clans()->GetClanSnapshotById(ClanId, Tmp))
			pClanName = Tmp.m_ClanName;
	}
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Are you sure you want to kick '%s' from clan '%s'? Type /clan_yes or /clan_no.", r.m_aUsername[0] ? r.m_aUsername : "<unknown>", pClanName);
	if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		GameServer()->Block().SendChatTarget(ClientId, aBuf);
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
		int Expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + Expiry * Server()->TickSpeed();
	}
	r.m_aUsername[0] = '\0';
	if(pTargetAccountName)
		str_copy(r.m_aUsername, pTargetAccountName, sizeof(r.m_aUsername));
	m_Requests.push_back(r);

	const char *pClanName = "<clan>";
	if(GameServer()->Block().Clans())
	{
		CClansData Tmp;
		if(GameServer()->Block().Clans()->GetClanSnapshotById(ClanId, Tmp))
			pClanName = Tmp.m_ClanName;
	}
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Are you sure you want to give ownership of clan '%s' to '%s'? Type /clan_yes or /clan_no.", pClanName, r.m_aUsername[0] ? r.m_aUsername : "<target>");
	if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		GameServer()->Block().SendChatTarget(ClientId, aBuf);
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
		int Expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + Expiry * Server()->TickSpeed();
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
		GameServer()->Block().SendChatTarget(ClientId, aBuf);
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
		int Expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + Expiry * Server()->TickSpeed();
	}
	r.m_aNewClanName[0] = '\0';
	if(pNewClanName)
		str_copy(r.m_aNewClanName, pNewClanName, sizeof(r.m_aNewClanName));
	m_Requests.push_back(r);

	char aBuf[256];
	const int Price = g_Config.m_SvClanCreatePrice;
	if(Price > 0)
		str_format(aBuf, sizeof(aBuf), "Are you sure you want to create clan '%s' for %d BP? Type /clan_yes or /clan_no.", r.m_aNewClanName[0] ? r.m_aNewClanName : "<name>", Price);
	else
		str_format(aBuf, sizeof(aBuf), "Are you sure you want to create clan '%s'? Type /clan_yes or /clan_no.", r.m_aNewClanName[0] ? r.m_aNewClanName : "<name>");
	if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		GameServer()->Block().SendChatTarget(ClientId, aBuf);
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
		int Expiry = ExpireSeconds > 0 ? ExpireSeconds : g_Config.m_SvClanConfirmExpiry;
		r.m_ExpireTick = Server()->Tick() + Expiry * Server()->TickSpeed();
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
		GameServer()->Block().SendChatClan(ClanId, aBuf);
	}
	return r.m_Id;
}

bool CRequests::AcceptRequest(int RequestId)
{
	auto It = std::find_if(m_Requests.begin(), m_Requests.end(), [RequestId](const SRequest &r) { return r.m_Id == RequestId; });
	if(It == m_Requests.end())
		return false;
	if(It->m_Type == SRequest::EType::OneOnOne)
	{
		// start a 1on1 match via new component-based event system
		int From = It->m_From;
		int To = It->m_To;
		int Wager = It->m_Wager;

		bool FromPresent = CheckClientId(From) && GameServer()->m_apPlayers[From];
		bool ToPresent = CheckClientId(To) && GameServer()->m_apPlayers[To];
		if(!FromPresent || !ToPresent)
		{
			if(FromPresent)
				GameServer()->Block().SendChatTarget(From, "Your 1on1 could not be started because the other player disconnected.");
			if(ToPresent)
				GameServer()->Block().SendChatTarget(To, "The 1on1 you tried to accept could not be started because the other player disconnected.");
			// Erase by id after handling
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}

		if(GameServer()->Block().isInEvent(From) || GameServer()->Block().isInEvent(To))
		{
			GameServer()->Block().SendChatTarget(To, "Cannot start 1on1: one of the players is already in an event.");
			GameServer()->Block().SendChatTarget(From, "Your 1on1 could not be started because a player is in another event.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}

		CPlayer *pFrom = GameServer()->m_apPlayers[From];
		CPlayer *pTo = GameServer()->m_apPlayers[To];
		if(!pFrom || !pTo)
			return false;
		if(Wager > 0)
		{
			if(!pFrom->Block().IsLoggedIn() || !pTo->Block().IsLoggedIn())
			{
				GameServer()->Block().SendChatTarget(To, "Both players must be logged in to play with a wager.");
				GameServer()->Block().SendChatTarget(From, "Both players must be logged in to play with a wager.");
				int Id = It->m_Id;
				auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
				if(EraseIt != m_Requests.end())
					m_Requests.erase(EraseIt);
				return false;
			}
			if(pFrom->Block().GetPlayerBlockpoints() < Wager || pTo->Block().GetPlayerBlockpoints() < Wager)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "Cannot start 1on1: %s doesn't have enough blockpoints for the wager.", pFrom->Block().GetPlayerBlockpoints() < Wager ? SafeClientName(GameServer(), From) : SafeClientName(GameServer(), To));
				GameServer()->Block().SendChatTarget(From, aBuf);
				GameServer()->Block().SendChatTarget(To, aBuf);
				int Id = It->m_Id;
				auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
				if(EraseIt != m_Requests.end())
					m_Requests.erase(EraseIt);
				return false;
			}
		}

		{
			bool HasArenas = GameServer()->Block().ZoneManager()->Get1on1ArenaCount() > 0;
			if(!HasArenas)
			{
				GameServer()->Block().SendChatTarget(From, "Cannot start 1on1: map is missing 1on1 arena zones.");
				GameServer()->Block().SendChatTarget(To, "Cannot start 1on1: map is missing 1on1 arena zones.");
				int Id = It->m_Id;
				auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
				if(EraseIt != m_Requests.end())
					m_Requests.erase(EraseIt);
				return false;
			}
		}

		// notify both parties of acceptance before starting the event
		if(CheckClientId(From) && GameServer()->m_apPlayers[From])
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "%s accepted your 1on1 challenge%s%s", SafeClientName(GameServer(), To), Wager > 0 ? " (wager " : "", Wager > 0 ? std::to_string(Wager).c_str() : "");
			if(Wager > 0)
			{
				int Len = str_length(aBuf);
				if(Len < (int)sizeof(aBuf) - 2)
					str_append(aBuf, ")", sizeof(aBuf));
			}
			GameServer()->Block().SendChatTarget(From, aBuf);
		}
		if(CheckClientId(To) && GameServer()->m_apPlayers[To])
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "You accepted the 1on1 challenge from %s%s%s", SafeClientName(GameServer(), From), Wager > 0 ? " (wager " : "", Wager > 0 ? std::to_string(Wager).c_str() : "");
			if(Wager > 0)
			{
				int Len = str_length(aBuf);
				if(Len < (int)sizeof(aBuf) - 2)
					str_append(aBuf, ")", sizeof(aBuf));
			}
			GameServer()->Block().SendChatTarget(To, aBuf);
		}
		if(auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>(); Mgr)
		{
			// create a new independent 1on1 match via manager with config phase
			auto Match = Mgr->CreateMatchWithConfig(From, To, Wager);
			if(!Match)
			{
				if(CheckClientId(From) && GameServer()->m_apPlayers[From])
					GameServer()->Block().SendChatTarget(From, "Failed to start 1on1: opponent busy or no free teams available.");
				if(CheckClientId(To) && GameServer()->m_apPlayers[To])
					GameServer()->Block().SendChatTarget(To, "Failed to start 1on1: opponent busy or no free teams available.");
			}
		}
		else
		{
			// fallback to broadcast if manager isn't available
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Starting 1on1 between %s and %s (wager %d)", SafeClientName(GameServer(), From), SafeClientName(GameServer(), To), Wager);
			GameServer()->Block().SendChatTarget(-1, aBuf);
		}

		// success path: remove the request by id
		{
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
		}
		return true;
	}

	if(It->m_Type == SRequest::EType::ClanDeleteConfirm)
	{
		int ClientId = It->m_From; // same as To
		int ClanId = It->m_ClanId;
		CPlayer *pPl = CheckClientId(ClientId) ? GameServer()->m_apPlayers[ClientId] : nullptr;
		if(!pPl || !pPl->Block().IsLoggedIn())
		{
			if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
				GameServer()->Block().SendChatTarget(ClientId, "You must be logged in to confirm clan deletion.");
			m_Requests.erase(It);
			return false;
		}
		if(pPl->Block().GetClanId() != ClanId || pPl->Block().GetAuthLevel() != ClanAuthLevel::LEADER)
		{
			GameServer()->Block().SendChatTarget(ClientId, "You are no longer the leader of this clan.");
			m_Requests.erase(It);
			return false;
		}
		if(GameServer()->Block().Clans())
		{
			GameServer()->Block().Clans()->DeleteClan(ClientId, ClanId, pPl->Block().GetAccId());
			GameServer()->Block().SendChatTarget(ClientId, "Clan deletion confirmed.");
		}
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::ClanKickConfirm)
	{
		int ClientId = It->m_From;
		int ClanId = It->m_ClanId;
		CPlayer *pPl = CheckClientId(ClientId) ? GameServer()->m_apPlayers[ClientId] : nullptr;
		if(!pPl || !pPl->Block().IsLoggedIn())
		{
			if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
				GameServer()->Block().SendChatTarget(ClientId, "You must be logged in to confirm clan kick.");
			m_Requests.erase(It);
			return false;
		}
		if(pPl->Block().GetClanId() != ClanId || pPl->Block().GetAuthLevel() < ClanAuthLevel::COLEADER)
		{
			GameServer()->Block().SendChatTarget(ClientId, "You no longer have permission to kick from this clan.");
			m_Requests.erase(It);
			return false;
		}
		if(GameServer()->Block().Clans())
		{
			GameServer()->Block().Clans()->RemoveFromClan(ClientId, It->m_aUsername, ClanId);
			// char aBuf[192];
			// str_format(aBuf, sizeof(aBuf), "Clan kick confirmed: '%s' will be removed.", it->m_aUsername);
			// GameServer()->Block().SendChatTarget(clientId, aBuf);
		}
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::ClanTransferConfirm)
	{
		int ClientId = It->m_From;
		int ClanId = It->m_ClanId;
		CPlayer *pPl = CheckClientId(ClientId) ? GameServer()->m_apPlayers[ClientId] : nullptr;
		if(!pPl || !pPl->Block().IsLoggedIn())
		{
			if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
				GameServer()->Block().SendChatTarget(ClientId, "You must be logged in to confirm clan transfer.");
			m_Requests.erase(It);
			return false;
		}
		if(pPl->Block().GetClanId() != ClanId || pPl->Block().GetAuthLevel() != ClanAuthLevel::LEADER)
		{
			GameServer()->Block().SendChatTarget(ClientId, "You are no longer the leader of this clan.");
			m_Requests.erase(It);
			return false;
		}
		if(It->m_aUsername[0] == '\0')
		{
			GameServer()->Block().SendChatTarget(ClientId, "Invalid transfer target.");
			m_Requests.erase(It);
			return false;
		}
		if(GameServer()->Block().Clans())
		{
			GameServer()->Block().Clans()->TransferLeadership(ClientId, It->m_aUsername, ClanId);
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Clan transfer requested: '%s' will be made leader.", It->m_aUsername);
			GameServer()->Block().SendChatTarget(ClientId, aBuf);
		}
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::ClanRenameConfirm)
	{
		int ClientId = It->m_From;
		int ClanId = It->m_ClanId;
		CPlayer *pPl = CheckClientId(ClientId) ? GameServer()->m_apPlayers[ClientId] : nullptr;
		if(!pPl || !pPl->Block().IsLoggedIn())
		{
			if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
				GameServer()->Block().SendChatTarget(ClientId, "You must be logged in to confirm clan rename.");
			m_Requests.erase(It);
			return false;
		}
		if(pPl->Block().GetClanId() != ClanId || pPl->Block().GetAuthLevel() != ClanAuthLevel::LEADER)
		{
			GameServer()->Block().SendChatTarget(ClientId, "You are no longer the leader of this clan.");
			m_Requests.erase(It);
			return false;
		}
		// re-validate price at accept time
		if(g_Config.m_SvClanRenamePrice > 0 && pPl->Block().GetPlayerBlockpoints() < g_Config.m_SvClanRenamePrice)
		{
			GameServer()->Block().SendChatTarget(ClientId, "Insufficient blockpoints to complete rename.");
			m_Requests.erase(It);
			return false;
		}

		if(GameServer()->Block().Clans())
		{
			GameServer()->Block().Clans()->RenameClan(ClientId, ClanId, It->m_aNewClanName[0] ? It->m_aNewClanName : "");
			GameServer()->Block().SendChatTarget(ClientId, "Clan rename confirmed.");
		}
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::ClanCreateConfirm)
	{
		int ClientId = It->m_From;
		CPlayer *pPl = CheckClientId(ClientId) ? GameServer()->m_apPlayers[ClientId] : nullptr;
		if(!pPl || !pPl->Block().IsLoggedIn())
		{
			if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
				GameServer()->Block().SendChatTarget(ClientId, "You must be logged in to create a clan.");
			m_Requests.erase(It);
			return false;
		}
		// re-validate level requirement and BP price
		if(pPl->Block().GetPlayerLevel() < g_Config.m_SvClanMinLevel)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "You must be at least level %d to create a clan!", g_Config.m_SvClanMinLevel);
			GameServer()->Block().SendChatTarget(ClientId, aBuf);
			m_Requests.erase(It);
			return false;
		}
		if(g_Config.m_SvClanCreatePrice > 0 && pPl->Block().GetPlayerBlockpoints() < g_Config.m_SvClanCreatePrice)
		{
			GameServer()->Block().SendChatTarget(ClientId, "Insufficient blockpoints to create a clan.");
			m_Requests.erase(It);
			return false;
		}
		if(GameServer()->Block().Clans())
		{
			GameServer()->Block().Clans()->CreateClan(ClientId, It->m_aNewClanName[0] ? It->m_aNewClanName : "", pPl->Block().GetAccId());
			GameServer()->Block().SendChatTarget(ClientId, "Clan creation confirmed.");
		}
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::Clan)
	{
		// clan invite accept flow
		int From = It->m_From; // issuer
		int To = It->m_To; // accepting player
		int ClanId = It->m_ClanId;

		bool FromPresent = CheckClientId(From) && GameServer()->m_apPlayers[From];
		bool ToPresent = CheckClientId(To) && GameServer()->m_apPlayers[To];
		if(!FromPresent || !ToPresent)
		{
			if(FromPresent)
				GameServer()->Block().SendChatTarget(From, "Your clan invite could not be accepted because the other player disconnected.");
			if(ToPresent)
				GameServer()->Block().SendChatTarget(To, "You could not accept the clan invite because the inviter disconnected.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}

		CPlayer *pTo = GameServer()->m_apPlayers[To];
		CPlayer *pFrom = GameServer()->m_apPlayers[From];
		if(!pTo || !pFrom)
		{
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}

		if(pTo->Block().GetClanId() != 0)
		{
			GameServer()->Block().SendChatTarget(To, "You are already in a clan.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}

		if(!GameServer()->Block().Clans() || !GameServer()->Block().Clans()->IsClanJoinable(ClanId))
		{
			GameServer()->Block().SendChatTarget(To, "Clan is no longer joinable.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}

		if(pFrom->Block().GetClanId() != ClanId || pFrom->Block().GetAuthLevel() < ClanAuthLevel::COLEADER)
		{
			if(CheckClientId(To) && GameServer()->m_apPlayers[To])
				GameServer()->Block().SendChatTarget(To, "Invite no longer valid: inviter lost sufficient clan rights.");
			if(CheckClientId(From) && GameServer()->m_apPlayers[From])
				GameServer()->Block().SendChatTarget(From, "Your pending clan invite was invalidated due to insufficient rights.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}

		if(!GameServer()->Block().Clans())
		{
			dbg_msg("clan", "AcceptRequest: Clans() subsystem unavailable while accepting invite (from=%d to=%d clan=%d)", From, To, ClanId);
			if(CheckClientId(To) && GameServer()->m_apPlayers[To])
				GameServer()->Block().SendChatTarget(To, "Clan system unavailable. Try again later.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}

		GameServer()->Block().Clans()->AssignClan(From, pTo->Block().m_Account.m_aName, ClanId, pTo->Block().GetAccId());

		if(CheckClientId(From) && GameServer()->m_apPlayers[From])
			GameServer()->Block().SendChatTarget(From, "Your clan invite was accepted. Assigning player...");

		int Id = It->m_Id;
		auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
		if(EraseIt != m_Requests.end())
			m_Requests.erase(EraseIt);

		return true;
	}

	if(It->m_Type == SRequest::EType::ClanRenameNotice)
	{
		// simple notification request; no accept flow needed. Remove it if accepted generically.
		int Id = It->m_Id;
		auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
		if(EraseIt != m_Requests.end())
			m_Requests.erase(EraseIt);
		return true;
	}

	if(It->m_Type == SRequest::EType::BlockpointTransfer)
	{
		int From = It->m_From;
		int To = It->m_To;
		int Amount = It->m_Wager;
		bool FromPresent = CheckClientId(From) && GameServer()->m_apPlayers[From];
		bool ToPresent = CheckClientId(To) && GameServer()->m_apPlayers[To];
		CPlayer *pFrom = FromPresent ? GameServer()->m_apPlayers[From] : nullptr;
		CPlayer *pTo = ToPresent ? GameServer()->m_apPlayers[To] : nullptr;
		if(From == To)
		{
			// shouldn't happen, but guard against self-accept
			if(ToPresent)
				GameServer()->Block().SendChatTarget(To, "You cannot accept your own transfer offer.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}
		if(!FromPresent || !ToPresent || !pFrom || !pTo || !pFrom->Block().IsLoggedIn() || !pTo->Block().IsLoggedIn())
		{
			if(ToPresent)
				GameServer()->Block().SendChatTarget(To, "Transfer failed: one of the players disconnected or is not logged in.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}
		// disallow accept if either is in an event
		if(GameServer()->Block().isInEvent(From) || GameServer()->Block().isInEvent(To))
		{
			if(ToPresent)
				GameServer()->Block().SendChatTarget(To, "Transfers are not allowed while either player is in an event.");
			if(FromPresent)
				GameServer()->Block().SendChatTarget(From, "Your blockpoint transfer was cancelled: event restriction.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}
		// check bounds and current funds again at acceptance time
		if(Amount <= 0 || Amount < g_Config.m_SvBpTransferAmountMin || Amount > g_Config.m_SvBpTransferAmountCap || pFrom->Block().GetPlayerBlockpoints() < Amount)
		{
			GameServer()->Block().SendChatTarget(To, "Transfer cancelled: invalid amount or insufficient sender funds.");
			if(FromPresent)
				GameServer()->Block().SendChatTarget(From, "Your blockpoint transfer was cancelled due to invalid amount or insufficient funds.");
			int Id = It->m_Id;
			auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
			if(EraseIt != m_Requests.end())
				m_Requests.erase(EraseIt);
			return false;
		}
		// enforce daily caps at accept time (UTC day, per sender account)
		{
			const int FromAcc = pFrom->Block().GetAccId();
			int Today = CurrentUtcYyyymmdd();
			auto &Ctr = m_BpDailyCounters[FromAcc];
			if(Ctr.m_TodayDate != Today)
			{
				Ctr.m_TodayDate = Today;
				Ctr.m_TodayAmount = 0;
				Ctr.m_TodayCount = 0;
			}
			if(g_Config.m_SvBpTransferDailyCountCap > 0 && Ctr.m_TodayCount + 1 > g_Config.m_SvBpTransferDailyCountCap)
			{
				GameServer()->Block().SendChatTarget(To, "Transfer cancelled: sender reached daily transfer count cap.");
				if(FromPresent)
					GameServer()->Block().SendChatTarget(From, "Blockpoint transfer cancelled: daily transfer count cap reached.");
				int Id2 = It->m_Id;
				auto EraseIt2 = std::find_if(m_Requests.begin(), m_Requests.end(), [Id2](const SRequest &r) { return r.m_Id == Id2; });
				if(EraseIt2 != m_Requests.end())
					m_Requests.erase(EraseIt2);
				return false;
			}
			if(g_Config.m_SvBpTransferDailyAmountCap > 0 && Ctr.m_TodayAmount + Amount > g_Config.m_SvBpTransferDailyAmountCap)
			{
				GameServer()->Block().SendChatTarget(To, "Transfer cancelled: sender would exceed daily transfer amount cap.");
				if(FromPresent)
					GameServer()->Block().SendChatTarget(From, "Blockpoint transfer cancelled: daily transfer amount cap would be exceeded.");
				int Id2 = It->m_Id;
				auto EraseIt2 = std::find_if(m_Requests.begin(), m_Requests.end(), [Id2](const SRequest &r) { return r.m_Id == Id2; });
				if(EraseIt2 != m_Requests.end())
					m_Requests.erase(EraseIt2);
				return false;
			}
		}

		// remove request first to avoid any chance of re-entrancy duplicate application
		int Id = It->m_Id;
		auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
		if(EraseIt != m_Requests.end())
			m_Requests.erase(EraseIt);

		// apply transfer atomically after removal
		pFrom->Block().SetPlayerBlockpoints(pFrom->Block().GetPlayerBlockpoints() - Amount);
		pTo->Block().SetPlayerBlockpoints(pTo->Block().GetPlayerBlockpoints() + Amount);
		GameServer()->Block().Accounts()->Save(From, &pFrom->Block().m_Account);
		GameServer()->Block().Accounts()->Save(To, &pTo->Block().m_Account);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You sent %d blockpoints to %s (now %d)", Amount, SafeClientName(GameServer(), To), pFrom->Block().GetPlayerBlockpoints());
		GameServer()->Block().SendChatTarget(From, aBuf);
		str_format(aBuf, sizeof(aBuf), "You received %d blockpoints from %s (now %d)", Amount, SafeClientName(GameServer(), From), pTo->Block().GetPlayerBlockpoints());
		GameServer()->Block().SendChatTarget(To, aBuf);
		dbg_msg("bp_transfer", "transfer id=%d from=%d to=%d amount=%d", It->m_Id, From, To, Amount);

		// update daily counters for sender
		{
			const int FromAcc = pFrom->Block().GetAccId();
			int Today = CurrentUtcYyyymmdd();
			auto &Ctr = m_BpDailyCounters[FromAcc];
			if(Ctr.m_TodayDate != Today)
			{
				Ctr.m_TodayDate = Today;
				Ctr.m_TodayAmount = 0;
				Ctr.m_TodayCount = 0;
			}
			Ctr.m_TodayAmount += Amount;
			Ctr.m_TodayCount += 1;
		}

		// discord log for accepted transfer
		{
			CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Block().Http());
			const char *pUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
			if(Discord.IsConfigured(pUrl))
			{
				char aMsg[512];
				str_format(aMsg, sizeof(aMsg), "[BP] Transfer accepted: %s -> %s : %d BP | sender now %d, receiver now %d",
					SafeClientName(GameServer(), From), SafeClientName(GameServer(), To), Amount, pFrom->Block().GetPlayerBlockpoints(), pTo->Block().GetPlayerBlockpoints());
				CDiscordWebhook::SSendOptions Opt;
				Opt.m_pWebhookUrl = pUrl;
				Discord.Send(aMsg, Opt);
			}
		}
		return true;
	}

	// unknown/other request types: just erase and return false
	{
		int Id = It->m_Id;
		auto EraseIt = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
		if(EraseIt != m_Requests.end())
			m_Requests.erase(EraseIt);
	}
	return false;
}

bool CRequests::DeclineRequest(int RequestId)
{
	auto It = std::find_if(m_Requests.begin(), m_Requests.end(), [RequestId](const SRequest &r) { return r.m_Id == RequestId; });
	if(It == m_Requests.end())
		return false;

	if(It->m_Type == SRequest::EType::OneOnOne)
	{
		int From = It->m_From; // inviter
		int To = It->m_To; // declining player
		if(CheckClientId(To) && GameServer()->m_apPlayers[To])
			GameServer()->Block().SendChatTarget(To, "You declined the 1on1 challenge.");
		if(CheckClientId(From) && GameServer()->m_apPlayers[From])
		{
			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), "Your 1on1 challenge to %s was declined.", SafeClientName(GameServer(), To));
			GameServer()->Block().SendChatTarget(From, aBuf);
		}
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::Clan)
	{
		int From = It->m_From;
		int To = It->m_To;
		if(CheckClientId(To) && GameServer()->m_apPlayers[To])
			GameServer()->Block().SendChatTarget(To, "You have declined the clan invitation.");
		if(CheckClientId(From) && GameServer()->m_apPlayers[From])
			GameServer()->Block().SendChatTarget(From, "The clan invitation was declined.");
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::BlockpointTransfer)
	{
		int From = It->m_From;
		int To = It->m_To;
		if(CheckClientId(To) && GameServer()->m_apPlayers[To])
			GameServer()->Block().SendChatTarget(To, "You declined the blockpoint transfer.");
		if(CheckClientId(From) && GameServer()->m_apPlayers[From])
			GameServer()->Block().SendChatTarget(From, "Your blockpoint transfer was declined.");

		// discord log for decline
		{
			CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Block().Http());
			const char *pUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
			if(Discord.IsConfigured(pUrl))
			{
				char aMsg[512];
				str_format(aMsg, sizeof(aMsg), "[BP] Transfer declined: %s -> %s",
					SafeClientName(GameServer(), From), SafeClientName(GameServer(), To));
				CDiscordWebhook::SSendOptions Opt;
				Opt.m_pWebhookUrl = pUrl;
				Discord.Send(aMsg, Opt);
			}
		}
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::ClanDeleteConfirm)
	{
		int ClientId = It->m_From;
		if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
			GameServer()->Block().SendChatTarget(ClientId, "Cancelled clan deletion.");
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::ClanKickConfirm)
	{
		int ClientId = It->m_From;
		if(CheckClientId(ClientId) && GameServer()->m_apPlayers[ClientId])
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "Cancelled kicking '%s' from clan.", It->m_aUsername);
			GameServer()->Block().SendChatTarget(ClientId, aBuf);
		}
		m_Requests.erase(It);
		return true;
	}

	if(It->m_Type == SRequest::EType::ClanRenameNotice)
	{
		int To = It->m_To;
		if(CheckClientId(To) && GameServer()->m_apPlayers[To])
		{
			char aBuf[192];
			const char *pOld = It->m_aOldClanName[0] ? It->m_aOldClanName : "<old>";
			const char *pNew = It->m_aNewClanName[0] ? It->m_aNewClanName : "<new>";
			str_format(aBuf, sizeof(aBuf), "Dismissed: Clan renamed '%s' -> '%s'", pOld, pNew);
			GameServer()->Block().SendChatTarget(To, aBuf);
		}
		m_Requests.erase(It);
		return true;
	}

	char aBuf[256];
	str_copy(aBuf, "Your invite has been declined.", sizeof(aBuf));
	if(CheckClientId(It->m_From) && GameServer()->m_apPlayers[It->m_From])
		GameServer()->Block().SendChatTarget(It->m_From, aBuf);
	m_Requests.erase(It);
	return true;
}

bool CRequests::GetRequestInfo(int RequestId, SRequest &pOut) const
{
	auto It = std::find_if(m_Requests.begin(), m_Requests.end(), [RequestId](const SRequest &r) { return r.m_Id == RequestId; });
	if(It == m_Requests.end())
		return false;
	pOut = *It;
	return true;
}

std::vector<int> CRequests::GetRequestsFor(int ClientId, std::optional<SRequest::EType> TypeFilter) const
{
	std::vector<int> Out;
	for(const auto &r : m_Requests)
	{
		if(r.m_To == ClientId || r.m_From == ClientId)
		{
			if(!TypeFilter.has_value() || r.m_Type == *TypeFilter)
				Out.push_back(r.m_Id);
		}
	}
	return Out;
}

std::vector<int> CRequests::GetRequestIdsTo(int ToClient, std::optional<SRequest::EType> TypeFilter) const
{
	std::vector<int> Out;
	for(const auto &r : m_Requests)
	{
		if(r.m_To == ToClient)
		{
			if(!TypeFilter.has_value() || r.m_Type == *TypeFilter)
				Out.push_back(r.m_Id);
		}
	}
	return Out;
}

std::vector<int> CRequests::GetRequestIdsFromTo(int FromClient, int ToClient, std::optional<SRequest::EType> TypeFilter) const
{
	std::vector<int> Out;
	for(const auto &r : m_Requests)
	{
		if(r.m_To == ToClient && r.m_From == FromClient)
		{
			if(!TypeFilter.has_value() || r.m_Type == *TypeFilter)
				Out.push_back(r.m_Id);
		}
	}
	return Out;
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
		SRequest Req;
		if(!GetRequestInfo(Id, Req))
			continue; // already handled/removed

		char aBufFrom[256];
		char aBufTo[256];
		bool NotifyTo = false;

		if(Req.m_Type == SRequest::EType::OneOnOne)
		{
			const char *pToName = SafeClientName(GameServer(), Req.m_To);
			const char *pFromName = SafeClientName(GameServer(), Req.m_From);
			str_format(aBufFrom, sizeof(aBufFrom), "Your 1on1 invite to '%s' has expired.", pToName);
			str_format(aBufTo, sizeof(aBufTo), "The 1on1 invite from '%s' has expired.", pFromName);
			NotifyTo = true;
		}
		else if(Req.m_Type == SRequest::EType::BlockpointTransfer)
		{
			const char *pToName = SafeClientName(GameServer(), Req.m_To);
			const char *pFromName = SafeClientName(GameServer(), Req.m_From);
			str_format(aBufFrom, sizeof(aBufFrom), "Your blockpoint transfer to '%s' (%d BP) has expired.", pToName, Req.m_Wager);
			str_format(aBufTo, sizeof(aBufTo), "The blockpoint transfer from '%s' (%d BP) has expired.", pFromName, Req.m_Wager);
			NotifyTo = true;

			// discord log for expiry
			{
				CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Block().Http());
				const char *pUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
				if(Discord.IsConfigured(pUrl))
				{
					char aMsg[512];
					str_format(aMsg, sizeof(aMsg), "[BP] Transfer expired: %s -> %s : %d BP",
						SafeClientName(GameServer(), Req.m_From), SafeClientName(GameServer(), Req.m_To), Req.m_Wager);
					CDiscordWebhook::SSendOptions Opt;
					Opt.m_pWebhookUrl = pUrl;
					Discord.Send(aMsg, Opt);
				}
			}
		}
		else if(Req.m_Type == SRequest::EType::Clan)
		{
			const char *pToName = SafeClientName(GameServer(), Req.m_To);
			const char *pFromName = SafeClientName(GameServer(), Req.m_From);
			str_format(aBufFrom, sizeof(aBufFrom), "Your clan invite to '%s' has expired.", pToName);
			str_format(aBufTo, sizeof(aBufTo), "The clan invite from '%s' has expired.", pFromName);
			NotifyTo = true;
		}
		else if(Req.m_Type == SRequest::EType::ClanDeleteConfirm)
		{
			str_copy(aBufFrom, "Your clan deletion confirmation expired.", sizeof(aBufFrom));
		}
		else if(Req.m_Type == SRequest::EType::ClanKickConfirm)
		{
			char aTmp[64];
			str_format(aTmp, sizeof(aTmp), "%s", Req.m_aUsername[0] ? Req.m_aUsername : "target");
			str_format(aBufFrom, sizeof(aBufFrom), "Your confirmation to kick '%s' expired.", aTmp);
		}
		else if(Req.m_Type == SRequest::EType::ClanRenameConfirm)
		{
			str_copy(aBufFrom, "Clan rename confirmation expired.", sizeof(aBufFrom));
		}
		else if(Req.m_Type == SRequest::EType::ClanCreateConfirm)
		{
			str_copy(aBufFrom, "Clan creation confirmation expired.", sizeof(aBufFrom));
		}
		else if(Req.m_Type == SRequest::EType::ClanRenameNotice)
		{
			const char *pOld = Req.m_aOldClanName[0] ? Req.m_aOldClanName : "<old>";
			const char *pNew = Req.m_aNewClanName[0] ? Req.m_aNewClanName : "<new>";
			str_format(aBufFrom, sizeof(aBufFrom), "Clan rename notice expired: '%s' -> '%s'", pOld, pNew);
		}
		else
		{
			str_copy(aBufFrom, "Your shop request has expired.", sizeof(aBufFrom));
		}

		if(CheckClientId(Req.m_From) && GameServer()->m_apPlayers[Req.m_From])
			GameServer()->Block().SendChatTarget(Req.m_From, aBufFrom);
		if(NotifyTo && Req.m_From != Req.m_To && CheckClientId(Req.m_To) && GameServer()->m_apPlayers[Req.m_To])
			GameServer()->Block().SendChatTarget(Req.m_To, aBufTo);

		auto It = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
		if(It != m_Requests.end())
			m_Requests.erase(It);
	}
}

int CRequests::CancelRequestsInvolving(int ClientId, std::optional<SRequest::EType> TypeFilter, const char *pReason)
{
	int Cancelled = 0;
	// collect first to avoid iterator invalidation complexity
	std::vector<int> Ids;
	for(const auto &r : m_Requests)
	{
		if((r.m_From == ClientId || r.m_To == ClientId) && (!TypeFilter.has_value() || r.m_Type == *TypeFilter))
			Ids.push_back(r.m_Id);
	}
	for(int Id : Ids)
	{
		auto It = std::find_if(m_Requests.begin(), m_Requests.end(), [Id](const SRequest &r) { return r.m_Id == Id; });
		if(It == m_Requests.end())
			continue;
		// notify counterpart if online
		int Other = (It->m_From == ClientId) ? It->m_To : It->m_From;
		if(CheckClientId(Other) && GameServer()->m_apPlayers[Other])
		{
			char aBuf[256];
			if(pReason)
				str_format(aBuf, sizeof(aBuf), "A pending %s request was cancelled: %s", It->m_Type == SRequest::EType::BlockpointTransfer ? "blockpoint transfer" : It->m_Type == SRequest::EType::OneOnOne ? "1on1" :
																					     It->m_Type == SRequest::EType::Clan             ? "clan" :
																											       "request",
					pReason);
			else
				str_format(aBuf, sizeof(aBuf), "A pending %s request was cancelled.", It->m_Type == SRequest::EType::BlockpointTransfer ? "blockpoint transfer" : It->m_Type == SRequest::EType::OneOnOne ? "1on1" :
																					  It->m_Type == SRequest::EType::Clan             ? "clan" :
																											    "request");
			GameServer()->Block().SendChatTarget(Other, aBuf);
		}
		m_Requests.erase(It);
		Cancelled++;
	}
	if(Cancelled > 0)
		dbg_msg("requests", "cancelled %d request(s) involving client %d", Cancelled, ClientId);
	return Cancelled;
}
