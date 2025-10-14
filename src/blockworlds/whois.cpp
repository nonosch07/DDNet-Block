#include "whois.h"

#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>

static const char *TAG = "whois";

CWhoIs::CWhoIs(CGameContext *pGameServer, CDbConnectionPool *pPool) :
	m_pGameServer(pGameServer), m_pPool(pPool)
{
	mem_zero(m_aLastSnapshotTick, sizeof(m_aLastSnapshotTick));
	m_SnapshotIntervalTicks = pGameServer->Server()->TickSpeed() * 60; // every 60 seconds
	m_NextPurgeTick = 0;
}

IServer *CWhoIs::Server() const { return GameServer()->Server(); }

bool CWhoIs::ThreadEnsureSchema(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize)
{
	(void)pData;
	char aStmt[1024];
	// prefix
	const char *pfx = pSql->GetPrefix();
	// store IP and name normalized; store account id & name if available; ts utc
	str_format(aStmt, sizeof(aStmt),
		"CREATE TABLE IF NOT EXISTS %swhois_connections ("
		"id BIGINT PRIMARY KEY AUTO_INCREMENT,"
		"ip VARCHAR(45) NOT NULL,"
		"name VARCHAR(%d) NOT NULL,"
		"account_id INT NOT NULL DEFAULT 0,"
		"account_name VARCHAR(16) NOT NULL DEFAULT '',"
		"source ENUM('join','snapshot','leave') NOT NULL,"
		"created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		"KEY whois_idx_ip (ip),"
		"KEY whois_idx_name (name),"
		"KEY whois_idx_ip_name (ip,name),"
		"KEY whois_idx_name_ip (name,ip),"
		"KEY whois_idx_accid (account_id),"
		"KEY whois_idx_created_at (created_at)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;",
		pfx, MAX_NAME_LENGTH_SQL);
	if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	if(pSql->ExecuteUpdate(nullptr, pError, ErrorSize))
		return true;
	return false;
}

void CWhoIs::EnsureSchema()
{
	char aErr[256] = {};
	m_pPool->Execute(&CWhoIs::ThreadEnsureSchema, std::make_unique<ISqlData>(nullptr), "whois ensure schema");
}

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

void CWhoIs::NormalizeIpNoPort(char *pIp)
{
	char *pColon = str_rchr(pIp, ':');
	if(pColon)
	{
		int numColons = 0;
		for(char *p = pIp; *p; ++p)
			if(*p == ':')
				numColons++;
		if(numColons == 1)
			*pColon = '\0';
	}
}

bool CWhoIs::ThreadLog(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	(void)w; // we don't use backup phases here explicitly (pool handles), but compatible signature
	const auto *pReq = static_cast<const CSqlWhoIsLog *>(pData);
	char aStmt[512];
	// created_at uses default CURRENT_TIMESTAMP in schema; don't bind timestamp to avoid sqlite/mysql differences since we'd be fucked
	str_format(aStmt, sizeof(aStmt),
		"INSERT INTO %swhois_connections (ip, name, account_id, account_name, source) VALUES (?, ?, ?, ?, ?)",
		pSql->GetPrefix());
	if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	pSql->BindString(1, pReq->m_aIp);
	pSql->BindString(2, pReq->m_aName);
	pSql->BindInt(3, pReq->m_AccountId);
	pSql->BindString(4, pReq->m_aAccountName[0] ? pReq->m_aAccountName : "");
	pSql->BindString(5, pReq->m_aSource);
	if(pSql->ExecuteUpdate(nullptr, pError, ErrorSize))
		return true;
	return false;
}

static void PrintLines(CGameContext *pGame, int TargetClientId, const std::vector<std::string> &vLines, const char *pTag, bool SendToChat)
{
	for(const auto &s : vLines)
	{
		if(SendToChat && TargetClientId >= 0)
		{
			char aBuf[1024];
			str_format(aBuf, sizeof(aBuf), "[%s]: %s", pTag, s.c_str());
			pGame->SendChatTarget(TargetClientId, aBuf);
		}
		else
		{
			pGame->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pTag, s.c_str());
		}
	}
}

bool CWhoIs::ThreadQuery(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize)
{
	const auto *pReq = static_cast<const CSqlWhoIsQuery *>(pData);
	auto pRes = std::static_pointer_cast<CWhoIsResult>(pData->m_pResult);
	char aStmt[768];

	const bool ByIp = pReq->m_Mode == 0;
	const int Cut = pReq->m_Cutoff;

	if(ByIp)
	{
		char aLike[64];
		char aIp[64];
		str_copy(aIp, pReq->m_aSearch, sizeof(aIp));
		NormalizeIpNoPort(aIp);
		if(Cut <= 0)
		{
			str_format(aStmt, sizeof(aStmt),
				"SELECT name, COUNT(*) AS cnt FROM %swhois_connections WHERE ip = ? GROUP BY name ORDER BY cnt DESC LIMIT 200",
				pSql->GetPrefix());
			if(pSql->PrepareStatement(aStmt, pError, ErrorSize)) return true;
			pSql->BindString(1, aIp);

			bool End = false;
			int Total = 0;
			int Distinct = 0;
			struct Entry { char Name[32]; int Cnt; };
			std::vector<Entry> vEntries;
			while(!pSql->Step(&End, pError, ErrorSize) && !End)
			{
				Entry e{};
				pSql->GetString(1, e.Name, sizeof(e.Name));
				e.Cnt = pSql->GetInt(2);
				vEntries.push_back(e);
				Total += e.Cnt;
			}
			Distinct = (int)vEntries.size();

			int Shown = 0;
			std::string List;
			for(const auto &e : vEntries)
			{
				if(Shown >= 50) break;
				char aTmp[96];
				str_format(aTmp, sizeof(aTmp), "%s (%d)", e.Name, e.Cnt);
				if(!List.empty()) List += ", ";
				List += aTmp;
				Shown++;
			}
			if((int)vEntries.size() > Shown)
			{
				char aTmp[64];
				str_format(aTmp, sizeof(aTmp), ", and %d more", (int)vEntries.size() - Shown);
				List += aTmp;
			}

			char aLine[1024];
			str_format(aLine, sizeof(aLine), "%s connected %d times with %d names: %s", aIp, Total, Distinct, List.empty() ? "-" : List.c_str());
			pRes->m_vLines.emplace_back(aLine);
		}
		else
		{
			// /24 or /16: cut trailing octets and do prefix match
			int dots = 0; int lastDotPos[4] = {-1,-1,-1,-1};
			for(int i = 0; aIp[i]; ++i)
				if(aIp[i] == '.') lastDotPos[dots++] = i;
			if((Cut == 1 && dots >= 3) || (Cut >= 2 && dots >= 2))
			{
				int cutPos = (Cut == 1) ? lastDotPos[2] : lastDotPos[1];
				if(cutPos > 0) { aIp[cutPos] = '\0'; }
			}
			str_format(aLike, sizeof(aLike), "%s.%%", aIp);

			// fetch (ip,name,count) capped total rows to limit workload
			str_format(aStmt, sizeof(aStmt),
				"SELECT ip, name, COUNT(*) AS cnt FROM %swhois_connections WHERE ip LIKE ? GROUP BY ip,name ORDER BY ip ASC, cnt DESC LIMIT 5000",
				pSql->GetPrefix());
			if(pSql->PrepareStatement(aStmt, pError, ErrorSize)) return true;
			pSql->BindString(1, aLike);

			bool End = false;
			struct NameCnt { std::string Name; int Cnt; };
			// group by ip in memory, keeping top N names per ip
			std::vector<std::string> vIps;
			std::vector<std::vector<NameCnt>> vTopNames; // parallel vectors indexed by ip idx
			std::vector<int> vTotals;

			while(!pSql->Step(&End, pError, ErrorSize) && !End)
			{
				char aResIp[64] = {0};
				char aName[32] = {0};
				pSql->GetString(1, aResIp, sizeof(aResIp));
				pSql->GetString(2, aName, sizeof(aName));
				int Cnt = pSql->GetInt(3);

				// find or add ip index
				int idx = -1;
				if(!vIps.empty() && vIps.back() == aResIp)
					idx = (int)vIps.size() - 1;
				else
				{
					// new ip (due to ORDER BY ip)
					vIps.emplace_back(aResIp);
					vTopNames.emplace_back();
					vTotals.emplace_back(0);
					idx = (int)vIps.size() - 1;
					if(idx >= 100) // cap number of distinct IPs
						break;
				}

				vTotals[idx] += Cnt;
				auto &vec = vTopNames[idx];
				if((int)vec.size() < 50)
					vec.push_back({std::string(aName), Cnt});
			}

			for(size_t i = 0; i < vIps.size(); ++i)
			{
				// format line
				std::string List;
				for(size_t j = 0; j < vTopNames[i].size(); ++j)
				{
					char aTmp[96];
					str_format(aTmp, sizeof(aTmp), "%s (%d)", vTopNames[i][j].Name.c_str(), vTopNames[i][j].Cnt);
					if(!List.empty()) List += ", ";
					List += aTmp;
				}
				char aLine[1024];
				str_format(aLine, sizeof(aLine), "%s connected %d times with %d names: %s", vIps[i].c_str(), vTotals[i], (int)vTopNames[i].size(), List.empty() ? "-" : List.c_str());
				pRes->m_vLines.emplace_back(aLine);
			}

			if(vIps.empty())
			{
				char aMsg[128];
				str_format(aMsg, sizeof(aMsg), "No entries found for IP '%s'", pReq->m_aSearch);
				pRes->m_vLines.emplace_back(aMsg);
			}
		}
	}
	else
	{
		// by name: list distinct IPs and counts for that name
		str_format(aStmt, sizeof(aStmt),
			"SELECT ip, COUNT(*) AS cnt FROM %swhois_connections WHERE name = ? GROUP BY ip ORDER BY cnt DESC LIMIT 2000",
			pSql->GetPrefix());
		if(pSql->PrepareStatement(aStmt, pError, ErrorSize)) return true;
		pSql->BindString(1, pReq->m_aSearch);

		bool End = false;
		int Total = 0;
		struct IPCnt { char Ip[64]; int Cnt; };
		std::vector<IPCnt> vEntries;
		while(!pSql->Step(&End, pError, ErrorSize) && !End)
		{
			IPCnt e{};
			pSql->GetString(1, e.Ip, sizeof(e.Ip));
			e.Cnt = pSql->GetInt(2);
			vEntries.push_back(e);
			Total += e.Cnt;
			if((int)vEntries.size() >= 500) break; // safety cap
		}
		int Distinct = (int)vEntries.size();

		int Shown = 0;
		std::string List;
		for(const auto &e : vEntries)
		{
			if(Shown >= 50) break;
			char aTmp[96];
			str_format(aTmp, sizeof(aTmp), "%s (%d)", e.Ip, e.Cnt);
			if(!List.empty()) List += ", ";
			List += aTmp;
			Shown++;
		}
		if(Distinct > Shown)
		{
			char aTmp[64];
			str_format(aTmp, sizeof(aTmp), ", and %d more", Distinct - Shown);
			List += aTmp;
		}
		char aLine[1024];
		str_format(aLine, sizeof(aLine), "%s connected %d times with %d ips: %s", pReq->m_aSearch, Total, Distinct, List.empty() ? "-" : List.c_str());
		pRes->m_vLines.emplace_back(aLine);
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

bool CWhoIs::ThreadPurge(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	(void)w;
	const auto *pReq = static_cast<const CSqlWhoIsPurge *>(pData);

	char aNowTs[128];
	char aCreatedAtTs[128];
	pSql->ToUnixTimestamp("CURRENT_TIMESTAMP", aNowTs, sizeof(aNowTs));
	pSql->ToUnixTimestamp("created_at", aCreatedAtTs, sizeof(aCreatedAtTs));

	// convert retention months to seconds approx: 30 days per month
	int Months = pReq->m_RetentionMonths;
	if(Months <= 0)
		return false; // disabled
	const int64_t Seconds = (int64_t)Months * 30 * 24 * 60 * 60;

	char aStmt[512];

	str_format(aStmt, sizeof(aStmt),
		"DELETE FROM %swhois_connections WHERE (%s) < ((%s) - %" PRId64 ")",
		pSql->GetPrefix(), aCreatedAtTs, aNowTs, (int64_t)Seconds);
	if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	int Rows = 0;
	if(pSql->ExecuteUpdate(&Rows, pError, ErrorSize))
		return true;
	if(pData->m_pResult)
	{
		auto pRes = std::static_pointer_cast<CWhoIsResult>(pData->m_pResult);
		char aLine[128];
		str_format(aLine, sizeof(aLine), "purge removed %d old whois rows (>%d months)", Rows, Months);
		pRes->m_vLines.emplace_back(aLine);
		pRes->m_SendToChat = false; // console-only
		pRes->m_TargetClientId = -1;
		pRes->m_Completed.store(true, std::memory_order_release);
		pRes->m_Success = true;
	}
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
	m_pPool->ExecuteWrite(&CWhoIs::ThreadLog, std::move(pReq), "whois log join");
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
	m_pPool->ExecuteWrite(&CWhoIs::ThreadLog, std::move(pReq), "whois log leave");
}

void CWhoIs::SnapshotTick()
{
	int64_t Now = Server()->Tick();
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
			auto pReq = std::make_unique<CSqlWhoIsPurge>(pRes);
			pReq->m_RetentionMonths = Months;
			m_vInternalResults.push_back(pRes);
			m_pPool->ExecuteWrite(&CWhoIs::ThreadPurge, std::move(pReq), "whois purge");
		}
		m_NextPurgeTick = Now + Server()->TickSpeed() * 60 * 60 * 24;
	}
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!Server()->ClientIngame(i))
			continue;
		if(Now - m_aLastSnapshotTick[i] < m_SnapshotIntervalTicks)
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
		m_pPool->ExecuteWrite(&CWhoIs::ThreadLog, std::move(pReq), "whois snapshot");
		m_aLastSnapshotTick[i] = Now;
	}
}

void CWhoIs::CmdWhois(int RequesterId, int Mode, int Cutoff, int TargetClientId, std::shared_ptr<CWhoIsResult> pExisting)
{
	if(!Server()->ClientIngame(TargetClientId))
	{
		GameServer()->SendChatTarget(RequesterId, "Target client not in game");
		return;
	}
	char aIp[48] = {0}, aName[24] = {0};
	Server()->GetClientAddr(TargetClientId, aIp, sizeof(aIp));
	NormalizeIpNoPort(aIp);
	str_copy(aName, Server()->ClientName(TargetClientId), sizeof(aName));
	bool ByIp = Mode == 0;
	const char *pSearch = ByIp ? aIp : aName;
	CmdWhoisStr(RequesterId, Mode, Cutoff, pSearch, pExisting);
}

void CWhoIs::CmdWhoisStr(int RequesterId, int Mode, int Cutoff, const char *pSearch, std::shared_ptr<CWhoIsResult> pExisting)
{
	std::shared_ptr<CWhoIsResult> pRes = pExisting ? pExisting : std::make_shared<CWhoIsResult>();
	pRes->m_TargetClientId = RequesterId;
	str_copy(pRes->m_aTag, TAG, sizeof(pRes->m_aTag));
	auto pReq = std::make_unique<CSqlWhoIsQuery>(pRes);
	pReq->m_Mode = Mode;
	pReq->m_Cutoff = Cutoff;
	str_copy(pReq->m_aSearch, pSearch, sizeof(pReq->m_aSearch));
	m_pPool->Execute(&CWhoIs::ThreadQuery, std::move(pReq), "whois query");
}

void CWhoIs::DrainAndPrintResults()
{
	for(auto it = m_vInternalResults.begin(); it != m_vInternalResults.end();) {
		if((*it)->m_Completed.load(std::memory_order_acquire)) {
			PrintLines(GameServer(), (*it)->m_TargetClientId, (*it)->m_vLines, (*it)->m_aTag, (*it)->m_SendToChat);
			it = m_vInternalResults.erase(it);
		} else {
			++it;
		}
	}
}

void CWhoIs::PurgeNow(int RetentionMonths, bool Silent)
{
	if(RetentionMonths <= 0)
		return;
	auto pRes = Silent ? nullptr : std::make_shared<CWhoIsResult>();
	if(pRes)
	{
		str_copy(pRes->m_aTag, TAG, sizeof(pRes->m_aTag));
		pRes->m_SendToChat = false; // console only
		m_vInternalResults.push_back(pRes);
	}
	auto pReq = std::make_unique<CSqlWhoIsPurge>(pRes);
	pReq->m_RetentionMonths = RetentionMonths;
	m_pPool->ExecuteWrite(&CWhoIs::ThreadPurge, std::move(pReq), "whois purge forced");
}
