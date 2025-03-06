#include <cstddef>
#include <engine/server/databases/connection.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "accounts.h"
#include "engine/shared/config.h"

// Credits: Strongly inspired by ChillerDragon's account system.

CAdminCommandResult::CAdminCommandResult()
{
	SetVariant(Variant::DIRECT, NULL);
}

void CAdminCommandResult::SetVariant(Variant v, const CSqlAdminCommandRequest *pRequest)
{
	if(pRequest)
	{
		m_AdminClientId = pRequest->m_AdminClientId;
		m_TargetAccountId = pRequest->m_TargetAccountId;
		m_State = pRequest->m_State;
		m_Type = pRequest->m_Type;
		str_copy(m_aUsername, pRequest->m_aUsername, sizeof(m_aUsername));
		str_copy(m_aPassword, pRequest->m_aPassword, sizeof(m_aPassword));
	}
	else
	{
		m_AdminClientId = -1;
		m_TargetAccountId = -1;
		m_State = -1;
		m_Type = DIRECT;
		m_aUsername[0] = '\0';
		m_aPassword[0] = '\0';
	}
	m_MessageKind = v;
	switch(v)
	{
	case FREEZE_ACC:
	case MODERATOR:
	case SUPER_MODERATOR:
	case SUPPORTER:
	case DIRECT:
	case ALL:
		for(auto &aMessage : m_aaMessages)
			aMessage[0] = 0;
		break;
	case BROADCAST:
		m_aBroadcast[0] = 0;
		break;
	case LOG_ONLY:
		break;
	}
}

CAccountResult::CAccountResult()
{
	SetVariant(Variant::DIRECT);
}

CAccounts::CAccounts(CGameContext *pGameServer, CDbConnectionPool *pPool) :
	m_pPool(pPool),
	m_pGameServer(pGameServer),
	m_pServer(pGameServer->Server())
{
}

std::shared_ptr<CAdminCommandResult> CAccounts::NewSqlAdminCommandResult(int ClientId)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pCurPlayer) return nullptr;
	pCurPlayer->m_AdminCommandQueryResult.push(std::make_shared<CAdminCommandResult>());
	return pCurPlayer->m_AdminCommandQueryResult.back();
}

void CAccounts::ExecAdminThread(
	bool (*pFuncPtr)(IDbConnection *, const ISqlData *, char *pError, int ErrorSize),
	const char *pThreadName,
	int AdminClientId,
	int TargetAccountId,
	int State,
	CAdminCommandResult::Variant Type,
	const char *pUsername,
	const char *pPassword,
	const char *pQuery)
{
	auto pResult = NewSqlAdminCommandResult(AdminClientId);
	if(pResult == nullptr)
		return;
	auto Tmp = std::make_unique<CSqlAdminCommandRequest>(pResult);
	Tmp->m_AdminClientId = AdminClientId;
	Tmp->m_TargetAccountId = TargetAccountId;
	Tmp->m_State = State;
	Tmp->m_Type = Type;
	str_copy(Tmp->m_aUsername, pUsername, sizeof(Tmp->m_aUsername));
	str_copy(Tmp->m_aPassword, pPassword, sizeof(Tmp->m_aPassword));
	str_copy(Tmp->m_aQuery, pQuery, sizeof(Tmp->m_aQuery));

	m_pPool->Execute(pFuncPtr, std::move(Tmp), pThreadName);
}

std::shared_ptr<CAccountResult> CAccounts::NewSqlAccountResult(int ClientId)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pCurPlayer) return nullptr;
	pCurPlayer->m_AccountQueryResult.push(std::make_shared<CAccountResult>());
	return pCurPlayer->m_AccountQueryResult.back();
}

void CAccounts::ExecUserThread(
	bool (*pFuncPtr)(IDbConnection *, const ISqlData *, char *pError, int ErrorSize),
	const char *pThreadName,
	int ClientId,
	const char *pUsername,
	const char *pPassword,
	const char *pNewPassword,
	int AccountId,
	CAccountData *pAccountData)
{
	auto pResult = NewSqlAccountResult(ClientId);
	if(pResult == nullptr)
		return;
	auto Tmp = std::make_unique<CSqlAccountRequest>(pResult, m_pGameServer);
	Tmp->m_ClientId = ClientId;
	str_copy(Tmp->m_aUsername, pUsername, sizeof(Tmp->m_aUsername));
	str_copy(Tmp->m_aPassword, pPassword, sizeof(Tmp->m_aPassword));
	str_copy(Tmp->m_aNewPassword, pNewPassword, sizeof(Tmp->m_aNewPassword));
	if(pAccountData)
	{
		Tmp->m_AccountData = *pAccountData;
	}
	else
	{
		Tmp->m_AccountData = CAccountData();
	}
	Tmp->m_AccountId = AccountId;

	m_pPool->Execute(pFuncPtr, std::move(Tmp), pThreadName);
}

bool CAccounts::RateLimitPlayer(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(pPlayer == 0)
		return true;
	if(pPlayer->m_LastSqlQuery + (int64_t)g_Config.m_SvSqlQueriesDelay * Server()->TickSpeed() >= Server()->Tick())
		return true;
	pPlayer->m_LastSqlQuery = Server()->Tick();
	return false;
}

void CAccounts::Save(int ClientId, CAccountData *pAccountData)
{
	ExecUserThread(SaveThread, "save user", ClientId, "", "", "", 0, pAccountData);
}

bool CAccounts::SaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAccountRequest *pData = dynamic_cast<const CSqlAccountRequest *>(pGameData);
	CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());
	pResult->SetVariant(CAccountResult::LOG_ONLY);

	char aBuf[2048];
	str_copy(aBuf,
		"UPDATE accounts SET "
		"address = ?, vip = ?, pages = ?, level = ?, experience = ?, weaponkits = ?, ranking = ?, "
		"clanID = ?, auth_level = ?, blockpoints = ?, knockouts = ?, gundesign = ?, skinmani = ?, extras = ?, ranked_games = ?, "
		"ranked_kills = ?, ranked_deaths = ?, ranked_wins = ?, kills = ?, deaths = ?, tourney_win = ?, playtime = ?, killstreak = ?, "
		"last_name = ?, last_skin = ?, last_body_color = ?, last_feet_color = ? "
		"WHERE id = ?;",
		sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		dbg_msg("sql", "PrepareStatement failed: %s", pError);
		return true;
	}

	const CAccountData *pAcc = &pData->m_AccountData;
	int Index = 1;

#define BIND_STRING(dest) pSqlServer->BindString(Index++, dest)
#define BIND_INT(dest) pSqlServer->BindInt(Index++, dest)
#define BIND_INT64(dest) pSqlServer->BindInt64(Index++, dest)

	BIND_STRING(pAcc->m_aAddress);
	BIND_INT(pAcc->m_Vip);
	BIND_INT(pAcc->m_Pages);
	BIND_INT(pAcc->m_Level);
	BIND_INT(pAcc->m_Experience);
	BIND_INT(pAcc->m_Weaponkits);
	BIND_INT(pAcc->m_Ranking);
	BIND_INT(pAcc->m_ClanId);
	BIND_INT(pAcc->m_AuthLevel);
	BIND_INT(pAcc->m_Blockpoints);
	BIND_STRING(pAcc->m_aKnockouts);
	BIND_STRING(pAcc->m_aGundesign);
	BIND_STRING(pAcc->m_aSkinmani);
	BIND_STRING(pAcc->m_aExtras);
	BIND_INT(pAcc->m_RankedGames);
	BIND_INT(pAcc->m_RankedKills);
	BIND_INT(pAcc->m_RankedDeaths);
	BIND_INT(pAcc->m_RankedWins);
	BIND_INT(pAcc->m_Kills);
	BIND_INT(pAcc->m_Deaths);
	BIND_INT(pAcc->m_TourneyWin);
	BIND_INT64(pAcc->m_Playtime);
	BIND_INT(pAcc->m_Killstreak);
	BIND_STRING(pAcc->m_aLastName);
	BIND_STRING(pAcc->m_aLastSkin);
	BIND_INT(pAcc->m_LastBodyColor);
	BIND_INT(pAcc->m_LastFeetColor);
	BIND_INT(pAcc->m_Id);

#undef BIND_STRING
#undef BIND_INT
#undef BIND_INT64

	int NumUpdated = 0;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		return true;
	}
	return false;
}

void CAccounts::Login(int ClientId, const char *pUsername, const char *pPassword)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecUserThread(LoginThread, "login user", ClientId, pUsername, pPassword, "", 0, NULL);
}

bool CAccounts::LoginThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAccountRequest *pRequestData = dynamic_cast<const CSqlAccountRequest *>(pGameData);
	CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());

	SHA256_DIGEST HashedPassword = CGameContext::HashPassword(pRequestData->m_aPassword);
	char aHashedPassword[SHA256_DIGEST_LENGTH * 2 + 1];
	for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
		sprintf(&aHashedPassword[i * 2], "%02x", HashedPassword.data[i]);

	{ // wrong creds
		char aBuf[2048];
		str_copy(aBuf,
			"SELECT "
			"id "
			"FROM accounts "
			"WHERE name = ? AND password = ?;",
			sizeof(aBuf));

		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}

		pSqlServer->BindString(1, pRequestData->m_aUsername);
		pSqlServer->BindString(2, aHashedPassword);

		bool End;
		if(pSqlServer->Step(&End, pError, ErrorSize))
		{
			return true;
		}

		if(End)
		{
			pResult->SetVariant(CAccountResult::LOGIN_WRONG_PASS);
			return false;
		}
	}

	int AccountId = pSqlServer->GetInt(1);

	{ // is account busy
		char aBusyBuf[512];
		str_copy(aBusyBuf, "SELECT server_id FROM accounts_busy WHERE account_id = ?;", sizeof(aBusyBuf));

		if(pSqlServer->PrepareStatement(aBusyBuf, pError, ErrorSize))
		{
			dbg_msg("is_account_busy", "SQL preparation failed: %s", pError);
			return true;
		}

		pSqlServer->BindInt(1, AccountId);

		bool End = false;
		if(pSqlServer->Step(&End, pError, ErrorSize))
		{
			dbg_msg("is_account_busy", "Failed to retrieve server_id: %s", pError);
			pResult->m_Success = false;
			return true;
		}
		if(!End)
		{
			char aServerId[32];
			pSqlServer->GetString(1, aServerId, sizeof(pResult->m_aLoginServer));

			pResult->SetVariant(CAccountResult::LOGGED_IN_ALREADY);
			str_copy(pResult->m_aLoginServer, aServerId);
			return false;
		}
	}

	char aBuf[2048];
	str_copy(aBuf,
		"SELECT "
		"id, name, password, address, vip, pages, level, experience, weaponkits, ranking, "
		"clanID, auth_level, blockpoints, knockouts, gundesign, skinmani, extras, registerdate, ranked_games, "
		"ranked_kills, ranked_deaths, ranked_wins, kills, deaths, tourney_win, playtime, killstreak, "
		"last_name, last_skin, last_body_color, last_feet_color "
		"FROM accounts "
		"WHERE id = ?;",
		sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}

	pSqlServer->BindInt(1, AccountId);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}

	pResult->SetVariant(CAccountResult::LOGIN_INFO);

	int Index = 1;

#define SQL_GET_INT(idx, dest) dest = pSqlServer->GetInt(idx)
#define SQL_GET_INT64(idx, dest) dest = pSqlServer->GetInt64(idx)
#define SQL_GET_STRING(idx, dest) pSqlServer->GetString(idx, dest, sizeof(dest))

	SQL_GET_INT(Index++, pResult->m_Account.m_Id);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aName);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aPassword);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aAddress);
	SQL_GET_INT(Index++, pResult->m_Account.m_Vip);
	SQL_GET_INT(Index++, pResult->m_Account.m_Pages);
	SQL_GET_INT(Index++, pResult->m_Account.m_Level);
	SQL_GET_INT(Index++, pResult->m_Account.m_Experience);
	SQL_GET_INT(Index++, pResult->m_Account.m_Weaponkits);
	SQL_GET_INT(Index++, pResult->m_Account.m_Ranking);
	SQL_GET_INT(Index++, pResult->m_Account.m_ClanId);
	SQL_GET_INT(Index++, pResult->m_Account.m_AuthLevel);
	SQL_GET_INT(Index++, pResult->m_Account.m_Blockpoints);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aKnockouts);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aGundesign);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aSkinmani);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aExtras);
	SQL_GET_STRING(Index++, pResult->m_Account.m_RegisterDate);
	SQL_GET_INT(Index++, pResult->m_Account.m_RankedGames);
	SQL_GET_INT(Index++, pResult->m_Account.m_RankedKills);
	SQL_GET_INT(Index++, pResult->m_Account.m_RankedDeaths);
	SQL_GET_INT(Index++, pResult->m_Account.m_RankedWins);
	SQL_GET_INT(Index++, pResult->m_Account.m_Kills);
	SQL_GET_INT(Index++, pResult->m_Account.m_Deaths);
	SQL_GET_INT(Index++, pResult->m_Account.m_TourneyWin);
	SQL_GET_INT64(Index++, pResult->m_Account.m_Playtime);
	SQL_GET_INT(Index++, pResult->m_Account.m_Killstreak);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aLastName);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aLastSkin);
	SQL_GET_INT(Index++, pResult->m_Account.m_LastBodyColor);
	SQL_GET_INT(Index++, pResult->m_Account.m_LastFeetColor);

#undef SQL_GET_INT
#undef SQL_GET_INT64
#undef SQL_GET_STRING

	{ // set busy
		char aBusyBuf[512];
		str_copy(aBusyBuf, "INSERT INTO accounts_busy (server_id, account_id) VALUES (?, ?);", sizeof(aBusyBuf));

		if(pSqlServer->PrepareStatement(aBusyBuf, pError, ErrorSize))
		{
			dbg_msg("set_account_busy", "SQL preparation failed: %s", pError);
			return true;
		}

		pSqlServer->BindString(1, g_Config.m_SvServerId);
		pSqlServer->BindInt(2, AccountId);

		int Inserted;
		if(pSqlServer->ExecuteUpdate(&Inserted, pError, ErrorSize))
		{
			dbg_msg("set_account_busy", "Failed to set busy (%d): %s", AccountId, pError);
			pResult->m_Success = false;
			return true;
		}
		if(Inserted == 0)
		{
			dbg_msg("set_account_busy", "Failed to set busy (%d): %s", AccountId, pError);
		}
	}

	pResult->m_Account.m_pClanData = nullptr;
	if(pResult->m_Account.m_ClanId > 0)
	{
		CGameContext *pGameContext = pRequestData->m_pGameContext;
		const std::vector<CClansData> &vClans = pGameContext->Clans()->GetClansData();
		for(const CClansData &Clan : vClans)
		{
			if(Clan.m_Id == pResult->m_Account.m_ClanId)
			{
				pResult->m_Account.m_pClanData = &Clan;
				break;
			}
		}
	}

	return false;
}

void CAccounts::ExecuteSql(const char *pQuery)
{
	auto Tmp = std::make_unique<CSqlStringData>();
	str_copy(Tmp->m_aString, pQuery, sizeof(Tmp->m_aString));

	m_pPool->ExecuteWrite(ExecuteSqlThread, std::move(Tmp), "execute sql query");
}

bool CAccounts::ExecuteSqlThread(IDbConnection *pSqlServer, const ISqlData *pGameData, Write w, char *pError, int ErrorSize)
{
	if(w != Write::NORMAL && w != Write::NORMAL_FAILED)
	{
		dbg_assert(false, "ExecuteSqlThread failed to write");
		return true;
	}

	const CSqlStringData *pData = dynamic_cast<const CSqlStringData *>(pGameData);

	if(pSqlServer->PrepareStatement(pData->m_aString, pError, ErrorSize))
	{
		return true;
	}

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		dbg_assert(false, "ExecuteSqlThread did not step");
		return true;
	}
	return !End;
}

void CAccounts::Register(int ClientId, const char *pUsername, const char *pPassword)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecUserThread(RegisterThread, "register user", ClientId, pUsername, pPassword, "", 0, NULL);
}

bool CAccounts::RegisterThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAccountRequest *pData = static_cast<const CSqlAccountRequest *>(pGameData);
	CAccountResult *pResult = static_cast<CAccountResult *>(pGameData->m_pResult.get());
	pResult->SetVariant(CAccountResult::REGISTER);

	char aBuf[2048];
	str_copy(aBuf, "SELECT name FROM accounts WHERE name = ?;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Failed to prepare SELECT statement: %s", pError);
		return true;
	}

	pSqlServer->BindString(1, pData->m_aUsername);

	printf("Executing SELECT statement to check if username exists: %s\n", pData->m_aUsername);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Failed to execute SELECT statement: %s", pError);
		return true;
	}

	bool UsernameExists = !End;

	if(UsernameExists)
	{
		str_copy(pResult->m_aaMessages[0], "This username already exists.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	else
	{
		SHA256_DIGEST HashedPassword = CGameContext::HashPassword(pData->m_aPassword);
		char aHashedPassword[SHA256_DIGEST_LENGTH * 2 + 1];
		for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
			sprintf(&aHashedPassword[i * 2], "%02x", HashedPassword.data[i]);

		snprintf(aBuf, sizeof(aBuf), "INSERT INTO accounts (name, password) VALUES (?, ?);");

		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			snprintf(pError, ErrorSize, "Failed to prepare INSERT statement: %s", pError);
			return true;
		}

		pSqlServer->BindString(1, pData->m_aUsername);
		pSqlServer->BindString(2, aHashedPassword);

		int NumInserted;
		if(pSqlServer->ExecuteUpdate(&NumInserted, pError, ErrorSize))
		{
			snprintf(pError, ErrorSize, "Failed to execute INSERT statement: %s", pError);
			return true;
		}

		if(NumInserted == 1)
		{
			str_copy(pResult->m_aaMessages[0], "Account registered successfully!", sizeof(pResult->m_aaMessages[0]));
			str_copy(pResult->m_aaMessages[1], "Please log in with /login <name> <pass>", sizeof(pResult->m_aaMessages[1]));
		}
		else
		{
			snprintf(pError, ErrorSize, "Account registration failed. No rows inserted.");
			return true;
		}
	}

	return false;
}

void CAccounts::Logout(int ClientId, int AccountId)
{
	ExecUserThread(LogoutThread, "logout user", ClientId, "", "", "", AccountId, nullptr);
}

bool CAccounts::LogoutThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAccountRequest *pData = dynamic_cast<const CSqlAccountRequest *>(pGameData);
	char aBuf[512];
	str_copy(aBuf, "DELETE FROM accounts_busy WHERE account_id = ? AND server_id = ?;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		dbg_msg("logout", "SQL preparation failed: %s", pError);
		return true;
	}

	pSqlServer->BindInt(1, pData->m_AccountId);
	pSqlServer->BindString(2, g_Config.m_SvServerId);

	int Deleted;
	if(pSqlServer->ExecuteUpdate(&Deleted, pError, ErrorSize))
	{
		dbg_msg("logout", "Failed to remove account_busy entry: %s", pError);
		return true;
	}
	if(Deleted == 0)
	{
		dbg_msg("logout", "Failed to remove account_busy entry: %s", pError);
	}
	return false;
}

void CAccounts::ClearLogins()
{
	m_pPool->Execute(ClearLoginsThread, nullptr, "clear all logins");
}

bool CAccounts::ClearLoginsThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	char aBuf[512];
	str_copy(aBuf, "DELETE FROM accounts_busy WHERE server_id = ?;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		dbg_msg("clear_logins", "SQL preparation failed: %s", pError);
		return true;
	}

	pSqlServer->BindString(1, g_Config.m_SvServerId);
	pSqlServer->Print();

	int Deleted;
	if(pSqlServer->ExecuteUpdate(&Deleted, pError, ErrorSize))
	{
		dbg_msg("clear_logins", "Failed to clear logins for %s: %s", g_Config.m_SvServerId, pError);
		return true;
	}
	return false;
}

void CAccounts::ChangePassword(int ClientId, const char *pUsername, const char *pOldPassword, const char *pNewPassword)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecUserThread(ChangePasswordThread, "change password", ClientId, pUsername, pOldPassword, pNewPassword, 0, NULL);
}

bool CAccounts::ChangePasswordThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAccountRequest *pData = dynamic_cast<const CSqlAccountRequest *>(pGameData);
	CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());
	pResult->SetVariant(CAccountResult::DIRECT);

	char aBuf[2048];
	str_copy(aBuf, "SELECT password FROM accounts WHERE name = ?;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}

	pSqlServer->BindString(1, pData->m_aUsername);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}

	if(End)
	{
		str_copy(pResult->m_aaMessages[0], "Username not found.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	char aStoredPasswordHash[SHA256_DIGEST_LENGTH * 2 + 1];
	pSqlServer->GetString(1, aStoredPasswordHash, sizeof(aStoredPasswordHash));

	SHA256_DIGEST HashedOldPassword = CGameContext::HashPassword(pData->m_aPassword);
	char aHashedOldPassword[SHA256_DIGEST_LENGTH * 2 + 1];
	for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
		sprintf(&aHashedOldPassword[i * 2], "%02x", HashedOldPassword.data[i]);

	if(str_comp(aHashedOldPassword, aStoredPasswordHash) != 0)
	{
		str_copy(pResult->m_aaMessages[0], "Old password is incorrect.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	SHA256_DIGEST HashedNewPassword = CGameContext::HashPassword(pData->m_aNewPassword);
	char aHashedNewPassword[SHA256_DIGEST_LENGTH * 2 + 1];
	for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
		sprintf(&aHashedNewPassword[i * 2], "%02x", HashedNewPassword.data[i]);

	str_copy(aBuf, "UPDATE accounts SET password = ? WHERE name = ?;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}

	pSqlServer->BindString(1, aHashedNewPassword);
	pSqlServer->BindString(2, pData->m_aUsername);

	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		return true;
	}

	if(NumUpdated == 1)
	{
		str_copy(pResult->m_aaMessages[0], "Successfully changed your password.", sizeof(pResult->m_aaMessages[0]));
	}
	else
	{
		str_copy(pResult->m_aaMessages[0], "Password change failed, please try again or contact an administrator", sizeof(pResult->m_aaMessages[0]));
	}

	return false;
}

void CAccounts::ShowTopLevel(int ClientId)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecUserThread(ShowTopLevelThread, "show top level thread", ClientId, "", "", "", 0, NULL);
}

bool CAccounts::ShowTopLevelThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());

	char aBuf[512];
	str_copy(aBuf, "SELECT last_name, level FROM accounts ORDER BY level DESC LIMIT 10;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
		dbg_msg("top_level", "SQL preparation failed: %s", pError);
		pResult->m_Success = false;
		return true;
	}

	int Line = 0;
	str_copy(pResult->m_aaMessages[Line], "------------ Global Top Level ------------", sizeof(pResult->m_aaMessages[Line]));
	Line++;

	bool End = false;
	while(!pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		*pError = '\0';

		char aLastName[MAX_NAME_LENGTH];
		int Level;

		pSqlServer->GetString(1, aLastName, sizeof(aLastName));

		if(*pError != '\0')
		{
			dbg_msg("top_level", "Failed to retrieve Last Name: %s", pError);
			str_copy(pResult->m_aaMessages[0], "Failed to retrieve account last name.", sizeof(pResult->m_aaMessages[0]));
			pResult->m_Success = false;
			return true;
		}

		Level = pSqlServer->GetInt(2);

		str_format(aBuf, sizeof(aBuf), "%d. %s : %d", Line, aLastName, Level);
		str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));
		dbg_msg("top_level", "Retrieved: %s", aBuf);
		Line++;

		if(Line >= CAccountResult::MAX_MESSAGES)
			break;
	}

	if(Line == 1)
	{
		str_copy(pResult->m_aaMessages[Line], "No players found.", sizeof(pResult->m_aaMessages[Line]));
		pResult->m_Success = false;
	}
	else
	{
		pResult->m_Success = true;
	}

	if(*pError != '\0')
	{
		dbg_msg("top_level", "SQL stepping failed: %s", pError);
		str_copy(pResult->m_aaMessages[Line], "Error retrieving top level players.", sizeof(pResult->m_aaMessages[Line]));
		pResult->m_Success = false;
		return true;
	}

	return false;
}

void CAccounts::ShowTopBlockpoints(int ClientId)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecUserThread(ShowTopBlockpointsThread, "show top blockpoints thread", ClientId, "", "", "", 0, NULL);
}

bool CAccounts::ShowTopBlockpointsThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());
	pResult->SetVariant(CAccountResult::TOP_MESSAGES);

	char aBuf[512];
	str_copy(aBuf, "SELECT last_name, blockpoints FROM accounts ORDER BY blockpoints DESC LIMIT 10;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
		dbg_msg("top_blockpoints", "SQL preparation failed: %s", pError);
		pResult->m_Success = false;
		return true;
	}

	int Line = 0;
	str_copy(pResult->m_aaMessages[Line], "---------- Global Top Blockpoints ----------", sizeof(pResult->m_aaMessages[Line]));
	Line++;

	bool End = false;
	while(!pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		*pError = '\0';

		char aLastName[MAX_NAME_LENGTH];
		int Blockpoints;

		pSqlServer->GetString(1, aLastName, sizeof(aLastName));

		if(*pError != '\0')
		{
			dbg_msg("top_blockpoints", "Failed to retrieve Last Name: %s", pError);
			str_copy(pResult->m_aaMessages[0], "Failed to retrieve account last name.", sizeof(pResult->m_aaMessages[0]));
			pResult->m_Success = false;
			return true;
		}

		Blockpoints = pSqlServer->GetInt(2);

		str_format(aBuf, sizeof(aBuf), "%d. %s - %d", Line, aLastName, Blockpoints);
		str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));
		dbg_msg("top_blockpoints", "Retrieved: %s", aBuf);
		Line++;

		if(Line >= CAccountResult::MAX_MESSAGES)
			break;
	}

	if(Line == 1)
	{
		str_copy(pResult->m_aaMessages[Line], "No players found.", sizeof(pResult->m_aaMessages[Line]));
		pResult->m_Success = false;
	}
	else
	{
		pResult->m_Success = true;
	}

	if(*pError != '\0')
	{
		dbg_msg("top_level", "SQL stepping failed: %s", pError);
		str_copy(pResult->m_aaMessages[Line], "Error retrieving top level players.", sizeof(pResult->m_aaMessages[Line]));
		pResult->m_Success = false;
		return true;
	}

	return false;
}

void CAccounts::ShowTopKillStreak(int ClientId)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecUserThread(ShowTopKillStreaksThread, "show top killstreak thread", ClientId, "", "", "", 0, NULL);
}

bool CAccounts::ShowTopKillStreaksThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());
	pResult->SetVariant(CAccountResult::TOP_MESSAGES);

	char aBuf[512];
	str_copy(aBuf, "SELECT last_name, killstreak FROM accounts ORDER BY level DESC LIMIT 10;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
		dbg_msg("top_killstreak", "SQL preparation failed: %s", pError);
		pResult->m_Success = false;
		return true;
	}

	int Line = 0;
	str_copy(pResult->m_aaMessages[Line], "--------- Global Top Killstreak ---------", sizeof(pResult->m_aaMessages[Line]));
	Line++;

	bool End = false;
	while(!pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		*pError = '\0';

		char aLastName[MAX_NAME_LENGTH];
		int Blockpoints;

		pSqlServer->GetString(1, aLastName, sizeof(aLastName));

		if(*pError != '\0')
		{
			dbg_msg("top_killstreak", "Failed to retrieve Last Name: %s", pError);
			str_copy(pResult->m_aaMessages[0], "Failed to retrieve account last name.", sizeof(pResult->m_aaMessages[0]));
			pResult->m_Success = false;
			return true;
		}

		Blockpoints = pSqlServer->GetInt(2);

		str_format(aBuf, sizeof(aBuf), "%d. %s : %d", Line, aLastName, Blockpoints);
		str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));
		dbg_msg("top_killstreak", "Retrieved: %s", aBuf);
		Line++;

		if(Line >= CAccountResult::MAX_MESSAGES)
			break;
	}

	if(Line == 1)
	{
		str_copy(pResult->m_aaMessages[Line], "No players found.", sizeof(pResult->m_aaMessages[Line]));
		pResult->m_Success = false;
	}
	else
	{
		pResult->m_Success = true;
	}

	if(*pError != '\0')
	{
		dbg_msg("top_level", "SQL stepping failed: %s", pError);
		str_copy(pResult->m_aaMessages[Line], "Error retrieving top level players.", sizeof(pResult->m_aaMessages[Line]));
		pResult->m_Success = false;
		return true;
	}

	return false;
}
