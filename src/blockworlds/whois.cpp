#include "whois.h"

#include <base/system.h>
#include <engine/server.h>
#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "sql_prefix.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

static const char *TAG = "whois";

static std::atomic_bool g_WhoisSchemaInit{false};

static inline bool IsSqliteConnection(IDbConnection *pSql)
{
	const char *pIns = pSql->InsertIgnore();
	return pIns && str_comp(pIns, "INSERT OR IGNORE") == 0;
}

static void EnsureParentDirs(const char *pRelOrAbsPath)
{
	if(!pRelOrAbsPath || !*pRelOrAbsPath)
		return;
	fs_makedir_rec_for(pRelOrAbsPath);
}

static void ResolveWhoisDbPath(IStorage *pStorage, char *pOut, int OutSize)
{
	if(g_Config.m_SvWhoisDbPath[0] != '\0')
	{
		str_copy(pOut, g_Config.m_SvWhoisDbPath, OutSize);
		return;
	}

	char aAbs[IO_MAX_PATH_LENGTH] = {0};
	const char *pGot = pStorage->GetBinaryPathAbsolute("whois.sqlite", aAbs, sizeof(aAbs));
	if(pGot && aAbs[0])
		str_copy(pOut, aAbs, OutSize);
	else
		str_copy(pOut, "whois.sqlite", OutSize); // fallback to CWD
}

class CWhoisWorker
{
public:
	explicit CWhoisWorker(const char *pDbPath)
	{
		str_copy(m_aDbPath, pDbPath, sizeof(m_aDbPath));
	}
	~CWhoisWorker()
	{
		Stop();
	}

	void Start()
	{
		m_Shutdown = false;
		m_Thread = std::thread(&CWhoisWorker::Run, this);
	}
	void Stop()
	{
		{
			std::lock_guard<std::mutex> lg(m_Mtx);
			m_Shutdown = true;
		}
		m_Cv.notify_one();
		if(m_Thread.joinable())
			m_Thread.join();
	}

	void Enqueue(std::function<void(IDbConnection *)> Fn)
	{
		{
			std::lock_guard<std::mutex> lg(m_Mtx);
			m_Q.push(std::move(Fn));
		}
		m_Cv.notify_one();
	}

private:
	void Run()
	{
		EnsureParentDirs(m_aDbPath);
		std::unique_ptr<IDbConnection> pConn;
		char aErr[256] = {0};
		auto EnsureConn = [&]() -> IDbConnection * {
			if(!pConn)
				pConn = CreateSqliteConnection(m_aDbPath, false);
			if(!pConn)
				return nullptr;
			if(pConn->Connect(aErr, sizeof(aErr)))
				return nullptr;
			return pConn.get();
		};

		while(true)
		{
			std::function<void(IDbConnection *)> task;
			{
				std::unique_lock<std::mutex> lk(m_Mtx);
				m_Cv.wait(lk, [&] { return m_Shutdown || !m_Q.empty(); });
				if(m_Shutdown && m_Q.empty())
					break;
				task = std::move(m_Q.front());
				m_Q.pop();
			}
			IDbConnection *p = EnsureConn();
			if(p)
			{
				task(p);
				p->Disconnect();
			}
			else
			{
				dbg_msg("sql", "whois worker: failed to open sqlite: %s", aErr[0] ? aErr : "unknown error");
			}
		}
	}

	char m_aDbPath[IO_MAX_PATH_LENGTH]{};
	std::thread m_Thread;
	std::mutex m_Mtx;
	std::condition_variable m_Cv;
	std::queue<std::function<void(IDbConnection *)>> m_Q;
	bool m_Shutdown{false};
};

static bool ExecRaw(IDbConnection *pSql, const char *pStmt, char *pError, int ErrorSize)
{
	if(pSql->PrepareStatement(pStmt, pError, ErrorSize))
		return true;
	int Rows = 0;
	if(pSql->ExecuteUpdate(&Rows, pError, ErrorSize))
		return true;
	return false;
}

static bool EnsureWhoisSchema(IDbConnection *pSql, char *pError, int ErrorSize)
{
	// olny create whois schema on the dedicated SQLite fuckery
	if(!IsSqliteConnection(pSql))
	{
		str_copy(pError, "skip non-sqlite backend for whois", ErrorSize);
		return true; // signal worker to try next read DB
	}
	char aStmt[1024];
	str_format(aStmt, sizeof(aStmt),
		"CREATE TABLE IF NOT EXISTS %s ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  ip TEXT NOT NULL,"
		"  name TEXT NOT NULL,"
		"  account_id INTEGER NOT NULL DEFAULT 0,"
		"  source TEXT NOT NULL,"
		"  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP"
		")",
		TBL_WHOIS_CONNECTIONS);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;

	str_format(aStmt, sizeof(aStmt), "CREATE INDEX IF NOT EXISTS whois_idx_ip ON %s(ip)", TBL_WHOIS_CONNECTIONS);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;
	str_format(aStmt, sizeof(aStmt), "CREATE INDEX IF NOT EXISTS whois_idx_name ON %s(name)", TBL_WHOIS_CONNECTIONS);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;
	str_format(aStmt, sizeof(aStmt), "CREATE INDEX IF NOT EXISTS whois_idx_ip_name ON %s(ip,name)", TBL_WHOIS_CONNECTIONS);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;
	str_format(aStmt, sizeof(aStmt), "CREATE INDEX IF NOT EXISTS whois_idx_name_ip ON %s(name,ip)", TBL_WHOIS_CONNECTIONS);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;
	str_format(aStmt, sizeof(aStmt), "CREATE INDEX IF NOT EXISTS whois_idx_accid ON %s(account_id)", TBL_WHOIS_CONNECTIONS);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;
	str_format(aStmt, sizeof(aStmt), "CREATE INDEX IF NOT EXISTS whois_idx_created_at ON %s(created_at)", TBL_WHOIS_CONNECTIONS);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;

	// aggregates: names by IP
	str_format(aStmt, sizeof(aStmt),
		"CREATE TABLE IF NOT EXISTS %s ("
		"  ip TEXT NOT NULL,"
		"  name TEXT NOT NULL,"
		"  cnt INTEGER NOT NULL,"
		"  first_seen DATETIME NOT NULL,"
		"  last_seen DATETIME NOT NULL,"
		"  has_logins INTEGER NOT NULL DEFAULT 0,"
		"  last_acc_name TEXT,"
		"  PRIMARY KEY(ip, name)"
		")",
		TBL_WHOIS_AGG_NAMES_BY_IP);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;
	str_format(aStmt, sizeof(aStmt), "CREATE INDEX IF NOT EXISTS whois_names_by_ip_idx_ip ON %s(ip)", TBL_WHOIS_AGG_NAMES_BY_IP);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;

	str_format(aStmt, sizeof(aStmt),
		"CREATE TABLE IF NOT EXISTS %s ("
		"  name TEXT NOT NULL,"
		"  ip TEXT NOT NULL,"
		"  cnt INTEGER NOT NULL,"
		"  first_seen DATETIME NOT NULL,"
		"  last_seen DATETIME NOT NULL,"
		"  PRIMARY KEY(name, ip)"
		")",
		TBL_WHOIS_AGG_IPS_BY_NAME);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;
	str_format(aStmt, sizeof(aStmt), "CREATE INDEX IF NOT EXISTS whois_ips_by_name_idx_name ON %s(name)", TBL_WHOIS_AGG_IPS_BY_NAME);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;

	return false;
}

CWhoIs::CWhoIs(CGameContext *pGameServer, CDbConnectionPool *pPool) :
	m_pGameServer(pGameServer), m_pPool(pPool)
{
	mem_zero(m_aLastSnapshotTick, sizeof(m_aLastSnapshotTick));
	int Minutes = clamp(g_Config.m_SvWhoisSnapshotMinutes, 0, 24 * 60);
	if(Minutes <= 0)
		m_SnapshotIntervalTicks = 0; // disabled
	else
		m_SnapshotIntervalTicks = (int64_t)pGameServer->Server()->TickSpeed() * 60 * Minutes; // minutes
	m_NextPurgeTick = 0;

	ResolveWhoisDbPath(GameServer()->Storage(), m_aDbPath, sizeof(m_aDbPath));
	EnsureParentDirs(m_aDbPath);
	m_pWorker = std::make_unique<CWhoisWorker>(m_aDbPath);
	m_pWorker->Start();
	dbg_msg("sql", "whois sqlite path: %s", m_aDbPath);
}

IServer *CWhoIs::Server() const { return GameServer()->Server(); }

bool CWhoIs::GetClientIdentity(int ClientId, char *pOutIp, int OutIpSize, char *pOutName, int OutNameSize, int &OutAccId, char *pOutAccName, int OutAccNameSize)
{
	if(!Server()->ClientIngame(ClientId) || !GameServer()->m_apPlayers[ClientId])
		return false;
	Server()->GetClientAddr(ClientId, pOutIp, OutIpSize);
	NormalizeIpNoPort(pOutIp);
	str_copy(pOutName, Server()->ClientName(ClientId), OutNameSize);
	CPlayer *pPl = GameServer()->m_apPlayers[ClientId];
	OutAccId = pPl->IsLoggedIn() ? pPl->m_Account.m_Id : 0;
	if(pPl->IsLoggedIn())
		str_copy(pOutAccName, pPl->m_Account.m_aName, OutAccNameSize);
	else
		pOutAccName[0] = '\0';
	return true;
}

static void NormalizeIpNoPortHelper(char *pIp)
{
	if(!pIp || !*pIp)
		return;
	if(pIp[0] == '[')
	{
		const char *pEndC = str_find(pIp, "]");
		if(pEndC)
		{
			char *pEnd = const_cast<char *>(pEndC);
			*pEnd = '\0';
			size_t len = str_length(pIp + 1);
			mem_move(pIp, pIp + 1, len + 1); // includes the NUL
			return;
		}
	}
	const char *pColonConst = str_rchr(pIp, ':');
	if(pColonConst)
	{
		int numColons = 0;
		for(const char *p = pIp; *p; ++p)
			if(*p == ':')
				numColons++;
		if(numColons == 1)
		{
			char *pColon = const_cast<char *>(pColonConst);
			*pColon = '\0';
		}
	}
}

void CWhoIs::NormalizeIpNoPort(char *pIp)
{
	if(!pIp || !*pIp)
		return;
	// handle IPv6 bracketed address like [::1]:8303
	if(pIp[0] == '[')
	{
		const char *pEndC = str_find(pIp, "]");
		if(pEndC)
		{
			char *pEnd = const_cast<char *>(pEndC);
			*pEnd = '\0';
			size_t len = str_length(pIp + 1);
			mem_move(pIp, pIp + 1, len + 1); // includes the NUL
			return;
		}
	}
	const char *pColonConst = str_rchr(pIp, ':');
	if(pColonConst)
	{
		int numColons = 0;
		for(const char *p = pIp; *p; ++p)
			if(*p == ':')
				numColons++;
		if(numColons == 1)
		{
			char *pColon = const_cast<char *>(pColonConst);
			*pColon = '\0';
		}
	}
}

static bool RunLog(IDbConnection *pSql, const CSqlWhoIsLog *pReq, char *pError, int ErrorSize)
{
	char aStmt[512];
	if(!g_WhoisSchemaInit.load(std::memory_order_acquire))
	{
		if(EnsureWhoisSchema(pSql, pError, ErrorSize))
		{
			dbg_msg("sql", "whois: schema ensure failed: %s", pError[0] ? pError : "unknown error");
			return true;
		}
		g_WhoisSchemaInit.store(true, std::memory_order_release);
	}
	str_format(aStmt, sizeof(aStmt),
		"INSERT INTO %s (ip, name, account_id, source) VALUES (?, ?, ?, ?)",
		TBL_WHOIS_CONNECTIONS);
	if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	pSql->BindString(1, pReq->m_aIp);
	pSql->BindString(2, pReq->m_aName);
	pSql->BindInt(3, pReq->m_AccountId);
	pSql->BindString(4, pReq->m_aSource);
	int Affected = 0;
	if(pSql->ExecuteUpdate(&Affected, pError, ErrorSize))
		return true;

	str_format(aStmt, sizeof(aStmt),
		"INSERT INTO %s (ip, name, cnt, first_seen, last_seen, has_logins, last_acc_name) "
		"VALUES (?, ?, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, CASE WHEN ? <> 0 THEN 1 ELSE 0 END, CASE WHEN ? <> 0 THEN ? ELSE NULL END) "
		"ON CONFLICT(ip, name) DO UPDATE SET "
		"cnt = cnt + 1, "
		"last_seen = MAX(last_seen, excluded.last_seen), "
		"first_seen = MIN(first_seen, excluded.first_seen), "
		"has_logins = CASE WHEN excluded.has_logins = 1 THEN 1 ELSE has_logins END, "
		"last_acc_name = COALESCE(excluded.last_acc_name, last_acc_name)",
		TBL_WHOIS_AGG_NAMES_BY_IP);
	if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	pSql->BindString(1, pReq->m_aIp);
	pSql->BindString(2, pReq->m_aName);
	pSql->BindInt(3, pReq->m_AccountId);
	pSql->BindInt(4, pReq->m_AccountId);
	pSql->BindString(5, pReq->m_aAccountName);
	Affected = 0;
	if(pSql->ExecuteUpdate(&Affected, pError, ErrorSize))
		return true;

	str_format(aStmt, sizeof(aStmt),
		"INSERT INTO %s (name, ip, cnt, first_seen, last_seen) "
		"VALUES (?, ?, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP) "
		"ON CONFLICT(name, ip) DO UPDATE SET "
		"cnt = cnt + 1, "
		"last_seen = MAX(last_seen, excluded.last_seen), "
		"first_seen = MIN(first_seen, excluded.first_seen)",
		TBL_WHOIS_AGG_IPS_BY_NAME);
	if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	pSql->BindString(1, pReq->m_aName);
	pSql->BindString(2, pReq->m_aIp);
	Affected = 0;
	if(pSql->ExecuteUpdate(&Affected, pError, ErrorSize))
		return true;

	return false;
}

static void PrintLines(CGameContext *pGame, int TargetClientId, const std::vector<std::string> &vLines, const char *pTag, bool SendToChat)
{
	(void)TargetClientId;
	(void)SendToChat; // was for testing purposes
	for(const auto &s : vLines)
	{
		pGame->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pTag, s.c_str());
	}
}

static bool RunQuery(IDbConnection *pSql, const CSqlWhoIsQuery *pReq, std::shared_ptr<CWhoIsResult> pRes, char *pError, int ErrorSize)
{
	char aStmt[1024];
	if(!g_WhoisSchemaInit.load(std::memory_order_acquire))
	{
		if(EnsureWhoisSchema(pSql, pError, ErrorSize))
		{
			dbg_msg("sql", "whois: schema ensure failed (query): %s", pError[0] ? pError : "unknown error");
			return true;
		}
		g_WhoisSchemaInit.store(true, std::memory_order_release);
	}

	const bool ByIp = pReq->m_Mode == 0;
	int Cut = pReq->m_Cutoff;

	if(ByIp)
	{
		char aLike[64];
		char aIp[64];
		str_copy(aIp, pReq->m_aSearch, sizeof(aIp));
		{
			const char *pSlash = str_find(aIp, "/");
			if(pSlash)
			{
				int bits = str_toint(pSlash + 1);
				if(bits == 24)
					Cut = 1;
				else if(bits == 16)
					Cut = 2;
				else if(bits == 8)
					Cut = 3;
				// terminate IP before slash or optional preceding space
				size_t cutPos = (pSlash > aIp && pSlash[-1] == ' ') ? (size_t)(pSlash - aIp - 1) : (size_t)(pSlash - aIp);
				aIp[cutPos] = '\0';
			}
		}
		NormalizeIpNoPortHelper(aIp);
		if(Cut <= 0)
		{
			// exact IP: compute on-the-fly from raw connections
			int MaxRows = clamp(g_Config.m_SvWhoisMaxRows, 10, 100000);
			str_format(aStmt, sizeof(aStmt),
				"SELECT name, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen,"
				" CASE WHEN SUM(CASE WHEN account_id <> 0 THEN 1 ELSE 0 END) > 0 THEN 1 ELSE 0 END AS has_logins "
				"FROM %s WHERE ip = ? GROUP BY name ORDER BY cnt DESC LIMIT %d",
				TBL_WHOIS_CONNECTIONS, MaxRows);
			if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
				return true;
			pSql->BindString(1, aIp);

			bool End = false;
			int Total = 0;
			int Distinct = 0;
			struct Entry
			{
				char Name[32];
				int Cnt;
				char First[20];
				char Last[20];
				int HasLogins;
				char AccName[32];
			};
			std::vector<Entry> vEntries;
			char aGlobalFirst[20] = {0};
			char aGlobalLast[20] = {0};
			while(!pSql->Step(&End, pError, ErrorSize) && !End)
			{
				Entry e{};
				pSql->GetString(1, e.Name, sizeof(e.Name));
				e.Cnt = pSql->GetInt(2);
				mem_zero(e.First, sizeof(e.First));
				if(!pSql->IsNull(3))
					pSql->GetString(3, e.First, sizeof(e.First));
				mem_zero(e.Last, sizeof(e.Last));
				if(!pSql->IsNull(4))
					pSql->GetString(4, e.Last, sizeof(e.Last));
				e.HasLogins = pSql->GetInt(5);
				e.AccName[0] = '\0'; // no account name in raw table
				vEntries.push_back(e);
				Total += e.Cnt;
				// track global first/last
				if(e.First[0] && (!aGlobalFirst[0] || str_comp(e.First, aGlobalFirst) < 0))
					str_copy(aGlobalFirst, e.First, sizeof(aGlobalFirst));
				if(e.Last[0] && (!aGlobalLast[0] || str_comp(e.Last, aGlobalLast) > 0))
					str_copy(aGlobalLast, e.Last, sizeof(aGlobalLast));
			}
			Distinct = (int)vEntries.size();
			char aHead[256];
			if(aGlobalFirst[0] || aGlobalLast[0])
				str_format(aHead, sizeof(aHead), "%s connected %d times with %d names (first: %s, last: %s):", aIp, Total, Distinct, aGlobalFirst[0] ? aGlobalFirst : "-", aGlobalLast[0] ? aGlobalLast : "-");
			else
				str_format(aHead, sizeof(aHead), "%s connected %d times with %d names:", aIp, Total, Distinct);
			pRes->m_vLines.emplace_back(aHead);
			for(const auto &e : vEntries)
			{
				char aTmp[256];
				char aDetails[160] = {0};
				if(e.First[0] && e.Last[0])
					str_format(aDetails, sizeof(aDetails), "first: %s, last: %s", e.First, e.Last);
				else if(e.Last[0])
					str_format(aDetails, sizeof(aDetails), "last: %s", e.Last);
				char aAccPart[80] = {0};
				if(e.HasLogins > 0)
					str_copy(aAccPart, ", logins: yes", sizeof(aAccPart));
				if(aDetails[0])
					str_format(aTmp, sizeof(aTmp), " - %s (%d), %s%s", e.Name, e.Cnt, aDetails, aAccPart);
				else
					str_format(aTmp, sizeof(aTmp), " - %s (%d)%s", e.Name, e.Cnt, aAccPart);
				pRes->m_vLines.emplace_back(aTmp);
			}
			// footer if we likely hit the cap
			if((int)vEntries.size() >= MaxRows)
			{
				char aFoot[160];
				str_format(aFoot, sizeof(aFoot), "[truncated] Results capped at sv_whois_max_rows=%d. Refine query or raise cap.", MaxRows);
				pRes->m_vLines.emplace_back(aFoot);
			}
		}
		else
		{
			// /8, /24 or /16: cut trailing octets and do prefix match. Fallback to exact match
			int dots = 0;
			int lastDotPos[4] = {-1, -1, -1, -1};
			for(int i = 0; aIp[i]; ++i)
				if(aIp[i] == '.')
				{
					if(dots < 4)
						lastDotPos[dots] = i;
					dots++;
				}

			bool CanCut = (Cut == 1 && dots >= 3) || (Cut == 2 && dots >= 2) || (Cut == 3 && dots >= 1);
			if(CanCut)
			{
				int cutPos = (Cut == 1) ? lastDotPos[2] : (Cut == 2 ? lastDotPos[1] : lastDotPos[0]);
				if(cutPos > 0)
				{
					aIp[cutPos] = '\0';
				}
				str_format(aLike, sizeof(aLike), "%s.%%", aIp);
				str_format(aStmt, sizeof(aStmt),
					"SELECT ip, name, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen,"
					" CASE WHEN SUM(CASE WHEN account_id <> 0 THEN 1 ELSE 0 END) > 0 THEN 1 ELSE 0 END AS has_logins "
					"FROM %s WHERE ip LIKE ? GROUP BY ip, name ORDER BY ip ASC, cnt DESC",
					TBL_WHOIS_CONNECTIONS);
				if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
					return true;
				pSql->BindString(1, aLike);
			}
			else
			{
				// fallback: exact IP query shaped like prefix result (ip as first column)
				int MaxRows2 = clamp(g_Config.m_SvWhoisMaxRows, 10, 100000);
				str_format(aStmt, sizeof(aStmt),
					"SELECT ip, name, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen,"
					" CASE WHEN SUM(CASE WHEN account_id <> 0 THEN 1 ELSE 0 END) > 0 THEN 1 ELSE 0 END AS has_logins "
					"FROM %s WHERE ip = ? GROUP BY ip, name ORDER BY cnt DESC LIMIT %d",
					TBL_WHOIS_CONNECTIONS, MaxRows2);
				if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
					return true;
				pSql->BindString(1, aIp);
			}

			bool End = false;
			std::string curIp;
			int curTotal = 0;
			struct NameInfo
			{
				std::string Name;
				int Cnt;
				char First[20];
				char Last[20];
				int HasLogins;
				char AccName[32];
			};
			std::vector<NameInfo> curNames;
			const int MaxIps = clamp(g_Config.m_SvWhoisPrefixMaxIps, 1, 100000);
			const int MaxNamesPerIp = clamp(g_Config.m_SvWhoisPrefixNamesPerIp, 1, 100000);
			bool TruncatedIps = false;
			bool TruncatedNames = false;
			auto FlushCur = [&]() {
				if(curIp.empty())
					return;
				char aHead[256];
				// derive group first/last
				char aFirst[20] = {0};
				char aLast[20] = {0};
				for(const auto &nn : curNames)
				{
					if(nn.First[0] && (!aFirst[0] || str_comp(nn.First, aFirst) < 0))
						str_copy(aFirst, nn.First, sizeof(aFirst));
					if(nn.Last[0] && (!aLast[0] || str_comp(nn.Last, aLast) > 0))
						str_copy(aLast, nn.Last, sizeof(aLast));
				}
				if(aFirst[0] || aLast[0])
					str_format(aHead, sizeof(aHead), "%s connected %d times with %d names (first: %s, last: %s):", curIp.c_str(), curTotal, (int)curNames.size(), aFirst[0] ? aFirst : "-", aLast[0] ? aLast : "-");
				else
					str_format(aHead, sizeof(aHead), "%s connected %d times with %d names:", curIp.c_str(), curTotal, (int)curNames.size());
				pRes->m_vLines.emplace_back(aHead);
				int shown = 0;
				for(const auto &nn : curNames)
				{
					char aTmp[256];
					char aDetails[160] = {0};
					if(nn.First[0] && nn.Last[0])
						str_format(aDetails, sizeof(aDetails), "first: %s, last: %s", nn.First, nn.Last);
					else if(nn.Last[0])
						str_format(aDetails, sizeof(aDetails), "last: %s", nn.Last);
					char aAccPart[64] = {0};
					if(nn.AccName[0])
						str_format(aAccPart, sizeof(aAccPart), ", account: %s", nn.AccName);
					else if(nn.HasLogins > 0)
						str_copy(aAccPart, ", logins: yes", sizeof(aAccPart));

					if(aDetails[0])
						str_format(aTmp, sizeof(aTmp), " - %s (%d), %s%s", nn.Name.c_str(), nn.Cnt, aDetails, aAccPart);
					else
						str_format(aTmp, sizeof(aTmp), " - %s (%d)%s", nn.Name.c_str(), nn.Cnt, aAccPart);
					pRes->m_vLines.emplace_back(aTmp);
					if(++shown >= MaxNamesPerIp)
					{
						TruncatedNames = true;
						break;
					}
				}
				curIp.clear();
				curTotal = 0;
				curNames.clear();
			};

			int IpGroups = 0;
			while(!pSql->Step(&End, pError, ErrorSize) && !End)
			{
				char aResIp[64] = {0};
				char aName[32] = {0};
				pSql->GetString(1, aResIp, sizeof(aResIp));
				pSql->GetString(2, aName, sizeof(aName));
				int Cnt = pSql->GetInt(3);
				char aFirstShort[20] = {0};
				if(!pSql->IsNull(4))
					pSql->GetString(4, aFirstShort, sizeof(aFirstShort));
				char aLastShort[20] = {0};
				if(!pSql->IsNull(5))
					pSql->GetString(5, aLastShort, sizeof(aLastShort));
				int HasLogins = pSql->GetInt(6);
				if(curIp.empty())
					curIp = aResIp;
				if(curIp != aResIp)
				{
					FlushCur();
					curIp = aResIp;
					if(++IpGroups >= MaxIps)
					{
						TruncatedIps = true;
						break;
					}
				}
				curTotal += Cnt;
				NameInfo ni{};
				ni.Name = aName;
				ni.Cnt = Cnt;
				ni.HasLogins = HasLogins;
				ni.First[0] = '\0';
				ni.Last[0] = '\0';
				ni.AccName[0] = '\0';
				if(aFirstShort[0])
					str_copy(ni.First, aFirstShort, sizeof(ni.First));
				if(aLastShort[0])
					str_copy(ni.Last, aLastShort, sizeof(ni.Last));
				// leave AccName empty when using raw table
				curNames.emplace_back(std::move(ni));
			}
			FlushCur();
			if(TruncatedIps || TruncatedNames)
			{
				char aFoot[192];
				if(TruncatedIps && TruncatedNames)
					str_format(aFoot, sizeof(aFoot), "[truncated] Reached limits: ips=%d, names_per_ip=%d. Refine CIDR or raise caps.", MaxIps, MaxNamesPerIp);
				else if(TruncatedIps)
					str_format(aFoot, sizeof(aFoot), "[truncated] Reached limit ips=%d. Refine CIDR or raise cap.", MaxIps);
				else
					str_format(aFoot, sizeof(aFoot), "[truncated] Reached limit names_per_ip=%d. Refine CIDR or raise cap.", MaxNamesPerIp);
				pRes->m_vLines.emplace_back(aFoot);
			}

			if(curIp.empty() && pRes->m_vLines.empty())
			{
				char aMsg[128];
				str_format(aMsg, sizeof(aMsg), "No entries found for IP '%s'", pReq->m_aSearch);
				pRes->m_vLines.emplace_back(aMsg);
			}
		}
	}
	else
	{
		int MaxRows3 = clamp(g_Config.m_SvWhoisMaxRows, 10, 100000);
		bool Wildcard = (str_find(pReq->m_aSearch, "*") != nullptr) || (str_find(pReq->m_aSearch, "%") != nullptr);
		char aPattern[32];
		if(Wildcard)
		{
			// translate '*' -> '%'
			int k = 0;
			for(int i = 0; pReq->m_aSearch[i] && k < (int)sizeof(aPattern) - 1; ++i)
			{
				char c = pReq->m_aSearch[i];
				if(c == '*')
					c = '%';
				aPattern[k++] = c;
			}
			aPattern[k] = '\0';
			str_format(aStmt, sizeof(aStmt),
				"SELECT ip, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen "
				"FROM %s WHERE name LIKE ? GROUP BY ip ORDER BY cnt DESC LIMIT %d",
				TBL_WHOIS_CONNECTIONS, MaxRows3);
			if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
				return true;
			pSql->BindString(1, aPattern);
		}
		else
		{
			str_format(aStmt, sizeof(aStmt),
				"SELECT ip, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen "
				"FROM %s WHERE name = ? GROUP BY ip ORDER BY cnt DESC LIMIT %d",
				TBL_WHOIS_CONNECTIONS, MaxRows3);
			if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
				return true;
			pSql->BindString(1, pReq->m_aSearch);
		}

		bool End = false;
		int Total = 0;
		struct IPCnt
		{
			char Ip[64];
			int Cnt;
			char First[20];
			char Last[20];
		};
		std::vector<IPCnt> vEntries;
		char aGlobalFirst[20] = {0};
		char aGlobalLast[20] = {0};
		while(!pSql->Step(&End, pError, ErrorSize) && !End)
		{
			IPCnt e{};
			pSql->GetString(1, e.Ip, sizeof(e.Ip));
			e.Cnt = pSql->GetInt(2);
			mem_zero(e.First, sizeof(e.First));
			if(!pSql->IsNull(3))
				pSql->GetString(3, e.First, sizeof(e.First));
			mem_zero(e.Last, sizeof(e.Last));
			if(!pSql->IsNull(4))
				pSql->GetString(4, e.Last, sizeof(e.Last));
			vEntries.push_back(e);
			Total += e.Cnt;
			if(e.First[0] && (!aGlobalFirst[0] || str_comp(e.First, aGlobalFirst) < 0))
				str_copy(aGlobalFirst, e.First, sizeof(aGlobalFirst));
			if(e.Last[0] && (!aGlobalLast[0] || str_comp(e.Last, aGlobalLast) > 0))
				str_copy(aGlobalLast, e.Last, sizeof(aGlobalLast));
		}
		int Distinct = (int)vEntries.size();
		char aHead[256];
		if(aGlobalFirst[0] || aGlobalLast[0])
			str_format(aHead, sizeof(aHead), "%s connected %d times with %d ips (first: %s, last: %s):", pReq->m_aSearch, Total, Distinct, aGlobalFirst[0] ? aGlobalFirst : "-", aGlobalLast[0] ? aGlobalLast : "-");
		else
			str_format(aHead, sizeof(aHead), "%s connected %d times with %d ips:", pReq->m_aSearch, Total, Distinct);
		pRes->m_vLines.emplace_back(aHead);
		for(const auto &e : vEntries)
		{
			char aTmp[160];
			if(e.First[0] && e.Last[0])
				str_format(aTmp, sizeof(aTmp), " - %s (%d), first: %s, last: %s", e.Ip, e.Cnt, e.First, e.Last);
			else if(e.Last[0])
				str_format(aTmp, sizeof(aTmp), " - %s (%d), last: %s", e.Ip, e.Cnt, e.Last);
			else
				str_format(aTmp, sizeof(aTmp), " - %s (%d)", e.Ip, e.Cnt);
			pRes->m_vLines.emplace_back(aTmp);
		}
		// footer if we likely hit the cap
		if((int)vEntries.size() >= MaxRows3)
		{
			char aFoot[160];
			str_format(aFoot, sizeof(aFoot), "[truncated] Results capped at sv_whois_max_rows=%d. Refine query or raise cap.", MaxRows3);
			pRes->m_vLines.emplace_back(aFoot);
		}
		if(Distinct == 0)
		{
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "No entries found for name '%s'", pReq->m_aSearch);
			pRes->m_vLines.emplace_back(aMsg);
		}
	}

	pRes->m_Completed.store(true, std::memory_order_release);
	pRes->m_Success = true;
	return false;
}

void CWhoIs::LogJoin(int ClientId)
{
	char aIp[48] = {0}, aName[24] = {0}, aAcc[16] = {0};
	int AccId = 0;
	if(!GetClientIdentity(ClientId, aIp, sizeof(aIp), aName, sizeof(aName), AccId, aAcc, sizeof(aAcc)))
		return;
	auto pReq = std::make_unique<CSqlWhoIsLog>(nullptr);
	str_copy(pReq->m_aIp, aIp, sizeof(pReq->m_aIp));
	str_copy(pReq->m_aName, aName, sizeof(pReq->m_aName));
	pReq->m_AccountId = AccId;
	str_copy(pReq->m_aAccountName, aAcc, sizeof(pReq->m_aAccountName));
	str_copy(pReq->m_aSource, "join", sizeof(pReq->m_aSource));
	auto *pRaw = pReq.release();
	m_pWorker->Enqueue([pRaw](IDbConnection *pSql) {
		char aErr[256] = {0};
		RunLog(pSql, pRaw, aErr, sizeof(aErr));
		delete pRaw;
	});
	m_aLastSnapshotTick[ClientId] = Server()->Tick();
}

void CWhoIs::LogLeave(int ClientId)
{
	char aIp[48] = {0}, aName[24] = {0}, aAcc[16] = {0};
	int AccId = 0;
	// best effort, player object may already be gone when called from drop; get name from server
	Server()->GetClientAddr(ClientId, aIp, sizeof(aIp));
	NormalizeIpNoPort(aIp);
	str_copy(aName, Server()->ClientName(ClientId), sizeof(aName));
	if(GameServer()->m_apPlayers[ClientId] && GameServer()->m_apPlayers[ClientId]->IsLoggedIn())
	{
		AccId = GameServer()->m_apPlayers[ClientId]->m_Account.m_Id;
		str_copy(aAcc, GameServer()->m_apPlayers[ClientId]->m_Account.m_aName, sizeof(aAcc));
	}
	auto pReq = std::make_unique<CSqlWhoIsLog>(nullptr);
	str_copy(pReq->m_aIp, aIp, sizeof(pReq->m_aIp));
	str_copy(pReq->m_aName, aName, sizeof(pReq->m_aName));
	pReq->m_AccountId = AccId;
	str_copy(pReq->m_aAccountName, aAcc, sizeof(pReq->m_aAccountName));
	str_copy(pReq->m_aSource, "leave", sizeof(pReq->m_aSource));
	auto *pRaw = pReq.release();
	m_pWorker->Enqueue([pRaw](IDbConnection *pSql) {
		char aErr[256] = {0};
		RunLog(pSql, pRaw, aErr, sizeof(aErr));
		delete pRaw;
	});
}

void CWhoIs::SnapshotTick()
{
	int64_t Now = Server()->Tick();
	int MinutesCfg = clamp(g_Config.m_SvWhoisSnapshotMinutes, 0, 24 * 60);
	int64_t IntervalTicks = MinutesCfg > 0 ? (int64_t)Server()->TickSpeed() * 60 * MinutesCfg : 0;
	// daily purge scheduling based on config
	if(m_NextPurgeTick == 0)
	{
		m_NextPurgeTick = Now; // run immediately on first tick
	}
	if(Now >= m_NextPurgeTick)
	{
		int Months = g_Config.m_SvWhoisRetentionMonths;
		if(Months > 0)
		{
			auto pRes = std::make_shared<CWhoIsResult>();
			str_copy(pRes->m_aTag, TAG, sizeof(pRes->m_aTag));
			pRes->m_SendToChat = false;
			m_vInternalResults.push_back(pRes);
			m_pWorker->Enqueue([pRes, Months](IDbConnection *pSql) {
				char aStmt[512];
				char aErr[256] = {0};
				bool Failed = false;
				if(!g_WhoisSchemaInit.load(std::memory_order_acquire))
				{
					Failed = EnsureWhoisSchema(pSql, aErr, sizeof(aErr));
					if(!Failed)
						g_WhoisSchemaInit.store(true, std::memory_order_release);
				}
				int64_t Seconds = (int64_t)Months * 30 * 24 * 60 * 60;
				int Rows = 0;
				if(!Failed)
				{
					str_format(aStmt, sizeof(aStmt),
						"DELETE FROM %s WHERE (strftime('%%s', created_at)) < (strftime('%%s','now') - %" PRId64 ")",
						TBL_WHOIS_CONNECTIONS, Seconds);
					Failed = pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || pSql->ExecuteUpdate(&Rows, aErr, sizeof(aErr));
				}
				if(!Failed)
				{
					int Tmp = 0;
					str_format(aStmt, sizeof(aStmt),
						"DELETE FROM %s WHERE (strftime('%%s', last_seen)) < (strftime('%%s','now') - %" PRId64 ")",
						TBL_WHOIS_AGG_NAMES_BY_IP, Seconds);
					Failed = pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || pSql->ExecuteUpdate(&Tmp, aErr, sizeof(aErr));
					str_format(aStmt, sizeof(aStmt),
						"DELETE FROM %s WHERE (strftime('%%s', last_seen)) < (strftime('%%s','now') - %" PRId64 ")",
						TBL_WHOIS_AGG_IPS_BY_NAME, Seconds);
					Tmp = 0;
					Failed = Failed || pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || pSql->ExecuteUpdate(&Tmp, aErr, sizeof(aErr));
				}
				if(!Failed)
				{
					char aLine[128];
					str_format(aLine, sizeof(aLine), "purge removed %d old whois rows (>%d months)", Rows, Months);
					pRes->m_vLines.emplace_back(aLine);
					pRes->m_Success = true;
				}
				else
				{
					char aLine[192];
					str_format(aLine, sizeof(aLine), "whois purge failed: %s", aErr[0] ? aErr : "unknown error");
					pRes->m_vLines.emplace_back(aLine);
					pRes->m_Success = false;
				}
				pRes->m_Completed.store(true, std::memory_order_release);
			});
		}
		m_NextPurgeTick = Now + Server()->TickSpeed() * 60 * 60 * 24;
	}

	if(IntervalTicks > 0)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!Server()->ClientIngame(i))
				continue;
			if(Now - m_aLastSnapshotTick[i] < IntervalTicks)
				continue;
			char aIp[48] = {0}, aName[24] = {0}, aAcc[16] = {0};
			int AccId = 0;
			if(!GetClientIdentity(i, aIp, sizeof(aIp), aName, sizeof(aName), AccId, aAcc, sizeof(aAcc)))
				continue;
			auto pReq = std::make_unique<CSqlWhoIsLog>(nullptr);
			str_copy(pReq->m_aIp, aIp, sizeof(pReq->m_aIp));
			str_copy(pReq->m_aName, aName, sizeof(pReq->m_aName));
			pReq->m_AccountId = AccId;
			str_copy(pReq->m_aAccountName, aAcc, sizeof(pReq->m_aAccountName));
			str_copy(pReq->m_aSource, "snapshot", sizeof(pReq->m_aSource));
			auto *pRaw = pReq.release();
			m_pWorker->Enqueue([pRaw](IDbConnection *pSql) {
				char aErr[256] = {0};
				RunLog(pSql, pRaw, aErr, sizeof(aErr));
				delete pRaw;
			});
			m_aLastSnapshotTick[i] = Now;
		}
	}
}

void CWhoIs::CmdWhoisStr(int RequesterId, int Mode, int Cutoff, const char *pSearch, std::shared_ptr<CWhoIsResult> pExisting)
{
	std::shared_ptr<CWhoIsResult> pRes = pExisting ? pExisting : std::make_shared<CWhoIsResult>();
	pRes->m_TargetClientId = RequesterId; // retained for potential future use
	str_copy(pRes->m_aTag, TAG, sizeof(pRes->m_aTag));
	pRes->m_SendToChat = false; // enforce no chat output
	auto pReq = std::make_unique<CSqlWhoIsQuery>(pRes);
	pReq->m_Mode = Mode;
	pReq->m_Cutoff = Cutoff;
	str_copy(pReq->m_aSearch, pSearch, sizeof(pReq->m_aSearch));
	auto *pRaw = pReq.release();
	m_pWorker->Enqueue([pRaw](IDbConnection *pSql) {
		char aErr[256] = {0};
		auto pResLocal = std::static_pointer_cast<CWhoIsResult>(pRaw->m_pResult);
		RunQuery(pSql, pRaw, pResLocal, aErr, sizeof(aErr));
		delete pRaw;
	});
}

void CWhoIs::DrainAndPrintResults()
{
	for(auto it = m_vInternalResults.begin(); it != m_vInternalResults.end();)
	{
		if((*it)->m_Completed.load(std::memory_order_acquire))
		{
			PrintLines(GameServer(), (*it)->m_TargetClientId, (*it)->m_vLines, (*it)->m_aTag, (*it)->m_SendToChat);
			it = m_vInternalResults.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void CWhoIs::PurgeNow(int RetentionMonths, bool Silent)
{
	if(RetentionMonths <= 0)
		return;
	auto pRes = Silent ? nullptr : std::make_shared<CWhoIsResult>();
	int Months = RetentionMonths;
	if(pRes)
		m_vInternalResults.push_back(pRes);
	m_pWorker->Enqueue([this, pRes, Months](IDbConnection *pSql) {
		char aStmt[512];
		char aErr[256] = {0};
		bool Failed = false;
		if(!g_WhoisSchemaInit.load(std::memory_order_acquire))
		{
			Failed = EnsureWhoisSchema(pSql, aErr, sizeof(aErr));
			if(!Failed)
				g_WhoisSchemaInit.store(true, std::memory_order_release);
		}
		int64_t Seconds = (int64_t)Months * 30 * 24 * 60 * 60;
		int Rows = 0;
		if(!Failed)
		{
			str_format(aStmt, sizeof(aStmt),
				"DELETE FROM %s WHERE (strftime('%%s', created_at)) < (strftime('%%s','now') - %" PRId64 ")",
				TBL_WHOIS_CONNECTIONS, Seconds);
			Failed = pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || pSql->ExecuteUpdate(&Rows, aErr, sizeof(aErr));
		}
		if(!Failed)
		{
			int Tmp = 0;
			str_format(aStmt, sizeof(aStmt),
				"DELETE FROM %s WHERE (strftime('%%s', last_seen)) < (strftime('%%s','now') - %" PRId64 ")",
				TBL_WHOIS_AGG_NAMES_BY_IP, Seconds);
			Failed = pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || pSql->ExecuteUpdate(&Tmp, aErr, sizeof(aErr));
			str_format(aStmt, sizeof(aStmt),
				"DELETE FROM %s WHERE (strftime('%%s', last_seen)) < (strftime('%%s','now') - %" PRId64 ")",
				TBL_WHOIS_AGG_IPS_BY_NAME, Seconds);
			Tmp = 0;
			Failed = Failed || pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || pSql->ExecuteUpdate(&Tmp, aErr, sizeof(aErr));
		}
		if(pRes)
		{
			str_copy(pRes->m_aTag, TAG, sizeof(pRes->m_aTag));
			pRes->m_SendToChat = false;
			if(!Failed)
			{
				char aLine[128];
				str_format(aLine, sizeof(aLine), "purge removed %d old whois rows (>%d months)", Rows, Months);
				pRes->m_vLines.emplace_back(aLine);
				pRes->m_Success = true;
			}
			else
			{
				char aLine[192];
				str_format(aLine, sizeof(aLine), "whois purge failed: %s", aErr[0] ? aErr : "unknown error");
				pRes->m_vLines.emplace_back(aLine);
				pRes->m_Success = false;
			}
			pRes->m_Completed.store(true, std::memory_order_release);
		}
	});
}
