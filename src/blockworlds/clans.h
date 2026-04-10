#ifndef BLOCKWORLDS_CLANS_H
#define BLOCKWORLDS_CLANS_H

#include "common.h"
#include "engine/server/databases/connection_pool.h"
#include "engine/shared/config.h"
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class IDbConnection;
class IServer;
class CGameContext;

struct CClansData
{
	int m_Id;
	char m_ClanName[BW_CLAN_NAME_BUFFER_SIZE];
	int m_Level;
	int m_Experience;

	int m_LastSavedTick;
	bool m_Dirty; // true if EXP/level changed since last successful save
};

// Maximum number of members in a clan (including leaders & co-leaders)
#define MAX_CLAN_MEMBERS (g_Config.m_SvClanMaxMembers)

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
		MAX_MESSAGES = 26,
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
		ACTION_UPDATE_PLAYER_BY_NAME, // update a player found by name
		ACTION_UPDATE_TWO_PLAYERS, // update two players (one by client id, one by name) atomically
		ACTION_RESET_CLAN_PLAYERS, // reset all players of a clan (set clan id to 0)
		ACTION_NOTIFY_CLAN_RENAME // notify members of a clan rename (provide old/new names)
	} m_Action = ACTION_NONE;

	int m_ActionClientId = -1; // for ACTION_UPDATE_PLAYER_BY_CLIENT
	int m_ActionNewClanId = 0; // target clan id for update actions
	int m_ActionNewAuthLevel = 0; // auth level for update actions
	char m_ActionPlayerName[64]; // for ACTION_UPDATE_PLAYER_BY_NAME
	// used for ACTION_UPDATE_TWO_PLAYERS: m_ActionClientId / m_ActionNewAuthLevel apply to one player (by client id)
	// while m_ActionPlayerName / m_ActionNewAuthLevel2 apply to the other (by name)
	int m_ActionNewAuthLevel2 = 0;
	int m_ActionResetClanId = 0; // for ACTION_RESET_CLAN_PLAYERS
	char m_ActionOldClanName[BW_CLAN_NAME_BUFFER_SIZE]{};
	char m_ActionNewClanName[BW_CLAN_NAME_BUFFER_SIZE]{};
	int m_ActionChargeClientId = -1; // client to charge on success
	int m_ActionChargeAmount = 0; // blockpoints to deduct on success
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
	char m_aClanName[BW_CLAN_NAME_BUFFER_SIZE];
	char m_aUsername[12]; // Match SQL schema varchar(11) + null terminator
	int m_AccountId;
	int m_ClientId;
	int m_ClanId;
	int m_NewAuthLevel;
	char m_aNewClanName[BW_CLAN_NAME_BUFFER_SIZE];
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
	std::vector<std::shared_ptr<ISqlResult>> *m_pShutdownCollector = nullptr;

	// true once the initial DB load has succeeded at least once
	bool m_ClansLoaded = false;
	// server tick of the most recent LoadAllClans dispatch (for retry throttle)
	int m_LastLoadAttemptTick = 0;

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
	static bool TransferLeadershipThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool RenameClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool SaveClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ShowTopClansThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);
	static bool ShowClanMembersThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize);

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
	void TransferLeadership(int ClientId, const char *AccountName, int ClanId);
	void RenameClan(int ClientId, int ClanId, const char *pNewClanName);
	void SaveClan(int ClientId, int ClanId);
	void ShowTopClans(int ClientId);
	void ShowClanMembers(int ClientId, int ClanId);

	void QueueBackgroundSave(int ClanId);

	void LoadAllClans();

	void OnClansLoaded(const std::vector<CClansData> &vClans);

	int GetClanIdByName(const char *pClanName);

	std::string GetClanNameCopy(int ClanId) const;

	bool GetClanSnapshotById(int ClanId, CClansData &Out) const;

	void AddClanExp(int ClanId, int Amount);

	void AutosaveTick();

	bool IsClansLoaded() const { return m_ClansLoaded; }

	bool IsClanJoinable(int ClanId) const;

	int SaveAllClansOnShutdown();
	void BeginShutdownCollection(std::vector<std::shared_ptr<ISqlResult>> &v) { m_pShutdownCollector = &v; }
	void EndShutdownCollection() { m_pShutdownCollector = nullptr; }
};

#endif // BLOCKWORLDS_CLANS_H
