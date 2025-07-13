#include "gamecontext.h"

#include <engine/antibot.h>
#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamemodes/DDRace.h>
#include <game/server/player.h>
#include <game/server/save.h>
#include <game/server/teams.h>

#include <game/server/blockworlds/accounts.h>
#include <game/server/blockworlds/clans.h>
#include <game/server/blockworlds/events/base/eventhandler.h>
#include <game/server/blockworlds/requests/clan_requests/requests.h>

#include <game/server/blockworlds/components/core/component_factory.h>

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

	const char *pUsername = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);

	int NameLength = str_length(pUsername);
	int PasswordLength = str_length(pPassword);

	if(NameLength <= 2)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Your name must be at least 3 characters long!");

	if(PasswordLength < 5)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Your password must be at least 5 characters long!");

	if(str_comp(pUsername, pPassword) == 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Password must be different from username!");

	if(NameLength * sizeof(char) >= 11)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account name too long!");

	if(!CheckValidChars(pUsername) || !CheckValidChars(pPassword))
		return pSelf->SendChatTarget(pResult->m_ClientId, "Only the characters A-Z and 0-9 are allowed!");

	pSelf->Accounts()->Register(pResult->m_ClientId, pUsername, pPassword);
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

	str_format(aBuf, sizeof(aBuf), "*** Max Kill Streak: %d", pTargetPlayer->m_Account.m_Killstreak);
	pSelf->SendChatTarget(ClientId, aBuf);

	// global K/D ratio
	float KD = pTargetPlayer->m_Account.m_Deaths > 0 ? (float)pTargetPlayer->m_Account.m_Kills / pTargetPlayer->m_Account.m_Deaths : (float)pTargetPlayer->m_Account.m_Kills;
	str_format(aBuf, sizeof(aBuf), "*** K/D: %.2f", KD);
	pSelf->SendChatTarget(ClientId, aBuf);

	// LMB Wins
	str_format(aBuf, sizeof(aBuf), "*** LMB Wins: %d", pTargetPlayer->m_Account.m_TourneyWin);
	pSelf->SendChatTarget(ClientId, aBuf);

	// playtime in hours and minutes
	int Hours = pTargetPlayer->m_Account.m_Playtime / 3600;
	int Minutes = (pTargetPlayer->m_Account.m_Playtime % 3600) / 60;
	str_format(aBuf, sizeof(aBuf), "*** PlayTime: %d hours %d minutes", Hours, Minutes);
	pSelf->SendChatTarget(ClientId, aBuf);

	pSelf->SendChatTarget(ClientId, "*** ------Ranked------");

	// ranked stats: Games, Kills, Deaths, Wins
	str_format(aBuf, sizeof(aBuf), "*** Games: %d", pTargetPlayer->m_Account.m_RankedGames);
	pSelf->SendChatTarget(ClientId, aBuf);

	str_format(aBuf, sizeof(aBuf), "*** Rating: %d", pTargetPlayer->m_Account.m_Ranking);
	pSelf->SendChatTarget(ClientId, aBuf);

	str_format(aBuf, sizeof(aBuf), "*** Kills: %d", pTargetPlayer->m_Account.m_RankedKills);
	pSelf->SendChatTarget(ClientId, aBuf);

	str_format(aBuf, sizeof(aBuf), "*** Deaths: %d", pTargetPlayer->m_Account.m_RankedDeaths);
	pSelf->SendChatTarget(ClientId, aBuf);

	// ranked K/D ratio
	float RankedKD = pTargetPlayer->m_Account.m_RankedDeaths > 0 ? (float)pTargetPlayer->m_Account.m_RankedKills / pTargetPlayer->m_Account.m_RankedDeaths : (float)pTargetPlayer->m_Account.m_RankedKills;
	str_format(aBuf, sizeof(aBuf), "*** K/D: %.2f", RankedKD);
	pSelf->SendChatTarget(ClientId, aBuf);

	// ranked Wins and Win Rate
	str_format(aBuf, sizeof(aBuf), "*** Wins: %d", pTargetPlayer->m_Account.m_RankedWins);
	pSelf->SendChatTarget(ClientId, aBuf);

	float WinRate = pTargetPlayer->m_Account.m_RankedGames > 0 ? (float)pTargetPlayer->m_Account.m_RankedWins / pTargetPlayer->m_Account.m_RankedGames * 100.0f : 0.0f;
	str_format(aBuf, sizeof(aBuf), "*** Win Rate: %.2f%%", WinRate);
	pSelf->SendChatTarget(ClientId, aBuf);
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
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CCharacter *pChr = pPlayer->GetCharacter();

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are not logged in.");

	if(pPlayer->GetPlayerWeaponkits() < 1)
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

	pPlayer->SetPlayerWeaponkits(pPlayer->GetPlayerWeaponkits() - 1);
	pSelf->ModifyWeapons(pResult, pUserData, -1, false);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "You have successfuly used one of your weapon kits! you now have %d kits left.", pPlayer->GetPlayerWeaponkits());
	return pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
}

void CGameContext::ConDeathnote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
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

	pPlayer->SetPlayerPages(pPlayer->GetPlayerPages() - 1);
	pTarget->KillCharacter();

	char aBuff_From[128], aBuff_To[128];
	str_format(aBuff_From, sizeof(aBuff_From), "Successfully killed %s. %d pages remaining.", pSelf->Server()->ClientName(pTarget->GetCid()), pPlayer->GetPlayerPages());
	str_format(aBuff_To, sizeof(aBuff_To), "'%s' used a deathnote to kill you!", pSelf->Server()->ClientName(pResult->m_ClientId));
	pSelf->SendChatTarget(pResult->m_ClientId, aBuff_From);
	pSelf->SendChatTarget(pTarget->GetCid(), aBuff_To);
	pPlayer->m_LastDeathnote = pServer->Tick();
}

void CGameContext::ConCosmetics(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	bool Found = false;

	if(!str_comp_nocase(pResult->GetString(0), "gd"))
	{
		Found = pSelf->Cosmetics()->ToggleGundesign(pResult->m_ClientId, pResult->GetString(1));
	}
	else if(!str_comp_nocase(pResult->GetString(0), "ko"))
	{
		Found = pSelf->Cosmetics()->ToggleKnockout(pResult->m_ClientId, pResult->GetString(1));
	}
	else if(!str_comp_nocase(pResult->GetString(0), "sm"))
	{
		Found = pSelf->Cosmetics()->ToggleSkinmani(pResult->m_ClientId, pResult->GetString(1));
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Unknown cosmetics type [ko (knockout), gd (gundesign), sm (skinmani)]");
		return;
	}

	if(!Found)
		pSelf->SendChatTarget(pResult->m_ClientId, "Unknown cosmetics name");
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

	// make sure the clan data pointer is valid.
	if(!pPlayer->m_Account.m_pClanData)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Error: Something weird happened, try to login again.");
		return;
	}

	static const int s_MaxNum = 17;
	float Ratio = (float)pPlayer->GetClanExperience() / NeededClanExp(pPlayer->GetClanLevel());
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
	str_format(aBuf, sizeof(aBuf), "Clan level: %i", pPlayer->GetClanLevel());
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Clan Exp: %i", pPlayer->GetClanExperience());
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "Needed Exp: %i", NeededClanExp(pPlayer->GetClanLevel()));
	pSelf->SendChatTarget(pResult->m_ClientId, aBuf);
}

void CGameContext::ConBuy(IConsole::IResult *pResult, void *pUserData)
{
	// test command - replace that with tiles

	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CCharacter *pChr = pPlayer->GetCharacter();

	if(!pPlayer)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Player not found.");
		return;
	}

	int Category;
	bool Found = false;
	int CosmeticId = -1;

	std::string Type = pResult->GetString(0);
	std::string Name = pResult->GetString(1);

	if(Type == "gd")
	{
		CosmeticId = pSelf->Cosmetics()->FindGundesign(Name.c_str());
		Category = CShop::CATEGORY_GUNDESIGN;
		Found = (CosmeticId != -1);
	}
	else if(Type == "ko")
	{
		CosmeticId = pSelf->Cosmetics()->FindKnockoutEffect(Name.c_str());
		Category = CShop::CATEGORY_KNOCKOUT;
		Found = (CosmeticId != -1);
	}
	else if(Type == "sm")
	{
		CosmeticId = pSelf->Cosmetics()->FindSkinmani(Name.c_str());
		Category = CShop::CATEGORY_SKINMANI;
		Found = (CosmeticId != -1);
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Unknown cosmetics type. Use 'ko' (knockout), 'gd' (gundesign), or 'sm' (skinmani).");
		return;
	}

	if(!Found)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Unknown cosmetics name.");
		return;
	}

	if(pChr->m_PendingPurchase == nullptr)
	{
		new CShop(pSelf, pPlayer, Category, CosmeticId, 15);
		// pSelf->SendChatTarget(pResult->m_ClientId, "Purchase initiated. Confirm with /yes or cancel with /no.");
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "pendingpurchase isn't null");
	}
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

	if(pPlayer->GetAuthLevel() < 2)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You need to be the leader or co-leader to invite others.");
		return;
	}

	const char *pName = pResult->GetString(0);
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

	if(pTargetPlayer->GetClanId() != 0)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "This player is already in a clan.");
		return;
	}

	if(pTargetPlayer->GetCharacter()->m_PendingClanRequests)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "This player already has a pending clan invitation.");
		return;
	}

	new CClanRequests(pSelf, pTargetPlayer, pPlayer, pPlayer->GetClanId(), 15);
	pSelf->SendChatTarget(pResult->m_ClientId, "Clan invitation sent.");
}

void CGameContext::ConClanAccept(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pPlayer && pPlayer->GetCharacter() && pPlayer->GetCharacter()->m_PendingClanRequests)
	{
		pPlayer->GetCharacter()->m_PendingClanRequests->Accept();
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Nobody invited you!");
	}
}

void CGameContext::ConClanDecline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pPlayer && pPlayer->GetCharacter() && pPlayer->GetCharacter()->m_PendingClanRequests)
	{
		pPlayer->GetCharacter()->m_PendingClanRequests->Decline();
	}
	else
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Nobody invited you!");
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

	if(pPlayer->m_Account.m_ClanId > 0)
		return pSelf->SendChatTarget(ClientId, "You are already in a clan!");

	const char *pClanName = pResult->GetString(0);
	int ClanNameLength = str_length(pClanName);

	if(ClanNameLength < 3)
		return pSelf->SendChatTarget(ClientId, "Clan name must be at least 3 characters long!");
	if(ClanNameLength > 11)
		return pSelf->SendChatTarget(ClientId, "Clan name is too long (max 11 characters)!");

	if(!CheckValidChars(pClanName))
		return pSelf->SendChatTarget(ClientId, "Only A-Z and 0-9 are allowed in clan names!");

	for(const auto &Clan : pSelf->Clans()->GetClansData())
	{
		if(str_comp(Clan.m_ClanName, pClanName) == 0)
			return pSelf->SendChatTarget(ClientId, "This clan name is already taken!");
	}

	pSelf->Clans()->CreateClan(ClientId, pClanName, pPlayer->GetAccId());
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

	if(pPlayer->m_Account.m_ClanId < 1 || pPlayer->m_Account.m_AuthLevel != 3)
		return pSelf->SendChatTarget(ClientId, "You are either not in a clan or not its leader.");

	pSelf->Clans()->DeleteClan(ClientId, pPlayer->GetClanId(), pPlayer->GetAccId());
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

	if(pPlayer->m_Account.m_ClanId < 1 || pPlayer->m_Account.m_AuthLevel < 2)
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

	if(pTargetPlayer->m_Account.m_AuthLevel == 3)
	{
		return pSelf->SendChatTarget(ClientId, "You cannot remove a leader or co-leader from the clan.");
	}

	pSelf->Clans()->RemoveFromClan(TargetClientId, pTargetPlayer->m_Account.m_aName, pPlayer->m_Account.m_ClanId);
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

	if(pPlayer->m_Account.m_AuthLevel == 3)
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

	if(pIssuer->m_Account.m_ClanId < 1 || pIssuer->m_Account.m_AuthLevel < 3)
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

	if(pTargetPlayer->m_Account.m_AuthLevel == 3)
		return pSelf->SendChatTarget(IssuerId, "You cannot change the rank of the clan leader.");

	if(pTargetPlayer->m_Account.m_AuthLevel == NewAuthLevel)
		return pSelf->SendChatTarget(IssuerId, "The player already has that rank.");

	pSelf->Clans()->SetAuthLevel(TargetClientId, pTargetPlayer->m_Account.m_aName, NewAuthLevel, pIssuer->m_Account.m_ClanId);
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

	if(pPlayer->m_Account.m_ClanId < 1 || pPlayer->m_Account.m_AuthLevel != 3)
		return pSelf->SendChatTarget(ClientId, "Only the clan leader can rename the clan.");

	const char *pNewClanName = pResult->GetString(0);
	int ClanNameLength = str_length(pNewClanName);

	if(ClanNameLength < 3)
		return pSelf->SendChatTarget(ClientId, "Clan name must be at least 3 characters long!");
	if(ClanNameLength > 11)
		return pSelf->SendChatTarget(ClientId, "Clan name is too long (max 11 characters)!");

	if(!CheckValidChars(pNewClanName))
		return pSelf->SendChatTarget(ClientId, "Only A-Z and 0-9 are allowed in clan names!");

	for(const auto &Clan : pSelf->Clans()->GetClansData())
	{
		if(str_comp(Clan.m_ClanName, pNewClanName) == 0)
			return pSelf->SendChatTarget(ClientId, "This clan name is already taken!");
	}

	pSelf->Clans()->RenameClan(ClientId, pPlayer->m_Account.m_ClanId, pNewClanName);
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
	GetTilePositions(BW_1ON1_START_POS, pSelf, result);

	// some errors handling
	if(!pTarget)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Player not found");
	if(pTarget->GetCid() == pResult->m_ClientId)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You can't start a 1vs1 against yourself.");
	if(Wager < 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "The amount set for the Wager must be more than 0 or none");

	if(!pPlayer->IsLoggedIn() && Wager > 0)
		return pSelf->SendChatTarget(pResult->m_ClientId, "You have to be logged in to place a wager in the pot.");
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

	CInvite *pInvite = pSelf->getInvite(pPlayer->GetCid(), pTarget->GetCid(), CEvent::EVENT_1on1);
	if(pInvite && pInvite->m_pInviteFrom == pTarget && pPlayer == pInvite->m_pInviteTo)
		return pInvite->Accept();
	else if(pInvite)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Request cannot be send right now. Try again in a few seconds.");

	new CInvite(pSelf, pTarget, pPlayer, CEvent::EVENT_1on1, 30, Wager);
	pPlayer->sent1on1InviteTo = pTarget->GetCid();
	str_format(aBuf, sizeof(aBuf), "%s challenged you for an 1on1! (/accept, /decline) (%d BP)", pSelf->Server()->ClientName(pResult->m_ClientId), Wager);
	pSelf->SendChatTarget(pTarget->GetCid(), aBuf);
	str_format(aBuf, sizeof(aBuf), "Match request has been sent to '%s' (%d BP).", pSelf->Server()->ClientName(pTarget->GetCid()), Wager);
	pSelf->SendChatTarget(pPlayer->GetCid(), aBuf);
}

void CGameContext::Con1on1Accept(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!g_Config.m_Sv1on1system)
		return pSelf->SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");

	// do you really want to copy vector of pointers
	std::vector<CInvite *> pInvites = pSelf->getInvites(pResult->m_ClientId, 1);

	if(pInvites.empty())
		return pSelf->SendChatTarget(pResult->m_ClientId, "Nobody has invited you!");

	CPlayer *pTarget = pSelf->GetPlayerByName(pResult->GetString(0));

	if(!pTarget && pInvites.size() > 1)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Player not found.");

	if(pSelf->isInEvent(pResult->m_ClientId))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You are already in an event.");

	for(int i = pInvites.size() - 1; i >= 0; i--)
	{
		if(pInvites[i]->m_pInviteTo != pSelf->m_apPlayers[pResult->m_ClientId])
			continue;

		return pInvites[i]->Accept();
	}

	for(CEvent *pEvent : pSelf->m_vEvents)
		if(pEvent->pGetGametype() == CEvent::EVENT_INVITE)
			return ((CInvite *)pEvent)->Accept();
	pSelf->SendChatTarget(pResult->m_ClientId, "No invitation to accept was found.");
}

void CGameContext::Con1on1Decline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(!g_Config.m_Sv1on1system)
		return pSelf->SendChatTarget(pResult->m_ClientId, "1on1 matches are currently disabled.");

	if(!pResult->NumArguments())
		return;

	CPlayer *pTarget = pSelf->GetPlayerByName(pResult->GetString(0));

	if(!pTarget)
		return pSelf->SendChatTarget(pResult->m_ClientId, "This player doesn't exist.");

	std::vector<CInvite *> pInvites = pSelf->getInvites(pResult->m_ClientId, pTarget->GetCid());
	for(int i = pInvites.size() - 1; i >= 0; i--)
	{ // loop from behind so the last pInvite gets prioritized
		if(pInvites[i]->m_pInviteTo != pSelf->m_apPlayers[pResult->m_ClientId]) // is the player the player that received an request or the one that sent it?
			continue;
		pInvites[i]->Decline();
		break;
	}
}

void CGameContext::ConJoinEvent(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer->IsLoggedIn())
		return pSelf->SendChatTarget(pResult->m_ClientId, "You must be logged in with an account to participate in a tournament.");

	for(CEvent *pEvent : pSelf->m_vEvents)
	{
		if(pEvent->pGetGametype() != CEvent::EVENT_INVITE)
			continue;
		else if(pSelf->isInEvent(pResult->m_ClientId))
			return pSelf->SendChatTarget(pResult->m_ClientId, "You are either currently participating in an event or have already registered for an upcoming one. To exit, use the command /leave.");
		return ((BW_CEventHandler *)pEvent)->Accept(pPlayer);
	}
}

void CGameContext::ConCreateLMB(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(pSelf->isInEvent(pResult->m_ClientId))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You must finish your current event first (Or use '/leave' to leave).");

	new BW_CEventHandler(pSelf, nullptr, CEvent::EVENT_LMB, &pSelf->m_apPlayers);
}

void CGameContext::ConCreateTDM(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	if(pSelf->isInEvent(pResult->m_ClientId))
		return pSelf->SendChatTarget(pResult->m_ClientId, "You must finish your current event first (Or use '/leave' to leave).");

	new BW_CEventHandler(pSelf, nullptr, CEvent::EVENT_TDM, &pSelf->m_apPlayers);
}

void CGameContext::ConLeaveEvent(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(!g_Config.m_SvAccountsystem)
		return pSelf->SendChatTarget(pResult->m_ClientId, "Account system is currently disabled.");

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer)
		return;

	bool Found = false;
	for(CEvent *pEvent : pSelf->m_vEvents)
		if(pEvent->Leave(pPlayer))
			Found = true;

	if(!Found)
		pSelf->SendChatTarget(pResult->m_ClientId, "You are not in any event!");
}

// Components

void CGameContext::ConComponentList(IConsole::IResult *pResult, void *pUserData)
{
	auto Components = g_ComponentRegistry.All();
	auto ActiveComponents = g_ComponentRegistry.Active();

	for(const auto &item : Components)
	{
		auto Name = g_ComponentRegistry.Name(item.first);
		bool Active = std::find(ActiveComponents.begin(), ActiveComponents.end(), item.second) != ActiveComponents.end();

		dbg_msg("Components", "[%s] %s", Active ? "+" : " ", Name.c_str());
	}
}

void CGameContext::ConComponentPlug(IConsole::IResult *pResult, void *pUserData)
{
	char aName[64];
	str_copy(aName, pResult->GetString(0));
	str_clean_whitespaces(aName);

	auto pComponent = g_ComponentRegistry.Create(aName, (CGameContext*)pUserData);
	if(pComponent == nullptr)
	{
		dbg_msg("Components", "Component creation failed");
		return;
	}
	dbg_msg("Components", "Component created: %s (%p)", pComponent->GetName(), pComponent);
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
	dbg_msg("Components", "Component creation failed");
}
