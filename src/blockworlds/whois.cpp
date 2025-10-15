#include "whois.h"

#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include "whois.h"

#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <atomic>
#include "sql_prefix.h"

static const char *TAG = "whois";

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

void CWhoIs::NormalizeIpNoPort(char *pIp)
{
	if(!pIp || !*pIp)
		return;
	// handle IPv6 - might have fucked something up here idk
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
	// handle IPv4 - should work correctly
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

bool CWhoIs::ThreadLog(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{

	if(w == Write::BACKUP_FIRST || w == Write::NORMAL_SUCCEEDED)
		return false; // no-op success on backup
	if(w == Write::NORMAL_FAILED)
		return true; // propagate primary failure
	const auto *pReq = static_cast<const CSqlWhoIsLog *>(pData);
	char aStmt[512];
	str_format(aStmt, sizeof(aStmt),
		"INSERT INTO %s (ip, name, account_id, source) VALUES (?, ?, ?, ?)",
		TBL_WHOIS_CONNECTIONS);
	if(pSql->PrepareStatement(aStmt, pError, ErrorSize))
		return true;
	pSql->BindString(1, pReq->m_aIp);
	pSql->BindString(2, pReq->m_aName);
	pSql->BindInt(3, pReq->m_AccountId);
	pSql->BindString(4, pReq->m_aSource);
	if(pSql->ExecuteUpdate(nullptr, pError, ErrorSize))
		return true;
	return false;
}

static void PrintLines(CGameContext *pGame, int TargetClientId, const std::vector<std::string> &vLines, const char *pTag, bool SendToChat)
{
	(void)TargetClientId; (void)SendToChat; // whois no longer outputs to chat
	for(const auto &s : vLines)
	{
		pGame->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pTag, s.c_str());
	}
}

bool CWhoIs::ThreadQuery(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize)
{
	const auto *pReq = static_cast<const CSqlWhoIsQuery *>(pData);
	auto pRes = std::static_pointer_cast<CWhoIsResult>(pData->m_pResult);
	char aStmt[1024];

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
				"SELECT c.name, COUNT(*) AS cnt, MAX(c.created_at) AS last_seen, COUNT(DISTINCT NULLIF(c.account_id,0)) AS accs,"
				" (SELECT ac.name FROM %s ac WHERE ac.id = ("
				"   SELECT wi.account_id FROM %s wi"
				"   WHERE wi.ip = ? AND wi.account_id <> 0"
				"   ORDER BY wi.created_at DESC LIMIT 1)) AS acc_name"
				" FROM %s c WHERE c.ip = ? GROUP BY c.name ORDER BY cnt DESC",
				TBL_ACCOUNTS_CORE, TBL_WHOIS_CONNECTIONS, TBL_WHOIS_CONNECTIONS);
			if(pSql->PrepareStatement(aStmt, pError, ErrorSize)) return true;
			pSql->BindString(1, aIp);
			pSql->BindString(2, aIp);

			bool End = false;
			int Total = 0;
			int Distinct = 0;
			struct Entry { char Name[32]; int Cnt; char Last[20]; int Accs; char AccName[16]; };
			std::vector<Entry> vEntries;
			while(!pSql->Step(&End, pError, ErrorSize) && !End)
			{
				Entry e{};
				pSql->GetString(1, e.Name, sizeof(e.Name));
				e.Cnt = pSql->GetInt(2);
				char aLastFull[32] = {0};
				if(!pSql->IsNull(3))
					pSql->GetString(3, aLastFull, sizeof(aLastFull));
				if(aLastFull[0])
					str_copy(e.Last, aLastFull, 11);
				else
					e.Last[0] = '\0';
				e.Accs = pSql->GetInt(4);
				mem_zero(e.AccName, sizeof(e.AccName));
				if(!pSql->IsNull(5))
					pSql->GetString(5, e.AccName, sizeof(e.AccName));
				vEntries.push_back(e);
				Total += e.Cnt;
			}
			Distinct = (int)vEntries.size();

			char aHead[256];
			str_format(aHead, sizeof(aHead), "%s connected %d times with %d names:", aIp, Total, Distinct);
			pRes->m_vLines.emplace_back(aHead);
			for(const auto &e : vEntries)
			{
				char aTmp[256];
				// Build details
				char aDetails[160] = {0};
				if(e.Last[0])
					str_format(aDetails, sizeof(aDetails), "last: %s", e.Last);
				char aAccPart[64] = {0};
				if(e.AccName[0])
					str_format(aAccPart, sizeof(aAccPart), ", account: %s", e.AccName);
				else if(e.Accs > 0)
					str_copy(aAccPart, ", logins: yes", sizeof(aAccPart));

				if(aDetails[0])
					str_format(aTmp, sizeof(aTmp), " - %s (%d), %s%s", e.Name, e.Cnt, aDetails, aAccPart);
				else
					str_format(aTmp, sizeof(aTmp), " - %s (%d)%s", e.Name, e.Cnt, aAccPart);
				pRes->m_vLines.emplace_back(aTmp);
			}
		}
		else
		{
			// /8, /24 or /16: cut trailing octets and do prefix match. Fallback to exact matc
			int dots = 0; int lastDotPos[4] = {-1,-1,-1,-1};
			for(int i = 0; aIp[i]; ++i)
				if(aIp[i] == '.') { if(dots < 4) lastDotPos[dots] = i; dots++; }

			bool CanCut = (Cut == 1 && dots >= 3) || (Cut == 2 && dots >= 2) || (Cut == 3 && dots >= 1);
			if(CanCut)
			{
				int cutPos = (Cut == 1) ? lastDotPos[2] : (Cut == 2 ? lastDotPos[1] : lastDotPos[0]);
				if(cutPos > 0) { aIp[cutPos] = '\0'; }
				str_format(aLike, sizeof(aLike), "%s.%%", aIp);

				str_format(aStmt, sizeof(aStmt),
					"SELECT c.ip, c.name, COUNT(*) AS cnt, MAX(c.created_at) AS last_seen, COUNT(DISTINCT NULLIF(c.account_id,0)) AS accs,"
					" (SELECT ac.name FROM %s ac WHERE ac.id = ("
					"   SELECT wi.account_id FROM %s wi"
					"   WHERE wi.ip = c.ip AND wi.account_id <> 0"
					"   ORDER BY wi.created_at DESC LIMIT 1)) AS acc_name"
					" FROM %s c WHERE c.ip LIKE ? GROUP BY c.ip,c.name ORDER BY c.ip ASC, cnt DESC",
					TBL_ACCOUNTS_CORE, TBL_WHOIS_CONNECTIONS, TBL_WHOIS_CONNECTIONS);
				if(pSql->PrepareStatement(aStmt, pError, ErrorSize)) return true;
				pSql->BindString(1, aLike);
			}
			else
			{
				str_format(aStmt, sizeof(aStmt),
					"SELECT c.name, COUNT(*) AS cnt, MAX(c.created_at) AS last_seen, COUNT(DISTINCT NULLIF(c.account_id,0)) AS accs,"
					" (SELECT ac.name FROM %s ac WHERE ac.id = ("
					"   SELECT wi.account_id FROM %s wi"
					"   WHERE wi.ip = ? AND wi.account_id <> 0"
					"   ORDER BY wi.created_at DESC LIMIT 1)) AS acc_name"
					" FROM %s c WHERE c.ip = ? GROUP BY c.name ORDER BY cnt DESC",
					TBL_ACCOUNTS_CORE, TBL_WHOIS_CONNECTIONS, TBL_WHOIS_CONNECTIONS);
				if(pSql->PrepareStatement(aStmt, pError, ErrorSize)) return true;
				pSql->BindString(1, aIp);
				pSql->BindString(2, aIp);
			}

			bool End = false;
			std::string curIp;
			int curTotal = 0;
			struct NameInfo { std::string Name; int Cnt; char Last[20]; int Accs; char AccName[16]; };
			std::vector<NameInfo> curNames;
			auto FlushCur = [&]() {
				if(curIp.empty()) return;
				char aHead[256];
				str_format(aHead, sizeof(aHead), "%s connected %d times with %d names:", curIp.c_str(), curTotal, (int)curNames.size());
				pRes->m_vLines.emplace_back(aHead);
				for(const auto &nn : curNames)
				{
					char aTmp[256];
					char aDetails[160] = {0};
					if(nn.Last[0])
						str_format(aDetails, sizeof(aDetails), "last: %s", nn.Last);
					char aAccPart[64] = {0};
					if(nn.AccName[0])
						str_format(aAccPart, sizeof(aAccPart), ", account: %s", nn.AccName);
					else if(nn.Accs > 0)
						str_copy(aAccPart, ", logins: yes", sizeof(aAccPart));

					if(aDetails[0])
						str_format(aTmp, sizeof(aTmp), " - %s (%d), %s%s", nn.Name.c_str(), nn.Cnt, aDetails, aAccPart);
					else
						str_format(aTmp, sizeof(aTmp), " - %s (%d)%s", nn.Name.c_str(), nn.Cnt, aAccPart);
					pRes->m_vLines.emplace_back(aTmp);
				}
				curIp.clear(); curTotal = 0; curNames.clear();
			};

			while(!pSql->Step(&End, pError, ErrorSize) && !End)
			{
				char aResIp[64] = {0};
				char aName[32] = {0};
				pSql->GetString(1, aResIp, sizeof(aResIp));
				pSql->GetString(2, aName, sizeof(aName));
				int Cnt = pSql->GetInt(3);
				char aLastFull[32] = {0};
				if(!pSql->IsNull(4))
					pSql->GetString(4, aLastFull, sizeof(aLastFull));
				int Accs = pSql->GetInt(5);
				char aAccName[16] = {0};
				if(!pSql->IsNull(6))
					pSql->GetString(6, aAccName, sizeof(aAccName));
				if(curIp.empty()) curIp = aResIp;
				if(curIp != aResIp)
				{
					FlushCur();
					curIp = aResIp;
				}
				curTotal += Cnt;
				NameInfo ni{}; ni.Name = aName; ni.Cnt = Cnt; ni.Accs = Accs; ni.Last[0] = '\0'; ni.AccName[0] = '\0';
				if(aLastFull[0]) str_copy(ni.Last, aLastFull, 11);
				if(aAccName[0]) str_copy(ni.AccName, aAccName, sizeof(ni.AccName));
				curNames.emplace_back(std::move(ni));
			}
			FlushCur();

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
		str_format(aStmt, sizeof(aStmt),
			"SELECT ip, COUNT(*) AS cnt, MAX(created_at) AS last_seen FROM %s WHERE name = ? GROUP BY ip ORDER BY cnt DESC",
			TBL_WHOIS_CONNECTIONS);
		if(pSql->PrepareStatement(aStmt, pError, ErrorSize)) return true;
		pSql->BindString(1, pReq->m_aSearch);

		bool End = false;
		int Total = 0;
		struct IPCnt { char Ip[64]; int Cnt; char Last[20]; };
		std::vector<IPCnt> vEntries;
		while(!pSql->Step(&End, pError, ErrorSize) && !End)
		{
			IPCnt e{};
			pSql->GetString(1, e.Ip, sizeof(e.Ip));
			e.Cnt = pSql->GetInt(2);
			char aLastFull[32] = {0};
			if(!pSql->IsNull(3))
				pSql->GetString(3, aLastFull, sizeof(aLastFull));
			if(aLastFull[0]) str_copy(e.Last, aLastFull, 11);
			vEntries.push_back(e);
			Total += e.Cnt;
		}
		int Distinct = (int)vEntries.size();
		char aHead[256];
		str_format(aHead, sizeof(aHead), "%s connected %d times with %d ips:", pReq->m_aSearch, Total, Distinct);
		pRes->m_vLines.emplace_back(aHead);
		for(const auto &e : vEntries)
		{
			char aTmp[160];
			if(e.Last[0])
				str_format(aTmp, sizeof(aTmp), " - %s (%d), last: %s", e.Ip, e.Cnt, e.Last);
			else
				str_format(aTmp, sizeof(aTmp), " - %s (%d)", e.Ip, e.Cnt);
			pRes->m_vLines.emplace_back(aTmp);
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

bool CWhoIs::ThreadPurge(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	if(w == Write::BACKUP_FIRST || w == Write::NORMAL_SUCCEEDED)
		return false;
	if(w == Write::NORMAL_FAILED)
		return true;
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
		"DELETE FROM %s WHERE (%s) < ((%s) - %" PRId64 ")",
		TBL_WHOIS_CONNECTIONS, aCreatedAtTs, aNowTs, (int64_t)Seconds);
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
			auto pReq = std::make_unique<CSqlWhoIsPurge>(pRes);
			pReq->m_RetentionMonths = Months;
			m_vInternalResults.push_back(pRes);
			m_pPool->ExecuteWrite(&CWhoIs::ThreadPurge, std::move(pReq), "whois purge");
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
			m_pPool->ExecuteWrite(&CWhoIs::ThreadLog, std::move(pReq), "whois snapshot");
			m_aLastSnapshotTick[i] = Now;
		}
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
	pRes->m_TargetClientId = RequesterId; // retained for potential future use
	str_copy(pRes->m_aTag, TAG, sizeof(pRes->m_aTag));
	pRes->m_SendToChat = false; // enforce no chat output
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
