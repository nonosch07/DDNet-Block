#include <engine/antibot.h>
#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/ddnet.h>
#include <game/server/player.h>
#include <game/server/save.h>
#include <game/server/teams.h>

#include <block/accounts.h>
#include <block/base.h>
#include <block/clans.h>
#include <block/common.h>
#include <block/components/core/component_registry.h>
#include <block/components/events.h>
#include <block/components/events/1on1.h>
#include <block/components/oneonone_manager.h>
#include <block/components/requests.h>
#include <block/context.h>
#include <block/shop/storemanager.h>
#include <block/util.h>
#include <block/votes/votemanager.h>
#include <block/whois.h>
#include <block/zones/zone.h>

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>

extern std::mutex g_ClansDataMutex;
extern std::unordered_map<int, CClansData> g_ClanIdMap;

static inline bool CheckValidChars(const char *pStr)
{
	int Len = str_length(pStr);
	for(int i = 0; i < Len; i++)
		if((pStr[i] < 'a' || pStr[i] > 'z') &&
			(pStr[i] < 'A' || pStr[i] > 'Z') &&
			(pStr[i] < '0' || pStr[i] > '9'))
			return false;
	return true;
}

void CBlock::ConRegister(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(pSelf->m_apPlayers[pResult->m_ClientId]->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't create an account while being logged in!");
		return;
	}

	if(pSelf->Block().isInEvent(pResult->m_ClientId))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't register while participating in an event. Use /leave first.");
		return;
	}
	IZone *pSpawnZone = pSelf->Block().ZoneManager()->GetZone(ZONE_SPAWN);
	CPlayer *pReqPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(g_Config.m_SvShopServer != 1)
	{
		if(pSpawnZone && pReqPlayer && pReqPlayer->GetCharacter() && !pSpawnZone->IsInZone(pReqPlayer->GetCharacter()->m_Pos))
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can only login while in the spawn zone.");
			return;
		}
	}

	const char *pUsername = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);

	int NameLength = str_length(pUsername);
	int PasswordLength = str_length(pPassword);

	if(NameLength <= 2)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Your name must be at least 3 characters long!");
		return;
	}

	if(pReqPlayer)
	{
		const int RegisterCooldownSeconds = g_Config.m_SvRegisterCooldownPerIp > 0 ? g_Config.m_SvRegisterCooldownPerIp : 10;
		int64_t Now = pSelf->Server()->Tick();
		if(pReqPlayer->Block().m_LastRegisterTick != 0 && Now - pReqPlayer->Block().m_LastRegisterTick < RegisterCooldownSeconds * pSelf->Server()->TickSpeed())
		{
			int Remaining = (int)((RegisterCooldownSeconds * pSelf->Server()->TickSpeed() - (Now - pReqPlayer->Block().m_LastRegisterTick)) / pSelf->Server()->TickSpeed());
			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before trying again.", Remaining, Remaining != 1 ? "s" : "");
			pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
			return;
		}
	}

	if(PasswordLength < 5)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Your password must be at least 5 characters long!");
		return;
	}

	if(str_comp(pUsername, pPassword) == 0)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Password must be different from username!");
		return;
	}

	if(NameLength * sizeof(char) >= 11)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account name too long!");
		return;
	}

	if(!CheckValidChars(pUsername) || !CheckValidChars(pPassword))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Only the characters A-Z and 0-9 are allowed!");
		return;
	}

	char aAddrStrCheck[NETADDR_MAXSTRSIZE];
	BlockClientAddr(pSelf->Server(), pResult->m_ClientId, aAddrStrCheck, sizeof(aAddrStrCheck));
	int RemainingBan = 0;
	if(!pPlayer->Block().m_IsNpc)
	{
		if(pSelf->Block().Accounts()->IsIpBanned(aAddrStrCheck, RemainingBan))
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "Too many recent attempts from your connection. Please wait %d second%s and try again.", RemainingBan, RemainingBan == 1 ? "" : "s");
			pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
			return;
		}
		if(!pSelf->Block().Accounts()->RegisterIpAttempt(aAddrStrCheck))
		{
			pSelf->Block().Accounts()->IsIpBanned(aAddrStrCheck, RemainingBan);
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "You're trying a lot. Take a short break (%d second%s) and try again.", RemainingBan, RemainingBan == 1 ? "" : "s");
			pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
			return;
		}
	}

	pSelf->Block().Accounts()->Register(pResult->m_ClientId, pUsername, pPassword);
	if(pReqPlayer)
		pReqPlayer->Block().m_LastRegisterTick = pSelf->Server()->Tick();
}

void CBlock::ConLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(pSelf->m_apPlayers[pResult->m_ClientId]->Block().m_Account.m_Id > 0)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are already logged in!");
		return;
	}
	if(pSelf->Block().isInEvent(pResult->m_ClientId))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You must finish the ongoing event (or use '/leave' to leave).");
		return;
	}
	IZone *pSpawnZone = pSelf->Block().ZoneManager()->GetZone(ZONE_SPAWN);
	CPlayer *pReqPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(g_Config.m_SvShopServer != 1)
	{
		if(pSpawnZone && pReqPlayer && pReqPlayer->GetCharacter() && !pSpawnZone->IsInZone(pReqPlayer->GetCharacter()->m_Pos))
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can only login while in the spawn zone.");
			return;
		}
	}

	const char *pUsername = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);

	pSelf->Block().Accounts()->Login(pResult->m_ClientId, pUsername, pPassword);
}

void CBlock::ConAccountLogout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pSelf->m_apPlayers[pResult->m_ClientId]->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in!");
		return;
	}
	if(pSelf->Block().isInEvent(pResult->m_ClientId))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't logout while participating in an event. Use /leave first.");
		return;
	}

	// deregister from any events as candidate before logout
	{
		int Cid = pResult->m_ClientId;

		if(auto Events = g_ComponentRegistry.Get<CEvents>())
		{
			auto Subs = Events->GetSubComponents();
			for(auto &Sub : Subs)
			{
				CEventComponent *pEv = dynamic_cast<CEventComponent *>(Sub.operator->());
				if(!pEv)
					continue;

				// if registered as candidate, deregister
				const auto &Cands = pEv->Candidates();
				if(std::find(Cands.begin(), Cands.end(), Cid) != Cands.end())
				{
					pEv->DeRegister(Cid);
					continue; // we've removed them from this event
				}
			}
		}
	}
	IZone *pSpawnZone = pSelf->Block().ZoneManager()->GetZone(ZONE_SPAWN);
	CPlayer *pReqPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(pSpawnZone && pReqPlayer && pReqPlayer->GetCharacter() && !pSpawnZone->IsInZone(pReqPlayer->GetCharacter()->m_Pos))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can only logout while in the spawn zone.");
		return;
	}

	// cancel any pending requests involving this client before logout (so others aren't left with stale offers)
	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		Requests->CancelRequestsInvolving(pResult->m_ClientId, std::nullopt, "player logged out");
	}
	pPlayer->Block().OnPlayerLogout();
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "you have been logged out!");
}

void CBlock::ConDisplayBlockpoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pSelf->m_apPlayers[pResult->m_ClientId]->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in!");
		return;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "You currently have %d blockpoint%s!",
		pSelf->m_apPlayers[pResult->m_ClientId]->Block().m_Account.m_Blockpoints,
		pSelf->m_apPlayers[pResult->m_ClientId]->Block().m_Account.m_Blockpoints != 1 ? "s" : "");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
}

// /give_bp <playerName> <amount>
void CBlock::ConGiveBlockpointsRequest(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pFrom = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pFrom || !pFrom->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}
	if(pResult->NumArguments() < 2)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Usage: /give_bp <playerName> <amount>");
		return;
	}
	const char *pTargetName = pResult->GetString(0);
	int Amount = pResult->GetInteger(1);
	if(Amount < g_Config.m_SvBpTransferAmountMin)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Amount below minimum transfer threshold.");
		return;
	}
	if(Amount > g_Config.m_SvBpTransferAmountCap)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Amount exceeds max cap.");
		return;
	}
	if(Amount < 1)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Amount too small.");
		return;
	}
	CPlayer *pTo = pSelf->Block().GetPlayerByName(pTargetName);
	if(!pTo || !pTo->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Target player not found or not logged in.");
		return;
	}
	if(pTo->GetCid() == pFrom->GetCid())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You cannot transfer blockpoints to yourself.");
		return;
	}
	if(pFrom->Block().GetPlayerBlockpoints() < Amount)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You don't have enough blockpoints.");
		return;
	}
	// disallow if either player is currently in an event
	if(pSelf->Block().isInEvent(pFrom->GetCid()) || pSelf->Block().isInEvent(pTo->GetCid()))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Blockpoint transfers are not allowed while either player is in an event.");
		return;
	}
	int Cooldown = g_Config.m_SvBpTransferCooldown;
	if(Cooldown > 0 && pFrom->Block().m_LastBpTransferOfferTick != 0 && pSelf->Server()->Tick() - pFrom->Block().m_LastBpTransferOfferTick < Cooldown * pSelf->Server()->TickSpeed())
	{
		int Rem = (int)((Cooldown * pSelf->Server()->TickSpeed() - (pSelf->Server()->Tick() - pFrom->Block().m_LastBpTransferOfferTick)) / pSelf->Server()->TickSpeed());
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another transfer.", Rem, Rem != 1 ? "s" : "");
		pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
		return;
	}
	// outstanding per sender check
	if(auto RequestsTmp = g_ComponentRegistry.Get<CRequests>())
	{
		int Outstanding = 0;
		auto List = RequestsTmp->GetRequestsFor(pFrom->GetCid(), CRequests::SRequest::EType::BlockpointTransfer);
		for(int Id : List)
		{
			CRequests::SRequest Info;
			if(RequestsTmp->GetRequestInfo(Id, Info) && Info.m_From == pFrom->GetCid())
				Outstanding++;
		}
		if(Outstanding >= g_Config.m_SvBpTransferMaxOutstandingPerSender)
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "You have too many pending transfers. Wait for them to resolve.");
			return;
		}
	}
	auto Requests = g_ComponentRegistry.Get<CRequests>();
	if(!Requests)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
		return;
	}
	int Expiry = g_Config.m_SvBpTransferExpiry;
	int Id = Requests->CreateBlockpointTransfer(pFrom->GetCid(), pTo->GetCid(), Amount, Expiry);
	if(Id == -1)
		return; // error already messaged
	pFrom->Block().m_LastBpTransferOfferTick = pSelf->Server()->Tick();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Transfer offer (%ds) sent to %s for %d blockpoints.", Expiry, pSelf->Server()->ClientName(pTo->GetCid()), Amount);
	pSelf->Block().SendChatTarget(pFrom->GetCid(), aBuf);
}

// /accept_bp [playerName]
void CBlock::ConAcceptBlockpointsRequest(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}
	auto Requests = g_ComponentRegistry.Get<CRequests>();
	if(!Requests)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
		return;
	}
	auto MatchIds = Requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
	int Chosen = -1;
	if(pResult->NumArguments() > 0)
	{
		const char *pFromName = pResult->GetString(0);
		CPlayer *pFrom = pSelf->Block().GetPlayerByName(pFromName);
		if(!pFrom)
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player not found");
			return;
		}
		auto Specific = Requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
		for(int Id : Specific)
		{
			CRequests::SRequest Info;
			if(Requests->GetRequestInfo(Id, Info) && Info.m_ExpireTick > pSelf->Server()->Tick())
				Chosen = std::max(Chosen, Id);
		}
	}
	else if(MatchIds.size() == 1)
	{
		Chosen = MatchIds[0];
	}
	else
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, MatchIds.empty() ? "No blockpoint transfer to accept." : "Multiple transfers pending. Use /accept_bp <playerName>.");
		return;
	}
	if(Chosen == -1)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No valid (non-expired) transfer found.");
		return;
	}
	if(!Requests->AcceptRequest(Chosen))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to accept the transfer (it may have expired).");
		return;
	}
}

// /decline_bp [playerName]
void CBlock::ConDeclineBlockpointsRequest(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}
	auto Requests = g_ComponentRegistry.Get<CRequests>();
	if(!Requests)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
		return;
	}
	auto MatchIds = Requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
	int Chosen = -1;
	if(pResult->NumArguments() > 0)
	{
		const char *pFromName = pResult->GetString(0);
		CPlayer *pFrom = pSelf->Block().GetPlayerByName(pFromName);
		if(!pFrom)
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player not found");
			return;
		}
		auto Specific = Requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
		for(int Id : Specific)
		{
			CRequests::SRequest Info;
			if(Requests->GetRequestInfo(Id, Info) && Info.m_ExpireTick > pSelf->Server()->Tick())
				Chosen = std::max(Chosen, Id);
		}
	}
	else if(MatchIds.size() == 1)
	{
		Chosen = MatchIds[0];
	}
	else
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, MatchIds.empty() ? "No blockpoint transfer to decline." : "Multiple transfers pending. Use /decline_bp <playerName>.");
		return;
	}
	if(Chosen == -1)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No valid (non-expired) transfer found.");
		return;
	}
	if(!Requests->DeclineRequest(Chosen))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to decline the transfer (it may have expired).");
		return;
	}
}

void CBlock::ConChangePassword(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pSelf->m_apPlayers[pResult->m_ClientId]->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in!");
		return;
	}

	const char *pOldPassword = pResult->GetString(0);
	const char *pNewPassword = pResult->GetString(1);

	int OldLength = str_length(pOldPassword);
	int NewLength = str_length(pNewPassword);

	if(OldLength < 5)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Old password incorrect (must be at least 5 chars long).");
		return;
	}

	if(NewLength < 5)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Your password must be at least 5 characters long!");
		return;
	}

	if(str_comp(pOldPassword, pNewPassword) == 0)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Password must be different from each other!");
		return;
	}

	if(!CheckValidChars(pOldPassword) || !CheckValidChars(pNewPassword))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Only the characters A-Z and 0-9 are allowed!");
		return;
	}

	pSelf->Block().Accounts()->ChangePassword(pResult->m_ClientId, pSelf->m_apPlayers[pResult->m_ClientId]->Block().m_Account.m_aName, pOldPassword, pNewPassword);
}

void CBlock::ConDisplayProfile(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	CPlayer *pTargetPlayer = pPlayer;

	if(pResult->NumArguments() == 1)
	{
		const char *pTargetName = pResult->GetString(0);
		bool FoundTarget = false;

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(pSelf->m_apPlayers[i] && !str_comp(pTargetName, pSelf->Server()->ClientName(i)))
			{
				pTargetPlayer = pSelf->m_apPlayers[i];
				FoundTarget = true;
				break;
			}
		}

		if(!FoundTarget)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "Player not found");
			return;
		}
	}

	if(!pTargetPlayer->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(ClientId, "The target player is not logged in.");
		return;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Profile of %s",
		pTargetPlayer->Block().m_Account.m_aName);
	pSelf->Block().SendChatTarget(ClientId, aBuf);

	pSelf->Block().SendChatTarget(ClientId, "------Global------");

	// global stats: Kills, Deaths, Max Kill Streak
	str_format(aBuf, sizeof(aBuf), "Kills: %d", pTargetPlayer->Block().m_Account.m_Kills);
	pSelf->Block().SendChatTarget(ClientId, aBuf);

	str_format(aBuf, sizeof(aBuf), "Deaths: %d", pTargetPlayer->Block().m_Account.m_Deaths);
	pSelf->Block().SendChatTarget(ClientId, aBuf);

	// global K/D ratio
	float KD = pTargetPlayer->Block().m_Account.m_Deaths > 0 ? (float)pTargetPlayer->Block().m_Account.m_Kills / pTargetPlayer->Block().m_Account.m_Deaths : (float)pTargetPlayer->Block().m_Account.m_Kills;
	str_format(aBuf, sizeof(aBuf), "K/D: %.2f", KD);
	pSelf->Block().SendChatTarget(ClientId, aBuf);

	str_format(aBuf, sizeof(aBuf), "Max Kill Streak: %d", pTargetPlayer->Block().m_Account.m_Killstreak);
	pSelf->Block().SendChatTarget(ClientId, aBuf);

	// LMB Wins
	str_format(aBuf, sizeof(aBuf), "LMB Wins: %d", pTargetPlayer->Block().m_Account.m_TourneyWin);
	pSelf->Block().SendChatTarget(ClientId, aBuf);

	// playtime in hours and minutes
	int Hours = pTargetPlayer->Block().m_Account.m_Playtime / 3600;
	int Minutes = (pTargetPlayer->Block().m_Account.m_Playtime % 3600) / 60;
	str_format(aBuf, sizeof(aBuf), "PlayTime: %d hours %d minutes", Hours, Minutes);
	pSelf->Block().SendChatTarget(ClientId, aBuf);

	// pSelf->Block().SendChatTarget(ClientId, "------Ranked------");

	// // ranked stats: Games, Kills, Deaths, Wins
	// str_format(aBuf, sizeof(aBuf), "Games: %d", pTargetPlayer->m_Account.m_RankedGames);
	// pSelf->Block().SendChatTarget(ClientId, aBuf);

	// str_format(aBuf, sizeof(aBuf), "Rating: %d", pTargetPlayer->m_Account.m_Ranking);
	// pSelf->Block().SendChatTarget(ClientId, aBuf);

	// str_format(aBuf, sizeof(aBuf), "Kills: %d", pTargetPlayer->m_Account.m_RankedKills);
	// pSelf->Block().SendChatTarget(ClientId, aBuf);

	// str_format(aBuf, sizeof(aBuf), "Deaths: %d", pTargetPlayer->m_Account.m_RankedDeaths);
	// pSelf->Block().SendChatTarget(ClientId, aBuf);

	// // ranked K/D ratio
	// float RankedKD = pTargetPlayer->m_Account.m_RankedDeaths > 0 ? (float)pTargetPlayer->m_Account.m_RankedKills / pTargetPlayer->m_Account.m_RankedDeaths : (float)pTargetPlayer->m_Account.m_RankedKills;
	// str_format(aBuf, sizeof(aBuf), "K/D: %.2f", RankedKD);
	// pSelf->Block().SendChatTarget(ClientId, aBuf);

	// // ranked Wins and Win Rate
	// str_format(aBuf, sizeof(aBuf), "Wins: %d", pTargetPlayer->m_Account.m_RankedWins);
	// pSelf->Block().SendChatTarget(ClientId, aBuf);

	// float WinRate = pTargetPlayer->m_Account.m_RankedGames > 0 ? (float)pTargetPlayer->m_Account.m_RankedWins / pTargetPlayer->m_Account.m_RankedGames * 100.0f : 0.0f;
	// str_format(aBuf, sizeof(aBuf), "Win Rate: %.2f%%", WinRate);
	// pSelf->Block().SendChatTarget(ClientId, aBuf);
}

void CBlock::ConGetCid(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(pResult->NumArguments() != 1)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Usage: /getcid <player name>");
		return;
	}

	const char *pTargetName = pResult->GetString(0);
	CPlayer *pTarget = pSelf->Block().GetPlayerByName(pTargetName);
	if(!pTarget)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player not found");
		return;
	}

	pSelf->Block().SendChatTarget(pResult->m_ClientId, "%s -> cid %d", pSelf->Server()->ClientName(pTarget->GetCid()), pTarget->GetCid());
}

void CBlock::ConStatusAccounts(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const char *pFilter = pResult->NumArguments() == 1 ? pResult->GetString(0) : "";
	char aBuf[512];

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = pSelf->m_apPlayers[i];
		if(!pPlayer)
			continue;
		if(!pPlayer->Block().IsLoggedIn())
			continue;

		// filter by name (client name or account name)
		if(pFilter[0] != '\0')
		{
			if(!str_utf8_find_nocase(pPlayer->Block().GetPlayerName(), pFilter) && !str_utf8_find_nocase(pPlayer->Block().m_Account.m_aName, pFilter))
				continue;
		}

		int Hours = pPlayer->Block().m_Account.m_Playtime / 3600;
		int Minutes = (pPlayer->Block().m_Account.m_Playtime % 3600) / 60;

		const char *pClanName = " ";
		if(pSelf->Block().Clans())
		{
			CClansData Tmp;
			if(pSelf->Block().Clans()->GetClanSnapshotById(pPlayer->Block().m_Account.m_ClanId, Tmp))
				pClanName = Tmp.m_ClanName;
		}
		str_format(aBuf, sizeof(aBuf), "cid=%d, accid=%d, acc_name='%s', ig_name='%s', vip=%d, clan='%s', clanid=%d, auth=%d, playtime=%02d:%02d, ranking=%d, kills=%d, deaths=%d",
			i,
			pPlayer->Block().m_Account.m_Id,
			pPlayer->Block().m_Account.m_aName,
			pSelf->Server()->ClientName(i),
			pPlayer->Block().m_Account.m_Vip,
			pClanName,
			pPlayer->Block().m_Account.m_ClanId,
			(int)pPlayer->Block().m_Account.m_AuthLevel,
			Hours,
			Minutes,
			pPlayer->Block().m_Account.m_Ranking,
			pPlayer->Block().m_Account.m_Kills,
			pPlayer->Block().m_Account.m_Deaths);

		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CBlock::ConDisplayTopLevel(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Block().Accounts()->ShowTopLevel(ClientId);
}

void CBlock::ConIpBans(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!CheckClientId(pResult->m_ClientId))
		return;
	// pPlayer not used here; avoid unused variable warning
	if(!pPlayer || !pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Permission denied");
		return;
	}

	auto Bans = pSelf->Block().Accounts()->ListIpBans();
	if(Bans.empty())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No active IP bans.");
		return;
	}

	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Active IP bans:");
	char aBuf[256];
	for(const auto &b : Bans)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d second%s remaining", b.first.c_str(), b.second, b.second != 1 ? "s" : "");
		pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
	}
}

void CBlock::ConIpBanClear(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!CheckClientId(pResult->m_ClientId))
		return;
	// pPlayer not used here; avoid unused variable warning
	if(!pPlayer || !pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Permission denied");
		return;
	}

	const char *pIp = pResult->GetString(0);
	pSelf->Block().Accounts()->ClearIpBan(pIp);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Cleared IP ban for %s", pIp);
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
}

void CBlock::ConListOutstandingInvites(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Permission denied");
		return;
	}

	int Target = pResult->GetInteger(0);
	if(!CheckClientId(Target))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Invalid client id");
		return;
	}

	if(!pSelf->m_apPlayers[Target])
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Target player not online");
		return;
	}

	auto Requests = g_ComponentRegistry.Get<CRequests>();
	if(!Requests)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Request component not available");
		return;
	}

	auto IdsFrom = Requests->GetRequestIdsFromTo(Target, Target, std::nullopt); // get both to and from via helper below

	auto All = Requests->GetRequestsFor(Target, std::nullopt);
	if(All.empty())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No outstanding requests for player");
		return;
	}

	char aBuf[256];
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Outstanding requests (id, type, from -> to):");
	for(int Id : All)
	{
		str_format(aBuf, sizeof(aBuf), "id=%d", Id);
		pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
	}
}

// admin console commands to modify account attributes
void CBlock::ConGivePages(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerPages(pTarget->Block().GetPlayerPages() + Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d pages to %s (now %d)", Amount, pTarget->Block().GetPlayerName(), pTarget->Block().GetPlayerPages());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConSetPages(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerPages(Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set pages for %s to %d", pTarget->Block().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConSetLevel(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerLevel(Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set level for %s to %d", pTarget->Block().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConSetExperience(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerExperience(Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set experience for %s to %d", pTarget->Block().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConSetWeaponkitsAdmin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerWeaponkits(Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set weaponkits for %s to %d", pTarget->Block().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConSetBlockpoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerBlockpoints(Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set blockpoints for %s to %d", pTarget->Block().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConSetPassive(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Seconds = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerPassive(Seconds);
	if(pTarget->GetCharacter())
		pTarget->GetCharacter()->Core()->m_Passive = true;
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set passive seconds for %s to %d", pTarget->Block().GetPlayerName(), Seconds);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConTelekinesis(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id");
		return;
	}
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Player not found");
		return;
	}
	pPlayer->Block().m_TelekinesisEnabled = !pPlayer->Block().m_TelekinesisEnabled;
	pPlayer->Block().m_TelekinesisTarget = -1;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Telekinesis %s.", pPlayer->Block().m_TelekinesisEnabled ? "enabled" : "disabled");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CBlock::ConKnockout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id");
		return;
	}
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Player not found");
		return;
	}
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "You must be alive to use this command");
		return;
	}

	const char *pName = pResult->GetString(0);
	if(!pName || !pName[0])
	{
		// list available knockouts
		char aBuf[256];
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Available knockouts:");
		for(int i = 0; i < CCosmeticsHandler::NUM_KNOCKOUTS; i++)
		{
			str_format(aBuf, sizeof(aBuf), "  %d: %s", i, CCosmeticsHandler::ms_KnockoutNames[i]);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		}
		return;
	}

	// try parsing as number first, then as name
	int Effect = -1;
	if(pName[0] >= '0' && pName[0] <= '9')
		Effect = str_toint(pName);
	if(Effect < 0 || Effect >= CCosmeticsHandler::NUM_KNOCKOUTS)
		Effect = pSelf->Block().Cosmetics()->FindKnockoutEffect(pName);

	if(Effect < 0 || Effect >= CCosmeticsHandler::NUM_KNOCKOUTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Unknown knockout. Use 'knockout' without arguments to list all.");
		return;
	}

	const CNetObj_PlayerInput &Input = pChr->Block().GetLatestInput();
	vec2 MousePos = pChr->m_Pos + vec2(Input.m_TargetX, Input.m_TargetY);
	pSelf->Block().Cosmetics()->DoKnockoutEffectRaw(MousePos, Effect);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Triggered '%s' at (%.0f, %.0f)", CCosmeticsHandler::ms_KnockoutNames[Effect], MousePos.x, MousePos.y);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CBlock::ConWhoisAccount(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	if(!pName || !pName[0])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Usage: whois_account <account name>");
		return;
	}
	if(pSelf->Block().WhoIs())
		pSelf->Block().WhoIs()->CmdWhoisAccount(-1, pName);
}

void CBlock::ConGiveLevel(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerLevel(pTarget->Block().GetPlayerLevel() + Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d levels to %s (now %d)", Amount, pTarget->Block().GetPlayerName(), pTarget->Block().GetPlayerLevel());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConGiveExperience(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerExperience(pTarget->Block().GetPlayerExperience() + Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d experience to %s (now %d)", Amount, pTarget->Block().GetPlayerName(), pTarget->Block().GetPlayerExperience());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConGiveWeaponkits(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerWeaponkits(pTarget->Block().GetPlayerWeaponkits() + Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d weaponkits to %s (now %d)", Amount, pTarget->Block().GetPlayerName(), pTarget->Block().GetPlayerWeaponkits());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConGiveBlockpoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Amount = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerBlockpoints(pTarget->Block().GetPlayerBlockpoints() + Amount);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d blockpoints to %s (now %d)", Amount, pTarget->Block().GetPlayerName(), pTarget->Block().GetPlayerBlockpoints());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConGivePassive(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Seconds = pResult->GetInteger(1);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}
	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}
	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Block().SetPlayerPassive(Seconds);
	if(pTarget->GetCharacter())
		pTarget->GetCharacter()->Core()->m_Passive = true;
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d seconds of passive to %s", Seconds, pTarget->Block().GetPlayerName());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConSetVip(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Target = pResult->GetInteger(0);
	int Vip = pResult->GetInteger(1);

	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid target client id");
		return;
	}

	CPlayer *pTarget = pSelf->m_apPlayers[Target];
	if(!pTarget)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player not online");
		return;
	}

	if(!pTarget->Block().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}

	int NewVip = Vip ? 1 : 0;
	pTarget->Block().SetPlayerVip(NewVip);
	pSelf->Block().Accounts()->Save(Target, &pTarget->Block().m_Account);

	char aBuf[128];
	if(NewVip)
		str_format(aBuf, sizeof(aBuf), "Set VIP for %s (now vip=%d)", pTarget->Block().GetPlayerName(), pTarget->Block().GetPlayerVip());
	else
		str_format(aBuf, sizeof(aBuf), "Removed VIP from %s", pTarget->Block().GetPlayerName());

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Block().SendChatTarget(Target, aBuf);
}

void CBlock::ConSetVipAccount(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments() < 2)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Usage: vip_account <account_name> <0|1>");
		return;
	}

	const char *pAccountName = pResult->GetString(0);
	int Vip = pResult->GetInteger(1) ? 1 : 0;

	if(str_length(pAccountName) == 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Account name must be non-empty");
		return;
	}

	pSelf->Block().Accounts()->SetVipByNameAdmin(pResult->m_ClientId, pAccountName, Vip);

	// iff the player is currently online and logged in under that account, also update in-memory shittery
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *p = pSelf->m_apPlayers[i];
		if(!p)
			continue;
		if(!p->Block().IsLoggedIn())
			continue;
		if(str_comp(p->Block().GetPlayerName(), pAccountName) == 0 || str_comp(p->Block().m_Account.m_aName, pAccountName) == 0)
		{
			p->Block().SetPlayerVip(Vip);
			pSelf->Block().Accounts()->Save(i, &p->Block().m_Account);
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Your VIP status was %s by an admin.", Vip ? "enabled" : "disabled");
			pSelf->Block().SendChatTarget(i, aBuf);
			break;
		}
	}

	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "Queued VIP=%d for account '%s' (offline-capable)", Vip, pAccountName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CBlock::ConAdminSetPassword(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments() < 2)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Usage: set_password <account_name> <new_password>");
		return;
	}

	const char *pAccountName = pResult->GetString(0);
	const char *pNewPassword = pResult->GetString(1);

	if(str_length(pAccountName) == 0 || str_length(pNewPassword) == 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Account name and new password must be non-empty");
		return;
	}

	SHA256_DIGEST HashedNewPassword = CBlock::HashPassword(pNewPassword);
	char aHashedNewPassword[SHA256_DIGEST_LENGTH * 2 + 1];
	sha256_str(HashedNewPassword, aHashedNewPassword, sizeof(aHashedNewPassword));

	char aEscapedName[128];
	{
		int Di = 0;
		for(int Si = 0; pAccountName[Si] && Di < (int)sizeof(aEscapedName) - 2; ++Si)
		{
			if(pAccountName[Si] == '\'')
			{
				if(Di < (int)sizeof(aEscapedName) - 3)
				{
					aEscapedName[Di++] = '\'';
					aEscapedName[Di++] = '\'';
				}
			}
			else
			{
				aEscapedName[Di++] = pAccountName[Si];
			}
		}
		aEscapedName[Di] = '\0';
	}

	pSelf->Block().Accounts()->ChangePasswordAdmin(pResult->m_ClientId, pAccountName, pNewPassword);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Set password for account '%s'", pAccountName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// notify player if they are online
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *p = pSelf->m_apPlayers[i];
		if(!p)
			continue;
		if(!p->Block().IsLoggedIn())
			continue;
		if(str_comp(p->Block().GetPlayerName(), pAccountName) == 0 || str_comp(p->Block().m_Account.m_aName, pAccountName) == 0)
		{
			pSelf->Block().SendChatTarget(i, "An administrator has changed your account password.");
			break;
		}
	}
}

void CBlock::ConDisplayTopBlockpoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Block().Accounts()->ShowTopBlockpoints(ClientId);
}

void CBlock::ConDisplayTopKillStreak(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Block().Accounts()->ShowTopKillStreak(ClientId);
}

void CBlock::ConWeaponKit(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You must be alive to use a weapon kit.");
		return;
	}

	if(!pSelf->Block().m_WeaponkitsAllowed)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Weaponkits are currently disabled on this server.");
		return;
	}

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	if(!pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}

	if(pPlayer->Block().GetPlayerWeaponkits() < 1 && !pPlayer->Block().GetPlayerVip())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You don't have any weapon kits, make a trip to the store and purchase some!");
		return;
	}

	// check if the player has all weapons
	bool HasAllWeapons = true;
	for(int i = WEAPON_GUN; i < NUM_WEAPONS - 1; i++)
	{
		if(!pChr->Block().Core().m_aWeapons[i].m_Got)
		{
			HasAllWeapons = false;
			break;
		}
	}

	if(HasAllWeapons)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You already have all weapons.");
		return;
	}

	// restrict usage to spawn zone if spawn zone exists
	IZone *pSpawnZone = pSelf->Block().ZoneManager()->GetZone(ZONE_SPAWN);
	if(pSpawnZone)
	{
		if(!pSpawnZone->IsInZone(pChr->m_Pos))
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can only use weapon kits while in the spawn zone.");
			return;
		}
	}

	pSelf->ModifyWeapons(pResult, pSelf, -1, false);

	char aBuf[128];

	if(pPlayer->Block().GetPlayerVip())
		str_copy(aBuf, "You have successfully used a weaponkit!", sizeof(aBuf));
	else
	{
		str_format(aBuf, sizeof(aBuf), "You have successfully used a weaponkit! %d kits left.", pPlayer->Block().GetPlayerWeaponkits());
		pPlayer->Block().SetPlayerWeaponkits(pPlayer->Block().GetPlayerWeaponkits() - 1);
	}
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
}

void CBlock::ConDeathnote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	IServer *pServer = pSelf->Server();

	if(!pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}

	if(!pResult->NumArguments())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Invalid arguments... Usage: deathnote [player]");
		return;
	}

	CPlayer *pTarget = pSelf->Block().GetPlayerByName(pResult->GetString(0));

	if(!pTarget)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "This player doesn't exist.");
		return;
	}

	if(pPlayer->Block().GetPlayerPages() < 1)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You don't have any deathnotes, make a trip to the store and purchase some!");
		return;
	}

	// prevent using deathnote if the target is inside passive or no-collision zones
	IZone *pPassiveZone = pSelf->Block().ZoneManager()->GetZone(ZONE_PASSIVE);
	IZone *pNoCollZone = pSelf->Block().ZoneManager()->GetZone(ZONE_NOCOLL);

	// prevent using deathnote if target is participating in an event
	if(pTarget && pSelf->Block().isInEvent(pTarget->GetCid()))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a player participating in an event.");
		return;
	}

	int CurrentTick = pServer->Tick();
	int CooldownTick = pPlayer->Block().m_LastDeathnote + (pServer->TickSpeed() * g_Config.m_SvDeathNoteCoolDown);
	if(CurrentTick < CooldownTick)
	{
		int RemainingTicks = CooldownTick - CurrentTick;
		int CooldownTime = (RemainingTicks + pServer->TickSpeed() - 1) / pServer->TickSpeed();
		int CooldownMinutes = CooldownTime / 60;
		int CooldownSeconds = CooldownTime % 60;

		char aBuf[256];
		str_copy(aBuf, "You have to wait ", sizeof(aBuf));
		if(CooldownMinutes > 0)
		{
			if(CooldownMinutes == 1)
				str_format(aBuf + str_length(aBuf), sizeof(aBuf) - str_length(aBuf), "%d minute ", CooldownMinutes);
			else
				str_format(aBuf + str_length(aBuf), sizeof(aBuf) - str_length(aBuf), "%d minutes ", CooldownMinutes);
		}

		if(CooldownSeconds > 0)
		{
			if(CooldownSeconds == 1)
				str_format(aBuf + str_length(aBuf), sizeof(aBuf) - str_length(aBuf), "%d second ", CooldownSeconds);
			else
				str_format(aBuf + str_length(aBuf), sizeof(aBuf) - str_length(aBuf), "%d seconds ", CooldownSeconds);
		}

		str_append(aBuf, "until you can write down a player in your deathnote.", sizeof(aBuf));
		pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
		return;
	}

	// block if the target is currently inside a passive or no-collision zone
	CCharacter *pTChar = pTarget->GetCharacter();
	if(pTChar)
	{
		if(pPassiveZone && pPassiveZone->IsInZone(pTChar->m_Pos) && pTChar->Core()->m_Passive)
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a player inside a passive zone.");
			return;
		}
		if(pNoCollZone && pNoCollZone->IsInZone(pTChar->m_Pos))
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a player inside a no-collision zone.");
			return;
		}
		if(pTChar->Core()->m_Solo)
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a solo player.");
			return; // bad mapper
		}
	}

	// consume a page and apply kill
	pPlayer->Block().SetPlayerPages(pPlayer->Block().GetPlayerPages() - 1);
	pTarget->KillCharacter();

	char ABuffFrom[128], ABuffTo[128];
	str_format(ABuffFrom, sizeof(ABuffFrom), "Successfully killed %s. %d pages remaining.", pSelf->Server()->ClientName(pTarget->GetCid()), pPlayer->Block().GetPlayerPages());
	str_format(ABuffTo, sizeof(ABuffTo), "'%s' used a deathnote to kill you!", pSelf->Server()->ClientName(pResult->m_ClientId));
	pSelf->Block().SendChatTarget(pResult->m_ClientId, ABuffFrom);
	pSelf->Block().SendChatTarget(pTarget->GetCid(), ABuffTo);
	pPlayer->Block().m_LastDeathnote = pServer->Tick();
}

void CBlock::ConPassiveRemover(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}
	if(!g_Config.m_SvPassiveRemoverEnabled)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Passive Remover feature is currently disabled.");
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}

	if(!pResult->NumArguments())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Usage: /passiveremover [player]");
		return;
	}

	if(pPlayer->Block().GetPlayerPassiveRemovers() < 1)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You don't have any passive removers! Purchase one from the shop.");
		return;
	}

	if(pPlayer->Block().m_PassiveRemoverUseCooldown > 0)
	{
		int Mins = pPlayer->Block().m_PassiveRemoverUseCooldown / 60;
		int Secs = pPlayer->Block().m_PassiveRemoverUseCooldown % 60;
		char aCooldownBuf[128];
		if(Mins > 0)
			str_format(aCooldownBuf, sizeof(aCooldownBuf), "You can use a Passive Remover again in %d min.", Mins);
		else
			str_format(aCooldownBuf, sizeof(aCooldownBuf), "You can use a Passive Remover again in %d sec.", Secs);
		pSelf->Block().SendChatTarget(pResult->m_ClientId, aCooldownBuf);
		return;
	}

	CPlayer *pTarget = pSelf->Block().GetPlayerByName(pResult->GetString(0));
	if(!pTarget)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player not found.");
		return;
	}

	if(pTarget->GetCid() == pResult->m_ClientId)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't use a passive remover on yourself.");
		return;
	}

	if(pSelf->Block().isInEvent(pTarget->GetCid()))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't use a passive remover on a player in an event.");
		return;
	}

	// Check the target actually has passive
	bool HasPassive = false;
	if(pTarget->Block().IsLoggedIn() && pTarget->Block().GetPlayerPassive() > 0)
		HasPassive = true;
	else if(!pTarget->Block().IsLoggedIn() && pTarget->Block().m_LocalPassiveDuration > 0)
		HasPassive = true;

	if(!HasPassive)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "That player doesn't have passive protection.");
		return;
	}

	// Consume one passive remover
	pPlayer->Block().SetPlayerPassiveRemovers(pPlayer->Block().GetPlayerPassiveRemovers() - 1);

	// Strip target's passive
	if(pTarget->Block().IsLoggedIn())
		pTarget->Block().SetPlayerPassive(0);
	pTarget->Block().m_LocalPassiveDuration = 0;
	pTarget->Block().m_UsePassiveProtection = false;
	pTarget->Block().m_PassivePendingEnable = false;

	// If target's character is currently passive, remove the flag
	CCharacter *pTChar = pTarget->GetCharacter();
	if(pTChar && pTChar->Core()->m_Passive)
		pTChar->Block().Core().m_Passive = false;

	// Set a cooldown before the target can redo the passive race
	pTarget->Block().m_PassiveRaceCooldown = g_Config.m_SvPassiveRemoverCooldown;

	char aBufFrom[128], aBufTo[256];
	str_format(aBufFrom, sizeof(aBufFrom), "You stripped %s's passive protection! %d removers remaining.",
		pSelf->Server()->ClientName(pTarget->GetCid()), pPlayer->Block().GetPlayerPassiveRemovers());
	int CooldownMins = g_Config.m_SvPassiveRemoverCooldown / 60;
	int CooldownSecs = g_Config.m_SvPassiveRemoverCooldown % 60;
	if(g_Config.m_SvPassiveRemoverCooldown > 0)
		str_format(aBufTo, sizeof(aBufTo), "'%s' used a Passive Remover on you! Your passive has been removed. You cannot redo the passive race for %d:%02d minutes.",
			pSelf->Server()->ClientName(pResult->m_ClientId), CooldownMins, CooldownSecs);
	else
		str_format(aBufTo, sizeof(aBufTo), "'%s' used a Passive Remover on you! Your passive has been removed.",
			pSelf->Server()->ClientName(pResult->m_ClientId));
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBufFrom);
	pSelf->Block().SendChatTarget(pTarget->GetCid(), aBufTo);

	// Set use cooldown on the triggerer
	pPlayer->Block().m_PassiveRemoverUseCooldown = g_Config.m_SvPassiveRemoverUseCooldown;
}

void CBlock::ConExp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	char aBuf[256];

	if(!pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}

	static const int s_MaxNum = 17;
	float a = (float)pPlayer->Block().GetPlayerExperience() / NeededAccountExp(pPlayer->Block().GetPlayerLevel());
	int Num = round_to_int(a * s_MaxNum);

	static char s_ExpTopLeft[] = {-30, -107, -108, 0};
	static char s_ExpTopMidFull[] = {-30, -107, -90, 0};
	static char s_ExpTopMidEmpty[] = {-30, -107, -112, 0};
	static char s_ExpTopRight[] = {-30, -107, -105, 0};
	static char s_ExpBotLeft[] = {-30, -107, -102, 0};
	static char s_ExpBotMidFull[] = {-30, -107, -87, 0};
	static char s_ExpBotMidEmpty[] = {-30, -107, -112, 0};
	static char s_ExpBotRight[] = {-30, -107, -99, 0};

	char aBarTop[64];
	char aBarBot[64];

	str_format(aBarTop, sizeof(aBarTop), "%s", s_ExpTopLeft);
	for(int i = 0; i < Num; i++)
		str_format(aBarTop + strlen(aBarTop), sizeof(aBarTop) - strlen(aBarTop), "%s", s_ExpTopMidFull);
	for(int i = 0; i < s_MaxNum - Num; i++)
		str_format(aBarTop + strlen(aBarTop), sizeof(aBarTop) - strlen(aBarTop), "%s", s_ExpTopMidEmpty);
	str_format(aBarTop + strlen(aBarTop), sizeof(aBarTop) - strlen(aBarTop), "%s", s_ExpTopRight);

	str_format(aBarBot, sizeof(aBarBot), "%s", s_ExpBotLeft);
	for(int i = 0; i < Num; i++)
		str_format(aBarBot + strlen(aBarBot), sizeof(aBarBot) - strlen(aBarBot), "%s", s_ExpBotMidFull);
	for(int i = 0; i < s_MaxNum - Num; i++)
		str_format(aBarBot + strlen(aBarBot), sizeof(aBarBot) - strlen(aBarBot), "%s", s_ExpBotMidEmpty);
	str_format(aBarBot + strlen(aBarBot), sizeof(aBarBot) - strlen(aBarBot), "%s", s_ExpBotRight);

	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Experience Bar:");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBarTop);
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBarBot);
	str_format(aBuf, sizeof(aBuf), "Account level: %i", pPlayer->Block().GetPlayerLevel());
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Account Exp: %i", pPlayer->Block().GetPlayerExperience());
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Needed Exp: %i", NeededAccountExp(pPlayer->Block().GetPlayerLevel()));
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
}
void CBlock::ConClanExp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	char aBuf[256];

	if(!pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}
	if(!pPlayer->Block().GetClanId())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not in a clan");
		return;
	}

	// obtain a snapshot copy of the clan data (thread-safe)
	CClansData ClanTmp;
	if(!pSelf->Block().Clans()->GetClanSnapshotById(pPlayer->Block().GetClanId(), ClanTmp))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Error: Something weird happened, try to login again.");
		return;
	}

	static const int s_MaxNum = 17;
	float Ratio = (float)ClanTmp.m_Experience / NeededClanExp(ClanTmp.m_Level);
	int Num = round_to_int(Ratio * s_MaxNum);

	static char s_ExpTopLeft[] = {-30, -107, -108, 0};
	static char s_ExpTopMidFull[] = {-30, -107, -90, 0};
	static char s_ExpTopMidEmpty[] = {-30, -107, -112, 0};
	static char s_ExpTopRight[] = {-30, -107, -105, 0};
	static char s_ExpBotLeft[] = {-30, -107, -102, 0};
	static char s_ExpBotMidFull[] = {-30, -107, -87, 0};
	static char s_ExpBotMidEmpty[] = {-30, -107, -112, 0};
	static char s_ExpBotRight[] = {-30, -107, -99, 0};

	char aBarTop[64];
	char aBarBot[64];

	str_format(aBarTop, sizeof(aBarTop), "%s", s_ExpTopLeft);
	for(int i = 0; i < Num; i++)
		str_format(aBarTop + strlen(aBarTop), sizeof(aBarTop) - strlen(aBarTop), "%s", s_ExpTopMidFull);
	for(int i = 0; i < s_MaxNum - Num; i++)
		str_format(aBarTop + strlen(aBarTop), sizeof(aBarTop) - strlen(aBarTop), "%s", s_ExpTopMidEmpty);
	str_format(aBarTop + strlen(aBarTop), sizeof(aBarTop) - strlen(aBarTop), "%s", s_ExpTopRight);

	str_format(aBarBot, sizeof(aBarBot), "%s", s_ExpBotLeft);
	for(int i = 0; i < Num; i++)
		str_format(aBarBot + strlen(aBarBot), sizeof(aBarBot) - strlen(aBarBot), "%s", s_ExpBotMidFull);
	for(int i = 0; i < s_MaxNum - Num; i++)
		str_format(aBarBot + strlen(aBarBot), sizeof(aBarBot) - strlen(aBarBot), "%s", s_ExpBotMidEmpty);
	str_format(aBarBot + strlen(aBarBot), sizeof(aBarBot) - strlen(aBarBot), "%s", s_ExpBotRight);

	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Clan Experience Bar:");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBarTop);
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBarBot);
	str_format(aBuf, sizeof(aBuf), "Clan level: %i", ClanTmp.m_Level);
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Clan Exp: %i", ClanTmp.m_Experience);
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Needed Exp: %i", NeededClanExp(ClanTmp.m_Level));
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
}

void CBlock::ConClanList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You must be logged in.");
		return;
	}
	if(pPlayer->Block().GetClanId() <= 0)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not in a clan.");
		return;
	}

	pSelf->Block().Clans()->ShowClanMembers(pResult->m_ClientId, pPlayer->Block().GetClanId());
}

void CBlock::ConClanHelp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Clan system commands:");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_create <name> - Create a new clan");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_delete - Delete your clan (leader only)");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_leave - Leave your clan");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_invite <player> - Invite a player");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_accept | /clan_decline - Respond to invite");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_kick <player> - Kick a member (leader/co-leader)");
	// Updated role command
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_role <player> <member|coleader> - Set role (leader only)");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_rename <newname> - Rename clan (leader only)");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_transfer <player> - Transfer clan leadership (leader only)");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_exp - Show clan EXP progress");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/clan_list - List clan members");
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Max members per clan: %d", g_Config.m_SvClanMaxMembers);
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Rename price: %d BP", g_Config.m_SvClanRenamePrice);
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Create price: %d BP", g_Config.m_SvClanCreatePrice);
	pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
}

void CBlock::ConAccountHelp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system commands:");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/register <name> <pass> - Create an account");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/login <name> <pass> - Log in");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/logout - Log out");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/password <old> <new> - Change password");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/exp - Show your EXP");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/profile [name] - View a profile");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/bp - Show your blockpoints");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/give_bp <player> <amount> - Offer BP transfer");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "/accept_bp [player] | /decline_bp [player] - Respond to BP transfer");
}

void CBlock::ConBuy(IConsole::IResult *pResult, void *pUserData)
{
	// // test command - replace that with tiles

	// CBlock *pBw = (CBlock *)pUserData;
	// CGameContext *pSelf = pBw->GameServer();
	// CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	// CCharacter *pChr = pPlayer->GetCharacter();

	// if(!pPlayer)
	// {
	// 	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player not found.");
	// 	return;
	// }

	// int Category;
	// bool Found = false;
	// int CosmeticId = -1;

	// std::string Type = pResult->GetString(0);
	// std::string Name = pResult->GetString(1);

	// if(Type == "gd")
	// {
	// 	CosmeticId = pSelf->Cosmetics()->FindGundesign(Name.c_str());
	// 	Category = CShop::CATEGORY_GUNDESIGN;
	// 	Found = (CosmeticId != -1);
	// }
	// else if(Type == "ko")
	// {
	// 	CosmeticId = pSelf->Cosmetics()->FindKnockoutEffect(Name.c_str());
	// 	Category = CShop::CATEGORY_KNOCKOUT;
	// 	Found = (CosmeticId != -1);
	// }
	// else if(Type == "sm")
	// {
	// 	CosmeticId = pSelf->Cosmetics()->FindSkinmani(Name.c_str());
	// 	Category = CShop::CATEGORY_SKINMANI;
	// 	Found = (CosmeticId != -1);
	// }
	// else
	// {
	// 	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Unknown cosmetics type. Use 'ko' (knockout), 'gd' (gundesign), or 'sm' (skinmani).");
	// 	return;
	// }

	// if(!Found)
	// {
	// 	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Unknown cosmetics name.");
	// 	return;
	// }

	// if(pChr->m_PendingPurchase == nullptr)
	// {
	// 	new CShop(pSelf, pPlayer, Category, CosmeticId, 15);
	// 	// register shop request so it's stored centrally
	// 	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	// 	{
	// 		requests->CreateShopRequest(pPlayer->GetCid(), Category, CosmeticId, 0, 15);
	// 	}
	// 	// pSelf->Block().SendChatTarget(pResult->m_ClientId, "Purchase initiated. Confirm with /yes or cancel with /no.");
	// }
	// else
	// {
	// 	pSelf->Block().SendChatTarget(pResult->m_ClientId, "pendingpurchase isn't null");
	// }
}

void CBlock::ConShopPurchase(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pPlayer && pPlayer->GetCharacter() && pPlayer->GetCharacter()->Block().m_PendingPurchase)
	{
		pPlayer->GetCharacter()->Block().m_PendingPurchase->Purchase();
	}
	else
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No item available for purchase.");
	}
}

void CBlock::ConShopDecline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pPlayer && pPlayer->GetCharacter() && pPlayer->GetCharacter()->Block().m_PendingPurchase)
	{
		pPlayer->GetCharacter()->Block().m_PendingPurchase->Decline();
	}
	else
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No item available to decline.");
	}
}

void CBlock::ConClanInvite(IConsole::IResult *pResult, void *pUserData)
{
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer || !pPlayer->Block().IsLoggedIn() || !pPlayer->GetCharacter())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}

	if(pPlayer->Block().GetClanId() == 0)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not in a clan to invite others.");
		return;
	}

	if(pPlayer->Block().GetAuthLevel() < ClanAuthLevel::COLEADER)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You need to be the leader or co-leader to invite others.");
		return;
	}

	const int CooldownSeconds = g_Config.m_SvClanInviteCooldown; // configured cooldown
	if(pPlayer->Block().m_LastClanInviteTick != 0 && pSelf->Server()->Tick() - pPlayer->Block().m_LastClanInviteTick < CooldownSeconds * pSelf->Server()->TickSpeed())
	{
		int Rem = (int)((CooldownSeconds * pSelf->Server()->TickSpeed() - (pSelf->Server()->Tick() - pPlayer->Block().m_LastClanInviteTick)) / pSelf->Server()->TickSpeed());
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another clan invite.", Rem, Rem != 1 ? "s" : "");
		pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
		return;
	}

	const char *pName = pResult->GetString(0);
	if(!pName || pName[0] == '\0')
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Usage: /clan_invite <playername>");
		return;
	}

	int Target = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pSelf->m_apPlayers[i] && !str_comp(pName, pSelf->Server()->ClientName(i)))
		{
			Target = i;
			break;
		}
	}

	if(Target == -1)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player not found or not logged in.");
		return;
	}

	CPlayer *pTargetPlayer = pSelf->m_apPlayers[Target];

	if(!pTargetPlayer || !pTargetPlayer->GetCharacter())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Target player is not available.");
		return;
	}
	if(!pTargetPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "This player is not logged in.");
		return;
	}

	if(Target == pPlayer->GetCid())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You cannot invite yourself.");
		return;
	}

	if(pTargetPlayer->Block().GetClanId() != 0)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "This player is already in a clan.");
		return;
	}

	if(pTargetPlayer->Block().GetPlayerLevel() < 10)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "This player must be at least level 10 to join a clan.");
		return;
	}

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		auto Incoming = Requests->GetRequestIdsTo(Target, CRequests::SRequest::EType::Clan);
		if(!Incoming.empty())
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player already has a pending clan invitation.");
			return;
		}

		int Id = Requests->CreateClanInvite(pPlayer->GetCid(), Target, pPlayer->Block().GetClanId(), g_Config.m_SvClanInviteExpiry);
		if(Id < 0)
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to send clan invitation.");
			return;
		}
		pPlayer->Block().m_LastClanInviteTick = pSelf->Server()->Tick();
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Clan invitation sent to %s.", pName);
		pSelf->Block().SendChatTarget(pResult->m_ClientId, aBuf);
		return;
	}
	else
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
		return;
	}
}

void CBlock::ConClanAccept(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You must be logged in to accept a clan invite.");
		return;
	}
	if(pPlayer->Block().GetClanId() != 0)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are already in a clan.");
		return;
	}

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		auto Ids = Requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::Clan);
		if(Ids.empty())
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "Nobody invited you!");
		}
		else
		{
			// accept the most recent invite (last id)
			int Id = Ids.back();
			if(!Requests->AcceptRequest(Id))
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to accept invite.");
			else
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Clan invite accepted.");
		}
	}
	else
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
	}
}

void CBlock::ConClanDecline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You must be logged in to decline a clan invite.");
		return;
	}

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		auto Ids = Requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::Clan);
		if(Ids.empty())
		{
			pSelf->Block().SendChatTarget(pResult->m_ClientId, "Nobody invited you!");
		}
		else
		{
			int Id = Ids.back();
			if(!Requests->DeclineRequest(Id))
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to decline invite.");
			else
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Clan invite declined.");
		}
	}
	else
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
	}
}

void CBlock::ConClanCreate(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(ClientId, "You must be logged in to create a clan!");
		return;
	}
	if(pPlayer->Block().m_Account.m_Level < g_Config.m_SvClanMinLevel)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "You must be at least level %d to create a clan!", g_Config.m_SvClanMinLevel);
		pSelf->Block().SendChatTarget(ClientId, aBuf);
		return;
	}
	if(pPlayer->Block().m_Account.m_ClanId > 0)
	{
		pSelf->Block().SendChatTarget(ClientId, "You are already in a clan!");
		return;
	}

	if(g_Config.m_SvClanCreatePrice > 0)
	{
		int Cost = g_Config.m_SvClanCreatePrice;
		if(pPlayer->Block().GetPlayerBlockpoints() < Cost)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "You need %d blockpoints to create a clan.", Cost);
			pSelf->Block().SendChatTarget(ClientId, aBuf);
			return;
		}
	}

	const char *pClanName = pResult->GetString(0);
	int ClanNameLength = str_length(pClanName);

	if(ClanNameLength < 3)
	{
		pSelf->Block().SendChatTarget(ClientId, "Clan name must be at least 3 characters long!");
		return;
	}
	if(ClanNameLength > BLOCK_CLAN_NAME_MAX_LENGTH)
	{
		pSelf->Block().SendChatTarget(ClientId, "Clan name is too long (max 11 characters)!");
		return;
	}

	if(pSelf->Block().Clans()->GetClanIdByName(pClanName) != -1)
	{
		pSelf->Block().SendChatTarget(ClientId, "This clan name is already taken!");
		return;
	}

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		Requests->CreateClanCreateConfirm(ClientId, pClanName, g_Config.m_SvClanConfirmExpiry);
		return; // request system will notify
	}

	pSelf->Block().SendChatTarget(ClientId, "Clan creation failed: request system unavailable.");
}

void CBlock::ConClanDelete(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(ClientId, "You must be logged in to delete a clan.");
		return;
	}

	dbg_msg("clan", "clan_delete: clanid=%d auth=%d", pPlayer->Block().m_Account.m_ClanId, (int)pPlayer->Block().m_Account.m_AuthLevel);

	if(pPlayer->Block().m_Account.m_ClanId < 1 || pPlayer->Block().m_Account.m_AuthLevel != ClanAuthLevel::LEADER)
	{
		pSelf->Block().SendChatTarget(ClientId, "You are either not in a clan or not its leader.");
		return;
	}

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		Requests->CreateClanDeleteConfirm(ClientId, pPlayer->Block().GetClanId(), g_Config.m_SvClanConfirmExpiry);
		return; // message sent by requests
	}
}

void CBlock::ConClanRemove(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(ClientId, "You must be logged in to remove a player from the clan.");
		return;
	}

	if(pPlayer->Block().m_Account.m_ClanId < 1 || pPlayer->Block().m_Account.m_AuthLevel < ClanAuthLevel::COLEADER)
	{
		pSelf->Block().SendChatTarget(ClientId, "You are not authorized to remove members from this clan.");
		return;
	}

	const char *pTargetName = pResult->GetString(0);
	if(!pTargetName || pTargetName[0] == '\0')
	{
		pSelf->Block().SendChatTarget(ClientId, "Usage: /clan_kick <player>");
		return;
	}

	bool FoundOnline = false;
	int TargetClientId = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pSelf->m_apPlayers[i] && !str_comp(pTargetName, pSelf->Server()->ClientName(i)))
		{
			TargetClientId = i;
			FoundOnline = true;
			break;
		}
	}

	if(FoundOnline)
	{
		CPlayer *pTargetPlayer = pSelf->m_apPlayers[TargetClientId];
		if(!pTargetPlayer || !pTargetPlayer->Block().m_Account.m_Id)
		{
			pSelf->Block().SendChatTarget(ClientId, "The target player is not logged in.");
			return;
		}
		if(pPlayer->Block().m_Account.m_ClanId != pTargetPlayer->Block().m_Account.m_ClanId)
		{
			pSelf->Block().SendChatTarget(ClientId, "The target player is not in your clan.");
			return;
		}
		if(ClientId == TargetClientId)
		{
			pSelf->Block().SendChatTarget(ClientId, "You cannot remove yourself from the clan.");
			return;
		}
		if(pTargetPlayer->Block().m_Account.m_AuthLevel >= ClanAuthLevel::COLEADER)
		{
			pSelf->Block().SendChatTarget(ClientId, "You cannot remove a leader or co-leader from the clan.");
			return;
		}
		if(auto Requests = g_ComponentRegistry.Get<CRequests>())
		{
			Requests->CreateClanKickConfirm(ClientId, pPlayer->Block().m_Account.m_ClanId, pTargetPlayer->Block().m_Account.m_aName, g_Config.m_SvClanConfirmExpiry);
			return; // message sent by requests
		}
		return; // fallback (no requests component)
	}

	// Offline target path: allow specifying an account name that is currently offline.
	// We cannot fully verify membership/auth synchronously here; RemoveFromClanThread will do validation.
	// Basic self-check to avoid kicking self by own account name.
	if(!str_comp(pTargetName, pPlayer->Block().m_Account.m_aName))
	{
		pSelf->Block().SendChatTarget(ClientId, "You cannot remove yourself from the clan.");
		return;
	}

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		Requests->CreateClanKickConfirm(ClientId, pPlayer->Block().m_Account.m_ClanId, pTargetName, g_Config.m_SvClanConfirmExpiry);
		char aBuf[160];
		str_format(aBuf, sizeof(aBuf), "Kick confirmation for offline member '%s' created. Type /clan_yes to confirm.", pTargetName);
		pSelf->Block().SendChatTarget(ClientId, aBuf);
		return;
	}
	pSelf->Block().SendChatTarget(ClientId, "Kick failed: request system unavailable.");
}

void CBlock::ConClanTransfer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(ClientId, "You must be logged in to transfer clan leadership.");
		return;
	}

	if(pPlayer->Block().m_Account.m_ClanId < 1 || pPlayer->Block().m_Account.m_AuthLevel != ClanAuthLevel::LEADER)
	{
		pSelf->Block().SendChatTarget(ClientId, "You are either not in a clan or not its leader.");
		return;
	}

	const char *pTargetName = pResult->GetString(0);
	if(!pTargetName || pTargetName[0] == '\0')
	{
		pSelf->Block().SendChatTarget(ClientId, "Usage: /clan_transfer <player>");
		return;
	}

	int TargetClientId = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pSelf->m_apPlayers[i] && !str_comp(pTargetName, pSelf->Server()->ClientName(i)))
		{
			TargetClientId = i;
			break;
		}
	}

	if(TargetClientId == -1)
	{
		pSelf->Block().SendChatTarget(ClientId, "Player not found or not online.");
		return;
	}

	CPlayer *pTargetPlayer = pSelf->m_apPlayers[TargetClientId];
	if(!pTargetPlayer || !pTargetPlayer->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(ClientId, "Target player must be logged in.");
		return;
	}

	if(pTargetPlayer->Block().m_Account.m_ClanId != pPlayer->Block().m_Account.m_ClanId)
	{
		pSelf->Block().SendChatTarget(ClientId, "Target player is not in your clan.");
		return;
	}

	if(TargetClientId == ClientId)
	{
		pSelf->Block().SendChatTarget(ClientId, "You cannot transfer leadership to yourself.");
		return;
	}

	if(pTargetPlayer->Block().GetAuthLevel() == ClanAuthLevel::LEADER)
	{
		pSelf->Block().SendChatTarget(ClientId, "Target is already a leader."); // shouldn't happen
		return;
	}

	auto Requests = g_ComponentRegistry.Get<CRequests>();
	if(!Requests)
	{
		pSelf->Block().SendChatTarget(ClientId, "Requests subsystem unavailable.");
		return;
	}
	int Id = Requests->CreateClanTransferConfirm(ClientId, pPlayer->Block().GetClanId(), pTargetPlayer->Block().m_Account.m_aName, g_Config.m_SvClanConfirmExpiry);
	if(Id < 0)
	{
		pSelf->Block().SendChatTarget(ClientId, "Failed to initiate clan transfer confirmation.");
		return;
	}
	pSelf->Block().SendChatTarget(ClientId, "Transfer confirmation sent. Type /clan_yes to confirm or /clan_no to cancel.");
}

// /clan_yes: confirm last self-addressed clan confirmation (delete/kick)
void CBlock::ConClanYes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	auto Requests = g_ComponentRegistry.Get<CRequests>();
	if(!Requests)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
		return;
	}
	auto Ids = Requests->GetRequestIdsTo(pResult->m_ClientId, std::nullopt);
	// find the most recent applicable clan confirm addressed to self
	int Chosen = -1;
	for(int Id : Ids)
	{
		CRequests::SRequest Info;
		if(!Requests->GetRequestInfo(Id, Info))
			continue;
		if(Info.m_To != pResult->m_ClientId || Info.m_From != pResult->m_ClientId)
			continue;
		if(Info.m_Type == CRequests::SRequest::EType::ClanDeleteConfirm || Info.m_Type == CRequests::SRequest::EType::ClanKickConfirm || Info.m_Type == CRequests::SRequest::EType::ClanRenameConfirm || Info.m_Type == CRequests::SRequest::EType::ClanCreateConfirm || Info.m_Type == CRequests::SRequest::EType::ClanTransferConfirm)
		{
			if(Info.m_ExpireTick > pSelf->Server()->Tick())
				Chosen = std::max(Chosen, Id);
		}
	}
	if(Chosen == -1)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No pending clan confirmation to accept.");
		return;
	}
	if(!Requests->AcceptRequest(Chosen))
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to accept confirmation.");
}

// /clan_no: decline last self-addressed clan confirmation
void CBlock::ConClanNo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	auto Requests = g_ComponentRegistry.Get<CRequests>();
	if(!Requests)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
		return;
	}
	auto Ids = Requests->GetRequestIdsTo(pResult->m_ClientId, std::nullopt);
	int Chosen = -1;
	for(int Id : Ids)
	{
		CRequests::SRequest Info;
		if(!Requests->GetRequestInfo(Id, Info))
			continue;
		if(Info.m_To != pResult->m_ClientId || Info.m_From != pResult->m_ClientId)
			continue;
		if(Info.m_Type == CRequests::SRequest::EType::ClanDeleteConfirm || Info.m_Type == CRequests::SRequest::EType::ClanKickConfirm || Info.m_Type == CRequests::SRequest::EType::ClanRenameConfirm || Info.m_Type == CRequests::SRequest::EType::ClanCreateConfirm || Info.m_Type == CRequests::SRequest::EType::ClanTransferConfirm)
		{
			if(Info.m_ExpireTick > pSelf->Server()->Tick())
				Chosen = std::max(Chosen, Id);
		}
	}
	if(Chosen == -1)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No pending clan confirmation to decline.");
		return;
	}
	if(!Requests->DeclineRequest(Chosen))
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to decline confirmation.");
}

void CBlock::ConClanLeave(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(ClientId, "You must be logged in to leave a clan.");
		return;
	}

	if(pPlayer->Block().m_Account.m_ClanId < 1)
	{
		pSelf->Block().SendChatTarget(ClientId, "You are not in a clan.");
		return;
	}

	if(pPlayer->Block().m_Account.m_AuthLevel == ClanAuthLevel::LEADER)
	{
		pSelf->Block().SendChatTarget(ClientId, "The clan leader cannot leave. You must delete the clan or transfer leadership.");
		return;
	}

	pSelf->Block().Clans()->ClanLeave(ClientId);
}

void CBlock::ConClanRole(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int IssuerId = pResult->m_ClientId;
	CPlayer *pIssuer = pSelf->m_apPlayers[IssuerId];
	if(!pIssuer)
		return;

	if(!pIssuer->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(IssuerId, "You must be logged in to change a player's clan role.");
		return;
	}

	if(pIssuer->Block().m_Account.m_ClanId < 1 || pIssuer->Block().m_Account.m_AuthLevel < ClanAuthLevel::LEADER)
	{
		pSelf->Block().SendChatTarget(IssuerId, "Only the clan leader can set roles.");
		return;
	}

	const char *pTargetName = pResult->GetString(0);
	const char *pRole = pResult->GetString(1);

	// map role string -> numeric auth level
	int NewAuthLevel = 0;
	{
		char aRole[32];
		str_copy(aRole, pRole, sizeof(aRole));
		// lowercase in-place
		for(char *c = aRole; *c; ++c)
		{
			if(*c >= 'A' && *c <= 'Z')
				*c = (char)(*c - 'A' + 'a');
		}
		if(!str_comp(aRole, "member"))
			NewAuthLevel = 1;
		else if(!str_comp(aRole, "coleader") || !str_comp(aRole, "co-leader") || !str_comp(aRole, "co_leader"))
			NewAuthLevel = 2;
		else if(!str_comp(aRole, "leader"))
		{
			pSelf->Block().SendChatTarget(IssuerId, "Use /clan_transfer to transfer leadership.");
			return;
		}
		else
		{
			pSelf->Block().SendChatTarget(IssuerId, "Invalid role. Allowed values: member | coleader");
			return;
		}
	}

	bool FoundTarget = false;
	int TargetClientId = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pSelf->m_apPlayers[i] && !str_comp(pTargetName, pSelf->Server()->ClientName(i)))
		{
			TargetClientId = i;
			FoundTarget = true;
			break;
		}
	}

	if(FoundTarget)
	{
		CPlayer *pTargetPlayer = pSelf->m_apPlayers[TargetClientId];

		if(!pTargetPlayer->Block().m_Account.m_Id)
		{
			pSelf->Block().SendChatTarget(IssuerId, "The target player is not logged in.");
			return;
		}

		if(pIssuer->Block().m_Account.m_ClanId != pTargetPlayer->Block().m_Account.m_ClanId)
		{
			pSelf->Block().SendChatTarget(IssuerId, "The target player is not in your clan.");
			return;
		}

		if(IssuerId == TargetClientId)
		{
			pSelf->Block().SendChatTarget(IssuerId, "You cannot change your own role.");
			return;
		}

		if(NewAuthLevel < 1 || NewAuthLevel > 2)
		{
			pSelf->Block().SendChatTarget(IssuerId, "Invalid role. Allowed values: member | coleader");
			return;
		}

		if(pTargetPlayer->Block().m_Account.m_AuthLevel == ClanAuthLevel::LEADER)
		{
			pSelf->Block().SendChatTarget(IssuerId, "You cannot change the role of the clan leader.");
			return;
		}

		if(pTargetPlayer->Block().m_Account.m_AuthLevel == static_cast<ClanAuthLevel>(NewAuthLevel))
		{
			pSelf->Block().SendChatTarget(IssuerId, "The player already has that role.");
			return;
		}

		pSelf->Block().Clans()->SetAuthLevel(IssuerId, pTargetPlayer->Block().m_Account.m_aName, NewAuthLevel, pIssuer->Block().m_Account.m_ClanId);
		return;
	}

	if(!str_comp(pTargetName, pIssuer->Block().m_Account.m_aName))
	{
		pSelf->Block().SendChatTarget(IssuerId, "You cannot change your own role.");
		return;
	}

	if(NewAuthLevel < 1 || NewAuthLevel > 2)
	{
		pSelf->Block().SendChatTarget(IssuerId, "Invalid role. Allowed values: member | coleader");
		return;
	}

	pSelf->Block().Clans()->SetAuthLevel(IssuerId, pTargetName, NewAuthLevel, pIssuer->Block().m_Account.m_ClanId);
}

void CBlock::ConClanRename(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Block().m_Account.m_Id)
	{
		pSelf->Block().SendChatTarget(ClientId, "You must be logged in to rename a clan.");
		return;
	}

	if(pPlayer->Block().m_Account.m_ClanId < 1 || pPlayer->Block().m_Account.m_AuthLevel != ClanAuthLevel::LEADER)
	{
		pSelf->Block().SendChatTarget(ClientId, "Only the clan leader can rename the clan.");
		return;
	}

	const char *pNewClanName = pResult->GetString(0);
	int ClanNameLength = str_length(pNewClanName);

	if(ClanNameLength < 3)
	{
		pSelf->Block().SendChatTarget(ClientId, "Clan name must be at least 3 characters long!");
		return;
	}
	if(ClanNameLength > BLOCK_CLAN_NAME_MAX_LENGTH)
	{
		pSelf->Block().SendChatTarget(ClientId, "Clan name is too long (max 11 characters)!");
		return;
	}

	if(pSelf->Block().Clans()->GetClanIdByName(pNewClanName) != -1)
	{
		pSelf->Block().SendChatTarget(ClientId, "This clan name is already taken!");
		return;
	}

	if(g_Config.m_SvClanRenamePrice > 0)
	{
		int Cost = g_Config.m_SvClanRenamePrice;
		if(pPlayer->Block().GetPlayerBlockpoints() < Cost)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "You need %d blockpoints to rename your clan.", Cost);
			pSelf->Block().SendChatTarget(ClientId, aBuf);
			return;
		}
	}

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		std::string OldName = pSelf->Block().Clans()->GetClanNameCopy(pPlayer->Block().m_Account.m_ClanId);
		Requests->CreateClanRenameConfirm(ClientId, pPlayer->Block().m_Account.m_ClanId, OldName.c_str(), pNewClanName, g_Config.m_SvClanConfirmExpiry);
		return; // request system will notify hopefully ;(
	}

	pSelf->Block().SendChatTarget(ClientId, "Clan rename failed: request system unavailable.");
}

void CBlock::ConDisplayTopClans(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Block().Clans()->ShowTopClans(ClientId);
}

void CBlock::ConContributors(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Huge thanks to Block contributors:");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "melon, Anime.pdf, zhn, ReiTW, Brokecdx-, Sakido, Gegongt, noby, potato, qxdFox");
}

void CBlock::ConCredits(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "This server has been developed by Nouaa.");
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Thanks to /contributors.");
}

void CBlock::Con1on1(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	if(!g_Config.m_Sv1on1system)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");
		return;
	}

	if(!pResult->NumArguments())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Challenge another player by writing '/1on1 name (blockpoints)'");
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "An example would be \"/1on1 nameless tee\" or \"/1on1 marcella 30\"");
		return;
	}

	const char *pEnemyName = pResult->GetString(0);
	int Wager = pResult->GetInteger(1);

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CPlayer *pTarget = pSelf->Block().GetPlayerByName(pEnemyName);

	bool HasArenas = !pSelf->Block().ZoneManager()->GetNamedQuadCenters("1on1_spawn").empty() ||
			 pSelf->Block().ZoneManager()->Get1on1ArenaCount() > 0;

	// some errors handling
	if(!pTarget)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player not found");
		return;
	}
	if(pTarget->GetCid() == pResult->m_ClientId)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't start a 1vs1 against yourself.");
		return;
	}
	if(Wager < 0)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "The amount set for the Wager must be more than 0 or none");
		return;
	}

	if(!pPlayer->Block().IsLoggedIn() && Wager > 0)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You have to be logged in to place a wager in the pot.");
		return;
	}
	if(Wager > 0 && (!pTarget->Block().IsLoggedIn()))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Target player must be logged in to play with a wager.");
		return;
	}
	if(Wager > pPlayer->Block().GetPlayerBlockpoints())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You can't afford to wager that much!");
		return;
	}

	if(Wager > pTarget->Block().GetPlayerBlockpoints())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player doesn't have enough blockpoints.");
		return;
	}

	if(pSelf->Block().isInEvent(pResult->m_ClientId))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You must finish the ongoing 1on1 match.");
		return;
	}

	if(pSelf->Block().isInEvent(pTarget->GetCid()))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "This player is already in a 1on1 match.");
		return;
	}

	char aBuf[256];
	if(!HasArenas)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Error: This map does not have any 1on1 spawn positions defined.");
		return;
	}

	// create invite via requests component
	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		{
			int Id = Requests->Create1on1Invite(pPlayer->GetCid(), pTarget->GetCid(), Wager, g_Config.m_Sv1on1InviteExpiry);
			if(Id == -1)
				return; // Create1on1Invite already informed the sender about the duplicate
			pPlayer->Block().m_Sent1on1InviteTo = pTarget->GetCid();
			str_format(aBuf, sizeof(aBuf), "Match request has been sent to '%s' (%d BP).", pSelf->Server()->ClientName(pTarget->GetCid()), Wager);
			pSelf->Block().SendChatTarget(pPlayer->GetCid(), aBuf);
		}
		return;
	}
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
}

void CBlock::Con1on1Accept(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	if(!g_Config.m_Sv1on1system)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");
		return;
	}

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		if(pResult->NumArguments() > 0 && pResult->GetString(0)[0])
		{
			const char *Arg = pResult->GetString(0);
			CPlayer *pFrom = pSelf->Block().GetPlayerByName(Arg);
			if(!pFrom)
			{
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player not found");
				return;
			}

			auto Ids = Requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
			if(Ids.empty())
			{
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "No invitation from that player was found.");
				return;
			}

			// choose the most recent valid one (highest id -> newest)
			int Chosen = -1;
			for(int Id : Ids)
			{
				CRequests::SRequest Info;
				if(Requests->GetRequestInfo(Id, Info))
				{
					if(Info.m_ExpireTick > pSelf->Server()->Tick())
						Chosen = std::max(Chosen, Id);
				}
			}
			if(Chosen == -1)
			{
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "No active (non-expired) invitation from that player was found.");
				return;
			}

			if(!Requests->AcceptRequest(Chosen))
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to accept the invitation (it may have expired).");
			return;
		}

		auto Pending = Requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
		if(Pending.size() == 1)
		{
			int Id = Pending[0];
			CRequests::SRequest Info;
			if(!Requests->GetRequestInfo(Id, Info) || Info.m_ExpireTick <= pSelf->Server()->Tick())
			{
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "No active invitation to accept was found (it may have expired).");
				return;
			}
			if(!Requests->AcceptRequest(Id))
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to accept the invitation (it may have expired).");
			return;
		}

		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No invitation to accept was found (try to use /accept <playerName>).");
		return;
	}
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
}

void CBlock::Con1on1Decline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	if(!g_Config.m_Sv1on1system)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");
		return;
	}

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
	{
		if(pResult->NumArguments() > 0 && pResult->GetString(0)[0])
		{
			const char *Arg = pResult->GetString(0);
			CPlayer *pFrom = pSelf->Block().GetPlayerByName(Arg);
			if(!pFrom)
			{
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Player not found");
				return;
			}

			auto Ids = Requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
			if(Ids.empty())
			{
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "No invitation from that player was found.");
				return;
			}

			int Chosen = -1;
			for(int Id : Ids)
			{
				CRequests::SRequest Info;
				if(Requests->GetRequestInfo(Id, Info))
				{
					if(Info.m_ExpireTick > pSelf->Server()->Tick())
						Chosen = std::max(Chosen, Id);
				}
			}
			if(Chosen == -1)
			{
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "No active (non-expired) invitation from that player was found.");
				return;
			}

			if(!Requests->DeclineRequest(Chosen))
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to decline the invitation (it may have expired).");
			return;
		}

		auto Pending = Requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
		if(Pending.size() == 1)
		{
			int Id = Pending[0];
			CRequests::SRequest Info;
			if(!Requests->GetRequestInfo(Id, Info) || Info.m_ExpireTick <= pSelf->Server()->Tick())
			{
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "No active invitation to decline was found (it may have expired).");
				return;
			}
			if(!Requests->DeclineRequest(Id))
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to decline the invitation (it may have expired).");
			return;
		}

		pSelf->Block().SendChatTarget(pResult->m_ClientId, "No invitation to decline was found (or use /decline <playerName>). Try checking your messages.");
		return;
	}
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
}

void CBlock::Con1on1Ready(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	if(!g_Config.m_Sv1on1system)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");
		return;
	}

	auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>();
	if(!Mgr)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "1on1 system is not available.");
		return;
	}

	auto Match = Mgr->GetMatchForPlayer(pResult->m_ClientId);
	if(!Match || !Match->IsInConfigPhase())
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not in a 1on1 warmup phase.");
		return;
	}

	// /ready now casts a "Start" vote (same as pressing F3 in the vote overlay)
	Match->OnDuelVote(pResult->m_ClientId, 1);
}

void CBlock::ConJoinEvent(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "use /join instead.");
}

void CBlock::ConCreateTDM(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	if(pSelf->Block().isInEvent(pResult->m_ClientId))
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "You must finish the ongoing event (or use '/leave' to leave).");
		return;
	}

	if(auto Events = g_ComponentRegistry.Get<CEvents>())
	{
		auto Ev = Events->CreateEventByName("tdm");
		if(Ev)
		{
			Ev->SetStateChangeCallback([](auto, auto) {});
			Events->SetActiveEvent(Ev);
			return;
		}
	}
	pSelf->Block().SendChatTarget(pResult->m_ClientId, "Failed to create TDM event: events subsystem unavailable.");
}

void CBlock::ConLeaveEvent(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
	{
		pSelf->Block().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer)
		return;

	// check if player is in a 1on1 match (any phase)
	if(auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>(); Mgr)
	{
		auto Match = Mgr->GetMatchForPlayer(pResult->m_ClientId);
		if(Match)
		{
			auto State = Match->GetState();
			if(State == COneOnOneEvent::EEventState::Preparation)
			{
				// during warmup/config phase - cancel without penalty, no escrow was collected
				int OtherCid = (pResult->m_ClientId == Match->m_Player1ID) ? Match->m_Player2ID : Match->m_Player1ID;
				pSelf->Block().SendChatTarget(pResult->m_ClientId, "[1on1] You left the warmup. Match cancelled.");
				pSelf->Block().SendChatTarget(OtherCid, "[1on1] Your opponent left during warmup. Match cancelled.");

				// clear duel config vote pages for both

				for(int Cid : {Match->m_Player1ID, Match->m_Player2ID})
				{
					g_VoteManager.NavigateToRoot(Cid);
					pSelf->Block().ClearVotes(Cid);
				}

				Match->AbortAndRefund(nullptr);
				return;
			}
			else if(State == COneOnOneEvent::EEventState::Active)
			{
				// during active match - leave counts as ragequit (opponent wins)
				Match->Leave(pResult->m_ClientId);
				return;
			}
		}
	}

	if(auto Events = g_ComponentRegistry.Get<CEvents>(); Events)
	{
		auto Subs = Events->GetSubComponents();
		bool Found = false;
		for(auto &Sub : Subs)
		{
			CEventComponent *pEv = dynamic_cast<CEventComponent *>(Sub.operator->());
			if(!pEv)
				continue;
			// try to leave active participation first
			if(pEv->Leave(pPlayer->GetCid()))
			{
				Found = true;
				continue;
			}
			// if not participating, try to deregister from registration phase
			if(pEv->DeRegister(pPlayer->GetCid()))
			{
				Found = true;
				continue;
			}
		}
		if(Found)
			return;
	}

	pSelf->Block().SendChatTarget(pResult->m_ClientId, "You are not in any event!");
}

// Components

void CBlock::ConComponentList(IConsole::IResult *pResult, void *pUserData)
{
	auto Components = g_ComponentRegistry.All();
	auto ActiveComponents = g_ComponentRegistry.Active();

	std::unordered_set<CComponent *> MainComponentPtrs;
	for(const auto &[Type, pSharedComp] : Components)
	{
		if(pSharedComp)
			MainComponentPtrs.insert(&*pSharedComp);
	}

	dbg_msg("Components", "Registered Components");
	for(const auto &[Type, pSharedComp] : Components)
	{
		auto Name = g_ComponentRegistry.Name(Type);
		const bool IsActive = (bool)pSharedComp; // lol
		dbg_msg("Components", "[%s] %s", IsActive ? "+" : " ", Name.c_str());
	}

	dbg_msg("Components", "Active Sub-Components");
	for(const auto &pActiveComp : ActiveComponents)
	{
		if(!MainComponentPtrs.contains(&*pActiveComp))
			dbg_msg("Components", "[+] %s", pActiveComp->GetName());
	}
}

void CBlock::ConComponentPlug(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	char aName[64];
	str_copy(aName, pResult->GetString(0));
	str_clean_whitespaces(aName);

	if(!pSelf->m_pScore)
	{
		pSelf->Block().m_ComponentsQueue.emplace(aName);
		return;
	}

	auto pComponent = g_ComponentRegistry.Create(aName, static_cast<CGameContext *>(pUserData));
	if(!pComponent)
	{
		dbg_msg("Components", "Component creation failed: %s", aName);
		return;
	}
	dbg_msg("Components", "Component created: %s (%p)", pComponent->GetName(), &*pComponent);
}

void CBlock::ConComponentUnPlug(IConsole::IResult *pResult, void *pUserData)
{
	char aName[64];
	str_copy(aName, pResult->GetString(0));
	str_clean_whitespaces(aName);

	bool Removed = g_ComponentRegistry.Remove(aName);
	if(Removed)
	{
		dbg_msg("Components", "Component removed: %s", aName);
		return;
	}
	dbg_msg("Components", "Component removal failed");
}

void CBlock::ConDisplayPages(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer || !pPlayer->Block().IsLoggedIn())
	{
		pSelf->Block().SendChatTarget(ClientId, "You must be logged in to use /pages.");
		return;
	}
	int Pages = pPlayer->Block().GetPlayerPages();
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "You have %d deathnote page%s.", Pages, Pages == 1 ? "" : "s");
	pSelf->Block().SendChatTarget(ClientId, aBuf);
}

void CBlock::ConPassive(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	int SecondsLeft = 0;
	if(!pPlayer->Block().IsLoggedIn())
		SecondsLeft = pPlayer->Block().m_LocalPassiveDuration;
	else
		SecondsLeft = pPlayer->Block().GetPlayerPassive();

	char aBuf[128];
	if(SecondsLeft > 0)
	{
		int Hours = SecondsLeft / 3600;
		int Minutes = (SecondsLeft % 3600) / 60;
		int Seconds = SecondsLeft % 60;
		if(Hours > 0)
			str_format(aBuf, sizeof(aBuf), "You have %dh %dm %ds of passive protection left.", Hours, Minutes, Seconds);
		else if(Minutes > 0)
			str_format(aBuf, sizeof(aBuf), "You have %dm %ds of passive protection left.", Minutes, Seconds);
		else
			str_format(aBuf, sizeof(aBuf), "You have %d seconds of passive protection left.", Seconds);
	}
	else
		str_copy(aBuf, "You have no passive protection left.", sizeof(aBuf));
	pSelf->Block().SendChatTarget(ClientId, aBuf);
}

void CBlock::ConSetGunDesignCosmetic(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;
	const int Index = pResult->GetInteger(0);
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Block().ToggleGunDesign(Index);
}

void CBlock::ConSetKnockoutCosmetic(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;
	const int Index = pResult->GetInteger(0);
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Block().ToggleKnockout(Index);
}

void CBlock::ConSetSkinManiCosmetic(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;
	const int Index = pResult->GetInteger(0);
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Block().ToggleSkinMani(Index);
}

void CBlock::ConSetSpecialCosmetic(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;
	const int Index = pResult->GetInteger(0);
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Block().ToggleSpecial(Index);
}

void CBlock::ConBanhammer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim();
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Block().m_BanhammerActive = !pPlayer->Block().m_BanhammerActive;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Banhammer %s for '%s'", pPlayer->Block().m_BanhammerActive ? "enabled" : "disabled", pSelf->Server()->ClientName(Victim));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}
