#ifndef GAME_SERVER_DDPP_ACCOUNTS_H
#define GAME_SERVER_DDPP_ACCOUNTS_H

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
		m_IsLoggedIn = 0;
		m_Vip = 0;
		m_Pages = 0;
		m_Level = 1;
		m_Experience = 0;
		m_Weaponkits = 0;
		m_Ranking = 0;
		m_aClan[0] = '\0';
		m_Blockpoints = 0;
		m_aKnockouts[0] = '\0';
		m_aGundesign[0] = '\0';
		m_aSkinmani[0] = '\0';
		m_aExtras[0] = '\0';
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
	int m_IsLoggedIn;
	int m_Vip;
	int m_Pages;
	int m_Level;
	int m_Experience;
	int m_Weaponkits;
	int m_Ranking;
	char m_aClan[15];
	int m_Blockpoints;
	char m_aKnockouts[256];
	char m_aGundesign[256];
	char m_aSkinmani[256];
	char m_aExtras[256];
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
		MAX_MESSAGES = 11, // tops' header + 10 results
	};

	// Message variants
	enum Variant
	{
		DIRECT, // Direct message to the player
		ALL, // Message to all players
		BROADCAST, // Broadcast message
		LOGGED_IN_ALREADY, // Player already logged in
		LOGIN_WRONG_PASS, // Incorrect login credentials
		LOGIN_INFO, // Information to be shown after login
		REGISTER, // Registration messages
		LOG_ONLY, // Log message (debugging purposes)
		TOP_MESSAGES // for top-level messages
	} m_MessageKind;

	char m_aaMessages[MAX_MESSAGES][512]; // Array to store messages
	char m_aBroadcast[1024]; // Broadcast message buffer
	CAccountData m_Account; // Account information

	// Set the message type variant
	void SetVariant(Variant v)
	{
		m_MessageKind = v;
		switch(v)
		{
		case REGISTER:
		case DIRECT:
		case ALL:
		case TOP_MESSAGES: // Add TOP_MESSAGES to this case to reset the message array
			for(auto &aMessage : m_aaMessages)
				aMessage[0] = 0; // Reset each message to an empty string
			break;

		case BROADCAST:
			m_aBroadcast[0] = 0; // Reset the broadcast message
			break;

		case LOGGED_IN_ALREADY:
			// No specific reset needed for this case
			break;

		case LOGIN_WRONG_PASS:
		case LOGIN_INFO:
			m_Account = CAccountData(); // Reset account data for login-related cases
			break;

		case LOG_ONLY:
			// No specific reset needed for this case
			break;
		}
	}
};

struct CSqlAccountRequest : ISqlData
{
	CSqlAccountRequest(std::shared_ptr<CAccountResult> pResult) :
		ISqlData(std::move(pResult))
	{
	}

	CAccountData m_AccountData;
	char m_aUsername[64];
	char m_aPassword[64];
	char m_aNewPassword[64];

	char m_aClanName[15];
	char m_AccountID;
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


class CAccounts
{
	CDbConnectionPool *m_pPool;
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const { return m_pServer; }
	CGameContext *m_pGameServer;
	IServer *m_pServer;

	// per player queries user

	// Accounts
	static bool LoginThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool RegisterThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool SaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ChangePasswordThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);

	static bool ShowTopLevelThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ShowTopBlockpointsThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ShowTopKillStreaksThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);

	// non ratelimited server side queries
	static bool SetLoggedInThread(IDbConnection *pSqlServer, const ISqlData *pGameData, Write w, char *pError, int ErrorSize);
	static bool LogoutUsernameThread(IDbConnection *pSqlServer, const ISqlData *pGameData, Write w, char *pError, int ErrorSize);
	static bool ExecuteSqlThread(IDbConnection *pSqlServer, const ISqlData *pGameData, Write w, char *pError, int ErrorSize);

	// returns new SqlResult bound to the player, if no current Thread is active for this player
	std::shared_ptr<CAccountResult> NewSqlAccountResult(int ClientId);
	std::shared_ptr<CAdminCommandResult> NewSqlAdminCommandResult(int ClientId);
	// Creates for player bound database requests (1 request max at a time per player)
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
	void Register(int ClientId, const char *pUsername, const char *pPassword);
	void ChangePassword(int ClientId, const char *pUsername, const char *pOldPassword, const char *pNewPassword);
	void SetLoggedIn(int ClientId, int LoggedIn, int AccountId);
	void ExecuteSql(const char *pQuery);

	void ShowTopLevel(int ClientId);
	void ShowTopBlockpoints(int ClientId);
	void ShowTopKillStreak(int ClientId);
};

#endif
