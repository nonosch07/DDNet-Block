#include "base/system.h"
#include "gamecontext.h"

#include <engine/antibot.h>
#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamemodes/DDRace.h>
#include <game/server/player.h>
#include <game/server/save.h>
#include <game/server/teams.h>

#include <blockworlds/accounts.h>
#include <blockworlds/clans.h>
#include <blockworlds/components/requests.h>

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>

#include <algorithm>
#include <blockworlds/zones/zone.h>
#include <string>
#include <unordered_map>

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

void CGameContext::ConRegister(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(pSelf->m_apPlayers[pResult->m_ClientId]->m_Account.m_Id)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can't create an account while being logged in!");

	if(pSelf->isInEvent(pResult->m_ClientId))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can't register while participating in an event. Use /leave first.");
	IZone *pSpawnZone = pSelf->ZoneManager()->GetZone(ZONE_SPAWN);
	CPlayer *pReqPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(pSpawnZone && pReqPlayer && pReqPlayer->GetCharacter() && !pSpawnZone->IsInZone(pReqPlayer->GetCharacter()->m_Pos))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can only register an account while in the spawn zone.");

	const char *pUsername = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);

	int NameLength = str_length(pUsername);
	int PasswordLength = str_length(pPassword);

	if(NameLength <= 2)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Your name must be at least 3 characters long!");

	if(pReqPlayer)
	{
		const int RegisterCooldownSeconds = g_Config.m_SvRegisterCooldownPerIp > 0 ? g_Config.m_SvRegisterCooldownPerIp : 10;
		int64_t now = pSelf->Server()->Tick();
		if(pReqPlayer->m_LastRegisterTick != 0 && now - pReqPlayer->m_LastRegisterTick < RegisterCooldownSeconds * pSelf->Server()->TickSpeed())
		{
			int remaining = (int)((RegisterCooldownSeconds * pSelf->Server()->TickSpeed() - (now - pReqPlayer->m_LastRegisterTick)) / pSelf->Server()->TickSpeed());
			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before trying again.", remaining, remaining != 1 ? "s" : "");
			return pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
		}
	}

	if(PasswordLength < 5)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Your password must be at least 5 characters long!");

	if(str_comp(pUsername, pPassword) == 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Password must be different from username!");

	if(NameLength * sizeof(char) >= 11)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account name too long!");

	if(!CheckValidChars(pUsername) || !CheckValidChars(pPassword))
		return pSelf->SendChatTarget(pResult->m_ClientId, "Only the characters A-Z and 0-9 are allowed!");

	char aAddrStrCheck[NETADDR_MAXSTRSIZE];
	pSelf->Server()->GetClientAddr(pResult->m_ClientId, aAddrStrCheck, sizeof(aAddrStrCheck));
	int RemainingBan = 0;
	if(!pPlayer->m_IsNpc)
	{
		if(pSelf->Accounts()->IsIpBanned(aAddrStrCheck, RemainingBan))
		{
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "Too many recent attempts from your connection. Please wait %d second%s and try again.", RemainingBan, RemainingBan == 1 ? "" : "s");
			return pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
		}
		if(!pSelf->Accounts()->RegisterIpAttempt(aAddrStrCheck))
		{
			pSelf->Accounts()->IsIpBanned(aAddrStrCheck, RemainingBan);
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "You're trying a lot. Take a short break (%d second%s) and try again.", RemainingBan, RemainingBan == 1 ? "" : "s");
			return pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
		}
	}

	pSelf->Accounts()->Register(pResult->m_ClientId, pUsername, pPassword);
	if(pReqPlayer)
		pReqPlayer->m_LastRegisterTick = pSelf->Server()->Tick();
}

void CGameContext::ConLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(pSelf->m_apPlayers[pResult->m_ClientId]->m_Account.m_Id > 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are already logged in!");
	if(pSelf->isInEvent(pResult->m_ClientId))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You must finish the ongoing event (or use '/leave' to leave).");
	IZone *pSpawnZone = pSelf->ZoneManager()->GetZone(ZONE_SPAWN);
	CPlayer *pReqPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(pSpawnZone && pReqPlayer && pReqPlayer->GetCharacter() && !pSpawnZone->IsInZone(pReqPlayer->GetCharacter()->m_Pos))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can only login while in the spawn zone.");

	const char *pUsername = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);

	pSelf->Accounts()->Login(pResult->m_ClientId, pUsername, pPassword);
}

void CGameContext::ConAccountLogout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pSelf->m_apPlayers[pResult->m_ClientId]->m_Account.m_Id)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in!");

	if(pSelf->isInEvent(pResult->m_ClientId))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can't logout while participating in an event. Use /leave first.");
	IZone *pSpawnZone = pSelf->ZoneManager()->GetZone(ZONE_SPAWN);
	CPlayer *pReqPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(pSpawnZone && pReqPlayer && pReqPlayer->GetCharacter() && !pSpawnZone->IsInZone(pReqPlayer->GetCharacter()->m_Pos))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can only logout while in the spawn zone.");

	// cancel any pending requests involving this client before logout (so others aren't left with stale offers)
	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		requests->CancelRequestsInvolving(pResult->m_ClientId, std::nullopt, "player logged out");
	}
	pPlayer->OnPlayerLogout();
	pSelf->SendChatTarget(pResult->m_ClientId, "you have been logged out!");
}

void CGameContext::ConDisplayBlockpoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pSelf->m_apPlayers[pResult->m_ClientId]->m_Account.m_Id)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in!");

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "You currently have %d blockpoint%s!",
		pSelf->m_apPlayers[pResult->m_ClientId]->m_Account.m_Blockpoints,
		pSelf->m_apPlayers[pResult->m_ClientId]->m_Account.m_Blockpoints != 1 ? "s" : "");
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
}

// /give_bp <playerName> <amount>
void CGameContext::ConGiveBlockpointsRequest(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pFrom = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pFrom || !pFrom->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in.");
	if(pResult->NumArguments() < 2)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Usage: /give_bp <playerName> <amount>");
	const char *pTargetName = pResult->GetString(0);
	int Amount = pResult->GetInteger(1);
	if(Amount < g_Config.m_SvBpTransferAmountMin)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Amount below minimum transfer threshold.");
	if(Amount > g_Config.m_SvBpTransferAmountCap)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Amount exceeds max cap.");
	if(Amount < 1)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Amount too small.");
	CPlayer *pTo = pSelf->GetPlayerByName(pTargetName);
	if(!pTo || !pTo->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "Target player not found or not logged in.");
	if(pTo->GetCid() == pFrom->GetCid())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You cannot transfer blockpoints to yourself.");
	if(pFrom->GetPlayerBlockpoints() < Amount)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You don't have enough blockpoints.");
	// disallow if either player is currently in an event
	if(pSelf->isInEvent(pFrom->GetCid()) || pSelf->isInEvent(pTo->GetCid()))
		return pSelf->SendChatTarget(pResult->m_ClientId, "Blockpoint transfers are not allowed while either player is in an event.");
	int Cooldown = g_Config.m_SvBpTransferCooldown;
	if(Cooldown > 0 && pFrom->m_LastBpTransferOfferTick != 0 && pSelf->Server()->Tick() - pFrom->m_LastBpTransferOfferTick < Cooldown * pSelf->Server()->TickSpeed())
	{
		int Rem = (int)((Cooldown * pSelf->Server()->TickSpeed() - (pSelf->Server()->Tick() - pFrom->m_LastBpTransferOfferTick)) / pSelf->Server()->TickSpeed());
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another transfer.", Rem, Rem != 1 ? "s" : "");
		return pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
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
			return pSelf->SendChatTarget(pResult->m_ClientId, "You have too many pending transfers. Wait for them to resolve.");
	}
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
	int expiry = g_Config.m_SvBpTransferExpiry;
	int id = requests->CreateBlockpointTransfer(pFrom->GetCid(), pTo->GetCid(), Amount, expiry);
	if(id == -1)
		return; // error already messaged
	pFrom->m_LastBpTransferOfferTick = pSelf->Server()->Tick();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Transfer offer (%ds) sent to %s for %d blockpoints.", expiry, pSelf->Server()->ClientName(pTo->GetCid()), Amount);
	pSelf->SendChatTarget(pFrom->GetCid(), aBuf);
}

// /accept_bp [playerName]
void CGameContext::ConAcceptBlockpointsRequest(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in.");
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
	auto matchIds = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
	int chosen = -1;
	if(pResult->NumArguments() > 0)
	{
		const char *pFromName = pResult->GetString(0);
		CPlayer *pFrom = pSelf->GetPlayerByName(pFromName);
		if(!pFrom)
			return pSelf->SendChatTarget(pResult->m_ClientId, "Player not found");
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
		return pSelf->SendChatTarget(pResult->m_ClientId, matchIds.empty() ? "No blockpoint transfer to accept." : "Multiple transfers pending. Use /accept_bp <playerName>.");
	}
	if(chosen == -1)
		return pSelf->SendChatTarget(pResult->m_ClientId, "No valid (non-expired) transfer found.");
	if(!requests->AcceptRequest(chosen))
		return pSelf->SendChatTarget(pResult->m_ClientId, "Failed to accept the transfer (it may have expired).");
}

// /decline_bp [playerName]
void CGameContext::ConDeclineBlockpointsRequest(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in.");
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
	auto matchIds = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::BlockpointTransfer);
	int chosen = -1;
	if(pResult->NumArguments() > 0)
	{
		const char *pFromName = pResult->GetString(0);
		CPlayer *pFrom = pSelf->GetPlayerByName(pFromName);
		if(!pFrom)
			return pSelf->SendChatTarget(pResult->m_ClientId, "Player not found");
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
		return pSelf->SendChatTarget(pResult->m_ClientId, matchIds.empty() ? "No blockpoint transfer to decline." : "Multiple transfers pending. Use /decline_bp <playerName>.");
	}
	if(chosen == -1)
		return pSelf->SendChatTarget(pResult->m_ClientId, "No valid (non-expired) transfer found.");
	if(!requests->DeclineRequest(chosen))
		return pSelf->SendChatTarget(pResult->m_ClientId, "Failed to decline the transfer (it may have expired).");
}

void CGameContext::ConChangePassword(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pSelf->m_apPlayers[pResult->m_ClientId]->m_Account.m_Id)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in!");

	const char *pOldPassword = pResult->GetString(0);
	const char *pNewPassword = pResult->GetString(1);

	int OldLength = str_length(pOldPassword);
	int NewLenght = str_length(pNewPassword);

	if(OldLength < 5)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Old password incorrect (must be at least 5 chars long).");

	if(NewLenght < 5)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Your password must be at least 5 characters long!");

	if(str_comp(pOldPassword, pNewPassword) == 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Password must be different from each other!");

	if(!CheckValidChars(pOldPassword) || !CheckValidChars(pNewPassword))
		return pSelf->SendChatTarget(pResult->m_ClientId, "Only the characters A-Z and 0-9 are allowed!");

	pSelf->Accounts()->ChangePassword(pResult->m_ClientId, pSelf->m_apPlayers[pResult->m_ClientId]->m_Account.m_aName, pOldPassword, pNewPassword);
}

void CGameContext::ConDisplayProfile(IConsole::IResult *pResult, void *pUserData)
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

	if(!pTargetPlayer->m_Account.m_Id)
	{
		pSelf->SendChatTarget(ClientId, "The target player is not logged in.");
		return;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "*** Profile of %s",
		pTargetPlayer->m_Account.m_aName);
	pSelf->SendChatTarget(ClientId, aBuf);

	pSelf->SendChatTarget(ClientId, "*** ------Global------");

	// global stats: Kills, Deaths, Max Kill Streak
	str_format(aBuf, sizeof(aBuf), "*** Kills: %d", pTargetPlayer->m_Account.m_Kills);
	pSelf->SendChatTarget(ClientId, aBuf);

	str_format(aBuf, sizeof(aBuf), "*** Deaths: %d", pTargetPlayer->m_Account.m_Deaths);
	pSelf->SendChatTarget(ClientId, aBuf);

	// global K/D ratio
	float KD = pTargetPlayer->m_Account.m_Deaths > 0 ? (float)pTargetPlayer->m_Account.m_Kills / pTargetPlayer->m_Account.m_Deaths : (float)pTargetPlayer->m_Account.m_Kills;
	str_format(aBuf, sizeof(aBuf), "*** K/D: %.2f", KD);
	pSelf->SendChatTarget(ClientId, aBuf);

	str_format(aBuf, sizeof(aBuf), "*** Max Kill Streak: %d", pTargetPlayer->m_Account.m_Killstreak);
	pSelf->SendChatTarget(ClientId, aBuf);

	// LMB Wins
	str_format(aBuf, sizeof(aBuf), "*** LMB Wins: %d", pTargetPlayer->m_Account.m_TourneyWin);
	pSelf->SendChatTarget(ClientId, aBuf);

	// playtime in hours and minutes
	int Hours = pTargetPlayer->m_Account.m_Playtime / 3600;
	int Minutes = (pTargetPlayer->m_Account.m_Playtime % 3600) / 60;
	str_format(aBuf, sizeof(aBuf), "*** PlayTime: %d hours %d minutes", Hours, Minutes);
	pSelf->SendChatTarget(ClientId, aBuf);

	pSelf->SendChatTarget(ClientId, "*** ------Cosmetics------");

	int OwnedSkinmani = 0;
	for(int i = 0; i < CCosmeticsHandler::NUM_SKINMANIS; i++)
	{
		if(pSelf->Cosmetics()->HasSkinmani(pTargetPlayer->GetCid(), i))
			OwnedSkinmani++;
	}
	str_format(aBuf, sizeof(aBuf), "*** Skin Manipulations: %d/%d", OwnedSkinmani, CCosmeticsHandler::NUM_SKINMANIS);
	pSelf->SendChatTarget(ClientId, aBuf);

	int OwnedGundesign = 0;
	for(int i = 0; i < CCosmeticsHandler::NUM_GUNDESIGNS; i++)
	{
		if(pSelf->Cosmetics()->HasGundesign(pTargetPlayer->GetCid(), i))
			OwnedGundesign++;
	}
	str_format(aBuf, sizeof(aBuf), "*** Gun Designs: %d/%d", OwnedGundesign, CCosmeticsHandler::NUM_GUNDESIGNS);
	pSelf->SendChatTarget(ClientId, aBuf);

	int OwnedKnockouts = 0;
	for(int i = 0; i < CCosmeticsHandler::NUM_KNOCKOUTS; i++)
	{
		if(pSelf->Cosmetics()->HasKnockoutEffect(pTargetPlayer->GetCid(), i))
			OwnedKnockouts++;
	}
	str_format(aBuf, sizeof(aBuf), "*** Knockout Effects: %d/%d", OwnedKnockouts, CCosmeticsHandler::NUM_KNOCKOUTS);
	pSelf->SendChatTarget(ClientId, aBuf);

	// pSelf->SendChatTarget(ClientId, "*** ------Ranked------");

	// // ranked stats: Games, Kills, Deaths, Wins
	// str_format(aBuf, sizeof(aBuf), "*** Games: %d", pTargetPlayer->m_Account.m_RankedGames);
	// pSelf->SendChatTarget(ClientId, aBuf);

	// str_format(aBuf, sizeof(aBuf), "*** Rating: %d", pTargetPlayer->m_Account.m_Ranking);
	// pSelf->SendChatTarget(ClientId, aBuf);

	// str_format(aBuf, sizeof(aBuf), "*** Kills: %d", pTargetPlayer->m_Account.m_RankedKills);
	// pSelf->SendChatTarget(ClientId, aBuf);

	// str_format(aBuf, sizeof(aBuf), "*** Deaths: %d", pTargetPlayer->m_Account.m_RankedDeaths);
	// pSelf->SendChatTarget(ClientId, aBuf);

	// // ranked K/D ratio
	// float RankedKD = pTargetPlayer->m_Account.m_RankedDeaths > 0 ? (float)pTargetPlayer->m_Account.m_RankedKills / pTargetPlayer->m_Account.m_RankedDeaths : (float)pTargetPlayer->m_Account.m_RankedKills;
	// str_format(aBuf, sizeof(aBuf), "*** K/D: %.2f", RankedKD);
	// pSelf->SendChatTarget(ClientId, aBuf);

	// // ranked Wins and Win Rate
	// str_format(aBuf, sizeof(aBuf), "*** Wins: %d", pTargetPlayer->m_Account.m_RankedWins);
	// pSelf->SendChatTarget(ClientId, aBuf);

	// float WinRate = pTargetPlayer->m_Account.m_RankedGames > 0 ? (float)pTargetPlayer->m_Account.m_RankedWins / pTargetPlayer->m_Account.m_RankedGames * 100.0f : 0.0f;
	// str_format(aBuf, sizeof(aBuf), "*** Win Rate: %.2f%%", WinRate);
	// pSelf->SendChatTarget(ClientId, aBuf);
}

void CGameContext::ConGetCid(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	if(pResult->NumArguments() != 1)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Usage: /getcid <player name>");
		return;
	}

	const char *pTargetName = pResult->GetString(0);
	CPlayer *pTarget = pSelf->GetPlayerByName(pTargetName);
	if(!pTarget)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Player not found");
		return;
	}

	pSelf->SendChatTarget(pResult->m_ClientId, "%s -> cid %d", pSelf->Server()->ClientName(pTarget->GetCid()), pTarget->GetCid());
}

void CGameContext::ConStatusAccounts(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const char *pFilter = pResult->NumArguments() == 1 ? pResult->GetString(0) : "";
	char aBuf[512];

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = pSelf->m_apPlayers[i];
		if(!pPlayer)
			continue;
		if(!pPlayer->IsLoggedIn())
			continue;

		// filter by name (client name or account name)
		if(pFilter[0] != '\0')
		{
			if(!str_utf8_find_nocase(pPlayer->GetPlayerName(), pFilter) && !str_utf8_find_nocase(pPlayer->m_Account.m_aName, pFilter))
				continue;
		}

		int Hours = pPlayer->m_Account.m_Playtime / 3600;
		int Minutes = (pPlayer->m_Account.m_Playtime % 3600) / 60;

		const char *pClanName = " ";
		if(pSelf->Clans())
		{
			CClansData tmp;
			if(pSelf->Clans()->GetClanSnapshotById(pPlayer->m_Account.m_ClanId, tmp))
				pClanName = tmp.m_ClanName;
		}
		str_format(aBuf, sizeof(aBuf), "cid=%d, accid=%d, acc_name='%s', ig_name='%s', vip=%d, clan='%s', clanid=%d, auth=%d, playtime=%02d:%02d, ranking=%d, kills=%d, deaths=%d",
			i,
			pPlayer->m_Account.m_Id,
			pPlayer->m_Account.m_aName,
			pSelf->Server()->ClientName(i),
			pPlayer->m_Account.m_Vip,
			pClanName,
			pPlayer->m_Account.m_ClanId,
			pPlayer->m_Account.m_AuthLevel,
			Hours,
			Minutes,
			pPlayer->m_Account.m_Ranking,
			pPlayer->m_Account.m_Kills,
			pPlayer->m_Account.m_Deaths);

		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CGameContext::ConDisplayTopLevel(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Accounts()->ShowTopLevel(ClientId);
}

void CGameContext::ConIpBans(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!CheckClientId(pResult->m_ClientId))
		return;
	// pPlayer not used here; avoid unused variable warning
	if(!pPlayer || !pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "Permission denied");

	auto bans = pSelf->Accounts()->ListIpBans();
	if(bans.empty())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "No active IP bans.");
		return;
	}

	pSelf->SendChatTarget(pResult->m_ClientId, "Active IP bans:");
	char aBuf[256];
	for(const auto &b : bans)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d second%s remaining", b.first.c_str(), b.second, b.second != 1 ? "s" : "");
		pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	}
}

void CGameContext::ConIpBanClear(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!CheckClientId(pResult->m_ClientId))
		return;
	// pPlayer not used here; avoid unused variable warning
	if(!pPlayer || !pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "Permission denied");

	const char *pIp = pResult->GetString(0);
	pSelf->Accounts()->ClearIpBan(pIp);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Cleared IP ban for %s", pIp);
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
}

void CGameContext::ConListOutstandingInvites(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "Permission denied");

	int Target = pResult->GetInteger(0);
	if(!CheckClientId(Target))
		return pSelf->SendChatTarget(pResult->m_ClientId, "Invalid client id");

	if(!pSelf->m_apPlayers[Target])
		return pSelf->SendChatTarget(pResult->m_ClientId, "Target player not online");

	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Request component not available");

	auto idsFrom = requests->GetRequestIdsFromTo(Target, Target, std::nullopt); // get both to and from via helper below

	auto all = requests->GetRequestsFor(Target, std::nullopt);
	if(all.empty())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "No outstanding requests for player");
		return;
	}

	char aBuf[256];
	pSelf->SendChatTarget(pResult->m_ClientId, "Outstanding requests (id, type, from -> to):");
	for(int id : all)
	{
		str_format(aBuf, sizeof(aBuf), "id=%d", id);
		pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	}
}

// admin console commands to modify account attributes
void CGameContext::ConGivePages(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->SetPlayerPages(pTarget->GetPlayerPages() + Amount);
	pSelf->Accounts()->Save(Target, &pTarget->m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d pages to %s (now %d)", Amount, pTarget->GetPlayerName(), pTarget->GetPlayerPages());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->SendChatTarget(Target, aBuf);
}

void CGameContext::ConGiveLevel(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->SetPlayerLevel(pTarget->GetPlayerLevel() + Amount);
	pSelf->Accounts()->Save(Target, &pTarget->m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d levels to %s (now %d)", Amount, pTarget->GetPlayerName(), pTarget->GetPlayerLevel());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->SendChatTarget(Target, aBuf);
}

void CGameContext::ConGiveExperience(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->SetPlayerExperience(pTarget->GetPlayerExperience() + Amount);
	pSelf->Accounts()->Save(Target, &pTarget->m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d experience to %s (now %d)", Amount, pTarget->GetPlayerName(), pTarget->GetPlayerExperience());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->SendChatTarget(Target, aBuf);
}

void CGameContext::ConGiveWeaponkits(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->SetPlayerWeaponkits(pTarget->GetPlayerWeaponkits() + Amount);
	pSelf->Accounts()->Save(Target, &pTarget->m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d weaponkits to %s (now %d)", Amount, pTarget->GetPlayerName(), pTarget->GetPlayerWeaponkits());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->SendChatTarget(Target, aBuf);
}

void CGameContext::ConGiveBlockpoints(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->SetPlayerBlockpoints(pTarget->GetPlayerBlockpoints() + Amount);
	pSelf->Accounts()->Save(Target, &pTarget->m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d blockpoints to %s (now %d)", Amount, pTarget->GetPlayerName(), pTarget->GetPlayerBlockpoints());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->SendChatTarget(Target, aBuf);
}

void CGameContext::ConGivePassive(IConsole::IResult *pResult, void *pUserData)
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
	if(!pTarget->IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}
	pTarget->SetPlayerPassive(Seconds);
	if(pTarget->GetCharacter())
		pTarget->GetCharacter()->Core()->m_Passive = true;
	pSelf->Accounts()->Save(Target, &pTarget->m_Account);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Gave %d seconds of passive to %s", Seconds, pTarget->GetPlayerName());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->SendChatTarget(Target, aBuf);
}

void CGameContext::ConSetVip(IConsole::IResult *pResult, void *pUserData)
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

	if(!pTarget->IsLoggedIn())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Target player is not logged in");
		return;
	}

	int newVip = Vip ? 1 : 0;
	pTarget->SetPlayerVip(newVip);
	pSelf->Accounts()->Save(Target, &pTarget->m_Account);

	char aBuf[128];
	if(newVip)
		str_format(aBuf, sizeof(aBuf), "Set VIP for %s (now vip=%d)", pTarget->GetPlayerName(), pTarget->GetPlayerVip());
	else
		str_format(aBuf, sizeof(aBuf), "Removed VIP from %s", pTarget->GetPlayerName());

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->SendChatTarget(Target, aBuf);
}

void CGameContext::ConAdminSetPassword(IConsole::IResult *pResult, void *pUserData)
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

	SHA256_DIGEST HashedNewPassword = CGameContext::HashPassword(pNewPassword);
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

	pSelf->Accounts()->ChangePasswordAdmin(pResult->m_ClientId, pAccountName, pNewPassword);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Set password for account '%s'", pAccountName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// notify player if they are online
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *p = pSelf->m_apPlayers[i];
		if(!p)
			continue;
		if(!p->IsLoggedIn())
			continue;
		if(str_comp(p->GetPlayerName(), pAccountName) == 0 || str_comp(p->m_Account.m_aName, pAccountName) == 0)
		{
			pSelf->SendChatTarget(i, "An administrator has changed your account password.");
			break;
		}
	}
}

void CGameContext::ConDisplayTopBlockpoints(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Accounts()->ShowTopBlockpoints(ClientId);
}

void CGameContext::ConDisplayTopKillStreak(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Accounts()->ShowTopKillStreak(ClientId);
}

void CGameContext::ConWeaponKit(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You must be alive to use a weapon kit.");

	if(!pSelf->m_WeaponkitsAllowed)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Weaponkits are currently disabled on this server.");
		return;
	}

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in.");

	if(pPlayer->GetPlayerWeaponkits() < 1 && !pPlayer->GetPlayerVip())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You don't have any weapon kits, make a trip to the store and purchase some!");

	// check if the player has all weapons
	bool HasAllWeapons = true;
	for(int i = WEAPON_GUN; i < NUM_WEAPONS - 1; i++)
	{
		if(!pChr->BWCore().m_aWeapons[i].m_Got)
		{
			HasAllWeapons = false;
			break;
		}
	}

	if(HasAllWeapons)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You already have all weapons.");

	// restrict usage to spawn zone if spawn zone exists
	IZone *pSpawnZone = pSelf->ZoneManager()->GetZone(ZONE_SPAWN);
	if(pSpawnZone)
	{
		if(!pSpawnZone->IsInZone(pChr->m_Pos))
			return pSelf->SendChatTarget(pResult->m_ClientId, "You can only use weapon kits while in the spawn zone.");
	}

	pSelf->ModifyWeapons(pResult, pUserData, -1, false);

	char aBuf[128];

	if(pPlayer->GetPlayerVip())
		str_copy(aBuf, "You have successfuly used a weaponkit!", sizeof(aBuf));
	else
	{
		str_format(aBuf, sizeof(aBuf), "You have successfuly used a weaponkit! %d kits left.", pPlayer->GetPlayerWeaponkits());
		pPlayer->SetPlayerWeaponkits(pPlayer->GetPlayerWeaponkits() - 1);
	}
	return pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
}

void CGameContext::ConDeathnote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	IServer *pServer = pSelf->Server();

	if(!pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in.");

	if(!pResult->NumArguments())
		return pSelf->SendChatTarget(pResult->m_ClientId, "Invalid arguments... Usage: deathnote [player]");

	CPlayer *pTarget = pSelf->GetPlayerByName(pResult->GetString(0));

	if(!pTarget)
		return pSelf->SendChatTarget(pResult->m_ClientId, "This player doesn't exist.");

	if(pPlayer->GetPlayerPages() < 1)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You don't have any deathnotes, make a trip to the store and purchase some!");

	// prevent using deathnote if the target is inside passive or no-collision zones
	IZone *pPassiveZone = pSelf->ZoneManager()->GetZone(ZONE_PASSIVE);
	IZone *pNoCollZone = pSelf->ZoneManager()->GetZone(ZONE_NOCOLL);

	// prevent using deathnote if target is participating in an event
	if(pTarget && pSelf->isInEvent(pTarget->GetCid()) != 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a player participating in an event.");

	int CurrentTick = pServer->Tick();
	int CooldownTick = pPlayer->m_LastDeathnote + (pServer->TickSpeed() * g_Config.m_SvDeathNoteCoolDown);
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
		return pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	}

	// block if the target is currently inside a passive or no-collision zone
	CCharacter *pTChar = pTarget->GetCharacter();
	if(pTChar)
	{
		if(pPassiveZone && pPassiveZone->IsInZone(pTChar->m_Pos))
			return pSelf->SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a player inside a passive zone.");
		if(pNoCollZone && pNoCollZone->IsInZone(pTChar->m_Pos))
			return pSelf->SendChatTarget(pResult->m_ClientId, "You can't use a deathnote on a player inside a no-collision zone.");
	}

	// consume a page and apply kill
	pPlayer->SetPlayerPages(pPlayer->GetPlayerPages() - 1);
	pTarget->KillCharacter();

	char aBuff_From[128], aBuff_To[128];
	str_format(aBuff_From, sizeof(aBuff_From), "Successfully killed %s. %d pages remaining.", pSelf->Server()->ClientName(pTarget->GetCid()), pPlayer->GetPlayerPages());
	str_format(aBuff_To, sizeof(aBuff_To), "'%s' used a deathnote to kill you!", pSelf->Server()->ClientName(pResult->m_ClientId));
	pSelf->SendChatTarget(pResult->m_ClientId, aBuff_From);
	pSelf->SendChatTarget(pTarget->GetCid(), aBuff_To);
	pPlayer->m_LastDeathnote = pServer->Tick();
}

void CGameContext::ConExp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	char aBuf[256];

	if(!pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in.");

	static const int s_MaxNum = 17;
	float a = (float)pPlayer->GetPlayerExperience() / NeededAccountExp(pPlayer->GetPlayerLevel());
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

	pSelf->SendChatTarget(pResult->m_ClientId, "Experience Bar:");
	pSelf->SendChatTarget(pResult->m_ClientId, aBarTop);
	pSelf->SendChatTarget(pResult->m_ClientId, aBarBot);
	str_format(aBuf, sizeof(aBuf), "Account level: %i", pPlayer->GetPlayerLevel());
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Account Exp: %i", pPlayer->GetPlayerExperience());
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Needed Exp: %i", NeededAccountExp(pPlayer->GetPlayerLevel()));
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
}
void CGameContext::ConClanExp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	char aBuf[256];

	if(!pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in.");
	if(!pPlayer->GetClanId())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not in a clan");

	// obtain a snapshot copy of the clan data (thread-safe)
	CClansData clanTmp;
	if(!pSelf->Clans()->GetClanSnapshotById(pPlayer->GetClanId(), clanTmp))
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Error: Something weird happened, try to login again.");
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

	pSelf->SendChatTarget(pResult->m_ClientId, "Clan Experience Bar:");
	pSelf->SendChatTarget(pResult->m_ClientId, aBarTop);
	pSelf->SendChatTarget(pResult->m_ClientId, aBarBot);
	str_format(aBuf, sizeof(aBuf), "Clan level: %i", clanTmp.m_Level);
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Clan Exp: %i", clanTmp.m_Experience);
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Needed Exp: %i", NeededClanExp(clanTmp.m_Level));
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
}

void CGameContext::ConClanList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You must be logged in.");
	if(pPlayer->GetClanId() <= 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not in a clan.");

	pSelf->Clans()->ShowClanMembers(pResult->m_ClientId, pPlayer->GetClanId());
}

void CGameContext::ConClanHelp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->SendChatTarget(pResult->m_ClientId, "Clan system commands:");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_create <name> — Create a new clan");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_delete — Delete your clan (leader only)");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_leave — Leave your clan");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_invite <player> — Invite a player");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_accept | /clan_decline — Respond to invite");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_kick <player> — Kick a member (leader/co-leader)");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_setlevel <player> <1|2> — Set rank (leader only)");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_rename <newname> — Rename clan (leader only)");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_exp — Show clan EXP progress");
	pSelf->SendChatTarget(pResult->m_ClientId, "/clan_list — List clan members");
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Max members per clan: %d", g_Config.m_SvClanMaxMembers);
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Rename price: %d BP", g_Config.m_SvClanRenamePrice);
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Create price: %d BP", g_Config.m_SvClanCreatePrice);
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
}

void CGameContext::ConAccountHelp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->SendChatTarget(pResult->m_ClientId, "Account system commands:");
	pSelf->SendChatTarget(pResult->m_ClientId, "/register <name> <pass> — Create an account");
	pSelf->SendChatTarget(pResult->m_ClientId, "/login <name> <pass> — Log in");
	pSelf->SendChatTarget(pResult->m_ClientId, "/logout_account — Log out");
	pSelf->SendChatTarget(pResult->m_ClientId, "/password <old> <new> — Change password");
	pSelf->SendChatTarget(pResult->m_ClientId, "/exp — Show your EXP");
	pSelf->SendChatTarget(pResult->m_ClientId, "/profile [name] — View a profile");
	pSelf->SendChatTarget(pResult->m_ClientId, "/bp — Show your blockpoints");
	pSelf->SendChatTarget(pResult->m_ClientId, "/give_bp <player> <amount> — Offer BP transfer");
	pSelf->SendChatTarget(pResult->m_ClientId, "/accept_bp [player] | /decline_bp [player] — Respond to BP transfer");
}

void CGameContext::ConBuy(IConsole::IResult *pResult, void *pUserData)
{
	// // test command - replace that with tiles

	// CGameContext *pSelf = (CGameContext *)pUserData;
	// CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	// CCharacter *pChr = pPlayer->GetCharacter();

	// if(!pPlayer)
	// {
	// 	pSelf->SendChatTarget(pResult->m_ClientId, "Player not found.");
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
	// 	pSelf->SendChatTarget(pResult->m_ClientId, "Unknown cosmetics type. Use 'ko' (knockout), 'gd' (gundesign), or 'sm' (skinmani).");
	// 	return;
	// }

	// if(!Found)
	// {
	// 	pSelf->SendChatTarget(pResult->m_ClientId, "Unknown cosmetics name.");
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
	// 	// pSelf->SendChatTarget(pResult->m_ClientId, "Purchase initiated. Confirm with /yes or cancel with /no.");
	// }
	// else
	// {
	// 	pSelf->SendChatTarget(pResult->m_ClientId, "pendingpurchase isn't null");
	// }
}

void CGameContext::ConShopPurchase(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pPlayer && pPlayer->GetCharacter() && pPlayer->GetCharacter()->m_PendingPurchase)
	{
		pPlayer->GetCharacter()->m_PendingPurchase->Purchase();
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "No item available for purchase.");
	}
}

void CGameContext::ConShopDecline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pPlayer && pPlayer->GetCharacter() && pPlayer->GetCharacter()->m_PendingPurchase)
	{
		pPlayer->GetCharacter()->m_PendingPurchase->Decline();
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "No item available to decline.");
	}
}

void CGameContext::ConClanInvite(IConsole::IResult *pResult, void *pUserData)
{
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer || !pPlayer->IsLoggedIn() || !pPlayer->GetCharacter())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in.");
		return;
	}

	if(pPlayer->GetClanId() == 0)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You are not in a clan to invite others.");
		return;
	}

	if(pPlayer->GetAuthLevel() < ClanAuthLevel::COLEADER)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You need to be the leader or co-leader to invite others.");
		return;
	}

	const int CooldownSeconds = g_Config.m_SvClanInviteCooldown; // configured cooldown
	if(pPlayer->m_LastClanInviteTick != 0 && pSelf->Server()->Tick() - pPlayer->m_LastClanInviteTick < CooldownSeconds * pSelf->Server()->TickSpeed())
	{
		int Rem = (int)((CooldownSeconds * pSelf->Server()->TickSpeed() - (pSelf->Server()->Tick() - pPlayer->m_LastClanInviteTick)) / pSelf->Server()->TickSpeed());
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Please wait %d second%s before sending another clan invite.", Rem, Rem != 1 ? "s" : "");
		pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
		return;
	}

	const char *pName = pResult->GetString(0);
	if(!pName || pName[0] == '\0')
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Usage: /clan_invite <playername>");
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
		pSelf->SendChatTarget(pResult->m_ClientId, "Player not found or not logged in.");
		return;
	}

	CPlayer *pTargetPlayer = pSelf->m_apPlayers[Target];

	if(!pTargetPlayer || !pTargetPlayer->GetCharacter())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Target player is not available.");
		return;
	}
	if(!pTargetPlayer->IsLoggedIn())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "This player is not logged in.");
		return;
	}

	if(Target == pPlayer->GetCid())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You cannot invite yourself.");
		return;
	}

	if(pTargetPlayer->GetClanId() != 0)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "This player is already in a clan.");
		return;
	}

	if(pTargetPlayer->GetPlayerLevel() < 10)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "This player must be at least level 10 to join a clan.");
		return;
	}

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		auto incoming = requests->GetRequestIdsTo(Target, CRequests::SRequest::EType::Clan);
		if(!incoming.empty())
		{
			pSelf->SendChatTarget(pResult->m_ClientId, "Player already has a pending clan invitation.");
			return;
		}

		int id = requests->CreateClanInvite(pPlayer->GetCid(), Target, pPlayer->GetClanId(), g_Config.m_SvClanInviteExpiry);
		if(id < 0)
		{
			pSelf->SendChatTarget(pResult->m_ClientId, "Failed to send clan invitation.");
			return;
		}
		pPlayer->m_LastClanInviteTick = pSelf->Server()->Tick();
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Clan invitation sent to %s.", pName);
		pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
		return;
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
		return;
	}
}

void CGameContext::ConClanAccept(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->IsLoggedIn())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You must be logged in to accept a clan invite.");
		return;
	}
	if(pPlayer->GetClanId() != 0)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You are already in a clan.");
		return;
	}

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		auto ids = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::Clan);
		if(ids.empty())
		{
			pSelf->SendChatTarget(pResult->m_ClientId, "Nobody invited you!");
		}
		else
		{
			// accept the most recent invite (last id)
			int id = ids.back();
			if(!requests->AcceptRequest(id))
				pSelf->SendChatTarget(pResult->m_ClientId, "Failed to accept invite.");
			else
				pSelf->SendChatTarget(pResult->m_ClientId, "Clan invite accepted.");
		}
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
	}
}

void CGameContext::ConClanDecline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->IsLoggedIn())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You must be logged in to decline a clan invite.");
		return;
	}

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		auto ids = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::Clan);
		if(ids.empty())
		{
			pSelf->SendChatTarget(pResult->m_ClientId, "Nobody invited you!");
		}
		else
		{
			int id = ids.back();
			if(!requests->DeclineRequest(id))
				pSelf->SendChatTarget(pResult->m_ClientId, "Failed to decline invite.");
			else
				pSelf->SendChatTarget(pResult->m_ClientId, "Clan invite declined.");
		}
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
	}
}

void CGameContext::ConClanCreate(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->m_Account.m_Id)
		return pSelf->SendChatTarget(ClientId, "You must be logged in to create a clan!");
	if(pPlayer->m_Account.m_Level < g_Config.m_SvClanMinLevel)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "You must be at least level %d to create a clan!", g_Config.m_SvClanMinLevel);
		return pSelf->SendChatTarget(ClientId, aBuf);
	}
	if(pPlayer->m_Account.m_ClanId > 0)
		return pSelf->SendChatTarget(ClientId, "You are already in a clan!");

	if(g_Config.m_SvClanCreatePrice > 0)
	{
		int cost = g_Config.m_SvClanCreatePrice;
		if(pPlayer->GetPlayerBlockpoints() < cost)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "You need %d blockpoints to create a clan.", cost);
			return pSelf->SendChatTarget(ClientId, aBuf);
		}
	}

	const char *pClanName = pResult->GetString(0);
	int ClanNameLength = str_length(pClanName);

	if(ClanNameLength < 3)
		return pSelf->SendChatTarget(ClientId, "Clan name must be at least 3 characters long!");
	if(ClanNameLength > 11)
		return pSelf->SendChatTarget(ClientId, "Clan name is too long (max 11 characters)!");

	if(pSelf->Clans()->GetClanIdByName(pClanName) != -1)
		return pSelf->SendChatTarget(ClientId, "This clan name is already taken!");

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		requests->CreateClanCreateConfirm(ClientId, pClanName, g_Config.m_SvClanConfirmExpiry);
		return; // request system will notify
	}

	return pSelf->SendChatTarget(ClientId, "Clan creation failed: request system unavailable.");
}

void CGameContext::ConClanDelete(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->m_Account.m_Id)
		return pSelf->SendChatTarget(ClientId, "You must be logged in to delete a clan.");

	dbg_msg("%d", "%d", pPlayer->m_Account.m_ClanId, pPlayer->m_Account.m_AuthLevel);

	if(pPlayer->m_Account.m_ClanId < 1 || pPlayer->m_Account.m_AuthLevel != ClanAuthLevel::LEADER)
		return pSelf->SendChatTarget(ClientId, "You are either not in a clan or not its leader.");

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		requests->CreateClanDeleteConfirm(ClientId, pPlayer->GetClanId(), g_Config.m_SvClanConfirmExpiry);
		return; // message sent by requests
	}
}

void CGameContext::ConClanRemove(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->m_Account.m_Id)
		return pSelf->SendChatTarget(ClientId, "You must be logged in to remove a player from the clan.");

	if(pPlayer->m_Account.m_ClanId < 1 || pPlayer->m_Account.m_AuthLevel < ClanAuthLevel::COLEADER)
		return pSelf->SendChatTarget(ClientId, "You are not authorized to remove members from this clan.");

	const char *pTargetName = pResult->GetString(0);
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

	if(!FoundTarget)
	{
		return pSelf->SendChatTarget(ClientId, "Player not found");
	}

	CPlayer *pTargetPlayer = pSelf->m_apPlayers[TargetClientId];

	if(!pTargetPlayer->m_Account.m_Id)
	{
		return pSelf->SendChatTarget(ClientId, "The target player is not logged in.");
	}

	if(pPlayer->m_Account.m_ClanId != pTargetPlayer->m_Account.m_ClanId)
	{
		return pSelf->SendChatTarget(ClientId, "The target player is not in your clan.");
	}

	if(ClientId == TargetClientId)
	{
		return pSelf->SendChatTarget(ClientId, "You cannot remove yourself from the clan.");
	}

	if(pTargetPlayer->m_Account.m_AuthLevel == ClanAuthLevel::LEADER)
	{
		return pSelf->SendChatTarget(ClientId, "You cannot remove a leader or co-leader from the clan.");
	}

	// confirmation
	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		requests->CreateClanKickConfirm(ClientId, pPlayer->m_Account.m_ClanId, pTargetPlayer->m_Account.m_aName, g_Config.m_SvClanConfirmExpiry);
		return; // message sent by requests
	}
}

// /clan_yes: confirm last self-addressed clan confirmation (delete/kick)
void CGameContext::ConClanYes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
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
		if(info.m_Type == CRequests::SRequest::EType::ClanDeleteConfirm || info.m_Type == CRequests::SRequest::EType::ClanKickConfirm || info.m_Type == CRequests::SRequest::EType::ClanRenameConfirm || info.m_Type == CRequests::SRequest::EType::ClanCreateConfirm)
		{
			if(info.m_ExpireTick > pSelf->Server()->Tick())
				chosen = std::max(chosen, id);
		}
	}
	if(chosen == -1)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "No pending clan confirmation to accept.");
		return;
	}
	if(!requests->AcceptRequest(chosen))
		pSelf->SendChatTarget(pResult->m_ClientId, "Failed to accept confirmation.");
}

// /clan_no: decline last self-addressed clan confirmation
void CGameContext::ConClanNo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	auto requests = g_ComponentRegistry.Get<CRequests>();
	if(!requests)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Requests subsystem unavailable.");
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
		if(info.m_Type == CRequests::SRequest::EType::ClanDeleteConfirm || info.m_Type == CRequests::SRequest::EType::ClanKickConfirm || info.m_Type == CRequests::SRequest::EType::ClanRenameConfirm || info.m_Type == CRequests::SRequest::EType::ClanCreateConfirm)
		{
			if(info.m_ExpireTick > pSelf->Server()->Tick())
				chosen = std::max(chosen, id);
		}
	}
	if(chosen == -1)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "No pending clan confirmation to decline.");
		return;
	}
	if(!requests->DeclineRequest(chosen))
		pSelf->SendChatTarget(pResult->m_ClientId, "Failed to decline confirmation.");
}

void CGameContext::ConClanLeave(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->m_Account.m_Id)
		return pSelf->SendChatTarget(ClientId, "You must be logged in to leave a clan.");

	if(pPlayer->m_Account.m_ClanId < 1)
		return pSelf->SendChatTarget(ClientId, "You are not in a clan.");

	if(pPlayer->m_Account.m_AuthLevel == ClanAuthLevel::LEADER)
		return pSelf->SendChatTarget(ClientId, "The clan leader cannot leave. You must delete the clan or transfer leadership.");

	pSelf->Clans()->ClanLeave(ClientId);
}

void CGameContext::ConClanSetAuth(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int IssuerId = pResult->m_ClientId;
	CPlayer *pIssuer = pSelf->m_apPlayers[IssuerId];
	int NewAuthLevel = pResult->GetInteger(1);

	if(!pIssuer)
		return;

	if(!pIssuer->m_Account.m_Id)
		return pSelf->SendChatTarget(IssuerId, "You must be logged in to change a player's clan rank.");

	if(pIssuer->m_Account.m_ClanId < 1 || pIssuer->m_Account.m_AuthLevel < ClanAuthLevel::LEADER)
		return pSelf->SendChatTarget(IssuerId, "Only the clan leader can set ranks.");

	if(NewAuthLevel == 3)
		return pSelf->SendChatTarget(IssuerId, "Use /clan_transfer to transfer leadership.");

	const char *pTargetName = pResult->GetString(0);
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

	if(!FoundTarget)
		return pSelf->SendChatTarget(IssuerId, "Player not found");

	CPlayer *pTargetPlayer = pSelf->m_apPlayers[TargetClientId];

	if(!pTargetPlayer->m_Account.m_Id)
		return pSelf->SendChatTarget(IssuerId, "The target player is not logged in.");

	if(pIssuer->m_Account.m_ClanId != pTargetPlayer->m_Account.m_ClanId)
		return pSelf->SendChatTarget(IssuerId, "The target player is not in your clan.");

	if(IssuerId == TargetClientId)
		return pSelf->SendChatTarget(IssuerId, "You cannot change your own rank.");

	if(NewAuthLevel < 1 || NewAuthLevel > 2)
		return pSelf->SendChatTarget(IssuerId, "Invalid rank. Allowed values: 1 (Member), 2 (Co-Leader). Use /clan_transfer to transfer leadership!");

	if(pTargetPlayer->m_Account.m_AuthLevel == ClanAuthLevel::LEADER)
		return pSelf->SendChatTarget(IssuerId, "You cannot change the rank of the clan leader.");

	if(pTargetPlayer->m_Account.m_AuthLevel == static_cast<ClanAuthLevel>(NewAuthLevel))
		return pSelf->SendChatTarget(IssuerId, "The player already has that rank.");

	// use IssuerId as the client to create the SQL result and for permission checks
	pSelf->Clans()->SetAuthLevel(IssuerId, pTargetPlayer->m_Account.m_aName, NewAuthLevel, pIssuer->m_Account.m_ClanId);
}

void CGameContext::ConClanRename(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->m_Account.m_Id)
		return pSelf->SendChatTarget(ClientId, "You must be logged in to rename a clan.");

	if(pPlayer->m_Account.m_ClanId < 1 || pPlayer->m_Account.m_AuthLevel != ClanAuthLevel::LEADER)
		return pSelf->SendChatTarget(ClientId, "Only the clan leader can rename the clan.");

	const char *pNewClanName = pResult->GetString(0);
	int ClanNameLength = str_length(pNewClanName);

	if(ClanNameLength < 3)
		return pSelf->SendChatTarget(ClientId, "Clan name must be at least 3 characters long!");
	if(ClanNameLength > 11)
		return pSelf->SendChatTarget(ClientId, "Clan name is too long (max 11 characters)!");

	if(!CheckValidChars(pNewClanName))
		return pSelf->SendChatTarget(ClientId, "Only A-Z and 0-9 are allowed in clan names!");

	if(pSelf->Clans()->GetClanIdByName(pNewClanName) != -1)
		return pSelf->SendChatTarget(ClientId, "This clan name is already taken!");

	if(g_Config.m_SvClanRenamePrice > 0)
	{
		int cost = g_Config.m_SvClanRenamePrice;
		if(pPlayer->GetPlayerBlockpoints() < cost)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "You need %d blockpoints to rename your clan.", cost);
			return pSelf->SendChatTarget(ClientId, aBuf);
		}
	}

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		std::string oldName = pSelf->Clans()->GetClanNameCopy(pPlayer->m_Account.m_ClanId);
		requests->CreateClanRenameConfirm(ClientId, pPlayer->m_Account.m_ClanId, oldName.c_str(), pNewClanName, g_Config.m_SvClanConfirmExpiry);
		return; // request system will notify hopefully ;(
	}

	return pSelf->SendChatTarget(ClientId, "Clan rename failed: request system unavailable.");
}

void CGameContext::ConDisplayTopClans(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pSelf->Clans()->ShowTopClans(ClientId);
}

void CGameContext::ConContributors(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->SendChatTarget(pResult->m_ClientId, "Huge thanks to Blockworlds contributors:");
	pSelf->SendChatTarget(pResult->m_ClientId, "melon, Anime.pdf, zhn, ReiTW, Brokecdx-, Sakido, Gegongt, noby, potato");
}

void CGameContext::Con1on1(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!g_Config.m_Sv1on1system)
		return pSelf->SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");

	if(!pResult->NumArguments())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Challenge another player by writing '/1on1 name (blockpoints)'");
		return pSelf->SendChatTarget(pResult->m_ClientId, "An example would be \"/1on1 nameless tee\" or \"/1on1 marcella 30\"");
	}

	const char *pEnemyName = pResult->GetString(0);
	int Wager = pResult->GetInteger(1);

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CPlayer *pTarget = pSelf->GetPlayerByName(pEnemyName);

	std::vector<vec2> result;
	GetTilePositions(TILE_BW_1ON1_START_POS, pSelf, result);

	// some errors handling
	if(!pTarget)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Player not found");
	if(pTarget->GetCid() == pResult->m_ClientId)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can't start a 1vs1 against yourself.");
	if(Wager < 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "The amount set for the Wager must be more than 0 or none");

	if(!pPlayer->IsLoggedIn() && Wager > 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You have to be logged in to place a wager in the pot.");
	if(Wager > 0 && (!pTarget->IsLoggedIn()))
		return pSelf->SendChatTarget(pResult->m_ClientId, "Target player must be logged in to play with a wager.");
	if(Wager > pPlayer->GetPlayerBlockpoints())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can't afford to wager that much!");

	if(Wager > pTarget->GetPlayerBlockpoints())
		return pSelf->SendChatTarget(pResult->m_ClientId, "Player doesn't have enough blockpoints.");

	if(pSelf->isInEvent(pResult->m_ClientId))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You must finish your 1on1 first (or use '/leave' to leave).");

	if(pSelf->isInEvent(pTarget->GetCid()))
		return pSelf->SendChatTarget(pResult->m_ClientId, "This player is already in an event.");

	char aBuf[256];
	if(result.empty())
		return pSelf->SendChatTarget(pResult->m_ClientId, "Error: This map does not have any spawn tiles for 1v1. (194 / Blue Spawn inside of Game Layer)");

	// create invite via requests component
	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		{
			int id = requests->Create1on1Invite(pPlayer->GetCid(), pTarget->GetCid(), Wager, g_Config.m_Sv1on1InviteExpiry);
			if(id == -1)
				return; // Create1on1Invite already informed the sender about the duplicate
			pPlayer->sent1on1InviteTo = pTarget->GetCid();
			str_format(aBuf, sizeof(aBuf), "Match request has been sent to '%s' (%d BP).", pSelf->Server()->ClientName(pTarget->GetCid()), Wager);
			pSelf->SendChatTarget(pPlayer->GetCid(), aBuf);
		}
		return;
	}
	pSelf->SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
}

void CGameContext::Con1on1Accept(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!g_Config.m_Sv1on1system)
		return pSelf->SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		if(pResult->NumArguments() > 0 && pResult->GetString(0)[0])
		{
			const char *arg = pResult->GetString(0);
			CPlayer *pFrom = pSelf->GetPlayerByName(arg);
			if(!pFrom)
			{
				pSelf->SendChatTarget(pResult->m_ClientId, "Player not found");
				return;
			}

			auto ids = requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
			if(ids.empty())
			{
				pSelf->SendChatTarget(pResult->m_ClientId, "No invitation from that player was found.");
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
				pSelf->SendChatTarget(pResult->m_ClientId, "No active (non-expired) invitation from that player was found.");
				return;
			}

			if(!requests->AcceptRequest(chosen))
				pSelf->SendChatTarget(pResult->m_ClientId, "Failed to accept the invitation (it may have expired).");
			return;
		}

		auto pending = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
		if(pending.size() == 1)
		{
			int id = pending[0];
			CRequests::SRequest info;
			if(!requests->GetRequestInfo(id, info) || info.m_ExpireTick <= pSelf->Server()->Tick())
			{
				pSelf->SendChatTarget(pResult->m_ClientId, "No active invitation to accept was found (it may have expired).");
				return;
			}
			if(!requests->AcceptRequest(id))
				pSelf->SendChatTarget(pResult->m_ClientId, "Failed to accept the invitation (it may have expired).");
			return;
		}

		pSelf->SendChatTarget(pResult->m_ClientId, "No invitation to accept was found (try to use /accept <playerName>).");
		return;
	}
	pSelf->SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
}

void CGameContext::Con1on1Decline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!g_Config.m_Sv1on1system)
		return pSelf->SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");

	if(auto requests = g_ComponentRegistry.Get<CRequests>())
	{
		if(pResult->NumArguments() > 0 && pResult->GetString(0)[0])
		{
			const char *arg = pResult->GetString(0);
			CPlayer *pFrom = pSelf->GetPlayerByName(arg);
			if(!pFrom)
			{
				pSelf->SendChatTarget(pResult->m_ClientId, "Player not found");
				return;
			}

			auto ids = requests->GetRequestIdsFromTo(pFrom->GetCid(), pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
			if(ids.empty())
			{
				pSelf->SendChatTarget(pResult->m_ClientId, "No invitation from that player was found.");
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
				pSelf->SendChatTarget(pResult->m_ClientId, "No active (non-expired) invitation from that player was found.");
				return;
			}

			if(!requests->DeclineRequest(chosen))
				pSelf->SendChatTarget(pResult->m_ClientId, "Failed to decline the invitation (it may have expired).");
			return;
		}

		auto pending = requests->GetRequestIdsTo(pResult->m_ClientId, CRequests::SRequest::EType::OneOnOne);
		if(pending.size() == 1)
		{
			int id = pending[0];
			CRequests::SRequest info;
			if(!requests->GetRequestInfo(id, info) || info.m_ExpireTick <= pSelf->Server()->Tick())
			{
				pSelf->SendChatTarget(pResult->m_ClientId, "No active invitation to decline was found (it may have expired).");
				return;
			}
			if(!requests->DeclineRequest(id))
				pSelf->SendChatTarget(pResult->m_ClientId, "Failed to decline the invitation (it may have expired).");
			return;
		}

		pSelf->SendChatTarget(pResult->m_ClientId, "No invitation to decline was found (or use /decline <playerName>). Try checking your messages.");
		return;
	}
	pSelf->SendChatTarget(pResult->m_ClientId, "Request subsystem is not available.");
}

void CGameContext::ConJoinEvent(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You must be logged in with an account to participate in a tournament.");

	if(auto events = g_ComponentRegistry.Get<CEvents>())
	{
		auto subs = events->GetSubComponents();
		for(auto &sub : subs)
		{
			CEventComponent *pEv = dynamic_cast<CEventComponent *>(sub.operator->());
			if(pEv && pEv->GetState() == CEventComponent::EEventState::Registration)
			{
				if(pSelf->isInEvent(pResult->m_ClientId))
					return pSelf->SendChatTarget(pResult->m_ClientId, "You are either currently participating in an event or have already registered for an upcoming one. To exit, use the command /leave.");
				pEv->Register(pResult->m_ClientId);
				return;
			}
		}
		pSelf->SendChatTarget(pResult->m_ClientId, "No active event at this time");
		return;
	}
}

void CGameContext::ConCreateTDM(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(pSelf->isInEvent(pResult->m_ClientId))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You must finish your current event first (Or use '/leave' to leave).");

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
	pSelf->SendChatTarget(pResult->m_ClientId, "Failed to create TDM event: events subsystem unavailable.");
}

void CGameContext::ConLeaveEvent(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer)
		return;

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

	pSelf->SendChatTarget(pResult->m_ClientId, "You are not in any event!");
}

// Components

void CGameContext::ConComponentList(IConsole::IResult *pResult, void *pUserData)
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

void CGameContext::ConComponentPlug(IConsole::IResult *pResult, void *pUserData)
{
	char aName[64];
	str_copy(aName, pResult->GetString(0));
	str_clean_whitespaces(aName);

	auto pComponent = g_ComponentRegistry.Create(aName, (CGameContext *)pUserData);
	if(!pComponent)
	{
		dbg_msg("Components", "Component creation failed");
		return;
	}
	dbg_msg("Components", "Component created: %s (%p)", pComponent->GetName(), &*pComponent);
}

void CGameContext::ConComponentUnPlug(IConsole::IResult *pResult, void *pUserData)
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
