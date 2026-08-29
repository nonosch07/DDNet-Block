#include "whois.h"

#include "sql/ddl.h"
#include "sql/mysql_config.h"
#include "sql_prefix.h"

#include <engine/server.h>
#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <block/base.h>
#include <block/util.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

static const char *TAG = "whois";

static std::atomic_bool g_WhoisSchemaInit{false};
// Set by a task that saw a statement fail. The worker reads it after the task
// returns and rebuilds the connection, because a dead socket looks exactly like
// a bad statement from in here.
static std::atomic_bool g_WhoisStatementFailed{false};

static void EnsureParentDirs(const char *pRelOrAbsPath)
{
	if(!pRelOrAbsPath || !*pRelOrAbsPath)
		return;
	if(fs_makedir_rec_for(pRelOrAbsPath) != 0)
		log_warn("whois", "could not create directory for '%s'", pRelOrAbsPath);
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

/// The whois backend, on a thread of its own.
///
/// It does not go through CDbConnectionPool on purpose. A whois lookup scans a
/// table that grows with every connection ever made, and the pool is shared with
/// account and clan saves: a slow scan there would hold up a player's
/// blockpoints. So whois gets one connection, used serially by one thread.
///
/// MySQL is preferred, because that is where the rest of the server's data
/// lives and it survives a wiped container. A server configured without a MySQL
/// write database falls back to a local SQLite file, which is also what the unit
/// and integration tests run against.
class CWhoisWorker
{
public:
	explicit CWhoisWorker(const char *pDbPath)
	{
		str_copy(m_aSqlitePath, pDbPath, sizeof(m_aSqlitePath));
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
			const std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Shutdown = true;
		}
		m_Cv.notify_one();
		if(m_Thread.joinable())
			m_Thread.join();
	}

	/// Queues a task. Droppable tasks are the ones that only record history --
	/// losing one costs a row, whereas dropping a lookup or a purge would leave
	/// somebody waiting forever on a result that never completes.
	///
	/// Returns false when a droppable task was dropped because the queue is full.
	bool Enqueue(std::function<void(IDbConnection *)> Task, bool Droppable = false)
	{
		{
			const std::lock_guard<std::mutex> Lock(m_Mutex);
			if(Droppable && m_Queue.size() >= MAX_QUEUED)
			{
				++m_Dropped;
				return false;
			}
			m_Queue.push(std::move(Task));
		}
		m_Cv.notify_one();
		return true;
	}

	size_t Dropped() const
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		return m_Dropped;
	}

	size_t Queued() const
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		return m_Queue.size();
	}

private:
	// A backlog this long already means the database cannot keep up; queueing
	// more of the same only turns a slow database into unbounded memory growth.
	static constexpr size_t MAX_QUEUED = 4096;

	/// Opens the backend, MySQL first. Called once, and again after a failure.
	bool Open()
	{
		m_pConn.reset();
		m_aError[0] = '\0';

		CMysqlConfig MysqlConfig;
		if(MysqlAvailable() && MysqlWriteConfig(&MysqlConfig))
		{
			// the tables are created by EnsureWhoisSchema, so this connection
			// never needs to run upstream's own setup
			MysqlConfig.m_Setup = false;
			std::unique_ptr<IDbConnection> pMysql = CreateMysqlConnection(MysqlConfig);
			if(pMysql && pMysql->Connect(m_aError, sizeof(m_aError)))
			{
				m_pConn = std::move(pMysql);
				if(!m_Announced)
					dbg_msg(TAG, "using mysql database '%s'", MysqlConfig.m_aDatabase);
				m_Announced = true;
				return true;
			}
			dbg_msg(TAG, "mysql unavailable (%s), falling back to sqlite", m_aError[0] ? m_aError : "unknown error");
		}

		EnsureParentDirs(m_aSqlitePath);
		std::unique_ptr<IDbConnection> pSqlite = CreateSqliteConnection(m_aSqlitePath, false);
		if(pSqlite && pSqlite->Connect(m_aError, sizeof(m_aError)))
		{
			m_pConn = std::move(pSqlite);
			if(!m_Announced)
				dbg_msg(TAG, "using sqlite database '%s'", m_aSqlitePath);
			m_Announced = true;
			return true;
		}
		return false;
	}

	void Run()
	{
		while(true)
		{
			std::function<void(IDbConnection *)> Task;
			{
				std::unique_lock<std::mutex> Lock(m_Mutex);
				m_Cv.wait(Lock, [&] { return m_Shutdown || !m_Queue.empty(); });
				if(m_Shutdown && m_Queue.empty())
					break;
				Task = std::move(m_Queue.front());
				m_Queue.pop();
			}

			// the connection is kept open across tasks: reconnecting per task
			// costs a TCP handshake and a login round trip on MySQL, which is
			// more expensive than everything whois actually does
			if(!m_pConn && !Open())
			{
				dbg_msg(TAG, "no database available: %s", m_aError[0] ? m_aError : "unknown error");
				Task(nullptr);
				continue;
			}

			Task(m_pConn.get());

			if(g_WhoisStatementFailed.exchange(false))
			{
				// a statement failed; the connection may be the reason, so drop
				// it and let the next task open a fresh one
				m_pConn.reset();
				g_WhoisSchemaInit.store(false, std::memory_order_release);
			}
		}
		m_pConn.reset();
	}

	char m_aSqlitePath[IO_MAX_PATH_LENGTH]{};
	char m_aError[256]{};
	bool m_Announced = false;
	std::unique_ptr<IDbConnection> m_pConn;

	std::thread m_Thread;
	mutable std::mutex m_Mutex;
	std::condition_variable m_Cv;
	std::queue<std::function<void(IDbConnection *)>> m_Queue;
	size_t m_Dropped = 0;
	bool m_Shutdown = false;
};

/// Runs a statement, and remembers a failure so the worker can rebuild the
/// connection. Returns true on *failure*, which is what the callers here expect.
static bool ExecRaw(IDbConnection *pSql, const char *pStmt, char *pError, int ErrorSize)
{
	int Rows = 0;
	if(!pSql->PrepareStatement(pStmt, pError, ErrorSize) || !pSql->ExecuteUpdate(&Rows, pError, ErrorSize))
	{
		g_WhoisStatementFailed.store(true, std::memory_order_release);
		return true;
	}
	return false;
}

/// The handful of places where the two backends genuinely differ.
///
/// Everything else in this file is plain SQL that both speak. Keeping the
/// differences in one struct means a new statement only has to think about the
/// dialect when it uses one of these.
struct SWhoisDialect
{
	bool m_Mysql;

	/// Wraps a timestamp column as a UNIX epoch, for the retention arithmetic.
	const char *m_pEpochOf; // printf pattern taking the column name
	/// The same for "right now".
	const char *m_pEpochNow;

	static SWhoisDialect Of(IDbConnection *pSql)
	{
		SWhoisDialect Dialect{};
		Dialect.m_Mysql = !IsSqliteConnection(pSql);
		if(Dialect.m_Mysql)
		{
			Dialect.m_pEpochOf = "UNIX_TIMESTAMP(%s)";
			Dialect.m_pEpochNow = "UNIX_TIMESTAMP()";
		}
		else
		{
			Dialect.m_pEpochOf = "strftime('%%s', %s)";
			Dialect.m_pEpochNow = "strftime('%%s','now')";
		}
		return Dialect;
	}
};

static bool EnsureWhoisSchema(IDbConnection *pSql, char *pError, int ErrorSize)
{
	const SWhoisDialect Dialect = SWhoisDialect::Of(pSql);
	char aStmt[1536];

	if(Dialect.m_Mysql)
	{
		// indexes are declared inside CREATE TABLE: a separate CREATE INDEX has
		// no portable IF NOT EXISTS, so it would fail on every restart
		str_format(aStmt, sizeof(aStmt),
			"CREATE TABLE IF NOT EXISTS `%s` ("
			"  `id` bigint(20) NOT NULL AUTO_INCREMENT,"
			"  `ip` varchar(47) NOT NULL,"
			"  `name` varchar(32) NOT NULL,"
			"  `account_id` int(11) NOT NULL DEFAULT 0,"
			"  `account_name` varchar(32) NOT NULL DEFAULT '',"
			"  `source` varchar(16) NOT NULL,"
			"  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,"
			"  PRIMARY KEY (`id`),"
			"  KEY `whois_idx_ip` (`ip`),"
			"  KEY `whois_idx_name` (`name`),"
			"  KEY `whois_idx_ip_name` (`ip`,`name`),"
			"  KEY `whois_idx_name_ip` (`name`,`ip`),"
			"  KEY `whois_idx_accid` (`account_id`),"
			"  KEY `whois_idx_accname` (`account_name`),"
			"  KEY `whois_idx_created_at` (`created_at`)"
			")" BLOCK_ENGINE_COLLATE,
			TBL_WHOIS_CONNECTIONS);
		if(ExecRaw(pSql, aStmt, pError, ErrorSize))
			return true;

		str_format(aStmt, sizeof(aStmt),
			"CREATE TABLE IF NOT EXISTS `%s` ("
			"  `ip` varchar(47) NOT NULL,"
			"  `name` varchar(32) NOT NULL,"
			"  `cnt` int(11) NOT NULL,"
			"  `first_seen` datetime NOT NULL,"
			"  `last_seen` datetime NOT NULL,"
			"  `has_logins` int(11) NOT NULL DEFAULT 0,"
			"  `last_acc_name` varchar(32) DEFAULT NULL,"
			"  PRIMARY KEY (`ip`,`name`),"
			"  KEY `whois_names_by_ip_idx_ip` (`ip`)"
			")" BLOCK_ENGINE_COLLATE,
			TBL_WHOIS_AGG_NAMES_BY_IP);
		if(ExecRaw(pSql, aStmt, pError, ErrorSize))
			return true;

		str_format(aStmt, sizeof(aStmt),
			"CREATE TABLE IF NOT EXISTS `%s` ("
			"  `name` varchar(32) NOT NULL,"
			"  `ip` varchar(47) NOT NULL,"
			"  `cnt` int(11) NOT NULL,"
			"  `first_seen` datetime NOT NULL,"
			"  `last_seen` datetime NOT NULL,"
			"  PRIMARY KEY (`name`,`ip`),"
			"  KEY `whois_ips_by_name_idx_name` (`name`)"
			")" BLOCK_ENGINE_COLLATE,
			TBL_WHOIS_AGG_IPS_BY_NAME);
		if(ExecRaw(pSql, aStmt, pError, ErrorSize))
			return true;

		// a table written by a build from before account_name existed
		if(!EnsureColumn(pSql, TBL_WHOIS_CONNECTIONS, "account_name", "varchar(32) NOT NULL DEFAULT ''", pError, ErrorSize))
		{
			g_WhoisStatementFailed.store(true, std::memory_order_release);
			return true;
		}
		return false;
	}

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

	static const char *const s_apIndexes[] = {
		"CREATE INDEX IF NOT EXISTS whois_idx_ip ON %s(ip)",
		"CREATE INDEX IF NOT EXISTS whois_idx_name ON %s(name)",
		"CREATE INDEX IF NOT EXISTS whois_idx_ip_name ON %s(ip,name)",
		"CREATE INDEX IF NOT EXISTS whois_idx_name_ip ON %s(name,ip)",
		"CREATE INDEX IF NOT EXISTS whois_idx_accid ON %s(account_id)",
		"CREATE INDEX IF NOT EXISTS whois_idx_created_at ON %s(created_at)",
	};
	for(const char *pPattern : s_apIndexes)
	{
		str_format(aStmt, sizeof(aStmt), pPattern, TBL_WHOIS_CONNECTIONS);
		if(ExecRaw(pSql, aStmt, pError, ErrorSize))
			return true;
	}

	{
		// SQLite has no ADD COLUMN IF NOT EXISTS either; here the duplicate is
		// the only error this can produce, so it is safe to ignore
		char aIgnored[256] = {0};
		str_format(aStmt, sizeof(aStmt), "ALTER TABLE %s ADD COLUMN account_name TEXT NOT NULL DEFAULT ''", TBL_WHOIS_CONNECTIONS);
		ExecRaw(pSql, aStmt, aIgnored, sizeof(aIgnored));
		g_WhoisStatementFailed.store(false, std::memory_order_release);
	}
	str_format(aStmt, sizeof(aStmt), "CREATE INDEX IF NOT EXISTS whois_idx_accname ON %s(account_name)", TBL_WHOIS_CONNECTIONS);
	if(ExecRaw(pSql, aStmt, pError, ErrorSize))
		return true;

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
	return ExecRaw(pSql, aStmt, pError, ErrorSize);
}

CWhoIs::CWhoIs(CGameContext *pGameServer, CDbConnectionPool *pPool) :
	m_pGameServer(pGameServer), m_pPool(pPool)
{
	mem_zero(m_aLastSnapshotTick, sizeof(m_aLastSnapshotTick));
	int Minutes = std::clamp(g_Config.m_SvWhoisSnapshotMinutes, 0, 24 * 60);
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

CWhoIs::~CWhoIs() = default;

IServer *CWhoIs::Server() const { return GameServer()->Server(); }

bool CWhoIs::GetClientIdentity(int ClientId, char *pOutIp, int OutIpSize, char *pOutName, int OutNameSize, int &OutAccId, char *pOutAccName, int OutAccNameSize)
{
	if(!Server()->ClientIngame(ClientId) || !GameServer()->m_apPlayers[ClientId])
		return false;
	BlockClientAddr(Server(), ClientId, pOutIp, OutIpSize);
	NormalizeIpNoPort(pOutIp);
	str_copy(pOutName, Server()->ClientName(ClientId), OutNameSize);
	CPlayer *pPl = GameServer()->m_apPlayers[ClientId];
	OutAccId = pPl->Block().IsLoggedIn() ? pPl->Block().m_Account.m_Id : 0;
	if(pPl->Block().IsLoggedIn())
		str_copy(pOutAccName, pPl->Block().m_Account.m_aName, OutAccNameSize);
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
			size_t Len = str_length(pIp + 1);
			mem_move(pIp, pIp + 1, Len + 1); // includes the NUL
			return;
		}
	}
	const char *pColonConst = str_rchr(pIp, ':');
	if(pColonConst)
	{
		int NumColons = 0;
		for(const char *p = pIp; *p; ++p)
			if(*p == ':')
				NumColons++;
		if(NumColons == 1)
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
			size_t Len = str_length(pIp + 1);
			mem_move(pIp, pIp + 1, Len + 1); // includes the NUL
			return;
		}
	}
	const char *pColonConst = str_rchr(pIp, ':');
	if(pColonConst)
	{
		int NumColons = 0;
		for(const char *p = pIp; *p; ++p)
			if(*p == ':')
				NumColons++;
		if(NumColons == 1)
		{
			char *pColon = const_cast<char *>(pColonConst);
			*pColon = '\0';
		}
	}
}

static bool RunLog(IDbConnection *pSql, const CSqlWhoIsLog *pReq, char *pError, int ErrorSize)
{
	const SWhoisDialect Dialect = SWhoisDialect::Of(pSql);
	char aStmt[768];
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
		"INSERT INTO %s (ip, name, account_id, account_name, source) VALUES (?, ?, ?, ?, ?)",
		TBL_WHOIS_CONNECTIONS);
	if(!pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	pSql->BindString(1, pReq->m_aIp);
	pSql->BindString(2, pReq->m_aName);
	pSql->BindInt(3, pReq->m_AccountId);
	pSql->BindString(4, pReq->m_aAccountName);
	pSql->BindString(5, pReq->m_aSource);
	int Affected = 0;
	if(!pSql->ExecuteUpdate(&Affected, pError, ErrorSize))
		return true;

	// MySQL has neither ON CONFLICT nor a two-argument MAX/MIN, so the upsert is
	// spelled twice. The semantics are identical: bump the counter, widen the
	// seen-window, and never clear a login flag or account name once set.
	if(Dialect.m_Mysql)
	{
		str_format(aStmt, sizeof(aStmt),
			"INSERT INTO %s (ip, name, cnt, first_seen, last_seen, has_logins, last_acc_name) "
			"VALUES (?, ?, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, CASE WHEN ? <> 0 THEN 1 ELSE 0 END, CASE WHEN ? <> 0 THEN ? ELSE NULL END) "
			"ON DUPLICATE KEY UPDATE "
			"cnt = cnt + 1, "
			"last_seen = GREATEST(last_seen, VALUES(last_seen)), "
			"first_seen = LEAST(first_seen, VALUES(first_seen)), "
			"has_logins = CASE WHEN VALUES(has_logins) = 1 THEN 1 ELSE has_logins END, "
			"last_acc_name = COALESCE(VALUES(last_acc_name), last_acc_name)",
			TBL_WHOIS_AGG_NAMES_BY_IP);
	}
	else
	{
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
	}
	if(!pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	pSql->BindString(1, pReq->m_aIp);
	pSql->BindString(2, pReq->m_aName);
	pSql->BindInt(3, pReq->m_AccountId);
	pSql->BindInt(4, pReq->m_AccountId);
	pSql->BindString(5, pReq->m_aAccountName);
	Affected = 0;
	if(!pSql->ExecuteUpdate(&Affected, pError, ErrorSize))
		return true;

	if(Dialect.m_Mysql)
	{
		str_format(aStmt, sizeof(aStmt),
			"INSERT INTO %s (name, ip, cnt, first_seen, last_seen) "
			"VALUES (?, ?, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP) "
			"ON DUPLICATE KEY UPDATE "
			"cnt = cnt + 1, "
			"last_seen = GREATEST(last_seen, VALUES(last_seen)), "
			"first_seen = LEAST(first_seen, VALUES(first_seen))",
			TBL_WHOIS_AGG_IPS_BY_NAME);
	}
	else
	{
		str_format(aStmt, sizeof(aStmt),
			"INSERT INTO %s (name, ip, cnt, first_seen, last_seen) "
			"VALUES (?, ?, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP) "
			"ON CONFLICT(name, ip) DO UPDATE SET "
			"cnt = cnt + 1, "
			"last_seen = MAX(last_seen, excluded.last_seen), "
			"first_seen = MIN(first_seen, excluded.first_seen)",
			TBL_WHOIS_AGG_IPS_BY_NAME);
	}
	if(!pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	pSql->BindString(1, pReq->m_aName);
	pSql->BindString(2, pReq->m_aIp);
	Affected = 0;
	if(!pSql->ExecuteUpdate(&Affected, pError, ErrorSize))
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
				int Bits = str_toint(pSlash + 1);
				if(Bits == 24)
					Cut = 1;
				else if(Bits == 16)
					Cut = 2;
				else if(Bits == 8)
					Cut = 3;
				// terminate IP before slash or optional preceding space
				size_t CutPos = (pSlash > aIp && pSlash[-1] == ' ') ? (size_t)(pSlash - aIp - 1) : (size_t)(pSlash - aIp);
				aIp[CutPos] = '\0';
			}
		}
		NormalizeIpNoPortHelper(aIp);
		if(Cut <= 0)
		{
			// exact IP: compute on-the-fly from raw connections
			int MaxRows = std::clamp(g_Config.m_SvWhoisMaxRows, 10, 100000);
			str_format(aStmt, sizeof(aStmt),
				"SELECT name, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen,"
				" CASE WHEN SUM(CASE WHEN account_id <> 0 THEN 1 ELSE 0 END) > 0 THEN 1 ELSE 0 END AS has_logins "
				"FROM %s WHERE ip = ? GROUP BY name ORDER BY cnt DESC LIMIT %d",
				TBL_WHOIS_CONNECTIONS, MaxRows);
			if(!pSql->PrepareStatement(aStmt, pError, ErrorSize))
				return true;
			pSql->BindString(1, aIp);

			bool End = false;
			int Total = 0;
			int Distinct = 0;
			struct SEntry
			{
				char m_Name[32];
				int m_Cnt;
				char m_First[20];
				char m_Last[20];
				int m_HasLogins;
				char m_AccName[32];
			};
			std::vector<SEntry> vEntries;
			char aGlobalFirst[20] = {0};
			char aGlobalLast[20] = {0};
			while(pSql->Step(&End, pError, ErrorSize) && !End)
			{
				SEntry e{};
				pSql->GetString(1, e.m_Name, sizeof(e.m_Name));
				e.m_Cnt = pSql->GetInt(2);
				mem_zero(e.m_First, sizeof(e.m_First));
				if(!pSql->IsNull(3))
					pSql->GetString(3, e.m_First, sizeof(e.m_First));
				mem_zero(e.m_Last, sizeof(e.m_Last));
				if(!pSql->IsNull(4))
					pSql->GetString(4, e.m_Last, sizeof(e.m_Last));
				e.m_HasLogins = pSql->GetInt(5);
				e.m_AccName[0] = '\0'; // no account name in raw table
				vEntries.push_back(e);
				Total += e.m_Cnt;
				// track global first/last
				if(e.m_First[0] && (!aGlobalFirst[0] || str_comp(e.m_First, aGlobalFirst) < 0))
					str_copy(aGlobalFirst, e.m_First, sizeof(aGlobalFirst));
				if(e.m_Last[0] && (!aGlobalLast[0] || str_comp(e.m_Last, aGlobalLast) > 0))
					str_copy(aGlobalLast, e.m_Last, sizeof(aGlobalLast));
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
				if(e.m_First[0] && e.m_Last[0])
					str_format(aDetails, sizeof(aDetails), "first: %s, last: %s", e.m_First, e.m_Last);
				else if(e.m_Last[0])
					str_format(aDetails, sizeof(aDetails), "last: %s", e.m_Last);
				char aAccPart[80] = {0};
				if(e.m_HasLogins > 0)
					str_copy(aAccPart, ", logins: yes", sizeof(aAccPart));
				if(aDetails[0])
					str_format(aTmp, sizeof(aTmp), " - %s (%d), %s%s", e.m_Name, e.m_Cnt, aDetails, aAccPart);
				else
					str_format(aTmp, sizeof(aTmp), " - %s (%d)%s", e.m_Name, e.m_Cnt, aAccPart);
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
			int Dots = 0;
			int LastDotPos[4] = {-1, -1, -1, -1};
			for(int i = 0; aIp[i]; ++i)
				if(aIp[i] == '.')
				{
					if(Dots < 4)
						LastDotPos[Dots] = i;
					Dots++;
				}

			bool CanCut = (Cut == 1 && Dots >= 3) || (Cut == 2 && Dots >= 2) || (Cut == 3 && Dots >= 1);
			if(CanCut)
			{
				int CutPos = (Cut == 1) ? LastDotPos[2] : (Cut == 2 ? LastDotPos[1] : LastDotPos[0]);
				if(CutPos > 0)
				{
					aIp[CutPos] = '\0';
				}
				str_format(aLike, sizeof(aLike), "%s.%%", aIp);
				str_format(aStmt, sizeof(aStmt),
					"SELECT ip, name, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen,"
					" CASE WHEN SUM(CASE WHEN account_id <> 0 THEN 1 ELSE 0 END) > 0 THEN 1 ELSE 0 END AS has_logins "
					"FROM %s WHERE ip LIKE ? GROUP BY ip, name ORDER BY ip ASC, cnt DESC",
					TBL_WHOIS_CONNECTIONS);
				if(!pSql->PrepareStatement(aStmt, pError, ErrorSize))
					return true;
				pSql->BindString(1, aLike);
			}
			else
			{
				// fallback: exact IP query shaped like prefix result (ip as first column)
				int MaxRows2 = std::clamp(g_Config.m_SvWhoisMaxRows, 10, 100000);
				str_format(aStmt, sizeof(aStmt),
					"SELECT ip, name, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen,"
					" CASE WHEN SUM(CASE WHEN account_id <> 0 THEN 1 ELSE 0 END) > 0 THEN 1 ELSE 0 END AS has_logins "
					"FROM %s WHERE ip = ? GROUP BY ip, name ORDER BY cnt DESC LIMIT %d",
					TBL_WHOIS_CONNECTIONS, MaxRows2);
				if(!pSql->PrepareStatement(aStmt, pError, ErrorSize))
					return true;
				pSql->BindString(1, aIp);
			}

			bool End = false;
			std::string CurIp;
			int CurTotal = 0;
			struct SNameInfo
			{
				std::string m_Name;
				int m_Cnt;
				char m_First[20];
				char m_Last[20];
				int m_HasLogins;
				char m_AccName[32];
			};
			std::vector<SNameInfo> CurNames;
			const int MaxIps = std::clamp(g_Config.m_SvWhoisPrefixMaxIps, 1, 100000);
			const int MaxNamesPerIp = std::clamp(g_Config.m_SvWhoisPrefixNamesPerIp, 1, 100000);
			bool TruncatedIps = false;
			bool TruncatedNames = false;
			auto FlushCur = [&]() {
				if(CurIp.empty())
					return;
				char aHead[256];
				// derive group first/last
				char aFirst[20] = {0};
				char aLast[20] = {0};
				for(const auto &Nn : CurNames)
				{
					if(Nn.m_First[0] && (!aFirst[0] || str_comp(Nn.m_First, aFirst) < 0))
						str_copy(aFirst, Nn.m_First, sizeof(aFirst));
					if(Nn.m_Last[0] && (!aLast[0] || str_comp(Nn.m_Last, aLast) > 0))
						str_copy(aLast, Nn.m_Last, sizeof(aLast));
				}
				if(aFirst[0] || aLast[0])
					str_format(aHead, sizeof(aHead), "%s connected %d times with %d names (first: %s, last: %s):", CurIp.c_str(), CurTotal, (int)CurNames.size(), aFirst[0] ? aFirst : "-", aLast[0] ? aLast : "-");
				else
					str_format(aHead, sizeof(aHead), "%s connected %d times with %d names:", CurIp.c_str(), CurTotal, (int)CurNames.size());
				pRes->m_vLines.emplace_back(aHead);
				int Shown = 0;
				for(const auto &Nn : CurNames)
				{
					char aTmp[256];
					char aDetails[160] = {0};
					if(Nn.m_First[0] && Nn.m_Last[0])
						str_format(aDetails, sizeof(aDetails), "first: %s, last: %s", Nn.m_First, Nn.m_Last);
					else if(Nn.m_Last[0])
						str_format(aDetails, sizeof(aDetails), "last: %s", Nn.m_Last);
					char aAccPart[64] = {0};
					if(Nn.m_AccName[0])
						str_format(aAccPart, sizeof(aAccPart), ", account: %s", Nn.m_AccName);
					else if(Nn.m_HasLogins > 0)
						str_copy(aAccPart, ", logins: yes", sizeof(aAccPart));

					if(aDetails[0])
						str_format(aTmp, sizeof(aTmp), " - %s (%d), %s%s", Nn.m_Name.c_str(), Nn.m_Cnt, aDetails, aAccPart);
					else
						str_format(aTmp, sizeof(aTmp), " - %s (%d)%s", Nn.m_Name.c_str(), Nn.m_Cnt, aAccPart);
					pRes->m_vLines.emplace_back(aTmp);
					if(++Shown >= MaxNamesPerIp)
					{
						TruncatedNames = true;
						break;
					}
				}
				CurIp.clear();
				CurTotal = 0;
				CurNames.clear();
			};

			int IpGroups = 0;
			while(pSql->Step(&End, pError, ErrorSize) && !End)
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
				if(CurIp.empty())
					CurIp = aResIp;
				if(CurIp != aResIp)
				{
					FlushCur();
					CurIp = aResIp;
					if(++IpGroups >= MaxIps)
					{
						TruncatedIps = true;
						break;
					}
				}
				CurTotal += Cnt;
				SNameInfo Ni{};
				Ni.m_Name = aName;
				Ni.m_Cnt = Cnt;
				Ni.m_HasLogins = HasLogins;
				Ni.m_First[0] = '\0';
				Ni.m_Last[0] = '\0';
				Ni.m_AccName[0] = '\0';
				if(aFirstShort[0])
					str_copy(Ni.m_First, aFirstShort, sizeof(Ni.m_First));
				if(aLastShort[0])
					str_copy(Ni.m_Last, aLastShort, sizeof(Ni.m_Last));
				// leave AccName empty when using raw table
				CurNames.emplace_back(std::move(Ni));
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

			if(CurIp.empty() && pRes->m_vLines.empty())
			{
				char aMsg[128];
				str_format(aMsg, sizeof(aMsg), "No entries found for IP '%s'", pReq->m_aSearch);
				pRes->m_vLines.emplace_back(aMsg);
			}
		}
	}
	else if(pReq->m_Mode == 1)
	{
		int MaxRows3 = std::clamp(g_Config.m_SvWhoisMaxRows, 10, 100000);
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
			if(!pSql->PrepareStatement(aStmt, pError, ErrorSize))
				return true;
			pSql->BindString(1, aPattern);
		}
		else
		{
			str_format(aStmt, sizeof(aStmt),
				"SELECT ip, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen "
				"FROM %s WHERE name = ? GROUP BY ip ORDER BY cnt DESC LIMIT %d",
				TBL_WHOIS_CONNECTIONS, MaxRows3);
			if(!pSql->PrepareStatement(aStmt, pError, ErrorSize))
				return true;
			pSql->BindString(1, pReq->m_aSearch);
		}

		bool End = false;
		int Total = 0;
		struct IPCnt
		{
			char m_Ip[64];
			int m_Cnt;
			char m_First[20];
			char m_Last[20];
		};
		std::vector<IPCnt> vEntries;
		char aGlobalFirst[20] = {0};
		char aGlobalLast[20] = {0};
		while(pSql->Step(&End, pError, ErrorSize) && !End)
		{
			IPCnt e{};
			pSql->GetString(1, e.m_Ip, sizeof(e.m_Ip));
			e.m_Cnt = pSql->GetInt(2);
			mem_zero(e.m_First, sizeof(e.m_First));
			if(!pSql->IsNull(3))
				pSql->GetString(3, e.m_First, sizeof(e.m_First));
			mem_zero(e.m_Last, sizeof(e.m_Last));
			if(!pSql->IsNull(4))
				pSql->GetString(4, e.m_Last, sizeof(e.m_Last));
			vEntries.push_back(e);
			Total += e.m_Cnt;
			if(e.m_First[0] && (!aGlobalFirst[0] || str_comp(e.m_First, aGlobalFirst) < 0))
				str_copy(aGlobalFirst, e.m_First, sizeof(aGlobalFirst));
			if(e.m_Last[0] && (!aGlobalLast[0] || str_comp(e.m_Last, aGlobalLast) > 0))
				str_copy(aGlobalLast, e.m_Last, sizeof(aGlobalLast));
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
			if(e.m_First[0] && e.m_Last[0])
				str_format(aTmp, sizeof(aTmp), " - %s (%d), first: %s, last: %s", e.m_Ip, e.m_Cnt, e.m_First, e.m_Last);
			else if(e.m_Last[0])
				str_format(aTmp, sizeof(aTmp), " - %s (%d), last: %s", e.m_Ip, e.m_Cnt, e.m_Last);
			else
				str_format(aTmp, sizeof(aTmp), " - %s (%d)", e.m_Ip, e.m_Cnt);
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
	else if(pReq->m_Mode == 2)
	{
		// account name: find all distinct names ever seen on this account
		int MaxRows4 = std::clamp(g_Config.m_SvWhoisMaxRows, 10, 100000);
		str_format(aStmt, sizeof(aStmt),
			"SELECT name, COUNT(*) AS cnt, date(MIN(created_at)) AS first_seen, date(MAX(created_at)) AS last_seen "
			"FROM %s WHERE account_name = ? AND account_name != '' "
			"GROUP BY name ORDER BY cnt DESC LIMIT %d",
			TBL_WHOIS_CONNECTIONS, MaxRows4);
		if(!pSql->PrepareStatement(aStmt, pError, ErrorSize))
			return true;
		pSql->BindString(1, pReq->m_aSearch);

		bool End2 = false;
		struct SNameEntry
		{
			char m_Name[32];
			int m_Cnt;
			char m_First[20];
			char m_Last[20];
		};
		std::vector<SNameEntry> vAccEntries;
		int AccTotal = 0;
		char aAccFirst[20] = {0};
		char aAccLast[20] = {0};
		while(pSql->Step(&End2, pError, ErrorSize) && !End2)
		{
			SNameEntry e{};
			pSql->GetString(1, e.m_Name, sizeof(e.m_Name));
			e.m_Cnt = pSql->GetInt(2);
			mem_zero(e.m_First, sizeof(e.m_First));
			if(!pSql->IsNull(3))
				pSql->GetString(3, e.m_First, sizeof(e.m_First));
			mem_zero(e.m_Last, sizeof(e.m_Last));
			if(!pSql->IsNull(4))
				pSql->GetString(4, e.m_Last, sizeof(e.m_Last));
			vAccEntries.push_back(e);
			AccTotal += e.m_Cnt;
			if(e.m_First[0] && (!aAccFirst[0] || str_comp(e.m_First, aAccFirst) < 0))
				str_copy(aAccFirst, e.m_First, sizeof(aAccFirst));
			if(e.m_Last[0] && (!aAccLast[0] || str_comp(e.m_Last, aAccLast) > 0))
				str_copy(aAccLast, e.m_Last, sizeof(aAccLast));
		}
		int AccDistinct = (int)vAccEntries.size();
		char aHead2[256];
		if(AccDistinct == 0)
		{
			str_format(aHead2, sizeof(aHead2), "No recorded names found for account '%s')", pReq->m_aSearch);
			pRes->m_vLines.emplace_back(aHead2);
		}
		else
		{
			if(aAccFirst[0] || aAccLast[0])
				str_format(aHead2, sizeof(aHead2), "Account '%s': %d name(s), %d connections (first: %s, last: %s):", pReq->m_aSearch, AccDistinct, AccTotal, aAccFirst[0] ? aAccFirst : "-", aAccLast[0] ? aAccLast : "-");
			else
				str_format(aHead2, sizeof(aHead2), "Account '%s': %d name(s), %d connections:", pReq->m_aSearch, AccDistinct, AccTotal);
			pRes->m_vLines.emplace_back(aHead2);
			for(const auto &e : vAccEntries)
			{
				char aTmp2[256];
				if(e.m_First[0] && e.m_Last[0])
					str_format(aTmp2, sizeof(aTmp2), " - %s (%d connections, first: %s, last: %s)", e.m_Name, e.m_Cnt, e.m_First, e.m_Last);
				else
					str_format(aTmp2, sizeof(aTmp2), " - %s (%d connections)", e.m_Name, e.m_Cnt);
				pRes->m_vLines.emplace_back(aTmp2);
			}
			if((int)vAccEntries.size() >= MaxRows4)
			{
				char aFoot2[160];
				str_format(aFoot2, sizeof(aFoot2), "[truncated] Results capped at sv_whois_max_rows=%d.", MaxRows4);
				pRes->m_vLines.emplace_back(aFoot2);
			}
		}
	}

	pRes->m_Completed.store(true, std::memory_order_release);
	pRes->m_Success = true;
	return false;
}

void CWhoIs::LogJoin(int ClientId, const char *pSource)
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
	str_copy(pReq->m_aSource, pSource, sizeof(pReq->m_aSource));
	// shared_ptr, not a released raw pointer: the task is not guaranteed to run
	// -- it is dropped when the queue is full and skipped when the worker stops
	// with a backlog -- and a raw delete inside it would leak in both cases
	const std::shared_ptr<CSqlWhoIsLog> pShared = std::move(pReq);
	m_pWorker->Enqueue([pShared](IDbConnection *pSql) {
		if(!pSql)
			return;
		char aErr[256] = {0};
		RunLog(pSql, pShared.get(), aErr, sizeof(aErr));
	},
		/* Droppable */ true);
	m_aLastSnapshotTick[ClientId] = Server()->Tick();
}

void CWhoIs::LogLeave(int ClientId)
{
	char aIp[48] = {0}, aName[24] = {0}, aAcc[16] = {0};
	int AccId = 0;
	// best effort, player object may already be gone when called from drop; get name from server
	BlockClientAddr(Server(), ClientId, aIp, sizeof(aIp));
	NormalizeIpNoPort(aIp);
	str_copy(aName, Server()->ClientName(ClientId), sizeof(aName));
	if(GameServer()->m_apPlayers[ClientId] && GameServer()->m_apPlayers[ClientId]->Block().IsLoggedIn())
	{
		AccId = GameServer()->m_apPlayers[ClientId]->Block().m_Account.m_Id;
		str_copy(aAcc, GameServer()->m_apPlayers[ClientId]->Block().m_Account.m_aName, sizeof(aAcc));
	}
	auto pReq = std::make_unique<CSqlWhoIsLog>(nullptr);
	str_copy(pReq->m_aIp, aIp, sizeof(pReq->m_aIp));
	str_copy(pReq->m_aName, aName, sizeof(pReq->m_aName));
	pReq->m_AccountId = AccId;
	str_copy(pReq->m_aAccountName, aAcc, sizeof(pReq->m_aAccountName));
	str_copy(pReq->m_aSource, "leave", sizeof(pReq->m_aSource));
	const std::shared_ptr<CSqlWhoIsLog> pShared = std::move(pReq);
	m_pWorker->Enqueue([pShared](IDbConnection *pSql) {
		if(!pSql)
			return;
		char aErr[256] = {0};
		RunLog(pSql, pShared.get(), aErr, sizeof(aErr));
	},
		/* Droppable */ true);
}

void CWhoIs::SnapshotTick()
{
	int64_t Now = Server()->Tick();
	int MinutesCfg = std::clamp(g_Config.m_SvWhoisSnapshotMinutes, 0, 24 * 60);
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
				if(!pSql)
				{
					char aLine[192];
					str_copy(aLine, "whois purge failed: database unavailable", sizeof(aLine));
					pRes->m_vLines.emplace_back(aLine);
					pRes->m_Success = false;
					pRes->m_Completed.store(true, std::memory_order_release);
					return;
				}
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
				const SWhoisDialect Dialect = SWhoisDialect::Of(pSql);
				char aEpochCreated[64];
				char aEpochLastSeen[64];
				str_format(aEpochCreated, sizeof(aEpochCreated), Dialect.m_pEpochOf, "created_at");
				str_format(aEpochLastSeen, sizeof(aEpochLastSeen), Dialect.m_pEpochOf, "last_seen");
				if(!Failed)
				{
					str_format(aStmt, sizeof(aStmt),
						"DELETE FROM %s WHERE %s < %s - %" PRId64,
						TBL_WHOIS_CONNECTIONS, aEpochCreated, Dialect.m_pEpochNow, Seconds);
					Failed = !pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || !pSql->ExecuteUpdate(&Rows, aErr, sizeof(aErr));
				}
				if(!Failed)
				{
					int Tmp = 0;
					str_format(aStmt, sizeof(aStmt),
						"DELETE FROM %s WHERE %s < %s - %" PRId64,
						TBL_WHOIS_AGG_NAMES_BY_IP, aEpochLastSeen, Dialect.m_pEpochNow, Seconds);
					Failed = !pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || !pSql->ExecuteUpdate(&Tmp, aErr, sizeof(aErr));
					str_format(aStmt, sizeof(aStmt),
						"DELETE FROM %s WHERE %s < %s - %" PRId64,
						TBL_WHOIS_AGG_IPS_BY_NAME, aEpochLastSeen, Dialect.m_pEpochNow, Seconds);
					Tmp = 0;
					Failed = Failed || !pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || !pSql->ExecuteUpdate(&Tmp, aErr, sizeof(aErr));
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
			const std::shared_ptr<CSqlWhoIsLog> pShared = std::move(pReq);
			m_pWorker->Enqueue([pShared](IDbConnection *pSql) {
				if(!pSql)
					return;
				char aErr[256] = {0};
				RunLog(pSql, pShared.get(), aErr, sizeof(aErr));
			},
				/* Droppable */ true);
			m_aLastSnapshotTick[i] = Now;
		}
	}
}

void CWhoIs::CmdWhoisStr(int RequesterId, int Mode, int Cutoff, const char *pSearch, const std::shared_ptr<CWhoIsResult> &pExisting)
{
	std::shared_ptr<CWhoIsResult> pRes = pExisting ? pExisting : std::make_shared<CWhoIsResult>();
	pRes->m_TargetClientId = RequesterId; // retained for potential future use
	str_copy(pRes->m_aTag, TAG, sizeof(pRes->m_aTag));
	pRes->m_SendToChat = false; // enforce no chat output
	auto pReq = std::make_unique<CSqlWhoIsQuery>(pRes);
	pReq->m_Mode = Mode;
	pReq->m_Cutoff = Cutoff;
	str_copy(pReq->m_aSearch, pSearch, sizeof(pReq->m_aSearch));
	const std::shared_ptr<CSqlWhoIsQuery> pShared = std::move(pReq);
	m_pWorker->Enqueue([pShared](IDbConnection *pSql) {
		char aErr[256] = {0};
		auto pResLocal = std::static_pointer_cast<CWhoIsResult>(pShared->m_pResult);
		if(!pSql)
		{
			pResLocal->m_vLines.emplace_back("whois failed: database unavailable");
			pResLocal->m_Success = false;
			pResLocal->m_Completed.store(true, std::memory_order_release);
			return;
		}
		const bool Failed = RunQuery(pSql, pShared.get(), pResLocal, aErr, sizeof(aErr));
		if(Failed && !pResLocal->m_Completed.load(std::memory_order_acquire))
		{
			char aLine[256];
			str_format(aLine, sizeof(aLine), "whois failed: %s", aErr[0] ? aErr : "unknown error");
			pResLocal->m_vLines.emplace_back(aLine);
			pResLocal->m_Success = false;
			pResLocal->m_Completed.store(true, std::memory_order_release);
		}
	});

	// An rcon caller has no other way to see the answer: console output is
	// routed to whoever is running the command, and only while it runs. So the
	// answer is waited for here rather than printed from DrainAndPrintResults --
	// but on the worker's connection, and with a deadline, so a slow database
	// costs a late reply instead of a frozen server.
	if(RequesterId < 0)
	{
		const int TimeoutMs = std::clamp(g_Config.m_SvWhoisRconTimeoutMs, 100, 30 * 1000);
		const int64_t Deadline = time_get() + (int64_t)TimeoutMs * time_freq() / 1000;
		while(!pRes->m_Completed.load(std::memory_order_acquire) && time_get() < Deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		if(!pRes->m_Completed.load(std::memory_order_acquire))
			pRes->m_vLines.emplace_back("whois: the database did not answer in time, try again");
		PrintLines(GameServer(), RequesterId, pRes->m_vLines, pRes->m_aTag, pRes->m_SendToChat);
		pRes->m_vLines.clear();
	}
}

void CWhoIs::DrainAndPrintResults()
{
	for(auto It = m_vInternalResults.begin(); It != m_vInternalResults.end();)
	{
		if((*It)->m_Completed.load(std::memory_order_acquire))
		{
			PrintLines(GameServer(), (*It)->m_TargetClientId, (*It)->m_vLines, (*It)->m_aTag, (*It)->m_SendToChat);
			It = m_vInternalResults.erase(It);
		}
		else
		{
			++It;
		}
	}
}

void CWhoIs::CmdWhoisAccount(int RequesterId, const char *pAccName)
{
	CmdWhoisStr(RequesterId, 2, 0, pAccName);
}

void CWhoIs::PurgeNow(int RetentionMonths, bool Silent)
{
	if(RetentionMonths <= 0)
		return;
	auto pRes = Silent ? nullptr : std::make_shared<CWhoIsResult>();
	int Months = RetentionMonths;
	if(pRes)
		m_vInternalResults.push_back(pRes);
	m_pWorker->Enqueue([pRes, Months](IDbConnection *pSql) {
		if(!pSql)
		{
			if(pRes)
			{
				char aLine[192];
				str_copy(aLine, "whois purge failed: database unavailable", sizeof(aLine));
				pRes->m_vLines.emplace_back(aLine);
				pRes->m_Success = false;
				pRes->m_Completed.store(true, std::memory_order_release);
			}
			return;
		}
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
		const SWhoisDialect Dialect = SWhoisDialect::Of(pSql);
		char aEpochCreated[64];
		char aEpochLastSeen[64];
		str_format(aEpochCreated, sizeof(aEpochCreated), Dialect.m_pEpochOf, "created_at");
		str_format(aEpochLastSeen, sizeof(aEpochLastSeen), Dialect.m_pEpochOf, "last_seen");
		if(!Failed)
		{
			str_format(aStmt, sizeof(aStmt),
				"DELETE FROM %s WHERE %s < %s - %" PRId64,
				TBL_WHOIS_CONNECTIONS, aEpochCreated, Dialect.m_pEpochNow, Seconds);
			Failed = !pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || !pSql->ExecuteUpdate(&Rows, aErr, sizeof(aErr));
		}
		if(!Failed)
		{
			int Tmp = 0;
			str_format(aStmt, sizeof(aStmt),
				"DELETE FROM %s WHERE %s < %s - %" PRId64,
				TBL_WHOIS_AGG_NAMES_BY_IP, aEpochLastSeen, Dialect.m_pEpochNow, Seconds);
			Failed = !pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || !pSql->ExecuteUpdate(&Tmp, aErr, sizeof(aErr));
			str_format(aStmt, sizeof(aStmt),
				"DELETE FROM %s WHERE %s < %s - %" PRId64,
				TBL_WHOIS_AGG_IPS_BY_NAME, aEpochLastSeen, Dialect.m_pEpochNow, Seconds);
			Tmp = 0;
			Failed = Failed || !pSql->PrepareStatement(aStmt, aErr, sizeof(aErr)) || !pSql->ExecuteUpdate(&Tmp, aErr, sizeof(aErr));
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
