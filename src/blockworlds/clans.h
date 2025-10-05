#ifndef BLOCKWORLDS_CLANS_H
#define BLOCKWORLDS_CLANS_H

#include "engine/server/databases/connection_pool.h"
#include <memory>
#include <unordered_set>
#include <vector>
#include <string>

class IDbConnection;
class IServer;
class CGameContext;

struct CClansData
{
	int m_Id;
	char m_ClanName[33];	// Match SQL schema varchar(32) + null terminator
	int m_Level;
	int m_Experience;

	int m_LastSavedTick;
};

struct CClanListResult : ISqlResult
{
	std::vector<CClansData> m_vClans;
	CClanListResult() {}
};

struct CSqlClanListRequest : ISqlData
{
	CSqlClanListRequest(std::shared_ptr<CClanListResult> pResult, class CClanManager *pClanManager) :
		ISqlData(std::move(pResult)), m_pClanManager(pClanManager) {}
	class CClanManager *m_pClanManager;
};

struct CClanResult : ISqlResult
{
	enum
	{
		MAX_MESSAGES = 15,
	};

	char m_aaMessages[MAX_MESSAGES][512];
	char m_aBroadcast[1024];

	enum Variant
	{
		DIRECT,
		ALL,
		BROADCAST,
		DELETE,
		CLAN,
	} m_MessageKind;
	Variant m_Type;

	void SetVariant(Variant v)
	{
		m_MessageKind = v;
		switch(v)
		{
		case DIRECT:
		case ALL:
		case BROADCAST:
			m_aBroadcast[0] = 0;
			break;
		case CLAN:
		case DELETE:
			break;
		}
	}

	CClanResult();

	// Actions for main-thread application (set by SQL worker threads)
	enum ActionType
	{
		ACTION_NONE = 0,
		ACTION_UPDATE_PLAYER_BY_CLIENT, // update a specific client id's in-memory clan/auth
		ACTION_UPDATE_PLAYER_BY_NAME,   // update a player found by name
		ACTION_RESET_CLAN_PLAYERS       // reset all players of a clan (set clan id to 0)
	} m_Action = ACTION_NONE;

	int m_ActionClientId = -1; // for ACTION_UPDATE_PLAYER_BY_CLIENT
	int m_ActionNewClanId = 0; // target clan id for update actions
	int m_ActionNewAuthLevel = 0; // auth level for update actions
	char m_ActionPlayerName[64]; // for ACTION_UPDATE_PLAYER_BY_NAME
	int m_ActionResetClanId = 0; // for ACTION_RESET_CLAN_PLAYERS
};

struct CSqlClanRequest : ISqlData
{
	CSqlClanRequest(std::shared_ptr<CClanResult> pResult, CClanManager *pClanMgr) :
		ISqlData(std::move(pResult)), m_AccountId(0), m_ClientId(-1), m_ClanId(0), m_NewAuthLevel(0), m_pClanManager(pClanMgr)
	{
		m_aClanName[0] = '\0';
		m_aUsername[0] = '\0';
		m_aNewClanName[0] = '\0';
	}
	char m_aClanName[33];	// Match SQL schema varchar(32) + null terminator
	char m_aUsername[12];	// Match SQL schema varchar(11) + null terminator
	int m_AccountId;
	int m_ClientId;
	int m_ClanId;
	int m_NewAuthLevel;
	char m_aNewClanName[33];	// Match SQL schema varchar(32) + null terminator
	CClanManager *m_pClanManager;
};

class CClanManager
{
	CDbConnectionPool *m_pPool;
	CGameContext *m_pGameServer;
	IServer *m_pServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const { return m_pServer; }

	// local cache of clans loaded from the skibidi db
	std::vector<CClansData> m_vClansData;

	void ExecClanThread(bool (*pFuncPtr)(IDbConnection *, const ISqlData *, char *, int),
		const char *pThreadName,
		int ClientId,
		const char *pClanName,
		const char *pUsername = nullptr,
		int AccountId = 0,
		int ClanId = 0,
		int AuthLevel = 0);

	bool RateLimitPlayer(int ClientId);

	static bool CreateClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool DeleteClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool AssignClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool RemoveFromClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ClanLeaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool SetAuthLevelThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool RenameClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool SaveClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ShowTopClansThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);

	static bool LoadClansThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);

	std::shared_ptr<CClanResult> NewSqlClanResult(int ClientId);

public:
	CClanManager(CGameContext *pGameServer, CDbConnectionPool *pPool);
	~CClanManager();

	void CreateClan(int ClientId, const char *pClanName, int AccountId);
	void DeleteClan(int ClientId, int ClanId, int AccountId);
	void AssignClan(int ClientId, const char *AccountName, int ClanId, int AccountId);
	void RemoveFromClan(int ClientId, const char *AccountName, int ClanId);
	void ClanLeave(int ClientId);
	void SetAuthLevel(int ClientId, const char *AccountName, int NewAuthLevel, int ClanId);
	void RenameClan(int ClientId, int ClanId, const char *pNewClanName);
	void SaveClan(int ClientId, int ClanId);
	void ShowTopClans(int ClientId);

	void LoadAllClans();

	void OnClansLoaded(const std::vector<CClansData> &vClans);

	int GetClanIdByName(const char *pClanName);
	// I didn't find any other way to do that so.. fuck it
	void UpdatePlayerClan(int ClientId, int NewClanId, int AuthLevel);
	void ResetPlayersClan(int ClanId);

	std::string GetClanNameCopy(int ClanId) const;

	bool GetClanSnapshotById(int ClanId, CClansData &Out) const;

	void AddClanExp(int ClanId, int Amount);

	// Returns true if the clan exists and can be joined (basic existence check)
	bool IsClanJoinable(int ClanId) const;
};

#endif // BLOCKWORLDS_CLANS_H
