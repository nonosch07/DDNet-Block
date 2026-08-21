
#include <cstddef>
#include <engine/server/databases/connection.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <mutex>

#include <base/hash_ctxt.h>
#include <blockworlds/bw_base.h>
#include <unordered_map>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <intrin.h> // For _ReadWriteBarrier
#endif

#include "accounts.h"
#include "cosmetics/cosmetics.h"
#include "password_hash.h"
#include "sql_prefix.h"
#include <blockworlds/bw_context.h>

// just to be safe when making new cosmetics
static void PadCosmeticString(char *pStr, int RequiredLen)
{
	int Len = str_length(pStr);
	if(Len < RequiredLen)
	{
		for(int i = Len; i < RequiredLen; i++)
			pStr[i] = '0';
		pStr[RequiredLen] = '\0';
	}
}

CAdminCommandResult::CAdminCommandResult()
{
	m_State = -1;
	m_Type = DIRECT;
	m_aUsername[0] = '\0';
	m_aPassword[0] = '\0';
	m_MessageKind = DIRECT;
	for(auto &aMessage : m_aaMessages)
		aMessage[0] = '\0';
}

void CAdminCommandResult::SetVariant(Variant v, const struct CSqlAdminCommandRequest *pRequest)
{
	m_MessageKind = v;
	switch(v)
	{
	case DIRECT:
	case ALL:
	case LOG_ONLY:
	case BROADCAST:
	case FREEZE_ACC:
	case MODERATOR:
	case SUPER_MODERATOR:
	case SUPPORTER:
		for(auto &aMessage : m_aaMessages)
			aMessage[0] = '\0';
		break;
	}
	if(pRequest)
	{
		m_State = pRequest->m_State;
		m_Type = pRequest->m_Type;
		str_copy(m_aUsername, pRequest->m_aUsername, sizeof(m_aUsername));
		str_copy(m_aPassword, pRequest->m_aPassword, sizeof(m_aPassword));
		m_AdminClientId = pRequest->m_AdminClientId;
		m_TargetAccountId = pRequest->m_TargetAccountId;
	}
}

CAccountResult::CAccountResult()
{
	SetVariant(DIRECT);
}

CAccounts::CAccounts(CGameContext *pGameServer, CDbConnectionPool *pPool) :
	m_pPool(pPool),
	m_pGameServer(pGameServer),
	m_pServer(pGameServer->Server())
{
}

bool CAccounts::SyncSaveBlocking(int ClientId, const CAccountData &Acc, int TimeoutMs)
{
	auto pResult = NewSqlAccountResult(ClientId);
	if(!pResult)
		return false;
	auto Tmp = std::make_unique<CSqlAccountRequest>(pResult, GameServer());
	Tmp->m_ClientId = ClientId;
	Tmp->m_AccountId = Acc.m_Id;
	str_copy(Tmp->m_aUsername, Acc.m_aName, sizeof(Tmp->m_aUsername));
	Tmp->m_AccountData = Acc;
	Tmp->m_Critical = true;
	Tmp->m_pFunc = SaveThread;
	m_pPool->ExecuteWrite([](IDbConnection *pSql, const ISqlData *pData, Write, char *pErr, int ErrSz) -> bool {
		auto *pReq = dynamic_cast<const CSqlAccountRequest *>(pData);
		return pReq && pReq->m_pFunc ? pReq->m_pFunc(pSql, pData, pErr, ErrSz) : true;
	},
		std::move(Tmp), "sync account save");
	int64_t start = time_get();
	int64_t deadline = start + (int64_t)TimeoutMs * time_freq() / 1000;
	while(!pResult->m_Completed.load(std::memory_order_relaxed) && time_get() < deadline)
	{
		for(int s = 0; s < 2000; ++s)
		{
#if defined(CONF_FAMILY_WINDOWS)
			_ReadWriteBarrier();
#else
			asm volatile("");
#endif
		}
		thread_yield();
	}
	return pResult->m_Completed.load() && pResult->m_Success;
}

struct IpTrackerEntry
{
	int Attempts = 0;
	int64_t FirstAttemptTick = 0;
	int64_t BannedUntilTick = 0;
	int64_t LastSeenTick = 0;
};

static std::unordered_map<std::string, IpTrackerEntry> s_IpTracker;
static std::mutex s_IpTrackerMutex;

static std::unordered_map<int, int64_t> s_LastPlayerActionTick;
static const int PLAYER_ACTION_COOLDOWN_TICKS = 50;

static inline int cfg_ip_max_attempts()
{
	int v = g_Config.m_SvRegisterIpMaxAttempts;
	if(v < 1)
		v = 1;
	return v;
}
static inline int cfg_ip_window_seconds()
{
	int v = g_Config.m_SvRegisterIpAttemptWindow;
	if(v < 1)
		v = 1;
	return v;
}
static inline int cfg_ip_ban_seconds()
{
	int v = g_Config.m_SvRegisterIpBanSeconds;
	if(v < 0)
		v = 0;
	return v;
}
static inline int cfg_ip_expiry_seconds()
{
	long long win = cfg_ip_window_seconds();
	long long exp = win * 15LL;
	if(exp < win)
		exp = win; // overflow/guard
	if(exp > 24LL * 60 * 60)
		exp = 24LL * 60 * 60; // cap to 24h
	return (int)exp;
}

bool CAccounts::IsIpBanned(const char *pIp, int &RemainingSeconds) const
{
	std::lock_guard<std::mutex> lock(s_IpTrackerMutex);
	auto it = s_IpTracker.find(pIp);
	int64_t Now = time_get();
	if(it == s_IpTracker.end())
	{
		RemainingSeconds = 0;
		return false;
	}
	const IpTrackerEntry &E = it->second;
	if(E.BannedUntilTick > Now)
	{
		RemainingSeconds = (int)((E.BannedUntilTick - Now) / time_freq());
		return true;
	}
	RemainingSeconds = 0;
	return false;
}

bool CAccounts::RateLimitPlayer(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false; // ignore invalid
	int64_t Now = Server()->Tick();
	if(GameServer()->m_apPlayers[ClientId] && GameServer()->m_apPlayers[ClientId]->Bw().m_IsNpc)
	{
		return false;
	}
	auto it = s_LastPlayerActionTick.find(ClientId);
	if(it != s_LastPlayerActionTick.end())
	{
		int64_t Delta = Now - it->second;
		if(Delta < 0)
			s_LastPlayerActionTick.erase(it);
		else if(Delta < PLAYER_ACTION_COOLDOWN_TICKS)
		{
			if(GameServer()->m_apPlayers[ClientId])
				GameServer()->Bw().SendChatTarget(ClientId, "Please wait a moment before trying again.");
			return true;
		}
	}
	s_LastPlayerActionTick[ClientId] = Now;
	return false;
}

bool CAccounts::RegisterIpAttempt(const char *pIp)
{
	std::lock_guard<std::mutex> lock(s_IpTrackerMutex);
	int64_t Now = time_get();
	int64_t Freq = time_freq();
	auto &E = s_IpTracker[pIp];

	if(E.LastSeenTick && Now - E.LastSeenTick > (int64_t)cfg_ip_expiry_seconds() * Freq)
	{
		E = IpTrackerEntry();
	}
	E.LastSeenTick = Now;
	if(E.BannedUntilTick > Now)
	{
		return false; // still banned
	}
	if(E.FirstAttemptTick == 0 || Now - E.FirstAttemptTick > (int64_t)cfg_ip_window_seconds() * Freq)
	{
		E.FirstAttemptTick = Now;
		E.Attempts = 0;
	}
	E.Attempts++;
	if(E.Attempts > cfg_ip_max_attempts())
	{
		E.BannedUntilTick = Now + (int64_t)cfg_ip_ban_seconds() * Freq;
		return false; // now banned
	}
	return true; // allowed
}

void CAccounts::ClearIpBan(const char *pIp)
{
	std::lock_guard<std::mutex> lock(s_IpTrackerMutex);
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
	std::lock_guard<std::mutex> lock(s_IpTrackerMutex);
	std::vector<std::pair<std::string, int>> v;
	int64_t Now = time_get();
	for(const auto &kv : s_IpTracker)
	{
		if(kv.second.BannedUntilTick > Now)
		{
			int Remaining = (int)((kv.second.BannedUntilTick - Now) / time_freq());
			v.emplace_back(kv.first, Remaining);
		}
	}
	return v;
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
	if(!pResult)
		return;
	auto Tmp = std::make_unique<CSqlAdminCommandRequest>(pResult);
	Tmp->m_AdminClientId = AdminClientId;
	Tmp->m_TargetAccountId = TargetAccountId;
	Tmp->m_State = State;
	Tmp->m_Type = Type;
	str_copy(Tmp->m_aUsername, pUsername, sizeof(Tmp->m_aUsername));
	str_copy(Tmp->m_aPassword, pPassword, sizeof(Tmp->m_aPassword));
	if(pQuery)
		str_copy(Tmp->m_aQuery, pQuery, sizeof(Tmp->m_aQuery));
	m_pPool->Execute(pFuncPtr, std::move(Tmp), pThreadName);
}

void CAccounts::Login(int ClientId, const char *pUsername, const char *pPassword)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecUserThread(LoginThread, "login user", ClientId, pUsername, pPassword, "", 0, NULL);
}

std::shared_ptr<CAccountResult> CAccounts::NewSqlAccountResult(int ClientId)
{
	auto pRes = std::make_shared<CAccountResult>();
	pRes->m_Account.m_ClientId = ClientId;
	// during shutdown flush , mark as critical so it's executed even late we don't care
	if(m_ShutdownFlushActive && m_pShutdownCollector)
	{
		m_pShutdownCollector->push_back(pRes);
	}
	return pRes;
}

std::shared_ptr<CAdminCommandResult> CAccounts::NewSqlAdminCommandResult(int ClientId)
{
	auto pRes = std::make_shared<CAdminCommandResult>();
	if(m_ShutdownFlushActive && m_pShutdownCollector)
		m_pShutdownCollector->push_back(pRes);
	return pRes;
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
	if(!pResult)
		return;
	auto Tmp = std::make_unique<CSqlAccountRequest>(pResult, GameServer());
	Tmp->m_ClientId = ClientId;
	Tmp->m_AccountId = AccountId;
	if(pUsername)
		str_copy(Tmp->m_aUsername, pUsername, sizeof(Tmp->m_aUsername));
	if(pPassword)
		str_copy(Tmp->m_aPassword, pPassword, sizeof(Tmp->m_aPassword));
	if(pNewPassword)
		str_copy(Tmp->m_aNewPassword, pNewPassword, sizeof(Tmp->m_aNewPassword));
	if(pAccountData)
		Tmp->m_AccountData = *pAccountData;
	if(m_ShutdownFlushActive)
		Tmp->m_Critical = true;
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameServer()->m_apPlayers[ClientId])
	{
		GameServer()->m_apPlayers[ClientId]->Bw().m_AccountQueryResult.push(pResult);
	}
	m_pPool->Execute(pFuncPtr, std::move(Tmp), pThreadName);
}

static bool SqlWritePerRequestAdapter(IDbConnection *pSqlServer, const ISqlData *pGameData, Write w, char *pError, int ErrorSize)
{
	// use MySQL only; skip backup phases (which are SQLite) safely
	if(w == Write::BACKUP_FIRST || w == Write::NORMAL_SUCCEEDED)
		return true; // treat backup phases as success (no-op)
	if(w == Write::NORMAL_FAILED)
		return false; // don't mask a primary failure
	auto *pReq = dynamic_cast<const CSqlAccountRequest *>(pGameData);
	if(!pReq || !pReq->m_pFunc)
		return false; // fail
	return pReq->m_pFunc(pSqlServer, pGameData, pError, ErrorSize);
}

bool CAccounts::LoginThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAccountRequest *pRequestData = dynamic_cast<const CSqlAccountRequest *>(pGameData);
	CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());

	{ // wrong creds
		char aBuf[2048];
		str_format(aBuf, sizeof(aBuf), "SELECT id, password FROM %s WHERE name = ?;", TBL_ACCOUNTS_CORE);

		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			dbg_msg("account", "Login failed - SQL prepare error: %s", pError);
			return false;
		}

		pSqlServer->BindString(1, pRequestData->m_aUsername);

		bool End;
		if(!pSqlServer->Step(&End, pError, ErrorSize))
		{
			dbg_msg("account", "Login failed - SQL step error: %s", pError);
			return false;
		}

		if(End)
		{
			pResult->SetVariant(CAccountResult::LOGIN_WRONG_PASS);
			return true;
		}
	}
	int AccountId = pSqlServer->GetInt(1);
	char aStored[256];
	pSqlServer->GetString(2, aStored, sizeof(aStored));
	//	dbg_msg("login", "Fetched stored hash len=%d sample='%.32s...'", (int)str_length(aStored), aStored);
	if(!pw_hash_verify(pRequestData->m_aPassword, aStored))
	{
		pResult->SetVariant(CAccountResult::LOGIN_WRONG_PASS);
		mem_zero(aStored, sizeof(aStored));
		return true;
	}
	mem_zero(aStored, sizeof(aStored));

	{ // is account busy
		char aBusyBuf[512];
		str_format(aBusyBuf, sizeof(aBusyBuf), "SELECT server_id FROM %s WHERE account_id = ?;", TBL_ACCOUNTS_BUSY);

		if(!pSqlServer->PrepareStatement(aBusyBuf, pError, ErrorSize))
		{
			dbg_msg("is_account_busy", "SQL preparation failed: %s", pError);
			return false;
		}

		pSqlServer->BindInt(1, AccountId);

		bool End = false;
		if(!pSqlServer->Step(&End, pError, ErrorSize))
		{
			dbg_msg("is_account_busy", "Failed to retrieve server_id: %s", pError);
			pResult->m_Success = false;
			return false;
		}
		if(!End)
		{
			char aServerId[32];
			pSqlServer->GetString(1, aServerId, sizeof(aServerId));

			pResult->SetVariant(CAccountResult::LOGGED_IN_ALREADY);
			str_copy(pResult->m_aLoginServer, aServerId, sizeof(pResult->m_aLoginServer));
			dbg_msg("account", "Account %d already logged in on server '%s'", AccountId, aServerId);
			return true;
		}
	}

	char aBuf[2048];
	str_format(aBuf, sizeof(aBuf),
		"SELECT c.id, c.name, c.password, c.address, i.vip, i.pages, p.level, p.experience, i.weaponkits, p.ranking, "
		"p.clanID, p.auth_level, p.blockpoints, i.knockouts, i.gundesign, i.skinmani, p.passive, c.registerdate, r.ranked_games, "
		"r.ranked_kills, r.ranked_deaths, r.ranked_wins, p.kills, p.deaths, p.tourney_win, p.playtime, p.killstreak, "
		"c.last_name, c.last_skin, c.last_body_color, c.last_feet_color, "
		"COALESCE(p.weekly_day, 0), COALESCE(p.weekly_last_claim, 0), COALESCE(p.weekly_exp_boost_until, 0), "
		"COALESCE(i.passive_removers, 0) FROM %s c "
		"JOIN %s p ON c.id=p.account_id "
		"JOIN %s i ON c.id=i.account_id "
		"JOIN %s r ON c.id=r.account_id WHERE c.id = ?;",
		TBL_ACCOUNTS_CORE, TBL_ACCOUNTS_PROGRESS, TBL_ACCOUNTS_INVENTORY, TBL_ACCOUNTS_RANKED);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return false;
	}

	pSqlServer->BindInt(1, AccountId);

	bool End;
	if(!pSqlServer->Step(&End, pError, ErrorSize))
	{
		return false;
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
	SQL_GET_INT(Index++, pResult->m_Account.m_WeeklyDay);
	SQL_GET_INT(Index++, pResult->m_Account.m_WeeklyLastClaim);
	SQL_GET_INT64(Index++, pResult->m_Account.m_WeeklyExpBoostUntil);
	SQL_GET_INT(Index++, pResult->m_Account.m_PassiveRemovers);

	PadCosmeticString(pResult->m_Account.m_aKnockouts, CCosmeticsHandler::NUM_KNOCKOUTS);
	PadCosmeticString(pResult->m_Account.m_aGundesign, CCosmeticsHandler::NUM_GUNDESIGNS);
	PadCosmeticString(pResult->m_Account.m_aSkinmani, CCosmeticsHandler::NUM_SKINMANIS);

#undef SQL_GET_INT
#undef SQL_GET_INT64
#undef SQL_GET_STRING

	{ // set busy with race condition protection
		char aBusyBuf[512];
		str_format(aBusyBuf, sizeof(aBusyBuf), "INSERT INTO %s (server_id, account_id) VALUES (?, ?) ON DUPLICATE KEY UPDATE server_id = VALUES(server_id);", TBL_ACCOUNTS_BUSY);

		if(!pSqlServer->PrepareStatement(aBusyBuf, pError, ErrorSize))
		{
			dbg_msg("set_account_busy", "SQL preparation failed: %s", pError);
			return false;
		}

		pSqlServer->BindString(1, g_Config.m_SvServerId);
		pSqlServer->BindInt(2, AccountId);

		int Affected;
		if(!pSqlServer->ExecuteUpdate(&Affected, pError, ErrorSize))
		{
			dbg_msg("set_account_busy", "Failed to set busy (%d): %s", AccountId, pError);
			pResult->m_Success = false;
			return false;
		}
		// dbg_msg("set_account_busy", "Successfully set busy for account %d on server %s", AccountId, g_Config.m_SvServerId);
	}

	return true;
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

void CAccounts::SetVipByNameAdmin(int AdminClientId, const char *pUsername, int Vip)
{
	if(RateLimitPlayer(AdminClientId))
		return;
	auto pResult = NewSqlAdminCommandResult(AdminClientId);
	if(!pResult)
		return;
	auto Tmp = std::make_unique<CSqlAdminCommandRequest>(pResult);
	Tmp->m_AdminClientId = AdminClientId;
	Tmp->m_TargetAccountId = 0;
	Tmp->m_State = Vip ? 1 : 0;
	Tmp->m_Type = CAdminCommandResult::DIRECT;
	str_copy(Tmp->m_aUsername, pUsername, sizeof(Tmp->m_aUsername));
	Tmp->m_aPassword[0] = '\0';
	m_pPool->Execute(SetVipByNameAdminThread, std::move(Tmp), "admin set vip by name");
}

bool CAccounts::SetVipByNameAdminThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAdminCommandRequest *pData = dynamic_cast<const CSqlAdminCommandRequest *>(pGameData);
	CAdminCommandResult *pResult = dynamic_cast<CAdminCommandResult *>(pGameData->m_pResult.get());
	if(!pResult || !pData)
		return false;
	pResult->SetVariant(CAdminCommandResult::DIRECT, pData);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "UPDATE %s i JOIN %s c ON i.account_id = c.id SET i.vip = ? WHERE c.name = ?;", TBL_ACCOUNTS_INVENTORY, TBL_ACCOUNTS_CORE);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare set VIP statement.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_State);
	pSqlServer->BindString(2, pData->m_aUsername);
	int NumUpdated = 0;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to execute set VIP statement.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(NumUpdated == 1)
	{
		str_format(pResult->m_aaMessages[0], sizeof(pResult->m_aaMessages[0]), "VIP set to %d for account '%s'.", pData->m_State, pData->m_aUsername);
		pResult->m_Success = true;
	}
	else
	{
		str_copy(pResult->m_aaMessages[0], "No account found with that name.", sizeof(pResult->m_aaMessages[0]));
		pResult->m_Success = false;
	}

	return true;
}

bool CAccounts::ChangePasswordAdminThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAdminCommandRequest *pData = dynamic_cast<const CSqlAdminCommandRequest *>(pGameData);
	CAdminCommandResult *pResult = dynamic_cast<CAdminCommandResult *>(pGameData->m_pResult.get());
	if(!pResult || !pData)
		return false;
	pResult->SetVariant(CAdminCommandResult::DIRECT, pData);

	char aHashedNewPassword[256];
	pw_hash_generate(pData->m_aPassword, aHashedNewPassword, sizeof(aHashedNewPassword), 0);

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "UPDATE %s SET password = ? WHERE name = ?;", TBL_ACCOUNTS_CORE);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare change password statement.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindString(1, aHashedNewPassword);
	pSqlServer->BindString(2, pData->m_aUsername);
	int NumUpdated = 0;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		mem_zero(aHashedNewPassword, sizeof(aHashedNewPassword));
		str_copy(pResult->m_aaMessages[0], "Failed to execute change password statement.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	mem_zero(aHashedNewPassword, sizeof(aHashedNewPassword));

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

	return true;
}

bool CAccounts::ExecuteSqlThread(IDbConnection *pSqlServer, const ISqlData *pGameData, Write w, char *pError, int ErrorSize)
{
	if(w == Write::BACKUP_FIRST || w == Write::NORMAL_SUCCEEDED)
		return true; // no-op success on backup
	if(w == Write::NORMAL_FAILED)
		return false; // propagate failure

	const CSqlStringData *pData = dynamic_cast<const CSqlStringData *>(pGameData);

	if(!pSqlServer->PrepareStatement(pData->m_aString, pError, ErrorSize))
	{
		return false;
	}

	bool End;
	if(!pSqlServer->Step(&End, pError, ErrorSize))
	{
		dbg_assert(false, "ExecuteSqlThread did not step");
		return false;
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

	if(str_length(pData->m_aUsername) < 3 || str_length(pData->m_aUsername) > 11)
	{
		str_copy(pResult->m_aaMessages[0], "Username must be between 3 and 11 characters.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(str_length(pData->m_aPassword) < 4)
	{
		str_copy(pResult->m_aaMessages[0], "Password must be at least 4 characters.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	for(int i = 0; pData->m_aUsername[i]; i++)
	{
		char c = pData->m_aUsername[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
		{
			str_copy(pResult->m_aaMessages[0], "Username can only contain letters, numbers, underscore and dash.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}
	}

	char aBuf[2048];
	char aHashedPassword[256];
	if(pw_hash_generate(pData->m_aPassword, aHashedPassword, sizeof(aHashedPassword), 0) != 0)
	{
		str_copy(pResult->m_aaMessages[0], "Internal password hashing error.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	// dbg_msg("register", "Generated hash length=%d sample='%.32s...'", (int)str_length(aHashedPassword), aHashedPassword);

	// dbg_msg("register", "BEGIN transaction");
	int txAffected = 0;
	if(!pSqlServer->PrepareStatement("BEGIN;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(&txAffected, pError, ErrorSize))
	{
		//	dbg_msg("register", "BEGIN failed: %s", pError);
		return false;
	}

	str_format(aBuf, sizeof(aBuf), "INSERT INTO %s (name, password) VALUES (?, ?);", TBL_ACCOUNTS_CORE);
	// dbg_msg("register", "Preparing core insert");
	bool Failed = false; // track failure instead of goto to keep initialization order safe
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		Failed = true;
	pSqlServer->BindString(1, pData->m_aUsername);
	pSqlServer->BindString(2, aHashedPassword);

	// dbg_msg("register", "Executing core insert for '%s'", pData->m_aUsername);
	if(!Failed && !pSqlServer->ExecuteUpdate(&txAffected, pError, ErrorSize))
	{
		if(pError && str_find_nocase(pError, "duplicate"))
		{
			str_copy(pResult->m_aaMessages[0], "This username already exists.", sizeof(pResult->m_aaMessages[0]));
			pResult->m_Account.m_ClientId = pData->m_ClientId;
			str_copy(pResult->m_Account.m_aName, pData->m_aUsername, sizeof(pResult->m_Account.m_aName));
			pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0);
			pSqlServer->ExecuteUpdate(&txAffected, nullptr, 0);
			return true;
		}
		Failed = true;
	}
	int NewId = 0;
	if(!Failed)
	{
		// dbg_msg("register", "Inserted core row");

		if(!pSqlServer->PrepareStatement("SELECT LAST_INSERT_ID();", pError, ErrorSize))
			Failed = true;
		bool IdEnd = false;
		if(!Failed && (!pSqlServer->Step(&IdEnd, pError, ErrorSize) || IdEnd))
			Failed = true;
		if(!Failed)
		{
			NewId = pSqlServer->GetInt(1);
			//	dbg_msg("register", "Fetched new id = %d", NewId);

			//	dbg_msg("register", "Creating dependent rows for id=%d", NewId);
			str_format(aBuf, sizeof(aBuf), "INSERT INTO %s (account_id) VALUES (?);", TBL_ACCOUNTS_PROGRESS);
			if(!Failed && !pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
				Failed = true;
			if(!Failed)
			{
				pSqlServer->BindInt(1, NewId);
				if(!pSqlServer->ExecuteUpdate(&txAffected, pError, ErrorSize))
					Failed = true;
			}
			str_format(aBuf, sizeof(aBuf), "INSERT INTO %s (account_id) VALUES (?);", TBL_ACCOUNTS_INVENTORY);
			if(!Failed && !pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
				Failed = true;
			if(!Failed)
			{
				pSqlServer->BindInt(1, NewId);
				if(!pSqlServer->ExecuteUpdate(&txAffected, pError, ErrorSize))
					Failed = true;
			}
			str_format(aBuf, sizeof(aBuf), "INSERT INTO %s (account_id) VALUES (?);", TBL_ACCOUNTS_RANKED);
			if(!Failed && !pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
				Failed = true;
			if(!Failed)
			{
				pSqlServer->BindInt(1, NewId);
				if(!pSqlServer->ExecuteUpdate(&txAffected, pError, ErrorSize))
					Failed = true;
			}
		}
		if(!Failed && (!pSqlServer->PrepareStatement("COMMIT;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(&txAffected, pError, ErrorSize)))
		{
			//	dbg_msg("register", "COMMIT failed: %s", pError);
			Failed = true;
		}
	}
	if(Failed)
	{
		pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0);
		pSqlServer->ExecuteUpdate(&txAffected, nullptr, 0);
		mem_zero(aHashedPassword, sizeof(aHashedPassword));
		str_copy(pResult->m_aaMessages[0], "Registration failed (transaction).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	mem_zero(aHashedPassword, sizeof(aHashedPassword));
	pResult->m_Account.m_Id = NewId;
	str_copy(pResult->m_Account.m_aName, pData->m_aUsername, sizeof(pResult->m_Account.m_aName));
	pResult->m_Account.m_ClientId = pData->m_ClientId;
	str_copy(pResult->m_aaMessages[0], "Account registered successfully!", sizeof(pResult->m_aaMessages[0]));
	str_copy(pResult->m_aaMessages[1], "Please log in with /login <name> <pass>", sizeof(pResult->m_aaMessages[1]));
	return true;
}

void CAccounts::Save(int ClientId, CAccountData *pAccountData)
{
	if(!pAccountData || pAccountData->m_Id <= 0)
		return; // nothing to save
	if(!pAccountData->m_DirtyCore && !pAccountData->m_DirtyProgress && !pAccountData->m_DirtyInventory && !pAccountData->m_DirtyRanked)
		return;
	CAccountData Snapshot = *pAccountData;
	pAccountData->m_DirtyCore = pAccountData->m_DirtyProgress = pAccountData->m_DirtyInventory = pAccountData->m_DirtyRanked = false;
	auto pResult = NewSqlAccountResult(ClientId);
	if(!pResult)
		return;
	auto Tmp = std::make_unique<CSqlAccountRequest>(pResult, GameServer());
	Tmp->m_ClientId = ClientId;
	Tmp->m_AccountId = Snapshot.m_Id;
	str_copy(Tmp->m_aUsername, Snapshot.m_aName, sizeof(Tmp->m_aUsername));
	Tmp->m_AccountData = Snapshot;
	Tmp->m_pFunc = SaveThread; // per-request target
	if(m_ShutdownFlushActive)
		Tmp->m_Critical = true;
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameServer()->m_apPlayers[ClientId])
		GameServer()->m_apPlayers[ClientId]->Bw().m_AccountQueryResult.push(pResult);
	m_pPool->ExecuteWrite(SqlWritePerRequestAdapter, std::move(Tmp), m_ShutdownFlushActive ? "save account shutdown" : "save account");
}

bool CAccounts::SaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAccountRequest *pReq = dynamic_cast<const CSqlAccountRequest *>(pGameData);
	if(!pReq)
		return false;
	const CAccountData &Acc = pReq->m_AccountData;
	bool DoCore = Acc.m_DirtyCore;
	bool DoProg = Acc.m_DirtyProgress;
	bool DoInv = Acc.m_DirtyInventory;
	bool DoRank = Acc.m_DirtyRanked;
	if(!DoCore && !DoProg && !DoInv && !DoRank)
		return true; // nothing to do

	int TransactionAffected = 0;
	if(!pSqlServer->PrepareStatement("BEGIN;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(&TransactionAffected, pError, ErrorSize))
		return false;

	char aBuf[1024];
	int Affected = 0;

	if(DoCore)
	{
		str_format(aBuf, sizeof(aBuf), "UPDATE %s SET address = ?, last_name = ?, last_skin = ?, last_body_color = ?, last_feet_color = ? WHERE id = ?;", TBL_ACCOUNTS_CORE);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
			goto fail;
		pSqlServer->BindString(1, Acc.m_aAddress);
		pSqlServer->BindString(2, Acc.m_aLastName);
		pSqlServer->BindString(3, Acc.m_aLastSkin);
		pSqlServer->BindInt(4, Acc.m_LastBodyColor);
		pSqlServer->BindInt(5, Acc.m_LastFeetColor);
		pSqlServer->BindInt(6, Acc.m_Id);
		if(!pSqlServer->ExecuteUpdate(&Affected, pError, ErrorSize))
			goto fail;
	}
	if(DoProg)
	{
		str_format(aBuf, sizeof(aBuf), "UPDATE %s SET level=?, experience=?, ranking=?, clanID=?, auth_level=?, blockpoints=?, passive=?, kills=?, deaths=?, tourney_win=?, playtime=?, killstreak=?, weekly_day=?, weekly_last_claim=?, weekly_exp_boost_until=? WHERE account_id=?;", TBL_ACCOUNTS_PROGRESS);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
			goto fail;
		pSqlServer->BindInt(1, Acc.m_Level);
		pSqlServer->BindInt(2, Acc.m_Experience);
		pSqlServer->BindInt(3, Acc.m_Ranking);
		pSqlServer->BindInt(4, Acc.m_ClanId);
		pSqlServer->BindInt(5, (int)Acc.m_AuthLevel);
		pSqlServer->BindInt(6, Acc.m_Blockpoints);
		pSqlServer->BindInt(7, Acc.m_Passive);
		pSqlServer->BindInt(8, Acc.m_Kills);
		pSqlServer->BindInt(9, Acc.m_Deaths);
		pSqlServer->BindInt(10, Acc.m_TourneyWin);
		pSqlServer->BindInt64(11, Acc.m_Playtime);
		pSqlServer->BindInt(12, Acc.m_Killstreak);
		pSqlServer->BindInt(13, Acc.m_WeeklyDay);
		pSqlServer->BindInt(14, Acc.m_WeeklyLastClaim);
		pSqlServer->BindInt64(15, Acc.m_WeeklyExpBoostUntil);
		pSqlServer->BindInt(16, Acc.m_Id);
		if(!pSqlServer->ExecuteUpdate(&Affected, pError, ErrorSize))
			goto fail;
	}
	if(DoInv)
	{
		str_format(aBuf, sizeof(aBuf), "UPDATE %s SET vip=?, pages=?, weaponkits=?, passive_removers=?, knockouts=?, gundesign=?, skinmani=? WHERE account_id=?;", TBL_ACCOUNTS_INVENTORY);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
			goto fail;
		pSqlServer->BindInt(1, Acc.m_Vip);
		pSqlServer->BindInt(2, Acc.m_Pages);
		pSqlServer->BindInt(3, Acc.m_Weaponkits);
		pSqlServer->BindInt(4, Acc.m_PassiveRemovers);
		pSqlServer->BindString(5, Acc.m_aKnockouts);
		pSqlServer->BindString(6, Acc.m_aGundesign);
		pSqlServer->BindString(7, Acc.m_aSkinmani);
		pSqlServer->BindInt(8, Acc.m_Id);
		if(!pSqlServer->ExecuteUpdate(&Affected, pError, ErrorSize))
			goto fail;
	}
	if(DoRank)
	{
		str_format(aBuf, sizeof(aBuf), "UPDATE %s SET ranked_games=?, ranked_kills=?, ranked_deaths=?, ranked_wins=? WHERE account_id=?;", TBL_ACCOUNTS_RANKED);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
			goto fail;
		pSqlServer->BindInt(1, Acc.m_RankedGames);
		pSqlServer->BindInt(2, Acc.m_RankedKills);
		pSqlServer->BindInt(3, Acc.m_RankedDeaths);
		pSqlServer->BindInt(4, Acc.m_RankedWins);
		pSqlServer->BindInt(5, Acc.m_Id);
		if(!pSqlServer->ExecuteUpdate(&Affected, pError, ErrorSize))
			goto fail;
	}

	if(!pSqlServer->PrepareStatement("COMMIT;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(&TransactionAffected, pError, ErrorSize))
		return false; // commit failed, we're doomed
	return true;

fail:
	pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0);
	pSqlServer->ExecuteUpdate(&TransactionAffected, nullptr, 0);
	return false;
}

void CAccounts::Logout(int ClientId, int AccountId)
{
	ExecUserThread(LogoutThread, m_ShutdownFlushActive ? "logout user shutdown" : "logout user", ClientId, "", "", "", AccountId, nullptr);
}

bool CAccounts::LogoutThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlAccountRequest *pData = dynamic_cast<const CSqlAccountRequest *>(pGameData);
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "DELETE FROM %s WHERE account_id = ? AND server_id = ?;", TBL_ACCOUNTS_BUSY);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		dbg_msg("logout", "SQL preparation failed: %s", pError);
		return false;
	}

	pSqlServer->BindInt(1, pData->m_AccountId);
	pSqlServer->BindString(2, g_Config.m_SvServerId);

	int Deleted;
	if(!pSqlServer->ExecuteUpdate(&Deleted, pError, ErrorSize))
	{
		dbg_msg("logout", "Failed to remove account_busy entry: %s", pError);
		return false;
	}
	// if Deleted == 0 thenb we silently ignore (can happen if bulk ClearLogins already removed it)
	return true;
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
	str_format(aBuf, sizeof(aBuf), "DELETE FROM %s WHERE server_id = ?;", TBL_ACCOUNTS_BUSY);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		dbg_msg("clear_logins", "SQL preparation failed: %s", pError);
		return false;
	}

	pSqlServer->BindString(1, g_Config.m_SvServerId);
	pSqlServer->Print();

	int Deleted;
	if(!pSqlServer->ExecuteUpdate(&Deleted, pError, ErrorSize))
	{
		dbg_msg("clear_logins", "Failed to clear logins for %s: %s", g_Config.m_SvServerId, pError);
		return false;
	}
	return true;
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
	str_format(aBuf, sizeof(aBuf), "SELECT password FROM %s WHERE name = ?;", TBL_ACCOUNTS_CORE);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return false;
	}

	pSqlServer->BindString(1, pData->m_aUsername);

	bool End;
	if(!pSqlServer->Step(&End, pError, ErrorSize))
	{
		return false;
	}

	if(End)
	{
		str_copy(pResult->m_aaMessages[0], "Username not found.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	char aStoredPasswordHash[256];
	pSqlServer->GetString(1, aStoredPasswordHash, sizeof(aStoredPasswordHash));
	if(!pw_hash_verify(pData->m_aPassword, aStoredPasswordHash))
	{
		str_copy(pResult->m_aaMessages[0], "Old password is incorrect.", sizeof(pResult->m_aaMessages[0]));
		mem_zero(aStoredPasswordHash, sizeof(aStoredPasswordHash));
		return true;
	}
	mem_zero(aStoredPasswordHash, sizeof(aStoredPasswordHash));

	char aHashedNewPassword[256];
	pw_hash_generate(pData->m_aNewPassword, aHashedNewPassword, sizeof(aHashedNewPassword), 0);

	str_format(aBuf, sizeof(aBuf), "UPDATE %s SET password = ? WHERE name = ?;", TBL_ACCOUNTS_CORE);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return false;
	}

	pSqlServer->BindString(1, aHashedNewPassword);
	pSqlServer->BindString(2, pData->m_aUsername);

	int NumUpdated;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		mem_zero(aHashedNewPassword, sizeof(aHashedNewPassword));
		return false;
	}
	mem_zero(aHashedNewPassword, sizeof(aHashedNewPassword));

	if(NumUpdated == 1)
	{
		str_copy(pResult->m_aaMessages[0], "Successfully changed your password.", sizeof(pResult->m_aaMessages[0]));
	}
	else
	{
		str_copy(pResult->m_aaMessages[0], "Password change failed, please try again or contact an administrator", sizeof(pResult->m_aaMessages[0]));
	}

	return true;
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
	pResult->SetVariant(CAccountResult::TOP_MESSAGES);

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "SELECT c.last_name, p.level FROM %s c JOIN %s p ON c.id=p.account_id ORDER BY p.level DESC LIMIT 10;", TBL_ACCOUNTS_CORE, TBL_ACCOUNTS_PROGRESS);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
		dbg_msg("top_level", "SQL preparation failed: %s", pError);
		pResult->m_Success = false;
		return false;
	}

	int Line = 0;
	str_copy(pResult->m_aaMessages[Line], "------------ Global Top Level ------------", sizeof(pResult->m_aaMessages[Line]));
	Line++;

	bool End = false;
	while(pSqlServer->Step(&End, pError, ErrorSize) && !End)
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
			return false;
		}

		Level = pSqlServer->GetInt(2);

		str_format(aBuf, sizeof(aBuf), "[%d] %s : %d", Line, aLastName, Level);
		str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));
		// dbg_msg("top_level", "Retrieved: %s", aBuf);
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
		return false;
	}

	return true;
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
	str_format(aBuf, sizeof(aBuf), "SELECT c.last_name, p.blockpoints FROM %s c JOIN %s p ON c.id=p.account_id ORDER BY p.blockpoints DESC LIMIT 10;", TBL_ACCOUNTS_CORE, TBL_ACCOUNTS_PROGRESS);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
		dbg_msg("top_blockpoints", "SQL preparation failed: %s", pError);
		pResult->m_Success = false;
		return false;
	}

	int Line = 0;
	str_copy(pResult->m_aaMessages[Line], "---------- Global Top Blockpoints ----------", sizeof(pResult->m_aaMessages[Line]));
	Line++;

	bool End = false;
	while(pSqlServer->Step(&End, pError, ErrorSize) && !End)
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
			return false;
		}

		Blockpoints = pSqlServer->GetInt(2);

		str_format(aBuf, sizeof(aBuf), "[%d] %s - %d", Line, aLastName, Blockpoints);
		str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));
		//	dbg_msg("top_blockpoints", "Retrieved: %s", aBuf);
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
		return false;
	}

	return true;
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
	str_format(aBuf, sizeof(aBuf), "SELECT c.last_name, p.killstreak FROM %s c JOIN %s p ON c.id=p.account_id ORDER BY p.killstreak DESC LIMIT 10;", TBL_ACCOUNTS_CORE, TBL_ACCOUNTS_PROGRESS);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
		dbg_msg("top_killstreak", "SQL preparation failed: %s", pError);
		pResult->m_Success = false;
		return false;
	}

	int Line = 0;
	str_copy(pResult->m_aaMessages[Line], "--------- Global Top Killstreak ---------", sizeof(pResult->m_aaMessages[Line]));
	Line++;

	bool End = false;
	while(pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		*pError = '\0';

		char aLastName[MAX_NAME_LENGTH];
		int Killstreak;

		pSqlServer->GetString(1, aLastName, sizeof(aLastName));

		if(*pError != '\0')
		{
			// dbg_msg("top_killstreak", "Failed to retrieve Last Name: %s", pError);
			str_copy(pResult->m_aaMessages[0], "Failed to retrieve account last name.", sizeof(pResult->m_aaMessages[0]));
			pResult->m_Success = false;
			return false;
		}

		Killstreak = pSqlServer->GetInt(2);

		str_format(aBuf, sizeof(aBuf), "[%d] %s : %d", Line, aLastName, Killstreak);
		str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));
		//	dbg_msg("top_killstreak", "Retrieved: %s", aBuf);
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
		return false;
	}

	return true;
}
