#include "clans.h"

#include "engine/shared/config.h"
#include "sql_prefix.h"

#include <engine/server/databases/connection.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/bw_context.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>

void ToLowercase(char *str)
{
	for(; *str; ++str)
	{
		if(*str >= 'A' && *str <= 'Z')
			*str = (char)(*str - 'A' + 'a');
	}
}

static void ToLowercaseAscii(std::string &Str)
{
	for(char &c : Str)
	{
		if(c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	}
}

// --- Utility: trim leading and trailing spaces (ASCII) in-place ---
static void TrimSpaces(char *pStr)
{
	if(!pStr)
		return;
	int Len = str_length(pStr);
	int Start = 0;
	while(Start < Len && pStr[Start] == ' ')
		Start++;
	int End = Len - 1;
	while(End >= Start && pStr[End] == ' ')
		End--;
	int NewLen = End - Start + 1;
	if(NewLen < 0)
		NewLen = 0; // all spaces
	if(Start > 0 && NewLen > 0)
		mem_move(pStr, pStr + Start, NewLen);
	pStr[NewLen] = '\0';
}

// --- CClanResult Implementation ---
CClanResult::CClanResult()
{
	for(auto &Msg : m_aaMessages)
		Msg[0] = '\0';
}

// --- CClanManager Constructor / Destructor ---
CClanManager::CClanManager(CGameContext *pGameServer, CDbConnectionPool *pPool) :
	m_pPool(pPool), m_pGameServer(pGameServer)
{
	m_pServer = m_pGameServer->Server();
}

CClanManager::~CClanManager() = default;

// for thread safety of m_vClansData and clan maps
std::mutex g_ClansDataMutex;
// definitions for global clan maps and mutex for external linkage
std::unordered_map<int, CClansData> g_ClanIdMap;
std::unordered_map<std::string, int> g_ClanNameToId;

// save de-duplication state
struct SClanSaveState
{
	bool m_Queued{false};
	bool m_InFlight{false};
	int m_LastEnqueueTick{0};
};

static std::unordered_map<int, SClanSaveState> g_ClanSaveState; // by clan id

static bool TryMarkSaveQueued_Locked(int ClanId, int Now)
{
	SClanSaveState &St = g_ClanSaveState[ClanId];
	if(St.m_Queued || St.m_InFlight)
		return false;
	St.m_Queued = true;
	St.m_LastEnqueueTick = Now;
	return true;
}

static void MarkSaveStarted_Locked(int ClanId)
{
	SClanSaveState &St = g_ClanSaveState[ClanId];
	St.m_InFlight = true;
	St.m_Queued = false;
}

static void MarkSaveFinished_Locked(int ClanId)
{
	SClanSaveState &St = g_ClanSaveState[ClanId];
	St.m_InFlight = false;
	St.m_Queued = false;
}

std::shared_ptr<CClanResult> CClanManager::NewSqlClanResult(int ClientId)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pCurPlayer || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;

	auto pResult = std::make_shared<CClanResult>();
	pCurPlayer->Bw().m_ClanQueryResult.push(pResult);
	return pResult;
}

// --- Helper function to execute SQL threads for clan operations ---
// note: the connection pool's Execute method expects three skibidi arguments:
// (callback, std::unique_ptr<const ISqlData>, thread name)
void CClanManager::ExecClanThread(bool (*pFuncPtr)(IDbConnection *, const ISqlData *, char *, int),
	const char *pThreadName,
	int ClientId,
	const char *pClanName,
	const char *pUsername,
	int AccountId,
	int ClanId,
	int AuthLevel)
{
	auto pResult = NewSqlClanResult(ClientId);
	if(pResult == nullptr)
		return;
	auto pRequest = std::make_unique<CSqlClanRequest>(pResult, this);
	str_copy(pRequest->m_aClanName, pClanName, sizeof(pRequest->m_aClanName));
	if(pUsername)
		str_copy(pRequest->m_aUsername, pUsername, sizeof(pRequest->m_aUsername));
	else
		pRequest->m_aUsername[0] = '\0';
	pRequest->m_AccountId = AccountId;
	pRequest->m_ClientId = ClientId;
	pRequest->m_ClanId = ClanId;
	pRequest->m_NewAuthLevel = AuthLevel;

	m_pPool->Execute(pFuncPtr, std::move(pRequest), pThreadName);
}

bool CClanManager::RateLimitPlayer(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(pPlayer == 0)
		return true;
	if(pPlayer->m_LastSqlQuery + (int64_t)g_Config.m_SvSqlQueriesDelay * Server()->TickSpeed() >= Server()->Tick())
		return true;
	pPlayer->m_LastSqlQuery = Server()->Tick();
	return false;
}

// --- Public API Functions ---
void CClanManager::CreateClan(int ClientId, const char *pClanName, int AccountId)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecClanThread(CreateClanThread, "create clan", ClientId, pClanName, "", AccountId);
}

void CClanManager::DeleteClan(int ClientId, int ClanId, int AccountId)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecClanThread(DeleteClanThread, "delete clan", ClientId, "", "", AccountId, ClanId);
}

void CClanManager::ClanLeave(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	if(RateLimitPlayer(ClientId))
		return;
	ExecClanThread(ClanLeaveThread, "clan leave", ClientId, "", pPlayer->Bw().GetPlayerName(), pPlayer->Bw().GetAccId(), pPlayer->Bw().m_Account.m_ClanId);
}

void CClanManager::SaveClan(int ClientId, int ClanId)
{
	// de-dup saves: if a save is already queued or in-flight, skip enqueue
	int Now = GameServer()->Server()->Tick();
	{
		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
		if(!TryMarkSaveQueued_Locked(ClanId, Now))
			return;
	}
	ExecClanThread(SaveClanThread, "save clan", ClientId, "", "", 0, ClanId);
}

void CClanManager::QueueBackgroundSave(int ClanId)
{
	int Now = GameServer()->Server()->Tick();
	{
		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
		if(!TryMarkSaveQueued_Locked(ClanId, Now))
			return; // already queued or saving
	}
	auto pResult = std::make_shared<CClanResult>();
	auto pRequest = std::make_unique<CSqlClanRequest>(pResult, this);
	pRequest->m_ClientId = -1;
	pRequest->m_ClanId = ClanId;
	m_pPool->Execute(SaveClanThread, std::move(pRequest), "autosave clan");
}

void CClanManager::ShowTopClans(int ClientId)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecClanThread(ShowTopClansThread, "show top clans thread", ClientId, "", "", 0, 0);
}

void CClanManager::ShowClanMembers(int ClientId, int ClanId)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecClanThread(ShowClanMembersThread, "show clan members thread", ClientId, "", "", 0, ClanId);
}

void CClanManager::SetAuthLevel(int ClientId, const char *AccountName, int NewAuthLevel, int ClanId)
{
	if(RateLimitPlayer(ClientId))
		return;
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	ExecClanThread(SetAuthLevelThread, "auth level thread", ClientId, "", AccountName, pPlayer->Bw().GetAccId(), ClanId, NewAuthLevel);
}

void CClanManager::TransferLeadership(int ClientId, const char *AccountName, int ClanId)
{
	if(RateLimitPlayer(ClientId))
		return;
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	ExecClanThread(TransferLeadershipThread, "transfer leadership", ClientId, "", AccountName, pPlayer->Bw().GetAccId(), ClanId);
}

void CClanManager::RenameClan(int ClientId, int ClanId, const char *pNewClanName)
{
	if(RateLimitPlayer(ClientId))
		return;
	auto pResult = NewSqlClanResult(ClientId);
	if(pResult == nullptr)
		return;
	auto pRequest = std::make_unique<CSqlClanRequest>(pResult, this);
	pRequest->m_ClientId = ClientId;
	pRequest->m_ClanId = ClanId;

	if(CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId])
	{
		pRequest->m_AccountId = pPlayer->Bw().GetAccId();
	}
	str_copy(pRequest->m_aNewClanName, pNewClanName, sizeof(pRequest->m_aNewClanName));
	m_pPool->Execute(RenameClanThread, std::move(pRequest), "rename clan");
}

void CClanManager::AssignClan(int ClientId, const char *AccountName, int ClanId, int AccountId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	if(RateLimitPlayer(ClientId))
		return;
	ExecClanThread(AssignClanThread, "assign clan", ClientId, "", AccountName, AccountId, ClanId);
}

void CClanManager::RemoveFromClan(int ClientId, const char *AccountName, int ClanId)
{
	if(RateLimitPlayer(ClientId))
		return;
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	ExecClanThread(RemoveFromClanThread, "remove from clan", ClientId, "", AccountName, pPlayer->Bw().GetAccId(), ClanId);
}

void CClanManager::LoadAllClans()
{
	m_ClansLoaded = false;
	m_LastLoadAttemptTick = GameServer()->Server()->Tick();
	std::shared_ptr<CClanListResult> pResult = std::make_shared<CClanListResult>();
	auto pRequest = std::make_unique<CSqlClanListRequest>(pResult, this);
	m_pPool->Execute(LoadClansThread, std::move(pRequest), "load clans");
}

// --- SQL Thread Functions for Clan Operations ---
static bool CheckClanPermission(IDbConnection *pSqlServer, int AccountId, int ClanId, int RequiredAuthLevel, char *pError, int ErrorSize)
{
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "SELECT clanID, auth_level FROM %s WHERE account_id = ?;", TBL_ACCOUNTS_PROGRESS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		return false;
	pSqlServer->BindInt(1, AccountId);
	bool End = false;
	if(!pSqlServer->Step(&End, pError, ErrorSize))
		return false;
	if(End)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Permission denied: account not found.", ErrorSize);
		return false;
	}

	int DbClanId = pSqlServer->GetInt(1);
	int DbAuthLevel = pSqlServer->GetInt(2);

	if(DbClanId != ClanId || DbAuthLevel < RequiredAuthLevel)
	{
		dbg_msg("clan", "Permission denied details: account clan=%d required clan=%d, account auth=%d required auth=%d", DbClanId, ClanId, DbAuthLevel, RequiredAuthLevel);
		if(pError && ErrorSize > 0)
			str_copy(pError, "Permission denied: insufficient clan rights.", ErrorSize);
		return false;
	}
	return true;
}
bool CClanManager::CreateClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::DIRECT);

	char aNormalizedName[BW_CLAN_NAME_BUFFER_SIZE];
	str_copy(aNormalizedName, pData->m_aClanName, sizeof(aNormalizedName));
	TrimSpaces(aNormalizedName);
	const int NameChars = str_length(aNormalizedName);
	if(NameChars < 3 || NameChars > BW_CLAN_NAME_MAX_LENGTH)
	{
		str_copy(pResult->m_aaMessages[0], "Clan name must be between 3 and 11 characters (after trimming).", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_format(aBuf, sizeof(aBuf), "SELECT clanID FROM %s WHERE account_id = ? LIMIT 1;", TBL_ACCOUNTS_PROGRESS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 090: Failed to validate account state.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_AccountId);
	bool EndAcc = false;
	if(!pSqlServer->Step(&EndAcc, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 091: Failed to validate account state (step).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(EndAcc)
	{
		str_copy(pResult->m_aaMessages[0], "Account not found.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	int ExistingClan = pSqlServer->GetInt(1);
	if(ExistingClan != 0)
	{
		str_copy(pResult->m_aaMessages[0], "You are already in a clan. Leave it before creating a new one.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	// Case-insensitive uniqueness check
	str_format(aBuf, sizeof(aBuf), "SELECT id FROM %s WHERE LOWER(name) = LOWER(?) LIMIT 1;", TBL_CLANS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 100: Failed to validate clan name uniqueness.", sizeof(pResult->m_aaMessages[0]));
		return false; // treat as handled error
	}
	pSqlServer->BindString(1, aNormalizedName);
	int ExistingId = -1;
	bool EndCheck = false;
	if(!pSqlServer->Step(&EndCheck, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 100: Failed to validate clan name uniqueness (step).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(!EndCheck)
		ExistingId = pSqlServer->GetInt(1);
	if(ExistingId != -1)
	{
		str_copy(pResult->m_aaMessages[0], "Clan name already exists.", sizeof(pResult->m_aaMessages[0]));
		return true; // not a server error
	}

	if(!pSqlServer->PrepareStatement("BEGIN;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 099: Failed to start transaction.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	bool TxFailed = false;

	str_format(aBuf, sizeof(aBuf), "INSERT INTO %s (name) VALUES (?);", TBL_CLANS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 101: Failed to prepare INSERT statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 101: Clan creation failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindString(1, aNormalizedName);
	int NumInserted;
	if(!pSqlServer->ExecuteUpdate(&NumInserted, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 102: Failed to execute INSERT statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 102: Clan creation failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumInserted != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 103: Clan creation failed. No rows inserted.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 103: Unable to create clan. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(!pSqlServer->PrepareStatement("SELECT LAST_INSERT_ID();", pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 104: Failed to retrieve clan id.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	int ClanId = -1;
	bool End = false;
	if(!pSqlServer->Step(&End, pError, ErrorSize) || End)
	{
		str_copy(pResult->m_aaMessages[0], "Error 105: Clan creation issue (no id).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	ClanId = pSqlServer->GetInt(1);
	if(ClanId <= 0)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 106: Invalid Clan ID.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 106: Clan creation issue. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	str_format(aBuf, sizeof(aBuf), "UPDATE %s SET clanID = ?, auth_level = %d WHERE account_id = ?;", TBL_ACCOUNTS_PROGRESS, (int)ClanAuthLevel::LEADER);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 107: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 107: Clan assignment failed. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, ClanId);
	pSqlServer->BindInt(2, pData->m_AccountId);
	int NumUpdated;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 108: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 108: Clan assignment failed. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 109: Account update failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 109: Failed to assign you as clan leader.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	// push the new clan data into the in-memory container
	{
		CClansData NewClan;
		NewClan.m_Id = ClanId;
		str_copy(NewClan.m_ClanName, aNormalizedName, sizeof(NewClan.m_ClanName));
		// keep the stored name as given, but map keys are lowercase
		char aNameLower[BW_CLAN_NAME_BUFFER_SIZE];
		str_copy(aNameLower, aNormalizedName, sizeof(aNameLower));
		ToLowercase(aNameLower);
		NewClan.m_Level = 1;
		NewClan.m_Experience = 0;
		NewClan.m_LastSavedTick = pData->m_pClanManager ? pData->m_pClanManager->GameServer()->Server()->Tick() : 0;
		NewClan.m_Dirty = true; // new clan needs a save after creation (level/exp baseline)
		if(pData->m_pClanManager)
		{
			std::lock_guard<std::mutex> lock(g_ClansDataMutex);
			pData->m_pClanManager->m_vClansData.push_back(NewClan);
			// update global maps so the clan becomes joinable immediately
			g_ClanIdMap[NewClan.m_Id] = NewClan;
			g_ClanNameToId[std::string(aNameLower)] = NewClan.m_Id;
		}
	}

	// request main thread to update the player's in-memory clan data
	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pResult->m_Action = CClanResult::ACTION_UPDATE_PLAYER_BY_CLIENT;
		pResult->m_ActionClientId = pData->m_ClientId;
		pResult->m_ActionNewClanId = ClanId;
		pResult->m_ActionNewAuthLevel = static_cast<int>(ClanAuthLevel::LEADER);
	}

	if(!pSqlServer->PrepareStatement("COMMIT;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
		TxFailed = true;
	if(TxFailed)
	{
		if(!pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0) == 0)
			pSqlServer->ExecuteUpdate(nullptr, nullptr, 0);
		str_copy(pResult->m_aaMessages[0], "Error 110: Transaction failed. Clan not created.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(g_Config.m_SvClanCreatePrice > 0)
	{
		pResult->m_ActionChargeClientId = pData->m_ClientId;
		pResult->m_ActionChargeAmount = g_Config.m_SvClanCreatePrice;
	}
	str_copy(pResult->m_aaMessages[0], "Clan created successfully!", sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;
	return true;
}

bool CClanManager::DeleteClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::DIRECT);

	if(!CheckClanPermission(pSqlServer, pData->m_AccountId, pData->m_ClanId, 3, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], pError, sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(!pSqlServer->PrepareStatement("BEGIN;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 191: Failed to start transaction.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	bool TxAbort = false;

	str_format(aBuf, sizeof(aBuf), "DELETE FROM %s WHERE id = ?;", TBL_CLANS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 101: Failed to prepare DELETE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 101: Clan deletion failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_ClanId);
	int NumDeleted;
	if(!pSqlServer->ExecuteUpdate(&NumDeleted, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 102: Failed to execute DELETE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 102: Clan deletion failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumDeleted != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 103: Clan deletion failed. No rows deleted.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 103: Unable to delete clan. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	str_format(aBuf, sizeof(aBuf), "UPDATE %s SET clanID = 0, auth_level = %d WHERE clanID = ?;", TBL_ACCOUNTS_PROGRESS, (int)ClanAuthLevel::NONE);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 104: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 104: Failed to update player data. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_ClanId);
	int NumUpdated;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 105: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 105: Failed to update player data. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumUpdated < 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 106: No players were updated. Clan members may not exist.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 106: Failed to update player data.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(pData->m_pClanManager)
	{
		CGameContext *pGameServer = pData->m_pClanManager->GameServer();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pPlayer = pGameServer->m_apPlayers[i];
			if(pPlayer && pPlayer->Bw().m_Account.m_ClanId == pData->m_ClanId)
			{
				pGameServer->SendChatTarget(i, "Your clan has been deleted by the owner.");
			}
		}
		// request main thread to reset players' clan
		pResult->m_Action = CClanResult::ACTION_RESET_CLAN_PLAYERS;
		pResult->m_ActionResetClanId = pData->m_ClanId;

		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
		auto &vec = pData->m_pClanManager->m_vClansData;
		for(auto it = vec.begin(); it != vec.end(); ++it)
		{
			if(it->m_Id == pData->m_ClanId)
			{
				// erase from vector
				std::string nameLower(it->m_ClanName);
				ToLowercaseAscii(nameLower);
				int id = it->m_Id;
				vec.erase(it);
				// erase from maps as well
				g_ClanIdMap.erase(id);
				g_ClanNameToId.erase(nameLower);
				break;
			}
		}
	}

	if(!TxAbort)
	{
		if(!pSqlServer->PrepareStatement("COMMIT;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
			TxAbort = true;
	}
	if(TxAbort)
	{
		if(!pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0) == 0)
			pSqlServer->ExecuteUpdate(nullptr, nullptr, 0);
		str_copy(pResult->m_aaMessages[0], "Error 199: Clan deletion transaction failed.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	str_copy(pResult->m_aaMessages[0], "Clan deleted successfully!", sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;
	return true;
}

bool CClanManager::AssignClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::DIRECT);

	if(pData->m_ClientId >= 0 && pData->m_ClanId > 0)
	{
		CPlayer *pInviter = pData->m_pClanManager->GameServer()->m_apPlayers[pData->m_ClientId];
		if(!pInviter)
		{
			str_copy(pError, "Inviter not found", ErrorSize);
			return false;
		}
		int inviterAccId = pInviter->Bw().GetAccId();
		// only coleader or leader can invite
		if(!CheckClanPermission(pSqlServer, inviterAccId, pData->m_ClanId, (int)ClanAuthLevel::COLEADER, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], pError, sizeof(pResult->m_aaMessages[0]));
			return false;
		}
	}

	if(!pSqlServer->PrepareStatement("BEGIN;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
		return false;
	bool TxFail = false;

	str_format(aBuf, sizeof(aBuf), "SELECT COUNT(*) FROM %s WHERE clanID = ?;", TBL_ACCOUNTS_PROGRESS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 209: Failed to check clan size.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_ClanId);
	bool EndCountMembers = false;
	if(!pSqlServer->Step(&EndCountMembers, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 209: Failed to check clan size (step).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(!EndCountMembers)
	{
		int MemberCount = pSqlServer->GetInt(1);
		if(MemberCount >= g_Config.m_SvClanMaxMembers)
		{
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "Clan is full (max %d members).", g_Config.m_SvClanMaxMembers);
			str_copy(pResult->m_aaMessages[0], aMsg, sizeof(pResult->m_aaMessages[0]));
			return true;
		}
	}

	if(pData->m_AccountId != 0)
	{
		str_format(aBuf, sizeof(aBuf), "SELECT clanID FROM %s WHERE account_id = ? LIMIT 1;", TBL_ACCOUNTS_PROGRESS);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], "Error 210: Failed to validate target account.", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		pSqlServer->BindInt(1, pData->m_AccountId);
		bool EndMember = false;
		if(!pSqlServer->Step(&EndMember, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], "Error 211: Failed to validate target account (step).", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		if(EndMember)
		{
			str_copy(pResult->m_aaMessages[0], "Target account not found.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}
		int TargetClan = pSqlServer->GetInt(1);
		if(TargetClan != 0)
		{
			str_copy(pResult->m_aaMessages[0], "Player is already in a clan.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}

		str_format(aBuf, sizeof(aBuf), "UPDATE %s SET clanID = ?, auth_level = %d WHERE account_id = ?;", TBL_ACCOUNTS_PROGRESS, (int)ClanAuthLevel::MEMBER);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			if(pError && *pError)
			{
				char aTmp[1024];
				str_format(aTmp, sizeof(aTmp), "Error 201: Failed to prepare UPDATE statement: %s", pError);
				str_copy(pError, aTmp, ErrorSize);
			}
			str_copy(pResult->m_aaMessages[0], "Error 201: Assign clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		pSqlServer->BindInt(1, pData->m_ClanId);
		pSqlServer->BindInt(2, pData->m_AccountId);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "SELECT p.clanID FROM %s c JOIN %s p ON c.id=p.account_id WHERE c.name = ? LIMIT 1;", TBL_ACCOUNTS_CORE, TBL_ACCOUNTS_PROGRESS);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], "Error 212: Failed to validate target username.", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		pSqlServer->BindString(1, pData->m_aUsername);
		bool EndMemberName = false;
		if(!pSqlServer->Step(&EndMemberName, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], "Error 213: Failed to validate target username (step).", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		if(EndMemberName)
		{
			str_copy(pResult->m_aaMessages[0], "Target player not found.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}
		int TargetClan = pSqlServer->GetInt(1);
		if(TargetClan != 0)
		{
			str_copy(pResult->m_aaMessages[0], "Player is already in a clan.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}

		str_format(aBuf, sizeof(aBuf), "UPDATE %s p JOIN %s c ON p.account_id=c.id SET p.clanID = ?, p.auth_level = %d WHERE c.name = ?;", TBL_ACCOUNTS_PROGRESS, TBL_ACCOUNTS_CORE, (int)ClanAuthLevel::MEMBER);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			if(pError && *pError)
			{
				char aTmp[1024];
				str_format(aTmp, sizeof(aTmp), "Error 201: Failed to prepare UPDATE statement: %s", pError);
				str_copy(pError, aTmp, ErrorSize);
			}
			str_copy(pResult->m_aaMessages[0], "Error 201: Assign clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		pSqlServer->BindInt(1, pData->m_ClanId);
		pSqlServer->BindString(2, pData->m_aUsername);
	}

	int NumUpdated;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 202: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 202: Assign clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 203: Assign clan failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 203: Unable to assign clan.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		// request main thread to update a player by name
		pResult->m_Action = CClanResult::ACTION_UPDATE_PLAYER_BY_NAME;
		pResult->m_ActionNewClanId = pData->m_ClanId;
		pResult->m_ActionNewAuthLevel = static_cast<int>(ClanAuthLevel::MEMBER);
		str_copy(pResult->m_ActionPlayerName, pData->m_aUsername, sizeof(pResult->m_ActionPlayerName));
	}

	{
		CGameContext *pGameServer = pData->m_pClanManager->GameServer();
		char aBroadcast[256];
		str_format(aBroadcast, sizeof(aBroadcast), "%s has joined the clan!", pData->m_aUsername);
		pGameServer->Bw().SendChatClan(pData->m_ClanId, aBroadcast);
	}

	if(!TxFail)
	{
		if(!pSqlServer->PrepareStatement("COMMIT;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
			TxFail = true;
	}
	if(TxFail)
	{
		pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0);
		pSqlServer->ExecuteUpdate(nullptr, nullptr, 0);
		return false;
	}
	// mark result as successful so the main thread processes it
	pResult->m_Success = true;
	return true;
}

bool CClanManager::RemoveFromClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];
	pResult->SetVariant(CClanResult::DIRECT);

	// get kicker's auth level
	str_format(aBuf, sizeof(aBuf), "SELECT auth_level FROM %s WHERE account_id = ? AND clanID = ?;", TBL_ACCOUNTS_PROGRESS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 310: Failed to get kicker's auth level.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_AccountId);
	pSqlServer->BindInt(2, pData->m_ClanId);
	bool EndKicker = false;
	if(!pSqlServer->Step(&EndKicker, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 311: Failed to get kicker's auth level (step).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(EndKicker)
	{
		str_copy(pResult->m_aaMessages[0], "You are not in this clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	int KickerAuth = pSqlServer->GetInt(1);

	if(KickerAuth < (int)ClanAuthLevel::COLEADER)
	{
		str_copy(pResult->m_aaMessages[0], "You don't have permission to kick members.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(!pSqlServer->PrepareStatement("BEGIN;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
		return false;
	bool TxErr = false;

	str_format(aBuf, sizeof(aBuf), "SELECT p.auth_level FROM %s c JOIN %s p ON c.id=p.account_id WHERE c.name = ? AND p.clanID = ? LIMIT 1;", TBL_ACCOUNTS_CORE, TBL_ACCOUNTS_PROGRESS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 320: Failed to validate target rank.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindString(1, pData->m_aUsername);
	pSqlServer->BindInt(2, pData->m_ClanId);
	bool EndRank = false;
	if(!pSqlServer->Step(&EndRank, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 321: Failed to validate target rank (step).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(EndRank)
	{
		str_copy(pResult->m_aaMessages[0], "Player not found in clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	int TargetAuth = pSqlServer->GetInt(1);

	if(KickerAuth == (int)ClanAuthLevel::LEADER)
	{
		if(TargetAuth >= (int)ClanAuthLevel::LEADER)
		{
			str_copy(pResult->m_aaMessages[0], "You cannot kick other leaders.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}
	}
	else if(KickerAuth == (int)ClanAuthLevel::COLEADER)
	{
		if(TargetAuth >= (int)ClanAuthLevel::COLEADER)
		{
			str_copy(pResult->m_aaMessages[0], "Co-leaders cannot kick other co-leaders or leaders.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}
	}

	str_format(aBuf, sizeof(aBuf), "UPDATE %s p JOIN %s c ON p.account_id=c.id SET p.clanID = 0, p.auth_level = %d WHERE c.name = ? AND p.clanID = ?;", TBL_ACCOUNTS_PROGRESS, TBL_ACCOUNTS_CORE, (int)ClanAuthLevel::NONE);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 301: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 301: Remove from clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindString(1, pData->m_aUsername);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 302: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 302: Remove from clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 303: Remove from clan failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 303: Unable to remove from clan.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		// request main thread to update a player by name (remove from clan)
		pResult->m_Action = CClanResult::ACTION_UPDATE_PLAYER_BY_NAME;
		pResult->m_ActionNewClanId = 0;
		pResult->m_ActionNewAuthLevel = static_cast<int>(ClanAuthLevel::NONE);
		str_copy(pResult->m_ActionPlayerName, pData->m_aUsername, sizeof(pResult->m_ActionPlayerName));
	}

	CGameContext *pGameServer = pData->m_pClanManager->GameServer();

	// -- notify the kicked player --
	int KickedClientId = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pTarget = pGameServer->m_apPlayers[i];
		if(pTarget && pTarget->Bw().IsLoggedIn() && str_comp(pTarget->Bw().m_Account.m_aName, pData->m_aUsername) == 0)
		{
			KickedClientId = i;
			break;
		}
	}
	if(KickedClientId != -1)
	{
		pGameServer->SendChatTarget(KickedClientId, "You have been kicked from your clan!");
	}

	// -- broadcast to remaining clan members using SendClanChat --
	{
		char aBroadcast[256];
		str_format(aBroadcast, sizeof(aBroadcast), "'%s' has been kicked from the clan!", pData->m_aUsername);
		if(KickedClientId != -1)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(i == KickedClientId)
					continue;
				CPlayer *pPl = pGameServer->m_apPlayers[i];
				if(!pPl || !pPl->Bw().IsLoggedIn())
					continue;
				if(pPl->Bw().GetClanId() != pData->m_ClanId)
					continue;
				pGameServer->SendChatTarget(i, aBroadcast);
			}
		}
		else
		{
			pGameServer->Bw().SendChatClan(pData->m_ClanId, aBroadcast);
		}
	}

	if(!TxErr)
	{
		if(!pSqlServer->PrepareStatement("COMMIT;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
			TxErr = true;
	}
	if(TxErr)
	{
		pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0);
		pSqlServer->ExecuteUpdate(nullptr, nullptr, 0);
		return false;
	}
	pResult->m_Success = true;

	return true;
}

bool CClanManager::ClanLeaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::DIRECT);

	if(!pSqlServer->PrepareStatement("BEGIN;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
		return false;
	bool TxErr = false;
	// prevent sole leader from leaving to avoid orphaned clan
	str_format(aBuf, sizeof(aBuf), "SELECT clanID, auth_level FROM %s WHERE account_id = ? LIMIT 1;", TBL_ACCOUNTS_PROGRESS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 560: Failed to validate leave state.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_AccountId);
	bool EndAcc = false;
	if(!pSqlServer->Step(&EndAcc, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 561: Failed to validate leave state (step).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(EndAcc)
	{
		str_copy(pResult->m_aaMessages[0], "Account not found.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	int DbClanId = pSqlServer->GetInt(1);
	int DbAuth = pSqlServer->GetInt(2);
	if(DbClanId != pData->m_ClanId || DbClanId == 0)
	{
		str_copy(pResult->m_aaMessages[0], "You are not in this clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(DbAuth == (int)ClanAuthLevel::LEADER)
	{
		str_format(aBuf, sizeof(aBuf), "SELECT COUNT(*) FROM %s WHERE clanID = ? AND auth_level = ?;", TBL_ACCOUNTS_PROGRESS);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], "Error 562: Failed to verify leader count.", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		pSqlServer->BindInt(1, pData->m_ClanId);
		pSqlServer->BindInt(2, (int)ClanAuthLevel::LEADER);
		bool EndCnt = false;
		if(!pSqlServer->Step(&EndCnt, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], "Error 563: Failed to verify leader count (step).", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		if(EndCnt)
		{
			str_copy(pResult->m_aaMessages[0], "Error 564: Leader count query empty.", sizeof(pResult->m_aaMessages[0]));
			return false; // treat as error; inconsistent state
		}
		int LeaderCount = pSqlServer->GetInt(1);
		if(LeaderCount <= 1)
		{
			str_copy(pResult->m_aaMessages[0], "You are the sole leader. Transfer leadership or delete the clan before leaving.", sizeof(pResult->m_aaMessages[0]));
			return true; // not a server error
		}
	}

	str_format(aBuf, sizeof(aBuf), "UPDATE %s SET clanID = 0, auth_level = %d WHERE account_id = ? AND clanID = ?;", TBL_ACCOUNTS_PROGRESS, (int)ClanAuthLevel::NONE);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 401: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 401: Clan leave failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_AccountId);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 402: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 402: Clan leave failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 403: Clan leave failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 403: Unable to leave clan.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pResult->m_Action = CClanResult::ACTION_UPDATE_PLAYER_BY_CLIENT;
		pResult->m_ActionClientId = pData->m_ClientId;
		pResult->m_ActionNewClanId = 0;
		pResult->m_ActionNewAuthLevel = static_cast<int>(ClanAuthLevel::NONE);
	}
	if(!TxErr)
	{
		if(!pSqlServer->PrepareStatement("COMMIT;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
			TxErr = true;
	}
	if(TxErr)
	{
		pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0);
		pSqlServer->ExecuteUpdate(nullptr, nullptr, 0);
		return false;
	}
	str_copy(pResult->m_aaMessages[0], "You have left the clan successfully!", sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;
	return true;
}

bool CClanManager::SetAuthLevelThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::CLAN);

	if(!pSqlServer->PrepareStatement("BEGIN;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
		return false;
	bool TxErr = false;
	if(!CheckClanPermission(pSqlServer, pData->m_AccountId, pData->m_ClanId, 3, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], pError, sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(pData->m_NewAuthLevel < (int)ClanAuthLevel::NONE || pData->m_NewAuthLevel > (int)ClanAuthLevel::LEADER)
	{
		str_copy(pResult->m_aaMessages[0], "Invalid auth level.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(pData->m_NewAuthLevel < (int)ClanAuthLevel::LEADER)
	{
		str_format(aBuf, sizeof(aBuf), "SELECT p.auth_level FROM %s c JOIN %s p ON c.id=p.account_id WHERE c.name = ? AND p.clanID = ? LIMIT 1;", TBL_ACCOUNTS_CORE, TBL_ACCOUNTS_PROGRESS);
		if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], "Error 550: Failed to read current auth.", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		pSqlServer->BindString(1, pData->m_aUsername);
		pSqlServer->BindInt(2, pData->m_ClanId);
		bool EndCur = false;
		if(!pSqlServer->Step(&EndCur, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], "Error 551: Failed to read current auth (step).", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
		if(EndCur)
		{
			str_copy(pResult->m_aaMessages[0], "Player not found.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}
		int CurrentAuth = pSqlServer->GetInt(1);
		if(CurrentAuth == (int)ClanAuthLevel::LEADER)
		{
			str_format(aBuf, sizeof(aBuf), "SELECT COUNT(*) FROM %s WHERE clanID = ? AND auth_level = 3;", TBL_ACCOUNTS_PROGRESS);
			if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
			{
				str_copy(pResult->m_aaMessages[0], "Error 552: Failed to count leaders.", sizeof(pResult->m_aaMessages[0]));
				return false;
			}
			pSqlServer->BindInt(1, pData->m_ClanId);
			bool EndCount = false;
			if(!pSqlServer->Step(&EndCount, pError, ErrorSize))
			{
				str_copy(pResult->m_aaMessages[0], "Error 553: Failed to count leaders (step).", sizeof(pResult->m_aaMessages[0]));
				return false;
			}
			if(!EndCount && pSqlServer->GetInt(1) <= 1)
			{
				str_copy(pResult->m_aaMessages[0], "Cannot demote the only leader. Promote another player first.", sizeof(pResult->m_aaMessages[0]));
				return true;
			}
		}
	}
	str_format(aBuf, sizeof(aBuf), "UPDATE %s p JOIN %s c ON p.account_id=c.id SET p.auth_level = ? WHERE c.name = ? AND p.clanID = ?;", TBL_ACCOUNTS_PROGRESS, TBL_ACCOUNTS_CORE);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 501: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 501: Set auth level failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_NewAuthLevel);
	pSqlServer->BindString(2, pData->m_aUsername);
	pSqlServer->BindInt(3, pData->m_ClanId);
	int NumUpdated;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 502: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 502: Set auth level failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 503: Set auth level failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 503: Unable to set role (maybe because target already has that role).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		// request main thread to update a player by name (auth level change)
		pResult->m_Action = CClanResult::ACTION_UPDATE_PLAYER_BY_NAME;
		pResult->m_ActionNewClanId = pData->m_ClanId;
		pResult->m_ActionNewAuthLevel = pData->m_NewAuthLevel;
		str_copy(pResult->m_ActionPlayerName, pData->m_aUsername, sizeof(pResult->m_ActionPlayerName));
	}

	char aRank[32];
	if(pData->m_NewAuthLevel == 2)
		str_copy(aRank, "co-leader", sizeof(aRank));
	else if(pData->m_NewAuthLevel == 1)
		str_copy(aRank, "member", sizeof(aRank));
	else
		str_format(aRank, sizeof(aRank), "rank %d", pData->m_NewAuthLevel);

	char aMessage[256];
	str_format(aMessage, sizeof(aMessage), "Auth level updated: %s is now %s.", pData->m_aUsername, aRank);
	if(!TxErr)
	{
		if(!pSqlServer->PrepareStatement("COMMIT;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
			TxErr = true;
	}
	if(TxErr)
	{
		pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0);
		pSqlServer->ExecuteUpdate(nullptr, nullptr, 0);
		return false;
	}
	str_copy(pResult->m_aaMessages[0], aMessage, sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;

	return true;
}

bool CClanManager::TransferLeadershipThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::CLAN);

	if(!pSqlServer->PrepareStatement("BEGIN;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
		return false;
	bool TxErr = false;

	// ensure caller is leader
	if(!CheckClanPermission(pSqlServer, pData->m_AccountId, pData->m_ClanId, (int)ClanAuthLevel::LEADER, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], pError, sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	// find target account id and validate membership
	str_format(aBuf, sizeof(aBuf), "SELECT p.account_id FROM %s c JOIN %s p ON c.id=p.account_id WHERE c.name = ? AND p.clanID = ? LIMIT 1;", TBL_ACCOUNTS_CORE, TBL_ACCOUNTS_PROGRESS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error TL01: Failed to validate target account.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindString(1, pData->m_aUsername);
	pSqlServer->BindInt(2, pData->m_ClanId);
	bool End = false;
	if(!pSqlServer->Step(&End, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error TL02: Failed to validate target account (step).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(End)
	{
		str_copy(pResult->m_aaMessages[0], "Target player not found in this clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	// promote target to leader
	str_format(aBuf, sizeof(aBuf), "UPDATE %s p JOIN %s c ON p.account_id=c.id SET p.auth_level = %d WHERE c.name = ? AND p.clanID = ?;", TBL_ACCOUNTS_PROGRESS, TBL_ACCOUNTS_CORE, (int)ClanAuthLevel::LEADER);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error TL03: Failed to prepare promote statement.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindString(1, pData->m_aUsername);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated = 0;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error TL04: Failed to execute promote statement.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumUpdated != 1)
	{
		str_copy(pResult->m_aaMessages[0], "Error TL05: Promote failed. Target may not exist or is not in the clan.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	// demote issuer to co-leader
	str_format(aBuf, sizeof(aBuf), "UPDATE %s SET auth_level = %d WHERE account_id = ? AND clanID = ?;", TBL_ACCOUNTS_PROGRESS, (int)ClanAuthLevel::COLEADER);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error TL06: Failed to prepare demote statement.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_AccountId);
	pSqlServer->BindInt(2, pData->m_ClanId);
	NumUpdated = 0;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error TL07: Failed to execute demote statement.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumUpdated != 1)
	{
		str_copy(pResult->m_aaMessages[0], "Error TL08: Demote failed. Issuer may not be in the clan.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	// prepare main-thread action to update both players in memory
	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pResult->m_Action = CClanResult::ACTION_UPDATE_TWO_PLAYERS;
		pResult->m_ActionClientId = pData->m_ClientId; // issuer client id
		pResult->m_ActionNewClanId = pData->m_ClanId;
		pResult->m_ActionNewAuthLevel = static_cast<int>(ClanAuthLevel::COLEADER); // issuer new level
		str_copy(pResult->m_ActionPlayerName, pData->m_aUsername, sizeof(pResult->m_ActionPlayerName)); // target name
		pResult->m_ActionNewAuthLevel2 = static_cast<int>(ClanAuthLevel::LEADER); // target new level
	}

	str_copy(pResult->m_aaMessages[0], "Clan ownership transferred successfully.", sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;

	if(!TxErr)
	{
		if(!pSqlServer->PrepareStatement("COMMIT;", pError, ErrorSize) || !pSqlServer->ExecuteUpdate(nullptr, pError, ErrorSize))
			TxErr = true;
	}
	if(TxErr)
	{
		if(!pSqlServer->PrepareStatement("ROLLBACK;", nullptr, 0) == 0)
			pSqlServer->ExecuteUpdate(nullptr, nullptr, 0);
		str_copy(pResult->m_aaMessages[0], "Error TL09: Transaction failed.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	return true;
}

bool CClanManager::RenameClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::DIRECT);

	char aNormalizedName[BW_CLAN_NAME_BUFFER_SIZE];
	str_copy(aNormalizedName, pData->m_aNewClanName, sizeof(aNormalizedName));
	TrimSpaces(aNormalizedName);
	const int NewNameLen = str_length(aNormalizedName);
	if(NewNameLen < 3 || NewNameLen > BW_CLAN_NAME_MAX_LENGTH)
	{
		pResult->SetVariant(CClanResult::DIRECT);
		str_copy(pResult->m_aaMessages[0], "Clan name must be between 3 and 11 characters (after trimming).", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(!CheckClanPermission(pSqlServer, pData->m_AccountId, pData->m_ClanId, 3, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], pError, sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	str_format(aBuf, sizeof(aBuf), "SELECT id FROM %s WHERE LOWER(name) = LOWER(?) AND id <> ? LIMIT 1;", TBL_CLANS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 600: Failed to validate clan name uniqueness.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindString(1, aNormalizedName);
	pSqlServer->BindInt(2, pData->m_ClanId);
	bool EndCheck = false;
	if(!pSqlServer->Step(&EndCheck, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 600: Failed to validate clan name uniqueness (step).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(!EndCheck)
	{
		str_copy(pResult->m_aaMessages[0], "Clan name already exists.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_format(aBuf, sizeof(aBuf), "UPDATE %s SET name = ? WHERE id = ?;", TBL_CLANS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 601: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 601: Rename clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindString(1, aNormalizedName);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 602: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 602: Rename clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 603: Rename clan failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 603: Unable to rename clan.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}

	// update the in-memory clan vector (protected by mutex)
	if(pData->m_pClanManager)
	{
		std::string oldNameLower;
		char aOldName[sizeof(((CClansData *)nullptr)->m_ClanName)] = {0};
		{
			std::lock_guard<std::mutex> lock(g_ClansDataMutex);
			auto &vec = pData->m_pClanManager->m_vClansData;
			for(auto &Clan : vec)
			{
				if(Clan.m_Id == pData->m_ClanId)
				{
					oldNameLower = Clan.m_ClanName;
					str_copy(aOldName, Clan.m_ClanName, sizeof(aOldName));
					str_copy(Clan.m_ClanName, aNormalizedName, sizeof(Clan.m_ClanName));
					break;
				}
			}
			// update global maps
			auto it = g_ClanIdMap.find(pData->m_ClanId);
			if(it != g_ClanIdMap.end())
			{
				// remove old name mapping
				ToLowercaseAscii(oldNameLower);
				if(!oldNameLower.empty())
					g_ClanNameToId.erase(oldNameLower);
				// set new name in id map and add name->id map
				it->second.m_ClanName[0] = '\0';
				str_copy(it->second.m_ClanName, aNormalizedName, sizeof(it->second.m_ClanName));
				std::string newLower(aNormalizedName);
				ToLowercaseAscii(newLower);
				g_ClanNameToId[newLower] = pData->m_ClanId;
			}
		}

		// request main-thread notification to clan members about rename
		pResult->m_Action = CClanResult::ACTION_NOTIFY_CLAN_RENAME;
		str_copy(pResult->m_ActionOldClanName, aOldName, sizeof(pResult->m_ActionOldClanName));
		str_copy(pResult->m_ActionNewClanName, aNormalizedName, sizeof(pResult->m_ActionNewClanName));
	}
	str_copy(pResult->m_aaMessages[0], "Clan renamed successfully!", sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;

	// set charge instruction for main thread to apply BP deduction safely
	pResult->m_ActionChargeClientId = pData->m_ClientId;
	pResult->m_ActionChargeAmount = g_Config.m_SvClanRenamePrice;
	return true;
}

// loads clans data into a CClansData vector
bool CClanManager::LoadClansThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanListRequest *pRequest = static_cast<const CSqlClanListRequest *>(pGameData);
	char aBuf[1024];
	// Use prefixed Blockworlds clans table
	str_format(aBuf, sizeof(aBuf), "SELECT id, name, level, experience FROM %s;", TBL_CLANS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Failed to prepare LOAD clans statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		return false;
	}

	bool End = false;
	CClanListResult *pResult = static_cast<CClanListResult *>(pGameData->m_pResult.get());
	while(!End)
	{
		if(!pSqlServer->Step(&End, pError, ErrorSize))
		{
			if(pError && *pError)
			{
				char aTmp[1024];
				str_format(aTmp, sizeof(aTmp), "Failed to execute LOAD clans statement: %s", pError);
				str_copy(pError, aTmp, ErrorSize);
			}
			return false;
		}
		if(!End)
		{
			CClansData Data;
			Data.m_Id = pSqlServer->GetInt(1);
			// read into a larger buffer first to avoid assertion if DB has oversized names
			char aNameBuf[256];
			pSqlServer->GetString(2, aNameBuf, sizeof(aNameBuf));
			if(str_length(aNameBuf) > BW_CLAN_NAME_MAX_LENGTH)
			{
				dbg_msg("clan", "WARNING: clan id=%d name '%s' exceeds %d byte limit (%d bytes), skipping",
					Data.m_Id, aNameBuf, BW_CLAN_NAME_MAX_LENGTH, str_length(aNameBuf));
				continue;
			}
			str_copy(Data.m_ClanName, aNameBuf, sizeof(Data.m_ClanName));
			Data.m_Level = pSqlServer->GetInt(3);
			Data.m_Experience = pSqlServer->GetInt(4);
			Data.m_LastSavedTick = 0;
			Data.m_Dirty = false; // fresh from DB
			pResult->m_vClans.push_back(Data);
		}
	}

	if(pRequest->m_pClanManager)
	{
		pRequest->m_pClanManager->OnClansLoaded(pResult->m_vClans);
	}

	dbg_msg("clan", "Loaded %d clans into memory", (int)pResult->m_vClans.size());
	return true;
}

void CClanManager::OnClansLoaded(const std::vector<CClansData> &vClans)
{
	std::lock_guard<std::mutex> lock(g_ClansDataMutex);
	m_vClansData = vClans; // atomic swap
	g_ClanIdMap.clear();
	g_ClanNameToId.clear();
	for(const auto &clan : vClans)
	{
		// validate for duplicate IDs or names
		if(g_ClanIdMap.count(clan.m_Id))
		{
			dbg_msg("clan", "Duplicate clan id %d detected!", clan.m_Id);
			continue;
		}
		std::string nameLower(clan.m_ClanName);
		ToLowercaseAscii(nameLower);
		if(g_ClanNameToId.count(nameLower))
		{
			dbg_msg("clan", "Duplicate clan name '%s' detected!", clan.m_ClanName);
			continue;
		}
		g_ClanIdMap[clan.m_Id] = clan; // m_Dirty already false
		g_ClanNameToId[nameLower] = clan.m_Id;
	}
	m_ClansLoaded = true;
	dbg_msg("clan", "Loaded %d clans into memory (map size: %zu)", (int)m_vClansData.size(), g_ClanIdMap.size());
}

// helper functions because I need help (i'm going crazy)
int CClanManager::GetClanIdByName(const char *pClanName)
{
	std::string nameLower(pClanName);
	ToLowercaseAscii(nameLower);
	std::lock_guard<std::mutex> lock(g_ClansDataMutex);
	auto it = g_ClanNameToId.find(nameLower);
	if(it != g_ClanNameToId.end())
		return it->second;
	return -1;
}

std::string CClanManager::GetClanNameCopy(int ClanId) const
{
	std::lock_guard<std::mutex> lock(g_ClansDataMutex);
	auto it = g_ClanIdMap.find(ClanId);
	if(it != g_ClanIdMap.end())
		return std::string(it->second.m_ClanName);
	return std::string(" ");
}

bool CClanManager::GetClanSnapshotById(int ClanId, CClansData &Out) const
{
	std::lock_guard<std::mutex> lock(g_ClansDataMutex);
	auto it = g_ClanIdMap.find(ClanId);
	if(it != g_ClanIdMap.end())
	{
		Out = it->second; // copy
		return true;
	}
	return false;
}

void CClanManager::AddClanExp(int ClanId, int Amount)
{
	std::lock_guard<std::mutex> lock(g_ClansDataMutex);
	auto it = g_ClanIdMap.find(ClanId);
	if(it != g_ClanIdMap.end())
	{
		CClansData &Clan = it->second;
		Clan.m_Experience += Amount;
		while(Clan.m_Experience >= NeededClanExp(Clan.m_Level))
		{
			int ExcessiveExp = Clan.m_Experience - NeededClanExp(Clan.m_Level);
			Clan.m_Level++;
			Clan.m_Experience = ExcessiveExp;

			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "[Clan LevelUp+]: Clan '%s' is now level %d!", Clan.m_ClanName, Clan.m_Level);
			GameServer()->Bw().SendChatTarget(-1, aBuf);
		}

		for(auto &vecClan : m_vClansData)
		{
			if(vecClan.m_Id == ClanId)
			{
				vecClan.m_Experience = Clan.m_Experience;
				vecClan.m_Level = Clan.m_Level;
				vecClan.m_Dirty = true; // mark dirty for autosave
				break;
			}
		}
	}
}

bool CClanManager::IsClanJoinable(int ClanId) const
{
	std::lock_guard<std::mutex> lock(g_ClansDataMutex);
	auto it = g_ClanIdMap.find(ClanId);
	return it != g_ClanIdMap.end();
}

bool CClanManager::SaveClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	char aBuf[1024];

	// mark start of save (prevent further enqueue) under mutex
	{
		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
		MarkSaveStarted_Locked(pData->m_ClanId);
	}

	if(pData->m_ClanId < 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error: Couldn't retrieve clan ID!", ErrorSize);
		return false;
	}
	// only write if the incoming state is not older than the DB lol
	str_format(aBuf, sizeof(aBuf),
		"UPDATE %s SET level = ?, experience = ? WHERE id = ? AND (level < ? OR (level = ? AND experience <= ?));",
		TBL_CLANS);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error: Failed to prepare SAVE clan statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		return false;
	}

	CClansData ClanCopy;
	if(!pData->m_pClanManager->GetClanSnapshotById(pData->m_ClanId, ClanCopy))
	{
		if(pError && ErrorSize > 0)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error: Clan id %d not found in memory", pData->m_ClanId);
			str_copy(pError, aTmp, ErrorSize);
		}
		return false;
	}

	pSqlServer->BindInt(1, ClanCopy.m_Level);
	pSqlServer->BindInt(2, ClanCopy.m_Experience);
	pSqlServer->BindInt(3, ClanCopy.m_Id);
	pSqlServer->BindInt(4, ClanCopy.m_Level);
	pSqlServer->BindInt(5, ClanCopy.m_Level);
	pSqlServer->BindInt(6, ClanCopy.m_Experience);

	int NumUpdated = 0;
	if(!pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error: Failed to execute SAVE clan statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		return false;
	}
	// allow 0 rows updated (if values didn’t change) without treating it as an error
	if(NumUpdated < 0)
	{
		if(pError && ErrorSize > 0)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error: SAVE clan failed with negative rows updated for clan id %d.", ClanCopy.m_Id);
			str_copy(pError, aTmp, ErrorSize);
		}
		return false;
	}
	// else if(NumUpdated == 0)
	// {
	// 	dbg_msg("clan", "Clan %d save: no changes made (0 rows updated).", ClanCopy.m_Id);
	// }
	// else
	// {
	// 	dbg_msg("clan", "Clan %d saved successfully (rows updated: %d).", ClanCopy.m_Id, NumUpdated);
	// }

	int CurrentTick = pData->m_pClanManager->GameServer()->Server()->Tick();
	// update the in-memory clan's last saved tick under mutex to avoid races with main thread
	{
		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
		for(auto &Clan : pData->m_pClanManager->m_vClansData)
		{
			if(Clan.m_Id == ClanCopy.m_Id)
			{
				Clan.m_LastSavedTick = CurrentTick;
				Clan.m_Dirty = false; // cleared after successful save attempt
				// mark finished save state
				MarkSaveFinished_Locked(Clan.m_Id);
				break;
			}
		}
		// keep the map entry in sync for snapshot-based reads
		auto it = g_ClanIdMap.find(ClanCopy.m_Id);
		if(it != g_ClanIdMap.end())
		{
			it->second.m_LastSavedTick = CurrentTick;
			it->second.m_Dirty = false;
		}
	}

	// if the clan became dirty again while saving (race), requeue a save
	CClansData AfterCopy;
	if(pData->m_pClanManager->GetClanSnapshotById(pData->m_ClanId, AfterCopy) && AfterCopy.m_Dirty)
	{
		pData->m_pClanManager->QueueBackgroundSave(pData->m_ClanId);
	}

	return true;
}

bool CClanManager::ShowTopClansThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	CClanResult *pResult = dynamic_cast<CClanResult *>(pGameData->m_pResult.get());
	pResult->SetVariant(CClanResult::DIRECT);
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "SELECT name, level, experience FROM %s ORDER BY level DESC, experience DESC LIMIT 10;", TBL_CLANS);

	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
		dbg_msg("top_clans", "SQL preparation failed: %s", pError);
		pResult->m_Success = false;
		return false;
	}

	int Line = 0;
	str_copy(pResult->m_aaMessages[Line], "---------- Top Clans ----------", sizeof(pResult->m_aaMessages[Line]));
	Line++;

	bool End = false;
	while(pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		*pError = '\0';
		char aClanName[BW_CLAN_NAME_BUFFER_SIZE];
		int Level = 0;

		pSqlServer->GetString(1, aClanName, sizeof(aClanName));
		if(*pError != '\0')
		{
			dbg_msg("top_clans", "Failed to retrieve clan name: %s", pError);
			str_copy(pResult->m_aaMessages[0], "Failed to retrieve clan name.", sizeof(pResult->m_aaMessages[0]));
			pResult->m_Success = false;
			return false;
		}

		Level = pSqlServer->GetInt(2);

		str_format(aBuf, sizeof(aBuf), "[%d] %s - %d", Line, aClanName, Level);
		str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));

		Line++;

		if(Line >= CClanResult::MAX_MESSAGES)
			break;
	}

	if(Line == 1)
	{
		str_copy(pResult->m_aaMessages[Line], "No clans found.", sizeof(pResult->m_aaMessages[Line]));
		pResult->m_Success = false;
	}
	else
	{
		pResult->m_Success = true;
	}

	if(*pError != '\0')
	{
		dbg_msg("top_clans", "SQL stepping failed: %s", pError);
		str_copy(pResult->m_aaMessages[Line], "Error retrieving top clans.", sizeof(pResult->m_aaMessages[Line]));
		pResult->m_Success = false;
		return false;
	}

	return true;
}

bool CClanManager::ShowClanMembersThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	pResult->SetVariant(CClanResult::DIRECT);

	if(pData->m_ClanId <= 0)
	{
		str_copy(pResult->m_aaMessages[0], "You are not in a clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	char aClanName[BW_CLAN_NAME_BUFFER_SIZE] = {0};
	if(pData->m_pClanManager)
	{
		auto nameStr = pData->m_pClanManager->GetClanNameCopy(pData->m_ClanId);
		str_copy(aClanName, nameStr.c_str(), sizeof(aClanName));
	}

	char aBuf[256];
	int Line = 0;

	str_format(aBuf, sizeof(aBuf), "SELECT COUNT(*) FROM %s WHERE clanID = ?;", TBL_ACCOUNTS_PROGRESS);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to read clan member count.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_ClanId);
	bool End = false;
	int MemberCount = 0;
	if(!pSqlServer->Step(&End, pError, ErrorSize) || End)
	{
		str_copy(pResult->m_aaMessages[0], "Failed to read clan member count (step).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	MemberCount = pSqlServer->GetInt(1);

	if(aClanName[0])
		str_format(aBuf, sizeof(aBuf), "Clan '%s' members (%d/%d):", aClanName, MemberCount, g_Config.m_SvClanMaxMembers);
	else
		str_format(aBuf, sizeof(aBuf), "Clan members (%d/%d):", MemberCount, g_Config.m_SvClanMaxMembers);
	str_copy(pResult->m_aaMessages[Line++], aBuf, sizeof(pResult->m_aaMessages[0]));

	str_format(aBuf, sizeof(aBuf),
		"SELECT c.name, c.last_name, p.auth_level FROM %s p JOIN %s c ON p.account_id=c.id WHERE p.clanID = ? ORDER BY p.auth_level DESC, c.name ASC;",
		TBL_ACCOUNTS_PROGRESS, TBL_ACCOUNTS_CORE);
	if(!pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to list clan members.", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	pSqlServer->BindInt(1, pData->m_ClanId);
	End = false;
	while(pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		char aName[32];
		pSqlServer->GetString(1, aName, sizeof(aName));
		char aLastName[32];
		pSqlServer->GetString(2, aLastName, sizeof(aLastName));
		int Auth = pSqlServer->GetInt(3);
		const char *pRank = (Auth == (int)ClanAuthLevel::LEADER) ? "Leader" : (Auth == (int)ClanAuthLevel::COLEADER ? "Co-Leader" : "Member");

		char aLine[96];
		str_format(aLine, sizeof(aLine), "- %s %s [%s]", aName, aLastName, pRank);
		if(Line < CClanResult::MAX_MESSAGES)
			str_copy(pResult->m_aaMessages[Line++], aLine, sizeof(pResult->m_aaMessages[0]));
		else
			break;
	}
	pResult->m_Success = true;
	return true;
}

int CClanManager::SaveAllClansOnShutdown()
{
	int Count = 0;
	std::vector<int> vIds;
	{
		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
		vIds.reserve(m_vClansData.size());
		for(const auto &c : m_vClansData)
			vIds.push_back(c.m_Id);
	}
	for(int id : vIds)
	{
		auto pResult = std::make_shared<CClanResult>();
		auto pRequest = std::make_unique<CSqlClanRequest>(pResult, this);
		pRequest->m_ClientId = -1;
		pRequest->m_ClanId = id;
		pRequest->m_Critical = true;
		if(m_pShutdownCollector)
			m_pShutdownCollector->push_back(pResult);
		m_pPool->Execute(SaveClanThread, std::move(pRequest), "save clan shutdown");
		Count++;
	}
	return Count;
}

void CClanManager::AutosaveTick()
{
	int Now = GameServer()->Server()->Tick();

	if(!m_ClansLoaded)
	{
		const int RetryIntervalTicks = GameServer()->Server()->TickSpeed() * 30;
		if(Now - m_LastLoadAttemptTick >= RetryIntervalTicks)
		{
			dbg_msg("clan", "Clan data not loaded yet - retrying LoadAllClans...");
			m_LastLoadAttemptTick = Now;
			std::shared_ptr<CClanListResult> pResult = std::make_shared<CClanListResult>();
			auto pRequest = std::make_unique<CSqlClanListRequest>(pResult, this);
			m_pPool->Execute(LoadClansThread, std::move(pRequest), "load clans retry");
		}
		return; // don't try to autosave when data isn't loaded yet
	}

	int IntervalTicks = GameServer()->Server()->TickSpeed() * g_Config.m_SvClanSaveInterval;
	std::vector<int> ToSave;
	{
		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
		for(const auto &Clan : m_vClansData)
		{
			bool Due = Clan.m_Dirty || (Clan.m_LastSavedTick + IntervalTicks < Now);
			if(Due)
			{
				if(TryMarkSaveQueued_Locked(Clan.m_Id, Now))
					ToSave.push_back(Clan.m_Id);
			}
		}
	}
	for(int id : ToSave)
	{
		// already marked queued; just enqueue the task
		auto pResult = std::make_shared<CClanResult>();
		auto pRequest = std::make_unique<CSqlClanRequest>(pResult, this);
		pRequest->m_ClientId = -1;
		pRequest->m_ClanId = id;
		m_pPool->Execute(SaveClanThread, std::move(pRequest), "autosave clan");
	}
}
