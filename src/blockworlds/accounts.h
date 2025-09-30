#ifndef BLOCKWORLDS_ACCOUNTS_H
#define BLOCKWORLDS_ACCOUNTS_H

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#include <engine/map.h>
#include <engine/server/databases/connection_pool.h>
#include <game/prng.h>
#include <game/server/save.h>
#include <game/voting.h>

#include <game/server/gamecontext.h>

#include "clans.h"

struct ISqlData;
class IDbConnection;
class IServer;
class CGameContext;

#define MAX_ASCII_FRAMES 16
#define MAX_SQL_ID_LENGTH 8

/*
	CAccountData

	Has an instance on every player object and one in the sql result
*/
struct CAccountData
{
	CAccountData()
	{
		m_ClientId = -1;

		m_Id = 0;
		m_aName[0] = '\0';
		m_aPassword[0] = '\0';
		m_aAddress[0] = '\0';
		m_Vip = 0;
		m_Pages = 0;
		m_Level = 1;
		m_Experience = 0;
		m_Weaponkits = 0;
		m_Ranking = 0;
		m_ClanId = 0;
		m_Blockpoints = 0;
		m_aKnockouts[0] = '\0';
		m_aGundesign[0] = '\0';
		m_aSkinmani[0] = '\0';
		m_Passive = 0;
		m_RegisterDate[0] = '\0';
		m_RankedGames = 0;
		m_RankedKills = 0;
		m_RankedDeaths = 0;
		m_RankedWins = 0;
		m_Kills = 0;
		m_Deaths = 0;
		m_TourneyWin = 0;
		m_Playtime = 0;
		m_Killstreak = 0;
		m_aLastName[0] = '\0';
		m_aLastSkin[0] = '\0';
		m_LastBodyColor = 0;
		m_LastFeetColor = 0;
	}

	// Meta
	int m_ClientId;

	int m_Id;
	char m_aName[64];
	char m_aPassword[256];
	char m_aAddress[48];
	int m_Vip;
	int m_Pages;
	int m_Level;
	int m_Experience;
	int m_Weaponkits;
	int m_Ranking;
	int m_ClanId;
	int m_AuthLevel;
	int m_Blockpoints;
	char m_aKnockouts[256];
	char m_aGundesign[256];
	char m_aSkinmani[256];
	int m_Passive;
	char m_RegisterDate[64];
	int m_RankedGames;
	int m_RankedKills;
	int m_RankedDeaths;
	int m_RankedWins;
	int m_Kills;
	int m_Deaths;
	int m_TourneyWin;
	long long m_Playtime;
	int m_Killstreak;
	char m_aLastName[16];
	char m_aLastSkin[32];
	int m_LastBodyColor;
	int m_LastFeetColor;

	const CClansData *m_pClanData;
};

struct CAdminCommandResult : ISqlResult
{
	CAdminCommandResult();

	enum
	{
		MAX_MESSAGES = 10,
	};

	enum Variant
	{
		DIRECT,
		ALL,
		BROADCAST,
		FREEZE_ACC,
		MODERATOR,
		SUPER_MODERATOR,
		SUPPORTER,
		LOG_ONLY,
	} m_MessageKind;

	char m_aaMessages[MAX_MESSAGES][512];
	char m_aBroadcast[1024];
	int m_AdminClientId;
	int m_TargetAccountId;
	char m_aUsername[64];
	char m_aPassword[64];
	int m_State;
	Variant m_Type;

	void SetVariant(Variant v, const struct CSqlAdminCommandRequest *pRequest);
};

struct CAccountResult : ISqlResult
{
	CAccountResult();

	enum
	{
		MAX_MESSAGES = 11,
	};

	enum Variant
	{
		DIRECT = 0,
		ALL,
		BROADCAST,
		LOGGED_IN_ALREADY,
		LOGIN_WRONG_PASS,
		LOGIN_INFO,
		REGISTER,
		LOG_ONLY,
		TOP_MESSAGES,
	} m_MessageKind;

	// IsAccountBusy
	char m_aLoginServer[32];

	char m_aaMessages[MAX_MESSAGES][512];
	char m_aBroadcast[1024];
	CAccountData m_Account;

	void SetVariant(Variant v)
	{
		m_MessageKind = v;
		switch(v)
		{
		case REGISTER:
		case DIRECT:
		case ALL:
		case TOP_MESSAGES:
			for(auto &aMessage : m_aaMessages)
				aMessage[0] = 0;
			break;

		case BROADCAST:
			m_aBroadcast[0] = 0;
			break;

		case LOGGED_IN_ALREADY:
			m_aLoginServer[0] = 0;
			break;

		case LOGIN_WRONG_PASS:
		case LOGIN_INFO:
			m_Account = CAccountData();
			break;

		case LOG_ONLY:
			break;
		}
	}
};

struct CSqlAccountRequest : ISqlData
{
	CSqlAccountRequest(std::shared_ptr<CAccountResult> pResult, CGameContext *pGameContext) :
		ISqlData(std::move(pResult)), m_pGameContext(pGameContext)
	{
	}

	CGameContext *m_pGameContext;

	CAccountData m_AccountData;
	char m_aUsername[64];
	char m_aPassword[64];
	char m_aNewPassword[64];
	char m_aClanName[11];
	int m_AccountId;
	int m_ClientId;
};

struct CSqlAdminCommandRequest : ISqlData
{
	CSqlAdminCommandRequest(std::shared_ptr<CAdminCommandResult> pResult) :
		ISqlData(std::move(pResult))
	{
	}

	char m_aQuery[128 + (MAX_CLIENTS * (MAX_SQL_ID_LENGTH + 1))];
	int m_AdminClientId;
	int m_TargetAccountId;
	char m_aUsername[64];
	char m_aPassword[64];
	int m_State;
	CAdminCommandResult::Variant m_Type;
};

struct CSqlSetLoginData : ISqlData
{
	CSqlSetLoginData() :
		ISqlData(nullptr)
	{
	}

	int m_AccountId;
	int m_LoggedIn;
};

struct CSqlCleanZombieAccountsData : ISqlData
{
	CSqlCleanZombieAccountsData() :
		ISqlData(nullptr)
	{
	}

	char m_aQuery[128 + (MAX_CLIENTS * (MAX_SQL_ID_LENGTH + 1))];
	int m_ClientId;
	int m_Port;
};

struct CSqlStringData : ISqlData
{
	CSqlStringData() :
		ISqlData(nullptr)
	{
	}

	char m_aString[1024];
};

inline int NeededAccountExp(int Level)
{
	return int(Level * 0.18 + 15);
}

inline int NeededClanExp(int Level)
{
	return int(Level * 2.10 + 30);
}

class CAccounts
{
	CDbConnectionPool *m_pPool;
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const { return m_pServer; }
	CGameContext *m_pGameServer;
	IServer *m_pServer;

	bool RateLimitPlayer(int ClientId);

	// per player queries user

	// Accounts
	static bool LoginThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool RegisterThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool SaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ChangePasswordThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ChangePasswordAdminThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);

	static bool ShowTopLevelThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ShowTopBlockpointsThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ShowTopKillStreaksThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);

	// non ratelimited server side queries
	static bool ClearLoginsThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool LogoutThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ExecuteSqlThread(IDbConnection *pSqlServer, const ISqlData *pGameData, Write w, char *pError, int ErrorSize);

	// returns new SqlResult bound to the player, if no current Thread is active for this player
	std::shared_ptr<CAccountResult> NewSqlAccountResult(int ClientId);
	std::shared_ptr<CAdminCommandResult> NewSqlAdminCommandResult(int ClientId);
	// Creates for player bound database requests
	void ExecUserThread(
		bool (*pFuncPtr)(IDbConnection *, const ISqlData *, char *pError, int ErrorSize),
		const char *pThreadName,
		int ClientId,
		const char *pUsername,
		const char *pPassword,
		const char *pNewPassword,
		int AccountId,
		CAccountData *pAccountData);
	void ExecAdminThread(
		bool (*pFuncPtr)(IDbConnection *, const ISqlData *, char *pError, int ErrorSize),
		const char *pThreadName,
		int AdminClientId,
		int TargetAccountId,
		int State,
		CAdminCommandResult::Variant Type,
		const char *pUsername,
		const char *pPassword,
		const char *pQuery);

public:
	CAccounts(CGameContext *pGameServer, CDbConnectionPool *pPool);
	~CAccounts() {}

	// Accounts
	void Save(int ClientId, CAccountData *pAccountData);
	void Login(int ClientId, const char *pUsername, const char *pPassword);
	void Logout(int ClientId, int AccountId);
	void ClearLogins();
	void Register(int ClientId, const char *pUsername, const char *pPassword);
	void ChangePassword(int ClientId, const char *pUsername, const char *pOldPassword, const char *pNewPassword);
	// Admin: change password by account name
	void ChangePasswordAdmin(int AdminClientId, const char *pUsername, const char *pNewPassword);
	void ExecuteSql(const char *pQuery);

	void ShowTopLevel(int ClientId);
	void ShowTopBlockpoints(int ClientId);
	void ShowTopKillStreak(int ClientId);
};

#endif
