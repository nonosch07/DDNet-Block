#include <cstddef>
#include <engine/server/databases/connection.h>
#include <engine/shared/protocol.h>
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

struct IpTrackerEntry
{
	int Attempts = 0;
	int64_t FirstAttemptTick = 0;
	int64_t BannedUntilTick = 0;
	int64_t LastSeenTick = 0;
};

static std::unordered_map<std::string, IpTrackerEntry> s_IpTracker;
static std::deque<std::string> s_IpOrder;
static std::mutex s_IpTrackerMutex;

bool CAccounts::IsIpBanned(const char *pIp, int &RemainingSeconds) const
{
	std::lock_guard<std::mutex> l(s_IpTrackerMutex);
	auto it = s_IpTracker.find(pIp);
	if(it == s_IpTracker.end())
	{
		RemainingSeconds = 0;
		return false;
	}
	int64_t now = Server()->Tick();
	if(it->second.BannedUntilTick > now)
	{
		RemainingSeconds = (int)((it->second.BannedUntilTick - now) / Server()->TickSpeed());
		return true;
	}
	RemainingSeconds = 0;
	return false;
}

bool CAccounts::RegisterIpAttempt(const char *pIp)
{
	std::lock_guard<std::mutex> l(s_IpTrackerMutex);
	const int TickSpeed = Server()->TickSpeed();
	int64_t now = Server()->Tick();

	const size_t MAX_ENTRIES = 8192;
	const size_t CLEANUP_THRESHOLD = MAX_ENTRIES + 1024;
	if(s_IpTracker.size() > CLEANUP_THRESHOLD)
	{
		size_t toRemove = s_IpTracker.size() - MAX_ENTRIES;
		for(size_t i = 0; i < toRemove && !s_IpOrder.empty(); ++i)
		{
			const std::string &old = s_IpOrder.front();
			s_IpOrder.pop_front();
			s_IpTracker.erase(old);
		}
		dbg_msg("account", "IP tracker cleanup: removed %zu entries, %zu remaining", toRemove, s_IpTracker.size());
	}

	auto &entry = s_IpTracker[pIp];
	entry.LastSeenTick = now;

	int WindowSeconds = g_Config.m_SvRegisterIpAttemptWindow;
	if(entry.FirstAttemptTick == 0 || now - entry.FirstAttemptTick > WindowSeconds * TickSpeed)
	{
		entry.Attempts = 0;
		entry.FirstAttemptTick = now;
	}

	entry.Attempts++;
	int MaxAttempts = g_Config.m_SvRegisterIpMaxAttempts;
	if(entry.Attempts > MaxAttempts)
	{
		int BanSeconds = g_Config.m_SvRegisterIpBanSeconds;
		entry.BannedUntilTick = now + BanSeconds * TickSpeed;
		// push ip into order as last seen
		s_IpOrder.push_back(std::string(pIp));
		return true;
	}

	s_IpOrder.push_back(std::string(pIp));
	return false;
}

void CAccounts::ClearIpBan(const char *pIp)
{
	std::lock_guard<std::mutex> l(s_IpTrackerMutex);
	auto it = s_IpTracker.find(pIp);
	if(it != s_IpTracker.end())
	{
		it->second.BannedUntilTick = 0;
		it->second.Attempts = 0;
		it->second.FirstAttemptTick = 0;
	}
}

std::vector<std::pair<std::string, int>> CAccounts::ListIpBans() const
{
	std::lock_guard<std::mutex> l(s_IpTrackerMutex);
	std::vector<std::pair<std::string, int>> out;
	int64_t now = Server()->Tick();
	for(const auto &kv : s_IpTracker)
	{
		if(kv.second.BannedUntilTick > now)
		{
			int remaining = (int)((kv.second.BannedUntilTick - now) / Server()->TickSpeed());
			out.emplace_back(kv.first, remaining);
		}
	}
	return out;
}

std::shared_ptr<CAdminCommandResult> CAccounts::NewSqlAdminCommandResult(int ClientId)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pCurPlayer || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;

	auto pResult = std::make_shared<CAdminCommandResult>();
	pCurPlayer->m_AdminCommandQueryResult.push(pResult);
	return pResult;
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
	if(!pCurPlayer || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;

	auto pResult = std::make_shared<CAccountResult>();
	pCurPlayer->m_AccountQueryResult.push(pResult);
	return pResult;
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
	if(m_ShutdownFlushActive)
		Tmp->m_Critical = true;

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
		"clanID = ?, auth_level = ?, blockpoints = ?, knockouts = ?, gundesign = ?, skinmani = ?, passive = ?, ranked_games = ?, "
		"ranked_kills = ?, ranked_deaths = ?, ranked_wins = ?, kills = ?, deaths = ?, tourney_win = ?, playtime = ?, killstreak = ?, "
		"last_name = ?, last_skin = ?, last_body_color = ?, last_feet_color = ? "
		"WHERE id = ?;",
		sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		dbg_msg("sql", "SaveThread - PrepareStatement failed for account %d: %s", pData->m_AccountData.m_Id, pError);
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
	BIND_INT(static_cast<int>(pAcc->m_AuthLevel));
	BIND_INT(pAcc->m_Blockpoints);
	BIND_STRING(pAcc->m_aKnockouts);
	BIND_STRING(pAcc->m_aGundesign);
	BIND_STRING(pAcc->m_aSkinmani);
	BIND_INT(pAcc->m_Passive);
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
		dbg_msg("sql", "SaveThread - ExecuteUpdate failed for account %d: %s", pAcc->m_Id, pError);
		return true;
	}
	dbg_msg("sql", "SaveThread - Successfully saved account %d (%d rows updated)", pAcc->m_Id, NumUpdated);
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
	sha256_str(HashedPassword, aHashedPassword, sizeof(aHashedPassword));

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
			dbg_msg("account", "Login failed - SQL prepare error: %s", pError);
			return true;
		}

		pSqlServer->BindString(1, pRequestData->m_aUsername);
		pSqlServer->BindString(2, aHashedPassword);

		bool End;
		if(pSqlServer->Step(&End, pError, ErrorSize))
		{
			dbg_msg("account", "Login failed - SQL step error: %s", pError);
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
			pSqlServer->GetString(1, aServerId, sizeof(aServerId));

			pResult->SetVariant(CAccountResult::LOGGED_IN_ALREADY);
			str_copy(pResult->m_aLoginServer, aServerId, sizeof(pResult->m_aLoginServer));
			dbg_msg("account", "Account %d already logged in on server '%s'", AccountId, aServerId);
			return false;
		}
	}

	char aBuf[2048];
	str_copy(aBuf,
		"SELECT "
		"id, name, password, address, vip, pages, level, experience, weaponkits, ranking, "
		"clanID, auth_level, blockpoints, knockouts, gundesign, skinmani, passive, registerdate, ranked_games, "
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
	{
		int rawAuthLevel = 0;
		SQL_GET_INT(Index++, rawAuthLevel);
		pResult->m_Account.m_AuthLevel = static_cast<ClanAuthLevel>(rawAuthLevel);
	}
	SQL_GET_INT(Index++, pResult->m_Account.m_Blockpoints);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aKnockouts);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aGundesign);
	SQL_GET_STRING(Index++, pResult->m_Account.m_aSkinmani);
	SQL_GET_INT(Index++, pResult->m_Account.m_Passive);
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

	{ // set busy with race condition protection
		char aBusyBuf[512];
		str_copy(aBusyBuf, "INSERT INTO accounts_busy (server_id, account_id) VALUES (?, ?) ON DUPLICATE KEY UPDATE server_id = VALUES(server_id);", sizeof(aBusyBuf));

		if(pSqlServer->PrepareStatement(aBusyBuf, pError, ErrorSize))
		{
			dbg_msg("set_account_busy", "SQL preparation failed: %s", pError);
			return true;
		}

		pSqlServer->BindString(1, g_Config.m_SvServerId);
		pSqlServer->BindInt(2, AccountId);

		int Affected;
		if(pSqlServer->ExecuteUpdate(&Affected, pError, ErrorSize))
		{
			dbg_msg("set_account_busy", "Failed to set busy (%d): %s", AccountId, pError);
			pResult->m_Success = false;
			return true;
		}
		dbg_msg("set_account_busy", "Successfully set busy for account %d on server %s", AccountId, g_Config.m_SvServerId);
	}

	return false;
}

void CAccounts::ExecuteSql(const char *pQuery)
{
	auto Tmp = std::make_unique<CSqlStringData>();
	str_copy(Tmp->m_aString, pQuery, sizeof(Tmp->m_aString));

	m_pPool->ExecuteWrite(ExecuteSqlThread, std::move(Tmp), "execute sql query");
}

void CAccounts::ChangePasswordAdmin(int AdminClientId, const char *pUsername, const char *pNewPassword)
{
	if(RateLimitPlayer(AdminClientId))
		return;
	auto pResult = NewSqlAdminCommandResult(AdminClientId);
	if(!pResult)
		return;
	auto Tmp = std::make_unique<CSqlAdminCommandRequest>(pResult);
	Tmp->m_AdminClientId = AdminClientId;
	Tmp->m_TargetAccountId = 0;
	Tmp->m_State = 0;
	Tmp->m_Type = CAdminCommandResult::DIRECT;
	str_copy(Tmp->m_aUsername, pUsername, sizeof(Tmp->m_aUsername));
	str_copy(Tmp->m_aPassword, pNewPassword, sizeof(Tmp->m_aPassword));
	m_pPool->Execute(ChangePasswordAdminThread, std::move(Tmp), "admin change password");
}

bool CAccounts::ChangePasswordAdminThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAdminCommandRequest *pData = dynamic_cast<const CSqlAdminCommandRequest *>(pGameData);
	CAdminCommandResult *pResult = dynamic_cast<CAdminCommandResult *>(pGameData->m_pResult.get());
	if(!pResult || !pData)
		return true;
	pResult->SetVariant(CAdminCommandResult::DIRECT, pData);

	SHA256_DIGEST HashedNewPassword = CGameContext::HashPassword(pData->m_aPassword);
	char aHashedNewPassword[SHA256_DIGEST_LENGTH * 2 + 1];
	sha256_str(HashedNewPassword, aHashedNewPassword, sizeof(aHashedNewPassword));

	char aBuf[512];
	str_copy(aBuf, "UPDATE accounts SET password = ? WHERE name = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare change password statement.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindString(1, aHashedNewPassword);
	pSqlServer->BindString(2, pData->m_aUsername);
	int NumUpdated = 0;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to execute change password statement.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(NumUpdated == 1)
	{
		str_copy(pResult->m_aaMessages[0], "Password changed successfully.", sizeof(pResult->m_aaMessages[0]));
		pResult->m_Success = true;
	}
	else
	{
		str_copy(pResult->m_aaMessages[0], "No account found with that name.", sizeof(pResult->m_aaMessages[0]));
		pResult->m_Success = false;
	}

	return false;
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

	// Input validation
	if(str_length(pData->m_aUsername) < 3 || str_length(pData->m_aUsername) > 11)
	{
		str_copy(pResult->m_aaMessages[0], "Username must be between 3 and 11 characters.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(str_length(pData->m_aPassword) < 4)
	{
		str_copy(pResult->m_aaMessages[0], "Password must be at least 4 characters.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	for(int i = 0; pData->m_aUsername[i]; i++)
	{
		char c = pData->m_aUsername[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
		{
			str_copy(pResult->m_aaMessages[0], "Username can only contain letters, numbers, underscore and dash.", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
	}

	char aBuf[2048];
	str_copy(aBuf, "SELECT name FROM accounts WHERE name = ?;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Failed to prepare SELECT statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		return true;
	}

	pSqlServer->BindString(1, pData->m_aUsername);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Failed to execute SELECT statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
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
		sha256_str(HashedPassword, aHashedPassword, sizeof(aHashedPassword));

		str_copy(aBuf, "INSERT INTO accounts (name, password) VALUES (?, ?);", sizeof(aBuf));

		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			if(pError && *pError)
			{
				char aTmp[1024];
				str_format(aTmp, sizeof(aTmp), "Failed to prepare INSERT statement: %s", pError);
				str_copy(pError, aTmp, ErrorSize);
			}
			return true;
		}

		pSqlServer->BindString(1, pData->m_aUsername);
		pSqlServer->BindString(2, aHashedPassword);

		int NumInserted;
		if(pSqlServer->ExecuteUpdate(&NumInserted, pError, ErrorSize))
		{
			if(pError && *pError)
			{
				char aTmp[1024];
				str_format(aTmp, sizeof(aTmp), "Failed to execute INSERT statement: %s", pError);
				str_copy(pError, aTmp, ErrorSize);
			}
			return true;
		}

		if(NumInserted == 1)
		{
			str_copy(pResult->m_aaMessages[0], "Account registered successfully!", sizeof(pResult->m_aaMessages[0]));
			str_copy(pResult->m_aaMessages[1], "Please log in with /login <name> <pass>", sizeof(pResult->m_aaMessages[1]));
		}
		else
		{
			if(pError && ErrorSize > 0)
			{
				str_copy(pError, "Account registration failed. No rows inserted.", ErrorSize);
			}
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
	struct CCriticalClear final : ISqlData
	{
		CCriticalClear() :
			ISqlData(nullptr) { m_Critical = true; }
	};
	auto p = std::make_unique<CCriticalClear>();
	m_pPool->Execute(ClearLoginsThread, std::move(p), "clear all logins");
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
	sha256_str(HashedOldPassword, aHashedOldPassword, sizeof(aHashedOldPassword));

	if(str_comp(aHashedOldPassword, aStoredPasswordHash) != 0)
	{
		str_copy(pResult->m_aaMessages[0], "Old password is incorrect.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	SHA256_DIGEST HashedNewPassword = CGameContext::HashPassword(pData->m_aNewPassword);
	char aHashedNewPassword[SHA256_DIGEST_LENGTH * 2 + 1];
	sha256_str(HashedNewPassword, aHashedNewPassword, sizeof(aHashedNewPassword));

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

		str_format(aBuf, sizeof(aBuf), "[%d] %s : %d", Line, aLastName, Level);
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

		str_format(aBuf, sizeof(aBuf), "[%d] %s - %d", Line, aLastName, Blockpoints);
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
		dbg_msg("top_blockpoints", "SQL stepping failed: %s", pError);
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

		str_format(aBuf, sizeof(aBuf), "[%d] %s : %d", Line, aLastName, Blockpoints);
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
