#ifndef GAME_SERVER_BLOCKWORLDS_CLANMANAGER_H
#define GAME_SERVER_BLOCKWORLDS_CLANMANAGER_H

#include "engine/server/databases/connection_pool.h"
#include <memory>
#include <vector>

class IDbConnection;
class IServer;
class CGameContext;

struct CClansData
{
	int m_Id;
	char m_ClanName[12];
	int m_Level;
	int m_Experience;
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
		MAX_MESSAGES = 10,
	};

	char m_aaMessages[MAX_MESSAGES][512];
	char m_aBroadcast[1024];

	enum Variant
	{
		DIRECT,
		ALL,
		BROADCAST,
		DELETE,
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
			m_aBroadcast[0] = 0; // reset the broadcast message
			break;
		case DELETE:
			break;
		}
	}

	CClanResult();
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
	char m_aClanName[12];
	char m_aUsername[64];
	int m_AccountId;
	int m_ClientId;
	int m_ClanId;
	int m_NewAuthLevel;
	char m_aNewClanName[11];
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
		int ClanId = 0);

	bool RateLimitPlayer(int ClientId);

	static bool CreateClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool DeleteClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool AssignClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool RemoveFromClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ClanLeaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool SetAuthLevelThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool RenameClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);

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

	void LoadAllClans();

	void OnClansLoaded(const std::vector<CClansData> &vClans)
	{
		m_vClansData = vClans;
	}

	int GetClanIdByName(const char *pClanName);
	// I didn't find any other way to do that so.. fuck it
	void UpdatePlayerClan(int ClientId, int NewClanId);
	void ResetPlayersClan(int ClanId);

	const std::vector<CClansData> &GetClansData() const { return m_vClansData; }
};

#endif // GAME_SERVER_BLOCKWORLDS_CLANMANAGER_H
