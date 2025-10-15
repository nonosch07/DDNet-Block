#ifndef BLOCKWORLDS_WHOIS_H
#define BLOCKWORLDS_WHOIS_H

#include <base/system.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/shared/protocol.h>

#include <atomic>
#include <memory>
#include <queue>
#include <string>
#include <vector>

class CGameContext;
class IDbConnection;
class IServer; // forward declaration for server interface

// Result object passed from SQL worker thread to main thread for printing
struct CWhoIsResult : ISqlResult
{
	CWhoIsResult() = default;

	int m_TargetClientId{-1};
	bool m_SendToChat{false}; // disabled: whois outputs should only go to rcon/log via Console()->Print

	char m_aTag[32]{"whois"};
	// Lines to print
	std::vector<std::string> m_vLines;
};

struct CSqlWhoIsLog : ISqlData
{
	CSqlWhoIsLog(std::shared_ptr<CWhoIsResult> pResult) :
		ISqlData(std::move(pResult)) {}

	char m_aIp[48]{}; // textual, no port
	char m_aName[24]{}; // current player name
	int m_AccountId{0}; // 0 if not logged in
	char m_aAccountName[16]{}; // empty if not logged in
	// source tag for event type: join|snapshot|leave (max 8 chars + NUL)
	char m_aSource[16]{"join"};
};

// Thread data for query
struct CSqlWhoIsQuery : ISqlData
{
	CSqlWhoIsQuery(std::shared_ptr<CWhoIsResult> pResult) :
		ISqlData(std::move(pResult)) {}

	// mode 0=ip, 1=name
	int m_Mode{0};
	// cutoff 0=/32, 1=/24, 2=/16, 3=/8 (applies to ip mode)
	int m_Cutoff{0};
	// search string (ip or name)
	char m_aSearch[64]{};
};

// Thread data for purge
struct CSqlWhoIsPurge : ISqlData
{
	CSqlWhoIsPurge(std::shared_ptr<CWhoIsResult> pResult) :
		ISqlData(std::move(pResult)) {}

	int m_RetentionMonths{0};
};

class CWhoIs
{
public:
	CWhoIs(CGameContext *pGameServer, CDbConnectionPool *pPool);

	// event logging
	void LogJoin(int ClientId);
	void LogLeave(int ClientId);
	void SnapshotTick(); // periodically snapshot all connected players

	// commands
	void CmdWhois(int RequesterId, int Mode, int Cutoff, int TargetClientId, std::shared_ptr<CWhoIsResult> pRes = nullptr);
	void CmdWhoisStr(int RequesterId, int Mode, int Cutoff, const char *pSearch, std::shared_ptr<CWhoIsResult> pRes = nullptr);

	// manual purge
	void PurgeNow(int RetentionMonths, bool Silent = false);

	// main thread: drain results and print
	void DrainAndPrintResults();

private:
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	bool GetClientIdentity(int ClientId, char *pOutIp, int OutIpSize, char *pOutName, int OutNameSize, int &OutAccId, char *pOutAccName, int OutAccNameSize);
	static void NormalizeIpNoPort(char *pIp);

	static bool ThreadLog(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize);
	static bool ThreadQuery(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize);
	static bool ThreadPurge(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize);

private:
	CGameContext *m_pGameServer{};
	CDbConnectionPool *m_pPool{};

	// last snapshot tick per client
	int64_t m_aLastSnapshotTick[MAX_CLIENTS]{};
	int64_t m_SnapshotIntervalTicks{0};
	int64_t m_NextPurgeTick{0};
	// internal queue for maintenance (purge) results only (whois query results managed externally by GameContext)
	std::vector<std::shared_ptr<CWhoIsResult>> m_vInternalResults;
};

#endif // BLOCKWORLDS_WHOIS_H
