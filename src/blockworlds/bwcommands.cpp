#include <blockworlds/bw_base.h>
#include <game/server/gamecontext.h>

#include <engine/antibot.h>
#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamemodes/ddnet.h>
#include <game/server/player.h>
#include <game/server/save.h>
#include <game/server/teams.h>

#include <blockworlds/accounts.h>
#include <blockworlds/bw_context.h>
#include <blockworlds/clans.h>
#include <blockworlds/common.h>
#include <blockworlds/components/requests.h>

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/components/events/1on1.h>
#include <blockworlds/components/oneonone_manager.h>
#include <blockworlds/votes/votemanager.h>

#include <algorithm>
#include <blockworlds/whois.h>
#include <blockworlds/zones/zone.h>
#include <set>
#include <string>
#include <unordered_map>
#include <blockworlds/bw_util.h>
#include <blockworlds/shop/storemanager.h>

extern std::mutex g_ClansDataMutex;
extern std::unordered_map<int, CClansData> g_ClanIdMap;

inline bool CheckValidChars(const char *pStr)
{
	int Len = str_length(pStr);
	for(int i = 0; i < Len; i++)
		if((pStr[i] < 'a' || pStr[i] > 'z') &&
			(pStr[i] < 'A' || pStr[i] > 'Z') &&
			(pStr[i] < '0' || pStr[i] > '9'))
			return false;
	return true;
}

void CBlockworlds::ConRegister(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(pSelf->m_apPlayers[pResult->m_ClientId]->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't create an account while being logged in!");

	if(pSelf->Bw().isInEvent(pResult->m_ClientId))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't register while participating in an event. Use /leave first.");
	IZone *pSpawnZone = pSelf->Bw().ZoneManager()->GetZone(ZONE_SPAWN);
	CPlayer *pReqPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(g_Config.m_SvShopServer != 1)
	{
		if(pSpawnZone && pReqPlayer && pReqPlayer->GetCharacter() && !pSpawnZone->IsInZone(pReqPlayer->GetCharacter()->m_Pos))
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can only login while in the spawn zone.");
	}

	const char *pUsername = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);

	int NameLength = str_length(pUsername);
	int PasswordLength = str_length(pPassword);

	if(NameLength <= 2)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Your name must be at least 3 characters long!");

	if(pReqPlayer)
	{
		const int RegisterCooldownSeconds = g_Config.m_SvRegisterCooldownPerIp > 0 ? g_Config.m_SvRegisterCooldownPerIp : 10;
		int64_t now = pSelf->Server()->Tick();
		if(pReqPlayer->Bw().m_LastRegisterTick != 0 && now - pReqPlayer->Bw().m_LastRegisterTick < RegisterCooldownSeconds * pSelf->Server()->TickSpeed())
		{
			int remaining = (int)((RegisterCooldownSeconds * pSelf->Server()->TickSpeed() - (now - pReqPlayer->Bw().m_LastRegisterTick)) / pSelf->Server()->TickSpeed());
			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before trying again.", remaining, remaining != 1 ? "s" : "");
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
		}
	}

	if(PasswordLength < 5)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Your password must be at least 5 characters long!");

	if(str_comp(pUsername, pPassword) == 0)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Password must be different from username!");

	if(NameLength * sizeof(char) >= 11)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account name too long!");

	if(!CheckValidChars(pUsername) || !CheckValidChars(pPassword))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Only the characters A-Z and 0-9 are allowed!");

	char aAddrStrCheck[NETADDR_MAXSTRSIZE];
	BwClientAddr(pSelf->Server(), pResult->m_ClientId, aAddrStrCheck, sizeof(aAddrStrCheck));
	int RemainingBan = 0;
	if(!pPlayer->Bw().m_IsNpc)
	{
		if(pSelf->Bw().Accounts()->IsIpBanned(aAddrStrCheck, RemainingBan))
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "Too many recent attempts from your connection. Please wait %d second%s and try again.", RemainingBan, RemainingBan == 1 ? "" : "s");
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
		}
		if(!pSelf->Bw().Accounts()->RegisterIpAttempt(aAddrStrCheck))
		{
			pSelf->Bw().Accounts()->IsIpBanned(aAddrStrCheck, RemainingBan);
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "You're trying a lot. Take a short break (%d second%s) and try again.", RemainingBan, RemainingBan == 1 ? "" : "s");
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
		}
	}

	pSelf->Bw().Accounts()->Register(pResult->m_ClientId, pUsername, pPassword);
	if(pReqPlayer)
		pReqPlayer->Bw().m_LastRegisterTick = pSelf->Server()->Tick();
}

void CBlockworlds::ConLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(pSelf->m_apPlayers[pResult->m_ClientId]->Bw().m_Account.m_Id > 0)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are already logged in!");
	if(pSelf->Bw().isInEvent(pResult->m_ClientId))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You must finish the ongoing event (or use '/leave' to leave).");
	IZone *pSpawnZone = pSelf->Bw().ZoneManager()->GetZone(ZONE_SPAWN);
	CPlayer *pReqPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(g_Config.m_SvShopServer != 1)
	{
		if(pSpawnZone && pReqPlayer && pReqPlayer->GetCharacter() && !pSpawnZone->IsInZone(pReqPlayer->GetCharacter()->m_Pos))
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can only login while in the spawn zone.");
	}

	const char *pUsername = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);

	pSelf->Bw().Accounts()->Login(pResult->m_ClientId, pUsername, pPassword);
}

void CBlockworlds::ConAccountLogout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pSelf->m_apPlayers[pResult->m_ClientId]->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in!");
	if(pSelf->Bw().isInEvent(pResult->m_ClientId))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't logout while participating in an event. Use /leave first.");

	// deregister from any events as candidate before logout
	{
		int Cid = pResult->m_ClientId;

		if(auto events = g_ComponentRegistry.Get<CEvents>())
		{
			auto subs = events->GetSubComponents();
			for(auto &sub : subs)
			{
				CEventComponent *pEv = dynamic_cast<CEventComponent *>(sub.operator->());
				if(!pEv)
					continue;

				// if registered as candidate, deregister
				const auto &cands = pEv->Candidates();
				if(std::find(cands.begin(), cands.end(), Cid) != cands.end())
				{
					pEv->DeRegister(Cid);
					continue; // we've removed them from this event
				}
			}
		}
	}
	IZone *pSpawnZone = pSelf->Bw().ZoneManager()->GetZone(ZONE_SPAWN);
	CPlayer *pReqPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(pSpawnZone && pReqPlayer && pReqPlayer->GetCharacter() && !pSpawnZone->IsInZone(pReqPlayer->GetCharacter()->m_Pos))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can only logout while in the spawn zone.");

	// cancel any pending requests involving this client before logout (so others aren't left with stale offers)
	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		requests->CancelRequestsInvolving(pResult->m_ClientId, std::nullopt, "player logged out");
	}
	pPlayer->Bw().OnPlayerLogout();
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "you have been logged out!");
}

void CBlockworlds::ConDisplayBlockpoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pSelf->m_apPlayers[pResult->m_ClientId]->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in!");

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "You currently have %d blockpoint%s!",
		pSelf->m_apPlayers[pResult->m_ClientId]->Bw().m_Account.m_Blockpoints,
		pSelf->m_apPlayers[pResult->m_ClientId]->Bw().m_Account.m_Blockpoints != 1 ? "s" : "");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
}

// /give_bp <playerName> <amount>
void CBlockworlds::ConGiveBlockpointsRequest(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pFrom = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pFrom || !pFrom->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
	if(pResult->NumArguments() < 2)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Usage: /give_bp <playerName> <amount>");
	const char *pTargetName = pResult->GetString(0);
	int Amount = pResult->GetInteger(1);
	if(Amount < g_Config.m_SvBpTransferAmountMin)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Amount below minimum transfer threshold.");
	if(Amount > g_Config.m_SvBpTransferAmountCap)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Amount exceeds max cap.");
	if(Amount < 1)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Amount too small.");
	CPlayer *pTo = pSelf->Bw().GetPlayerByName(pTargetName);
	if(!pTo || !pTo->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Target player not found or not logged in.");
	if(pTo->GetCid() == pFrom->GetCid())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You cannot transfer blockpoints to yourself.");
	if(pFrom->Bw().GetPlayerBlockpoints() < Amount)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You don't have enough blockpoints.");
	// disallow if either player is currently in an event
	if(pSelf->Bw().isInEvent(pFrom->GetCid()) || pSelf->Bw().isInEvent(pTo->GetCid()))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Blockpoint transfers are not allowed while either player is in an event.");
	int Cooldown = g_Config.m_SvBpTransferCooldown;
	if(Cooldown > 0 && pFrom->Bw().m_LastBpTransferOfferTick != 0 && pSelf->Server()->Tick() - pFrom->Bw().m_LastBpTransferOfferTick < Cooldown * pSelf->Server()->TickSpeed())
	{
		int Rem = (int)((Cooldown * pSelf->Server()->TickSpeed() - (pSelf->Server()->Tick() - pFrom->Bw().m_LastBpTransferOfferTick)) / pSelf->Server()->TickSpeed());
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another transfer.", Rem, Rem != 1 ? "s" : "");
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	}
	// outstanding per sender check
	if(auto requestsTmp = g_ComponentRegistry.Get<CRequests>())
	{
		int outstanding = 0;
		auto list = requestsTmp->GetRequestsFor(pFrom->GetCid(), CRequests::SRequest::EType::BlockpointTransfer);
		for(int id : list)
		{
			CRequests::SRequest info;
			if(requestsTmp->GetRequestInfo(id, info) && info.m_From == pFrom->GetCid())
				outstanding++;
		}
		if(outstanding >= g_Config.m_SvBpTransferMaxOutstandingPerSender)
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You have too many pending transfers. Wait for them to resolve.");
	}
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
	int expiry = g_Config.m_SvBpTransferExpiry;
	int id = requests->CreateBlockpointTransfer(pFrom->GetCid(), pTo->GetCid(), Amount, expiry);
	if(id == -1)
		return; // error already messaged
	pFrom->Bw().m_LastBpTransferOfferTick = pSelf->Server()->Tick();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Transfer offer (%ds) sent to %s for %d blockpoints.", expiry, pSelf->Server()->ClientName(pTo->GetCid()), Amount);
	pSelf->Bw().SendChatTarget(pFrom->GetCid(), aBuf);
}

// /accept_bp [playerName]
void CBlockworlds::ConAcceptBlockpointsRequest(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
	auto matchIds = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
	int chosen = -1;
	if(pResult->NumArguments() > 0)
	{
		const char *pFromName = pResult->GetString(0);
		CPlayer *pFrom = pSelf->Bw().GetPlayerByName(pFromName);
		if(!pFrom)
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player not found");
		auto specific = requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
		for(int id : specific)
		{
			CRequests::SRequest info;
			if(requests->GetRequestInfo(id, info) && info.m_ExpireTick > pSelf->Server()->Tick())
				chosen = std::max(chosen, id);
		}
	}
	else if(matchIds.size() == 1)
	{
		chosen = matchIds[0];
	}
	else
	{
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, matchIds.empty() ? "No blockpoint transfer to accept." : "Multiple transfers pending. Use /accept_bp <playerName>.");
	}
	if(chosen == -1)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No valid (non-expired) transfer found.");
	if(!requests->AcceptRequest(chosen))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to accept the transfer (it may have expired).");
}

// /decline_bp [playerName]
void CBlockworlds::ConDeclineBlockpointsRequest(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
	auto matchIds = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
	int chosen = -1;
	if(pResult->NumArguments() > 0)
	{
		const char *pFromName = pResult->GetString(0);
		CPlayer *pFrom = pSelf->Bw().GetPlayerByName(pFromName);
		if(!pFrom)
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player not found");
		auto specific = requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
		for(int id : specific)
		{
			CRequests::SRequest info;
			if(requests->GetRequestInfo(id, info) && info.m_ExpireTick > pSelf->Server()->Tick())
				chosen = std::max(chosen, id);
		}
	}
	else if(matchIds.size() == 1)
	{
		chosen = matchIds[0];
	}
	else
	{
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, matchIds.empty() ? "No blockpoint transfer to decline." : "Multiple transfers pending. Use /decline_bp <playerName>.");
	}
	if(chosen == -1)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No valid (non-expired) transfer found.");
	if(!requests->DeclineRequest(chosen))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to decline the transfer (it may have expired).");
}

void CBlockworlds::ConChangePassword(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pSelf->m_apPlayers[pResult->m_ClientId]->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in!");

	const char *pOldPassword = pResult->GetString(0);
	const char *pNewPassword = pResult->GetString(1);

	int OldLength = str_length(pOldPassword);
	int NewLenght = str_length(pNewPassword);

	if(OldLength < 5)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Old password incorrect (must be at least 5 chars long).");

	if(NewLenght < 5)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Your password must be at least 5 characters long!");

	if(str_comp(pOldPassword, pNewPassword) == 0)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Password must be different from each other!");

	if(!CheckValidChars(pOldPassword) || !CheckValidChars(pNewPassword))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Only the characters A-Z and 0-9 are allowed!");

	pSelf->Bw().Accounts()->ChangePassword(pResult->m_ClientId, pSelf->m_apPlayers[pResult->m_ClientId]->Bw().m_Account.m_aName, pOldPassword, pNewPassword);
}

void CBlockworlds::ConDisplayProfile(IConsole::IResult *pResult, void *pUserData)
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

	if(!pTargetPlayer->Bw().m_Account.m_Id)
	{
		pSelf->Bw().SendChatTarget(ClientId, "The target player is not logged in.");
		return;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Profile of %s",
		pTargetPlayer->Bw().m_Account.m_aName);
	pSelf->Bw().SendChatTarget(ClientId, aBuf);

	pSelf->Bw().SendChatTarget(ClientId, "------Global------");

	// global stats: Kills, Deaths, Max Kill Streak
	str_format(aBuf, sizeof(aBuf), "Kills: %d", pTargetPlayer->Bw().m_Account.m_Kills);
	pSelf->Bw().SendChatTarget(ClientId, aBuf);

	str_format(aBuf, sizeof(aBuf), "Deaths: %d", pTargetPlayer->Bw().m_Account.m_Deaths);
	pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// global K/D ratio
	float KD = pTargetPlayer->Bw().m_Account.m_Deaths > 0 ? (float)pTargetPlayer->Bw().m_Account.m_Kills / pTargetPlayer->Bw().m_Account.m_Deaths : (float)pTargetPlayer->Bw().m_Account.m_Kills;
	str_format(aBuf, sizeof(aBuf), "K/D: %.2f", KD);
	pSelf->Bw().SendChatTarget(ClientId, aBuf);

	str_format(aBuf, sizeof(aBuf), "Max Kill Streak: %d", pTargetPlayer->Bw().m_Account.m_Killstreak);
	pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// LMB Wins
	str_format(aBuf, sizeof(aBuf), "LMB Wins: %d", pTargetPlayer->Bw().m_Account.m_TourneyWin);
	pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// playtime in hours and minutes
	int Hours = pTargetPlayer->Bw().m_Account.m_Playtime / 3600;
	int Minutes = (pTargetPlayer->Bw().m_Account.m_Playtime % 3600) / 60;
	str_format(aBuf, sizeof(aBuf), "PlayTime: %d hours %d minutes", Hours, Minutes);
	pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// pSelf->Bw().SendChatTarget(ClientId, "------Ranked------");

	// // ranked stats: Games, Kills, Deaths, Wins
	// str_format(aBuf, sizeof(aBuf), "Games: %d", pTargetPlayer->m_Account.m_RankedGames);
	// pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// str_format(aBuf, sizeof(aBuf), "Rating: %d", pTargetPlayer->m_Account.m_Ranking);
	// pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// str_format(aBuf, sizeof(aBuf), "Kills: %d", pTargetPlayer->m_Account.m_RankedKills);
	// pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// str_format(aBuf, sizeof(aBuf), "Deaths: %d", pTargetPlayer->m_Account.m_RankedDeaths);
	// pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// // ranked K/D ratio
	// float RankedKD = pTargetPlayer->m_Account.m_RankedDeaths > 0 ? (float)pTargetPlayer->m_Account.m_RankedKills / pTargetPlayer->m_Account.m_RankedDeaths : (float)pTargetPlayer->m_Account.m_RankedKills;
	// str_format(aBuf, sizeof(aBuf), "K/D: %.2f", RankedKD);
	// pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// // ranked Wins and Win Rate
	// str_format(aBuf, sizeof(aBuf), "Wins: %d", pTargetPlayer->m_Account.m_RankedWins);
	// pSelf->Bw().SendChatTarget(ClientId, aBuf);

	// float WinRate = pTargetPlayer->m_Account.m_RankedGames > 0 ? (float)pTargetPlayer->m_Account.m_RankedWins / pTargetPlayer->m_Account.m_RankedGames * 100.0f : 0.0f;
	// str_format(aBuf, sizeof(aBuf), "Win Rate: %.2f%%", WinRate);
	// pSelf->Bw().SendChatTarget(ClientId, aBuf);
}

void CBlockworlds::ConGetCid(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(pResult->NumArguments() != 1)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Usage: /getcid <player name>");
		return;
	}

	const char *pTargetName = pResult->GetString(0);
	CPlayer *pTarget = pSelf->Bw().GetPlayerByName(pTargetName);
	if(!pTarget)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player not found");
		return;
	}

	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "%s -> cid %d", pSelf->Server()->ClientName(pTarget->GetCid()), pTarget->GetCid());
}

void CBlockworlds::ConStatusAccounts(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const char *pFilter = pResult->NumArguments() == 1 ? pResult->GetString(0) : "";
	char aBuf[512];

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = pSelf->m_apPlayers[i];
		if(!pPlayer)
			continue;
		if(!pPlayer->Bw().IsLoggedIn())
			continue;

		// filter by name (client name or account name)
		if(pFilter[0] != '\0')
		{
			if(!str_utf8_find_nocase(pPlayer->Bw().GetPlayerName(), pFilter) && !str_utf8_find_nocase(pPlayer->Bw().m_Account.m_aName, pFilter))
				continue;
		}

		int Hours = pPlayer->Bw().m_Account.m_Playtime / 3600;
		int Minutes = (pPlayer->Bw().m_Account.m_Playtime % 3600) / 60;

		const char *pClanName = " ";
		if(pSelf->Bw().Clans())
		{
			CClansData tmp;
			if(pSelf->Bw().Clans()->GetClanSnapshotById(pPlayer->Bw().m_Account.m_ClanId, tmp))
				pClanName = tmp.m_ClanName;
		}
		str_format(aBuf, sizeof(aBuf), "cid=%d, accid=%d, acc_name='%s', ig_name='%s', vip=%d, clan='%s', clanid=%d, auth=%d, playtime=%02d:%02d, ranking=%d, kills=%d, deaths=%d",
			i,
			pPlayer->Bw().m_Account.m_Id,
			pPlayer->Bw().m_Account.m_aName,
			pSelf->Server()->ClientName(i),
			pPlayer->Bw().m_Account.m_Vip,
			pClanName,
			pPlayer->Bw().m_Account.m_ClanId,
			(int)pPlayer->Bw().m_Account.m_AuthLevel,
			Hours,
			Minutes,
			pPlayer->Bw().m_Account.m_Ranking,
			pPlayer->Bw().m_Account.m_Kills,
			pPlayer->Bw().m_Account.m_Deaths);

		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CBlockworlds::ConDisplayTopLevel(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Bw().Accounts()->ShowTopLevel(ClientId);
}

void CBlockworlds::ConIpBans(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!CheckClientId(pResult->m_ClientId))
		return;
	// pPlayer not used here; avoid unused variable warning
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Permission denied");

	auto bans = pSelf->Bw().Accounts()->ListIpBans();
	if(bans.empty())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No active IP bans.");
		return;
	}

	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Active IP bans:");
	char aBuf[256];
	for(const auto &b : bans)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d second%s remaining", b.first.c_str(), b.second, b.second != 1 ? "s" : "");
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	}
}

void CBlockworlds::ConIpBanClear(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!CheckClientId(pResult->m_ClientId))
		return;
	// pPlayer not used here; avoid unused variable warning
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Permission denied");

	const char *pIp = pResult->GetString(0);
	pSelf->Bw().Accounts()->ClearIpBan(pIp);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Cleared IP ban for %s", pIp);
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
}

void CBlockworlds::ConListOutstandingInvites(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Permission denied");

	int Target = pResult->GetInteger(0);
	if(!CheckClientId(Target))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Invalid client id");

	if(!pSelf->m_apPlayers[Target])
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Target player not online");

	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Request component not available");

	auto idsFrom = requests->GetRequestIdsFromTo(Target, Target, std::nullopt); // get both to and from via helper below

	auto all = requests->GetRequestsFor(Target, std::nullopt);
	if(all.empty())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No outstanding requests for player");
		return;
	}

	char aBuf[256];
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Outstanding requests (id, type, from -> to):");
	for(int id : all)
	{
		str_format(aBuf, sizeof(aBuf), "id=%d", id);
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	}
}

// admin console commands to modify account attributes
void CBlockworlds::ConGivePages(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerPages(pTarget->Bw().GetPlayerPages() + Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d pages to %s (now %d)", Amount, pTarget->Bw().GetPlayerName(), pTarget->Bw().GetPlayerPages());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConSetPages(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerPages(Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set pages for %s to %d", pTarget->Bw().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConSetLevel(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerLevel(Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set level for %s to %d", pTarget->Bw().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConSetExperience(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerExperience(Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set experience for %s to %d", pTarget->Bw().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConSetWeaponkitsAdmin(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerWeaponkits(Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set weaponkits for %s to %d", pTarget->Bw().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConSetBlockpoints(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerBlockpoints(Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set blockpoints for %s to %d", pTarget->Bw().GetPlayerName(), Amount);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConSetPassive(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerPassive(Seconds);
	if(pTarget->GetCharacter())
		pTarget->GetCharacter()->Core()->m_Passive = true;
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Set passive seconds for %s to %d", pTarget->Bw().GetPlayerName(), Seconds);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConTelekinesis(IConsole::IResult *pResult, void *pUserData)
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
	pPlayer->Bw().m_TelekinesisEnabled = !pPlayer->Bw().m_TelekinesisEnabled;
	pPlayer->Bw().m_TelekinesisTarget = -1;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Telekinesis %s.", pPlayer->Bw().m_TelekinesisEnabled ? "enabled" : "disabled");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CBlockworlds::ConKnockout(IConsole::IResult *pResult, void *pUserData)
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
		Effect = pSelf->Bw().Cosmetics()->FindKnockoutEffect(pName);

	if(Effect < 0 || Effect >= CCosmeticsHandler::NUM_KNOCKOUTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Unknown knockout. Use 'knockout' without arguments to list all.");
		return;
	}

	const CNetObj_PlayerInput &Input = pChr->Bw().GetLatestInput();
	vec2 MousePos = pChr->m_Pos + vec2(Input.m_TargetX, Input.m_TargetY);
	pSelf->Bw().Cosmetics()->DoKnockoutEffectRaw(MousePos, Effect);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Triggered '%s' at (%.0f, %.0f)", CCosmeticsHandler::ms_KnockoutNames[Effect], MousePos.x, MousePos.y);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CBlockworlds::ConWhoisAccount(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	if(!pName || !pName[0])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Usage: whois_account <account name>");
		return;
	}
	if(pSelf->Bw().WhoIs())
		pSelf->Bw().WhoIs()->CmdWhoisAccount(-1, pName);
}

void CBlockworlds::ConGiveLevel(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerLevel(pTarget->Bw().GetPlayerLevel() + Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d levels to %s (now %d)", Amount, pTarget->Bw().GetPlayerName(), pTarget->Bw().GetPlayerLevel());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConGiveExperience(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerExperience(pTarget->Bw().GetPlayerExperience() + Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d experience to %s (now %d)", Amount, pTarget->Bw().GetPlayerName(), pTarget->Bw().GetPlayerExperience());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConGiveWeaponkits(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerWeaponkits(pTarget->Bw().GetPlayerWeaponkits() + Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d weaponkits to %s (now %d)", Amount, pTarget->Bw().GetPlayerName(), pTarget->Bw().GetPlayerWeaponkits());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConGiveBlockpoints(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerBlockpoints(pTarget->Bw().GetPlayerBlockpoints() + Amount);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d blockpoints to %s (now %d)", Amount, pTarget->Bw().GetPlayerName(), pTarget->Bw().GetPlayerBlockpoints());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConGivePassive(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->Bw().SetPlayerPassive(Seconds);
	if(pTarget->GetCharacter())
		pTarget->GetCharacter()->Core()->m_Passive = true;
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d seconds of passive to %s", Seconds, pTarget->Bw().GetPlayerName());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConSetVip(IConsole::IResult *pResult, void *pUserData)
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

	if(!pTarget->Bw().IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}

	int newVip = Vip ? 1 : 0;
	pTarget->Bw().SetPlayerVip(newVip);
	pSelf->Bw().Accounts()->Save(Target, &pTarget->Bw().m_Account);

	char aBuf[128];
	if(newVip)
		str_format(aBuf, sizeof(aBuf), "Set VIP for %s (now vip=%d)", pTarget->Bw().GetPlayerName(), pTarget->Bw().GetPlayerVip());
	else
		str_format(aBuf, sizeof(aBuf), "Removed VIP from %s", pTarget->Bw().GetPlayerName());

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->Bw().SendChatTarget(Target, aBuf);
}

void CBlockworlds::ConSetVipAccount(IConsole::IResult *pResult, void *pUserData)
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

	pSelf->Bw().Accounts()->SetVipByNameAdmin(pResult->m_ClientId, pAccountName, Vip);

	// iff the player is currently online and logged in under that account, also update in-memory shittery
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *p = pSelf->m_apPlayers[i];
		if(!p)
			continue;
		if(!p->Bw().IsLoggedIn())
			continue;
		if(str_comp(p->Bw().GetPlayerName(), pAccountName) == 0 || str_comp(p->Bw().m_Account.m_aName, pAccountName) == 0)
		{
			p->Bw().SetPlayerVip(Vip);
			pSelf->Bw().Accounts()->Save(i, &p->Bw().m_Account);
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Your VIP status was %s by an admin.", Vip ? "enabled" : "disabled");
			pSelf->Bw().SendChatTarget(i, aBuf);
			break;
		}
	}

	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "Queued VIP=%d for account '%s' (offline-capable)", Vip, pAccountName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CBlockworlds::ConAdminSetPassword(IConsole::IResult *pResult, void *pUserData)
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

	SHA256_DIGEST HashedNewPassword = CBlockworlds::HashPassword(pNewPassword);
	char aHashedNewPassword[SHA256_DIGEST_LENGTH * 2 + 1];
	sha256_str(HashedNewPassword, aHashedNewPassword, sizeof(aHashedNewPassword));

	char aEscapedName[128];
	{
		int di = 0;
		for(int si = 0; pAccountName[si] && di < (int)sizeof(aEscapedName) - 2; ++si)
		{
			if(pAccountName[si] == '\'')
			{
				if(di < (int)sizeof(aEscapedName) - 3)
				{
					aEscapedName[di++] = '\'';
					aEscapedName[di++] = '\'';
				}
			}
			else
			{
				aEscapedName[di++] = pAccountName[si];
			}
		}
		aEscapedName[di] = '\0';
	}

	pSelf->Bw().Accounts()->ChangePasswordAdmin(pResult->m_ClientId, pAccountName, pNewPassword);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Set password for account '%s'", pAccountName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// notify player if they are online
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *p = pSelf->m_apPlayers[i];
		if(!p)
			continue;
		if(!p->Bw().IsLoggedIn())
			continue;
		if(str_comp(p->Bw().GetPlayerName(), pAccountName) == 0 || str_comp(p->Bw().m_Account.m_aName, pAccountName) == 0)
		{
			pSelf->Bw().SendChatTarget(i, "An administrator has changed your account password.");
			break;
		}
	}
}

void CBlockworlds::ConDisplayTopBlockpoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Bw().Accounts()->ShowTopBlockpoints(ClientId);
}

void CBlockworlds::ConDisplayTopKillStreak(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Bw().Accounts()->ShowTopKillStreak(ClientId);
}

void CBlockworlds::ConWeaponKit(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You must be alive to use a weapon kit.");

	if(!pSelf->Bw().m_WeaponkitsAllowed)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Weaponkits are currently disabled on this server.");
		return;
	}

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in.");

	if(pPlayer->Bw().GetPlayerWeaponkits() < 1 && !pPlayer->Bw().GetPlayerVip())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You don't have any weapon kits, make a trip to the store and purchase some!");

	// check if the player has all weapons
	bool HasAllWeapons = true;
	for(int i = WEAPON_GUN; i < NUM_WEAPONS - 1; i++)
	{
		if(!pChr->Bw().Core().m_aWeapons[i].m_Got)
		{
			HasAllWeapons = false;
			break;
		}
	}

	if(HasAllWeapons)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You already have all weapons.");

	// restrict usage to spawn zone if spawn zone exists
	IZone *pSpawnZone = pSelf->Bw().ZoneManager()->GetZone(ZONE_SPAWN);
	if(pSpawnZone)
	{
		if(!pSpawnZone->IsInZone(pChr->m_Pos))
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can only use weapon kits while in the spawn zone.");
	}

	pSelf->ModifyWeapons(pResult, pSelf, -1, false);

	char aBuf[128];

	if(pPlayer->Bw().GetPlayerVip())
		str_copy(aBuf, "You have successfuly used a weaponkit!", sizeof(aBuf));
	else
	{
		str_format(aBuf, sizeof(aBuf), "You have successfuly used a weaponkit! %d kits left.", pPlayer->Bw().GetPlayerWeaponkits());
		pPlayer->Bw().SetPlayerWeaponkits(pPlayer->Bw().GetPlayerWeaponkits() - 1);
	}
	return pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
}

void CBlockworlds::ConDeathnote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	IServer *pServer = pSelf->Server();

	if(!pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in.");

	if(!pResult->NumArguments())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Invalid arguments... Usage: deathnote [player]");

	CPlayer *pTarget = pSelf->Bw().GetPlayerByName(pResult->GetString(0));

	if(!pTarget)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "This player doesn't exist.");

	if(pPlayer->Bw().GetPlayerPages() < 1)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You don't have any deathnotes, make a trip to the store and purchase some!");

	// prevent using deathnote if the target is inside passive or no-collision zones
	IZone *pPassiveZone = pSelf->Bw().ZoneManager()->GetZone(ZONE_PASSIVE);
	IZone *pNoCollZone = pSelf->Bw().ZoneManager()->GetZone(ZONE_NOCOLL);

	// prevent using deathnote if target is participating in an event
	if(pTarget && pSelf->Bw().isInEvent(pTarget->GetCid()))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a player participating in an event.");

	int CurrentTick = pServer->Tick();
	int CooldownTick = pPlayer->Bw().m_LastDeathnote + (pServer->TickSpeed() * g_Config.m_SvDeathNoteCoolDown);
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
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	}

	// block if the target is currently inside a passive or no-collision zone
	CCharacter *pTChar = pTarget->GetCharacter();
	if(pTChar)
	{
		if(pPassiveZone && pPassiveZone->IsInZone(pTChar->m_Pos) && pTChar->Core()->m_Passive)
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a player inside a passive zone.");
		if(pNoCollZone && pNoCollZone->IsInZone(pTChar->m_Pos))
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a player inside a no-collision zone.");
		if(pTChar->Core()->m_Solo)
			return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a solo player."); // bad mapper
	}

	// consume a page and apply kill
	pPlayer->Bw().SetPlayerPages(pPlayer->Bw().GetPlayerPages() - 1);
	pTarget->KillCharacter();

	char aBuff_From[128], aBuff_To[128];
	str_format(aBuff_From, sizeof(aBuff_From), "Successfully killed %s. %d pages remaining.", pSelf->Server()->ClientName(pTarget->GetCid()), pPlayer->Bw().GetPlayerPages());
	str_format(aBuff_To, sizeof(aBuff_To), "'%s' used a deathnote to kill you!", pSelf->Server()->ClientName(pResult->m_ClientId));
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuff_From);
	pSelf->Bw().SendChatTarget(pTarget->GetCid(), aBuff_To);
	pPlayer->Bw().m_LastDeathnote = pServer->Tick();
}

void CBlockworlds::ConPassiveRemover(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
	if (!g_Config.m_SvPassiveRemoverEnabled)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Passive Remover feature is currently disabled.");

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in.");

	if(!pResult->NumArguments())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Usage: /passiveremover [player]");

	if(pPlayer->Bw().GetPlayerPassiveRemovers() < 1)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You don't have any passive removers! Purchase one from the shop.");

	if(pPlayer->Bw().m_PassiveRemoverUseCooldown > 0)
	{
		int mins = pPlayer->Bw().m_PassiveRemoverUseCooldown / 60;
		int secs = pPlayer->Bw().m_PassiveRemoverUseCooldown % 60;
		char aCooldownBuf[128];
		if(mins > 0)
			str_format(aCooldownBuf, sizeof(aCooldownBuf), "You can use a Passive Remover again in %d min.", mins);
		else
			str_format(aCooldownBuf, sizeof(aCooldownBuf), "You can use a Passive Remover again in %d sec.", secs);
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, aCooldownBuf);
	}

	CPlayer *pTarget = pSelf->Bw().GetPlayerByName(pResult->GetString(0));
	if(!pTarget)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player not found.");

	if(pTarget->GetCid() == pResult->m_ClientId)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't use a passive remover on yourself.");

	if(pSelf->Bw().isInEvent(pTarget->GetCid()))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't use a passive remover on a player in an event.");

	// Check the target actually has passive
	bool HasPassive = false;
	if(pTarget->Bw().IsLoggedIn() && pTarget->Bw().GetPlayerPassive() > 0)
		HasPassive = true;
	else if(!pTarget->Bw().IsLoggedIn() && pTarget->Bw().m_LocalPassiveDuration > 0)
		HasPassive = true;

	if(!HasPassive)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "That player doesn't have passive protection.");

	// Consume one passive remover
	pPlayer->Bw().SetPlayerPassiveRemovers(pPlayer->Bw().GetPlayerPassiveRemovers() - 1);

	// Strip target's passive
	if(pTarget->Bw().IsLoggedIn())
		pTarget->Bw().SetPlayerPassive(0);
	pTarget->Bw().m_LocalPassiveDuration = 0;
	pTarget->Bw().m_UsePassiveProtection = false;
	pTarget->Bw().m_PassivePendingEnable = false;

	// If target's character is currently passive, remove the flag
	CCharacter *pTChar = pTarget->GetCharacter();
	if(pTChar && pTChar->Core()->m_Passive)
		pTChar->Bw().Core().m_Passive = false;

	// Set a cooldown before the target can redo the passive race
	pTarget->Bw().m_PassiveRaceCooldown = g_Config.m_SvPassiveRemoverCooldown;

	char aBufFrom[128], aBufTo[256];
	str_format(aBufFrom, sizeof(aBufFrom), "You stripped %s's passive protection! %d removers remaining.",
		pSelf->Server()->ClientName(pTarget->GetCid()), pPlayer->Bw().GetPlayerPassiveRemovers());
	int cooldownMins = g_Config.m_SvPassiveRemoverCooldown / 60;
	int cooldownSecs = g_Config.m_SvPassiveRemoverCooldown % 60;
	if(g_Config.m_SvPassiveRemoverCooldown > 0)
		str_format(aBufTo, sizeof(aBufTo), "'%s' used a Passive Remover on you! Your passive has been removed. You cannot redo the passive race for %d:%02d minutes.",
			pSelf->Server()->ClientName(pResult->m_ClientId), cooldownMins, cooldownSecs);
	else
		str_format(aBufTo, sizeof(aBufTo), "'%s' used a Passive Remover on you! Your passive has been removed.",
			pSelf->Server()->ClientName(pResult->m_ClientId));
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBufFrom);
	pSelf->Bw().SendChatTarget(pTarget->GetCid(), aBufTo);

	// Set use cooldown on the triggerer
	pPlayer->Bw().m_PassiveRemoverUseCooldown = g_Config.m_SvPassiveRemoverUseCooldown;
}

void CBlockworlds::ConExp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	char aBuf[256];

	if(!pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in.");

	static const int s_MaxNum = 17;
	float a = (float)pPlayer->Bw().GetPlayerExperience() / NeededAccountExp(pPlayer->Bw().GetPlayerLevel());
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

	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Experience Bar:");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBarTop);
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBarBot);
	str_format(aBuf, sizeof(aBuf), "Account level: %i", pPlayer->Bw().GetPlayerLevel());
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Account Exp: %i", pPlayer->Bw().GetPlayerExperience());
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Needed Exp: %i", NeededAccountExp(pPlayer->Bw().GetPlayerLevel()));
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
}
void CBlockworlds::ConClanExp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	char aBuf[256];

	if(!pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
	if(!pPlayer->Bw().GetClanId())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not in a clan");

	// obtain a snapshot copy of the clan data (thread-safe)
	CClansData clanTmp;
	if(!pSelf->Bw().Clans()->GetClanSnapshotById(pPlayer->Bw().GetClanId(), clanTmp))
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Error: Something weird happened, try to login again.");
		return;
	}

	static const int s_MaxNum = 17;
	float Ratio = (float)clanTmp.m_Experience / NeededClanExp(clanTmp.m_Level);
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

	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Clan Experience Bar:");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBarTop);
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBarBot);
	str_format(aBuf, sizeof(aBuf), "Clan level: %i", clanTmp.m_Level);
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Clan Exp: %i", clanTmp.m_Experience);
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Needed Exp: %i", NeededClanExp(clanTmp.m_Level));
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
}

void CBlockworlds::ConClanList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You must be logged in.");
	if(pPlayer->Bw().GetClanId() <= 0)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not in a clan.");

	pSelf->Bw().Clans()->ShowClanMembers(pResult->m_ClientId, pPlayer->Bw().GetClanId());
}

void CBlockworlds::ConClanHelp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Clan system commands:");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_create <name> - Create a new clan");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_delete - Delete your clan (leader only)");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_leave - Leave your clan");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_invite <player> - Invite a player");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_accept | /clan_decline - Respond to invite");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_kick <player> - Kick a member (leader/co-leader)");
	// Updated role command
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_role <player> <member|coleader> - Set role (leader only)");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_rename <newname> - Rename clan (leader only)");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_transfer <player> - Transfer clan leadership (leader only)");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_exp - Show clan EXP progress");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/clan_list - List clan members");
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Max members per clan: %d", g_Config.m_SvClanMaxMembers);
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Rename price: %d BP", g_Config.m_SvClanRenamePrice);
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Create price: %d BP", g_Config.m_SvClanCreatePrice);
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
}

void CBlockworlds::ConAccountHelp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system commands:");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/register <name> <pass> - Create an account");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/login <name> <pass> - Log in");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/logout - Log out");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/password <old> <new> - Change password");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/exp - Show your EXP");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/profile [name] - View a profile");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/bp - Show your blockpoints");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/give_bp <player> <amount> - Offer BP transfer");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "/accept_bp [player] | /decline_bp [player] - Respond to BP transfer");
}

void CBlockworlds::ConBuy(IConsole::IResult *pResult, void *pUserData)
{
	// // test command - replace that with tiles

	// CBlockworlds *pBw = (CBlockworlds *)pUserData;
	// CGameContext *pSelf = pBw->GameServer();
	// CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	// CCharacter *pChr = pPlayer->GetCharacter();

	// if(!pPlayer)
	// {
	// 	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player not found.");
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
	// 	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Unknown cosmetics type. Use 'ko' (knockout), 'gd' (gundesign), or 'sm' (skinmani).");
	// 	return;
	// }

	// if(!Found)
	// {
	// 	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Unknown cosmetics name.");
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
	// 	// pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Purchase initiated. Confirm with /yes or cancel with /no.");
	// }
	// else
	// {
	// 	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "pendingpurchase isn't null");
	// }
}

void CBlockworlds::ConShopPurchase(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pPlayer && pPlayer->GetCharacter() && pPlayer->GetCharacter()->Bw().m_PendingPurchase)
	{
		pPlayer->GetCharacter()->Bw().m_PendingPurchase->Purchase();
	}
	else
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No item available for purchase.");
	}
}

void CBlockworlds::ConShopDecline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pPlayer && pPlayer->GetCharacter() && pPlayer->GetCharacter()->Bw().m_PendingPurchase)
	{
		pPlayer->GetCharacter()->Bw().m_PendingPurchase->Decline();
	}
	else
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No item available to decline.");
	}
}

void CBlockworlds::ConClanInvite(IConsole::IResult *pResult, void *pUserData)
{
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer || !pPlayer->Bw().IsLoggedIn() || !pPlayer->GetCharacter())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}

	if(pPlayer->Bw().GetClanId() == 0)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not in a clan to invite others.");
		return;
	}

	if(pPlayer->Bw().GetAuthLevel() < ClanAuthLevel::COLEADER)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You need to be the leader or co-leader to invite others.");
		return;
	}

	const int CooldownSeconds = g_Config.m_SvClanInviteCooldown; // configured cooldown
	if(pPlayer->Bw().m_LastClanInviteTick != 0 && pSelf->Server()->Tick() - pPlayer->Bw().m_LastClanInviteTick < CooldownSeconds * pSelf->Server()->TickSpeed())
	{
		int Rem = (int)((CooldownSeconds * pSelf->Server()->TickSpeed() - (pSelf->Server()->Tick() - pPlayer->Bw().m_LastClanInviteTick)) / pSelf->Server()->TickSpeed());
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another clan invite.", Rem, Rem != 1 ? "s" : "");
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
		return;
	}

	const char *pName = pResult->GetString(0);
	if(!pName || pName[0] == '\0')
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Usage: /clan_invite <playername>");
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
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player not found or not logged in.");
		return;
	}

	CPlayer *pTargetPlayer = pSelf->m_apPlayers[Target];

	if(!pTargetPlayer || !pTargetPlayer->GetCharacter())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Target player is not available.");
		return;
	}
	if(!pTargetPlayer->Bw().IsLoggedIn())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "This player is not logged in.");
		return;
	}

	if(Target == pPlayer->GetCid())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You cannot invite yourself.");
		return;
	}

	if(pTargetPlayer->Bw().GetClanId() != 0)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "This player is already in a clan.");
		return;
	}

	if(pTargetPlayer->Bw().GetPlayerLevel() < 10)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "This player must be at least level 10 to join a clan.");
		return;
	}

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		auto incoming = requests->GetRequestIdsTo(Target, CRequests::SRequest::EType::Clan);
		if(!incoming.empty())
		{
			pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player already has a pending clan invitation.");
			return;
		}

		int id = requests->CreateClanInvite(pPlayer->GetCid(), Target, pPlayer->Bw().GetClanId(), g_Config.m_SvClanInviteExpiry);
		if(id < 0)
		{
			pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to send clan invitation.");
			return;
		}
		pPlayer->Bw().m_LastClanInviteTick = pSelf->Server()->Tick();
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Clan invitation sent to %s.", pName);
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, aBuf);
		return;
	}
	else
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
		return;
	}
}

void CBlockworlds::ConClanAccept(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You must be logged in to accept a clan invite.");
		return;
	}
	if(pPlayer->Bw().GetClanId() != 0)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are already in a clan.");
		return;
	}

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		auto ids = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::Clan);
		if(ids.empty())
		{
			pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Nobody invited you!");
		}
		else
		{
			// accept the most recent invite (last id)
			int id = ids.back();
			if(!requests->AcceptRequest(id))
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to accept invite.");
			else
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Clan invite accepted.");
		}
	}
	else
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
	}
}

void CBlockworlds::ConClanDecline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You must be logged in to decline a clan invite.");
		return;
	}

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		auto ids = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::Clan);
		if(ids.empty())
		{
			pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Nobody invited you!");
		}
		else
		{
			int id = ids.back();
			if(!requests->DeclineRequest(id))
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to decline invite.");
			else
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Clan invite declined.");
		}
	}
	else
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
	}
}

void CBlockworlds::ConClanCreate(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(ClientId, "You must be logged in to create a clan!");
	if(pPlayer->Bw().m_Account.m_Level < g_Config.m_SvClanMinLevel)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "You must be at least level %d to create a clan!", g_Config.m_SvClanMinLevel);
		return pSelf->Bw().SendChatTarget(ClientId, aBuf);
	}
	if(pPlayer->Bw().m_Account.m_ClanId > 0)
		return pSelf->Bw().SendChatTarget(ClientId, "You are already in a clan!");

	if(g_Config.m_SvClanCreatePrice > 0)
	{
		int cost = g_Config.m_SvClanCreatePrice;
		if(pPlayer->Bw().GetPlayerBlockpoints() < cost)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "You need %d blockpoints to create a clan.", cost);
			return pSelf->Bw().SendChatTarget(ClientId, aBuf);
		}
	}

	const char *pClanName = pResult->GetString(0);
	int ClanNameLength = str_length(pClanName);

	if(ClanNameLength < 3)
		return pSelf->Bw().SendChatTarget(ClientId, "Clan name must be at least 3 characters long!");
	if(ClanNameLength > BW_CLAN_NAME_MAX_LENGTH)
		return pSelf->Bw().SendChatTarget(ClientId, "Clan name is too long (max 11 characters)!");

	if(pSelf->Bw().Clans()->GetClanIdByName(pClanName) != -1)
		return pSelf->Bw().SendChatTarget(ClientId, "This clan name is already taken!");

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		requests->CreateClanCreateConfirm(ClientId, pClanName, g_Config.m_SvClanConfirmExpiry);
		return; // request system will notify
	}

	return pSelf->Bw().SendChatTarget(ClientId, "Clan creation failed: request system unavailable.");
}

void CBlockworlds::ConClanDelete(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(ClientId, "You must be logged in to delete a clan.");

	dbg_msg("clan", "clan_delete: clanid=%d auth=%d", pPlayer->Bw().m_Account.m_ClanId, (int)pPlayer->Bw().m_Account.m_AuthLevel);

	if(pPlayer->Bw().m_Account.m_ClanId < 1 || pPlayer->Bw().m_Account.m_AuthLevel != ClanAuthLevel::LEADER)
		return pSelf->Bw().SendChatTarget(ClientId, "You are either not in a clan or not its leader.");

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		requests->CreateClanDeleteConfirm(ClientId, pPlayer->Bw().GetClanId(), g_Config.m_SvClanConfirmExpiry);
		return; // message sent by requests
	}
}

void CBlockworlds::ConClanRemove(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(ClientId, "You must be logged in to remove a player from the clan.");

	if(pPlayer->Bw().m_Account.m_ClanId < 1 || pPlayer->Bw().m_Account.m_AuthLevel < ClanAuthLevel::COLEADER)
		return pSelf->Bw().SendChatTarget(ClientId, "You are not authorized to remove members from this clan.");

	const char *pTargetName = pResult->GetString(0);
	if(!pTargetName || pTargetName[0] == '\0')
		return pSelf->Bw().SendChatTarget(ClientId, "Usage: /clan_kick <player>");

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
		if(!pTargetPlayer || !pTargetPlayer->Bw().m_Account.m_Id)
			return pSelf->Bw().SendChatTarget(ClientId, "The target player is not logged in.");
		if(pPlayer->Bw().m_Account.m_ClanId != pTargetPlayer->Bw().m_Account.m_ClanId)
			return pSelf->Bw().SendChatTarget(ClientId, "The target player is not in your clan.");
		if(ClientId == TargetClientId)
			return pSelf->Bw().SendChatTarget(ClientId, "You cannot remove yourself from the clan.");
		if(pTargetPlayer->Bw().m_Account.m_AuthLevel >= ClanAuthLevel::COLEADER)
			return pSelf->Bw().SendChatTarget(ClientId, "You cannot remove a leader or co-leader from the clan.");
		if(auto requests = g_ComponentRegistry.Get<CRequests>())
		{
			requests->CreateClanKickConfirm(ClientId, pPlayer->Bw().m_Account.m_ClanId, pTargetPlayer->Bw().m_Account.m_aName, g_Config.m_SvClanConfirmExpiry);
			return; // message sent by requests
		}
		return; // fallback (no requests component)
	}

	// Offline target path: allow specifying an account name that is currently offline.
	// We cannot fully verify membership/auth synchronously here; RemoveFromClanThread will do validation.
	// Basic self-check to avoid kicking self by own account name.
	if(!str_comp(pTargetName, pPlayer->Bw().m_Account.m_aName))
		return pSelf->Bw().SendChatTarget(ClientId, "You cannot remove yourself from the clan.");

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		requests->CreateClanKickConfirm(ClientId, pPlayer->Bw().m_Account.m_ClanId, pTargetName, g_Config.m_SvClanConfirmExpiry);
		char aBuf[160];
		str_format(aBuf, sizeof(aBuf), "Kick confirmation for offline member '%s' created. Type /clan_yes to confirm.", pTargetName);
		pSelf->Bw().SendChatTarget(ClientId, aBuf);
		return;
	}
	return pSelf->Bw().SendChatTarget(ClientId, "Kick failed: request system unavailable.");
}

void CBlockworlds::ConClanTransfer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(ClientId, "You must be logged in to transfer clan leadership.");

	if(pPlayer->Bw().m_Account.m_ClanId < 1 || pPlayer->Bw().m_Account.m_AuthLevel != ClanAuthLevel::LEADER)
		return pSelf->Bw().SendChatTarget(ClientId, "You are either not in a clan or not its leader.");

	const char *pTargetName = pResult->GetString(0);
	if(!pTargetName || pTargetName[0] == '\0')
	{
		pSelf->Bw().SendChatTarget(ClientId, "Usage: /clan_transfer <player>");
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
		pSelf->Bw().SendChatTarget(ClientId, "Player not found or not online.");
		return;
	}

	CPlayer *pTargetPlayer = pSelf->m_apPlayers[TargetClientId];
	if(!pTargetPlayer || !pTargetPlayer->Bw().m_Account.m_Id)
	{
		pSelf->Bw().SendChatTarget(ClientId, "Target player must be logged in.");
		return;
	}

	if(pTargetPlayer->Bw().m_Account.m_ClanId != pPlayer->Bw().m_Account.m_ClanId)
	{
		pSelf->Bw().SendChatTarget(ClientId, "Target player is not in your clan.");
		return;
	}

	if(TargetClientId == ClientId)
	{
		pSelf->Bw().SendChatTarget(ClientId, "You cannot transfer leadership to yourself.");
		return;
	}

	if(pTargetPlayer->Bw().GetAuthLevel() == ClanAuthLevel::LEADER)
	{
		pSelf->Bw().SendChatTarget(ClientId, "Target is already a leader."); // shouldn't happen
		return;
	}

	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
	{
		pSelf->Bw().SendChatTarget(ClientId, "Requests subsystem unavailable.");
		return;
	}
	int id = requests->CreateClanTransferConfirm(ClientId, pPlayer->Bw().GetClanId(), pTargetPlayer->Bw().m_Account.m_aName, g_Config.m_SvClanConfirmExpiry);
	if(id < 0)
	{
		pSelf->Bw().SendChatTarget(ClientId, "Failed to initiate clan transfer confirmation.");
		return;
	}
	pSelf->Bw().SendChatTarget(ClientId, "Transfer confirmation sent. Type /clan_yes to confirm or /clan_no to cancel.");
}

// /clan_yes: confirm last self-addressed clan confirmation (delete/kick)
void CBlockworlds::ConClanYes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
		return;
	}
	auto ids = requests->GetRequestIdsTo(pResult->m_ClientId, std::nullopt);
	// find the most recent applicable clan confirm addressed to self
	int chosen = -1;
	for(int id : ids)
	{
		CRequests::SRequest info;
		if(!requests->GetRequestInfo(id, info))
			continue;
		if(info.m_To != pResult->m_ClientId || info.m_From != pResult->m_ClientId)
			continue;
		if(info.m_Type == CRequests::SRequest::EType::ClanDeleteConfirm || info.m_Type == CRequests::SRequest::EType::ClanKickConfirm || info.m_Type == CRequests::SRequest::EType::ClanRenameConfirm || info.m_Type == CRequests::SRequest::EType::ClanCreateConfirm || info.m_Type == CRequests::SRequest::EType::ClanTransferConfirm)
		{
			if(info.m_ExpireTick > pSelf->Server()->Tick())
				chosen = std::max(chosen, id);
		}
	}
	if(chosen == -1)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No pending clan confirmation to accept.");
		return;
	}
	if(!requests->AcceptRequest(chosen))
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to accept confirmation.");
}

// /clan_no: decline last self-addressed clan confirmation
void CBlockworlds::ConClanNo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
		return;
	}
	auto ids = requests->GetRequestIdsTo(pResult->m_ClientId, std::nullopt);
	int chosen = -1;
	for(int id : ids)
	{
		CRequests::SRequest info;
		if(!requests->GetRequestInfo(id, info))
			continue;
		if(info.m_To != pResult->m_ClientId || info.m_From != pResult->m_ClientId)
			continue;
		if(info.m_Type == CRequests::SRequest::EType::ClanDeleteConfirm || info.m_Type == CRequests::SRequest::EType::ClanKickConfirm || info.m_Type == CRequests::SRequest::EType::ClanRenameConfirm || info.m_Type == CRequests::SRequest::EType::ClanCreateConfirm || info.m_Type == CRequests::SRequest::EType::ClanTransferConfirm)
		{
			if(info.m_ExpireTick > pSelf->Server()->Tick())
				chosen = std::max(chosen, id);
		}
	}
	if(chosen == -1)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No pending clan confirmation to decline.");
		return;
	}
	if(!requests->DeclineRequest(chosen))
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to decline confirmation.");
}

void CBlockworlds::ConClanLeave(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(ClientId, "You must be logged in to leave a clan.");

	if(pPlayer->Bw().m_Account.m_ClanId < 1)
		return pSelf->Bw().SendChatTarget(ClientId, "You are not in a clan.");

	if(pPlayer->Bw().m_Account.m_AuthLevel == ClanAuthLevel::LEADER)
		return pSelf->Bw().SendChatTarget(ClientId, "The clan leader cannot leave. You must delete the clan or transfer leadership.");

	pSelf->Bw().Clans()->ClanLeave(ClientId);
}

void CBlockworlds::ConClanRole(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int IssuerId = pResult->m_ClientId;
	CPlayer *pIssuer = pSelf->m_apPlayers[IssuerId];
	if(!pIssuer)
		return;

	if(!pIssuer->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(IssuerId, "You must be logged in to change a player's clan role.");

	if(pIssuer->Bw().m_Account.m_ClanId < 1 || pIssuer->Bw().m_Account.m_AuthLevel < ClanAuthLevel::LEADER)
		return pSelf->Bw().SendChatTarget(IssuerId, "Only the clan leader can set roles.");

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
			return pSelf->Bw().SendChatTarget(IssuerId, "Use /clan_transfer to transfer leadership.");
		else
			return pSelf->Bw().SendChatTarget(IssuerId, "Invalid role. Allowed values: member | coleader");
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

		if(!pTargetPlayer->Bw().m_Account.m_Id)
			return pSelf->Bw().SendChatTarget(IssuerId, "The target player is not logged in.");

		if(pIssuer->Bw().m_Account.m_ClanId != pTargetPlayer->Bw().m_Account.m_ClanId)
			return pSelf->Bw().SendChatTarget(IssuerId, "The target player is not in your clan.");

		if(IssuerId == TargetClientId)
			return pSelf->Bw().SendChatTarget(IssuerId, "You cannot change your own role.");

		if(NewAuthLevel < 1 || NewAuthLevel > 2)
			return pSelf->Bw().SendChatTarget(IssuerId, "Invalid role. Allowed values: member | coleader");

		if(pTargetPlayer->Bw().m_Account.m_AuthLevel == ClanAuthLevel::LEADER)
			return pSelf->Bw().SendChatTarget(IssuerId, "You cannot change the role of the clan leader.");

		if(pTargetPlayer->Bw().m_Account.m_AuthLevel == static_cast<ClanAuthLevel>(NewAuthLevel))
			return pSelf->Bw().SendChatTarget(IssuerId, "The player already has that role.");

		pSelf->Bw().Clans()->SetAuthLevel(IssuerId, pTargetPlayer->Bw().m_Account.m_aName, NewAuthLevel, pIssuer->Bw().m_Account.m_ClanId);
		return;
	}

	if(!str_comp(pTargetName, pIssuer->Bw().m_Account.m_aName))
		return pSelf->Bw().SendChatTarget(IssuerId, "You cannot change your own role.");

	if(NewAuthLevel < 1 || NewAuthLevel > 2)
		return pSelf->Bw().SendChatTarget(IssuerId, "Invalid role. Allowed values: member | coleader");

	pSelf->Bw().Clans()->SetAuthLevel(IssuerId, pTargetName, NewAuthLevel, pIssuer->Bw().m_Account.m_ClanId);
}

void CBlockworlds::ConClanRename(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Bw().m_Account.m_Id)
		return pSelf->Bw().SendChatTarget(ClientId, "You must be logged in to rename a clan.");

	if(pPlayer->Bw().m_Account.m_ClanId < 1 || pPlayer->Bw().m_Account.m_AuthLevel != ClanAuthLevel::LEADER)
		return pSelf->Bw().SendChatTarget(ClientId, "Only the clan leader can rename the clan.");

	const char *pNewClanName = pResult->GetString(0);
	int ClanNameLength = str_length(pNewClanName);

	if(ClanNameLength < 3)
		return pSelf->Bw().SendChatTarget(ClientId, "Clan name must be at least 3 characters long!");
	if(ClanNameLength > BW_CLAN_NAME_MAX_LENGTH)
		return pSelf->Bw().SendChatTarget(ClientId, "Clan name is too long (max 11 characters)!");

	if(pSelf->Bw().Clans()->GetClanIdByName(pNewClanName) != -1)
		return pSelf->Bw().SendChatTarget(ClientId, "This clan name is already taken!");

	if(g_Config.m_SvClanRenamePrice > 0)
	{
		int cost = g_Config.m_SvClanRenamePrice;
		if(pPlayer->Bw().GetPlayerBlockpoints() < cost)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "You need %d blockpoints to rename your clan.", cost);
			return pSelf->Bw().SendChatTarget(ClientId, aBuf);
		}
	}

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		std::string oldName = pSelf->Bw().Clans()->GetClanNameCopy(pPlayer->Bw().m_Account.m_ClanId);
		requests->CreateClanRenameConfirm(ClientId, pPlayer->Bw().m_Account.m_ClanId, oldName.c_str(), pNewClanName, g_Config.m_SvClanConfirmExpiry);
		return; // request system will notify hopefully ;(
	}

	return pSelf->Bw().SendChatTarget(ClientId, "Clan rename failed: request system unavailable.");
}

void CBlockworlds::ConDisplayTopClans(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Bw().Clans()->ShowTopClans(ClientId);
}

void CBlockworlds::ConContributors(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Huge thanks to Blockworlds contributors:");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "melon, Anime.pdf, zhn, ReiTW, Brokecdx-, Sakido, Gegongt, noby, potato, qxdFox");
}

void CBlockworlds::ConCredits(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "This server has been developped by Nouaa.");
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Thanks to /contributors.");
}

void CBlockworlds::Con1on1(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!g_Config.m_Sv1on1system)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");

	if(!pResult->NumArguments())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Challenge another player by writing '/1on1 name (blockpoints)'");
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "An example would be \"/1on1 nameless tee\" or \"/1on1 marcella 30\"");
	}

	const char *pEnemyName = pResult->GetString(0);
	int Wager = pResult->GetInteger(1);

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CPlayer *pTarget = pSelf->Bw().GetPlayerByName(pEnemyName);

	bool hasArenas = !pSelf->Bw().ZoneManager()->GetNamedQuadCenters("1on1_spawn").empty() ||
			 pSelf->Bw().ZoneManager()->Get1on1ArenaCount() > 0;

	// some errors handling
	if(!pTarget)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player not found");
	if(pTarget->GetCid() == pResult->m_ClientId)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't start a 1vs1 against yourself.");
	if(Wager < 0)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "The amount set for the Wager must be more than 0 or none");

	if(!pPlayer->Bw().IsLoggedIn() && Wager > 0)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You have to be logged in to place a wager in the pot.");
	if(Wager > 0 && (!pTarget->Bw().IsLoggedIn()))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Target player must be logged in to play with a wager.");
	if(Wager > pPlayer->Bw().GetPlayerBlockpoints())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You can't afford to wager that much!");

	if(Wager > pTarget->Bw().GetPlayerBlockpoints())
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player doesn't have enough blockpoints.");

	if(pSelf->Bw().isInEvent(pResult->m_ClientId))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You must finish the ongoing 1on1 match.");

	if(pSelf->Bw().isInEvent(pTarget->GetCid()))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "This player is already in a 1on1 match.");

	char aBuf[256];
	if(!hasArenas)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Error: This map does not have any 1on1 spawn positions defined.");

	// create invite via requests component
	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		{
			int id = requests->Create1on1Invite(pPlayer->GetCid(), pTarget->GetCid(), Wager, g_Config.m_Sv1on1InviteExpiry);
			if(id == -1)
				return; // Create1on1Invite already informed the sender about the duplicate
			pPlayer->Bw().sent1on1InviteTo = pTarget->GetCid();
			str_format(aBuf, sizeof(aBuf), "Match request has been sent to '%s' (%d BP).", pSelf->Server()->ClientName(pTarget->GetCid()), Wager);
			pSelf->Bw().SendChatTarget(pPlayer->GetCid(), aBuf);
		}
		return;
	}
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
}

void CBlockworlds::Con1on1Accept(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!g_Config.m_Sv1on1system)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		if(pResult->NumArguments() > 0 && pResult->GetString(0)[0])
		{
			const char *arg = pResult->GetString(0);
			CPlayer *pFrom = pSelf->Bw().GetPlayerByName(arg);
			if(!pFrom)
			{
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player not found");
				return;
			}

			auto ids = requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
			if(ids.empty())
			{
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No invitation from that player was found.");
				return;
			}

			// choose the most recent valid one (highest id -> newest)
			int chosen = -1;
			for(int id : ids)
			{
				CRequests::SRequest info;
				if(requests->GetRequestInfo(id, info))
				{
					if(info.m_ExpireTick > pSelf->Server()->Tick())
						chosen = std::max(chosen, id);
				}
			}
			if(chosen == -1)
			{
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No active (non-expired) invitation from that player was found.");
				return;
			}

			if(!requests->AcceptRequest(chosen))
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to accept the invitation (it may have expired).");
			return;
		}

		auto pending = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
		if(pending.size() == 1)
		{
			int id = pending[0];
			CRequests::SRequest info;
			if(!requests->GetRequestInfo(id, info) || info.m_ExpireTick <= pSelf->Server()->Tick())
			{
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No active invitation to accept was found (it may have expired).");
				return;
			}
			if(!requests->AcceptRequest(id))
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to accept the invitation (it may have expired).");
			return;
		}

		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No invitation to accept was found (try to use /accept <playerName>).");
		return;
	}
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
}

void CBlockworlds::Con1on1Decline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!g_Config.m_Sv1on1system)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		if(pResult->NumArguments() > 0 && pResult->GetString(0)[0])
		{
			const char *arg = pResult->GetString(0);
			CPlayer *pFrom = pSelf->Bw().GetPlayerByName(arg);
			if(!pFrom)
			{
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Player not found");
				return;
			}

			auto ids = requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
			if(ids.empty())
			{
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No invitation from that player was found.");
				return;
			}

			int chosen = -1;
			for(int id : ids)
			{
				CRequests::SRequest info;
				if(requests->GetRequestInfo(id, info))
				{
					if(info.m_ExpireTick > pSelf->Server()->Tick())
						chosen = std::max(chosen, id);
				}
			}
			if(chosen == -1)
			{
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No active (non-expired) invitation from that player was found.");
				return;
			}

			if(!requests->DeclineRequest(chosen))
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to decline the invitation (it may have expired).");
			return;
		}

		auto pending = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
		if(pending.size() == 1)
		{
			int id = pending[0];
			CRequests::SRequest info;
			if(!requests->GetRequestInfo(id, info) || info.m_ExpireTick <= pSelf->Server()->Tick())
			{
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No active invitation to decline was found (it may have expired).");
				return;
			}
			if(!requests->DeclineRequest(id))
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to decline the invitation (it may have expired).");
			return;
		}

		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "No invitation to decline was found (or use /decline <playerName>). Try checking your messages.");
		return;
	}
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
}

void CBlockworlds::Con1on1Ready(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!g_Config.m_Sv1on1system)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");

	auto mgr = g_ComponentRegistry.Get<COneOnOneManager>();
	if(!mgr)
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "1on1 system is not available.");
		return;
	}

	auto match = mgr->GetMatchForPlayer(pResult->m_ClientId);
	if(!match || !match->IsInConfigPhase())
	{
		pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not in a 1on1 warmup phase.");
		return;
	}

	// /ready now casts a "Start" vote (same as pressing F3 in the vote overlay)
	match->OnDuelVote(pResult->m_ClientId, 1);
}

void CBlockworlds::ConJoinEvent(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "use /join instead.");
}

void CBlockworlds::ConCreateTDM(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(pSelf->Bw().isInEvent(pResult->m_ClientId))
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You must finish the ongoing event (or use '/leave' to leave).");

	if(auto events = g_ComponentRegistry.Get<CEvents>())
	{
		auto ev = events->CreateEventByName("tdm");
		if(ev)
		{
			ev->SetStateChangeCallback([](auto, auto) {});
			events->SetActiveEvent(ev);
			return;
		}
	}
	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Failed to create TDM event: events subsystem unavailable.");
}

void CBlockworlds::ConLeaveEvent(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->Bw().SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer)
		return;

	// check if player is in a 1on1 match (any phase)
	if(auto mgr = g_ComponentRegistry.Get<COneOnOneManager>(); mgr)
	{
		auto match = mgr->GetMatchForPlayer(pResult->m_ClientId);
		if(match)
		{
			auto state = match->GetState();
			if(state == COneOnOneEvent::EEventState::Preparation)
			{
				// during warmup/config phase - cancel without penalty, no escrow was collected
				int otherCid = (pResult->m_ClientId == match->m_Player1ID) ? match->m_Player2ID : match->m_Player1ID;
				pSelf->Bw().SendChatTarget(pResult->m_ClientId, "[1on1] You left the warmup. Match cancelled.");
				pSelf->Bw().SendChatTarget(otherCid, "[1on1] Your opponent left during warmup. Match cancelled.");

				// clear duel config vote pages for both
				extern CVoteManager g_VoteManager;
				for(int cid : {match->m_Player1ID, match->m_Player2ID})
				{
					g_VoteManager.NavigateToRoot(cid);
					pSelf->Bw().ClearVotes(cid);
				}

				match->AbortAndRefund(nullptr);
				return;
			}
			else if(state == COneOnOneEvent::EEventState::Active)
			{
				// during active match - leave counts as ragequit (opponent wins)
				match->Leave(pResult->m_ClientId);
				return;
			}
		}
	}

	if(auto events = g_ComponentRegistry.Get<CEvents>(); events)
	{
		auto subs = events->GetSubComponents();
		bool Found = false;
		for(auto &sub : subs)
		{
			CEventComponent *pEv = dynamic_cast<CEventComponent *>(sub.operator->());
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

	pSelf->Bw().SendChatTarget(pResult->m_ClientId, "You are not in any event!");
}

// Components

void CBlockworlds::ConComponentList(IConsole::IResult *pResult, void *pUserData)
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
		if(MainComponentPtrs.count(&*pActiveComp) == 0)
			dbg_msg("Components", "[+] %s", pActiveComp->GetName());
	}
}

void CBlockworlds::ConComponentPlug(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	char aName[64];
	str_copy(aName, pResult->GetString(0));
	str_clean_whitespaces(aName);

	if(!pSelf->m_pScore)
	{
		pSelf->Bw().m_ComponentsQueue.emplace(aName);
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

void CBlockworlds::ConComponentUnPlug(IConsole::IResult *pResult, void *pUserData)
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

void CBlockworlds::ConDisplayPages(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
	{
		pSelf->Bw().SendChatTarget(ClientId, "You must be logged in to use /pages.");
		return;
	}
	int Pages = pPlayer->Bw().GetPlayerPages();
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "You have %d deathnote page%s.", Pages, Pages == 1 ? "" : "s");
	pSelf->Bw().SendChatTarget(ClientId, aBuf);
}

void CBlockworlds::ConPassive(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	int secondsLeft = 0;
	if(!pPlayer->Bw().IsLoggedIn())
		secondsLeft = pPlayer->Bw().m_LocalPassiveDuration;
	else
		secondsLeft = pPlayer->Bw().GetPlayerPassive();

	char aBuf[128];
	if(secondsLeft > 0)
	{
		int hours = secondsLeft / 3600;
		int minutes = (secondsLeft % 3600) / 60;
		int seconds = secondsLeft % 60;
		if(hours > 0)
			str_format(aBuf, sizeof(aBuf), "You have %dh %dm %ds of passive protection left.", hours, minutes, seconds);
		else if(minutes > 0)
			str_format(aBuf, sizeof(aBuf), "You have %dm %ds of passive protection left.", minutes, seconds);
		else
			str_format(aBuf, sizeof(aBuf), "You have %d seconds of passive protection left.", seconds);
	}
	else
		str_copy(aBuf, "You have no passive protection left.", sizeof(aBuf));
	pSelf->Bw().SendChatTarget(ClientId, aBuf);
}

void CBlockworlds::ConSetGunDesignCosmetic(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;
	const int Index = pResult->GetInteger(0);
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Bw().ToggleGunDesign(Index);
}

void CBlockworlds::ConSetKnockoutCosmetic(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;
	const int Index = pResult->GetInteger(0);
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Bw().ToggleKnockout(Index);
}

void CBlockworlds::ConSetSkinManiCosmetic(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;
	const int Index = pResult->GetInteger(0);
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Bw().ToggleSkinMani(Index);
}

void CBlockworlds::ConSetSpecialCosmetic(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;
	const int Index = pResult->GetInteger(0);
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Bw().ToggleSpecial(Index);
}

void CBlockworlds::ConBanhammer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim();
	if(!CheckClientId(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;
	pPlayer->Bw().m_BanhammerActive = !pPlayer->Bw().m_BanhammerActive;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Banhammer %s for '%s'", pPlayer->Bw().m_BanhammerActive ? "enabled" : "disabled", pSelf->Server()->ClientName(Victim));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}
