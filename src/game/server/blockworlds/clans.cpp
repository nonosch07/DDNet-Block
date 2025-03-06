
#include <engine/server/databases/connection.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "clans.h"
#include "engine/shared/config.h"
#include "engine/shared/protocol.h"

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

CClanManager::~CClanManager()
{
}

std::shared_ptr<CClanResult> CClanManager::NewSqlClanResult(int ClientId)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pCurPlayer)	return nullptr;
	pCurPlayer->m_ClanQueryResult.push(std::make_shared<CClanResult>());
	return pCurPlayer->m_ClanQueryResult.back();
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
	int ClanId)
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
	ExecClanThread(ClanLeaveThread, "clan leave", ClientId, "", pPlayer->GetPlayerName(), pPlayer->GetAccId(), pPlayer->m_Account.m_ClanId);
}

void CClanManager::SetAuthLevel(int ClientId, const char *AccountName, int NewAuthLevel, int ClanId)
{
	auto pResult = NewSqlClanResult(ClientId);
	if(pResult == nullptr)
		return;
	auto pRequest = std::make_unique<CSqlClanRequest>(pResult, this);
	// for SetAuthLevel, we don't need the clan name.
	pRequest->m_ClientId = ClientId;
	pRequest->m_ClanId = ClanId;
	pRequest->m_AccountId = 0;
	str_copy(pRequest->m_aUsername, AccountName, sizeof(pRequest->m_aUsername));
	pRequest->m_NewAuthLevel = NewAuthLevel;
	m_pPool->Execute(SetAuthLevelThread, std::move(pRequest), "set auth level");
}

void CClanManager::RenameClan(int ClientId, int ClanId, const char *pNewClanName)
{
	auto pResult = NewSqlClanResult(ClientId);
	if(pResult == nullptr)
		return;
	auto pRequest = std::make_unique<CSqlClanRequest>(pResult, this);
	pRequest->m_ClientId = ClientId;
	pRequest->m_ClanId = ClanId;
	str_copy(pRequest->m_aNewClanName, pNewClanName, sizeof(pRequest->m_aNewClanName));
	m_pPool->Execute(RenameClanThread, std::move(pRequest), "rename clan");
}

void CClanManager::AssignClan(int ClientId, const char *AccountName, int ClanId, int AccountId)
{
	// note: i pass an empty string for clan name since it’s not used here
	ExecClanThread(AssignClanThread, "assign clan", ClientId, "", AccountName, AccountId, ClanId);
}

// remove an account from a clan (set clanID and auth_level to 0)
void CClanManager::RemoveFromClan(int ClientId, const char *AccountName, int ClanId)
{
	ExecClanThread(RemoveFromClanThread, "remove from clan", ClientId, "", AccountName, 0, ClanId);
}

void CClanManager::LoadAllClans()
{
	std::shared_ptr<CClanListResult> pResult = std::make_shared<CClanListResult>();
	auto pRequest = std::make_unique<CSqlClanListRequest>(pResult, this);
	m_pPool->Execute(LoadClansThread, std::move(pRequest), "load clans");
}

// --- SQL Thread Functions for Clan Operations ---
bool CClanManager::CreateClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	str_copy(aBuf, "INSERT INTO clans (name) VALUES (?);", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 101: Failed to prepare INSERT statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 101: Clan creation failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindString(1, pData->m_aClanName);
	int NumInserted;
	if(pSqlServer->ExecuteUpdate(&NumInserted, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 102: Failed to execute INSERT statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 102: Clan creation failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumInserted != 1)
	{
		snprintf(pError, ErrorSize, "Error 103: Clan creation failed. No rows inserted.");
		str_copy(pResult->m_aaMessages[0], "Error 103: Unable to create clan. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_copy(aBuf, "SELECT id FROM clans WHERE name = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 104: Failed to prepare SELECT statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 104: Clan creation issue. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindString(1, pData->m_aClanName);
	int ClanId = -1;
	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 105: Failed to execute SELECT statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 105: Clan creation issue. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(!End)
		ClanId = pSqlServer->GetInt(1);
	if(ClanId == -1)
	{
		snprintf(pError, ErrorSize, "Error 106: Failed to retrieve Clan ID.");
		str_copy(pResult->m_aaMessages[0], "Error 106: Clan creation issue. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_copy(aBuf, "UPDATE accounts SET clanID = ?, auth_level = 3 WHERE id = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 107: Failed to prepare UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 107: Clan assignment failed. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindInt(1, ClanId);
	pSqlServer->BindInt(2, pData->m_AccountId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 108: Failed to execute UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 108: Clan assignment failed. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		snprintf(pError, ErrorSize, "Error 109: Account update failed. No rows updated.");
		str_copy(pResult->m_aaMessages[0], "Error 109: Failed to assign you as clan leader.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pData->m_pClanManager->UpdatePlayerClan(pData->m_ClientId, ClanId);
	}

	{
		CClansData NewClan;
		NewClan.m_Id = ClanId;
		str_copy(NewClan.m_ClanName, pData->m_aClanName, sizeof(NewClan.m_ClanName));
		NewClan.m_Level = 1;
		NewClan.m_Experience = 0;
		if(pData->m_pClanManager)
		{
			pData->m_pClanManager->m_vClansData.push_back(NewClan);
		}
	}

	str_copy(pResult->m_aaMessages[0], "Clan created successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
}

bool CClanManager::DeleteClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	str_copy(aBuf, "DELETE FROM clans WHERE id = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 101: Failed to prepare DELETE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 101: Clan deletion failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	pSqlServer->BindInt(1, pData->m_ClanId);
	int NumDeleted;
	if(pSqlServer->ExecuteUpdate(&NumDeleted, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 102: Failed to execute DELETE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 102: Clan deletion failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumDeleted != 1)
	{
		snprintf(pError, ErrorSize, "Error 103: Clan deletion failed. No rows deleted.");
		str_copy(pResult->m_aaMessages[0], "Error 103: Unable to delete clan. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_copy(aBuf, "UPDATE accounts SET clanID = 0, auth_level = 0 WHERE clanID = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 104: Failed to prepare UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 104: Failed to update player data. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindInt(1, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 105: Failed to execute UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 105: Failed to update player data. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated < 1)
	{
		snprintf(pError, ErrorSize, "Error 106: No players were updated. Clan members may not exist.");
		str_copy(pResult->m_aaMessages[0], "Error 106: Failed to update player data.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(pData->m_pClanManager)
	{
		pData->m_pClanManager->ResetPlayersClan(pData->m_ClanId);
	}

	if(pData->m_pClanManager)
	{
		auto &vec = pData->m_pClanManager->m_vClansData;
		for(auto it = vec.begin(); it != vec.end(); ++it)
		{
			if(it->m_Id == pData->m_ClanId)
			{
				vec.erase(it);
				break;
			}
		}
	}

	pResult->SetVariant(CClanResult::DELETE);
	str_copy(pResult->m_aaMessages[0], "Clan deleted successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
}

bool CClanManager::AssignClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	if(pData->m_AccountId != 0)
	{
		str_copy(aBuf, "UPDATE accounts SET clanID = ?, auth_level = 1 WHERE id = ?;", sizeof(aBuf));
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			snprintf(pError, ErrorSize, "Error 201: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pResult->m_aaMessages[0], "Error 201: Assign clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}
		pSqlServer->BindInt(1, pData->m_ClanId);
		pSqlServer->BindInt(2, pData->m_AccountId);
	}
	else
	{
		str_copy(aBuf, "UPDATE accounts SET clanID = ?, auth_level = 1 WHERE name = ?;", sizeof(aBuf));
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			snprintf(pError, ErrorSize, "Error 201: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pResult->m_aaMessages[0], "Error 201: Assign clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}
		pSqlServer->BindInt(1, pData->m_ClanId);
		pSqlServer->BindString(2, pData->m_aUsername);
	}

	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 202: Failed to execute UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 202: Assign clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		snprintf(pError, ErrorSize, "Error 203: Assign clan failed. No rows updated.");
		str_copy(pResult->m_aaMessages[0], "Error 203: Unable to assign clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pData->m_pClanManager->UpdatePlayerClan(pData->m_ClientId, pData->m_ClanId);
	}
	str_copy(pResult->m_aaMessages[0], "Clan assigned successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
}

bool CClanManager::RemoveFromClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	str_copy(aBuf, "UPDATE accounts SET clanID = 0, auth_level = 0 WHERE name = ? AND clanID = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 301: Failed to prepare UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 301: Remove from clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindString(1, pData->m_aUsername);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 302: Failed to execute UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 302: Remove from clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		snprintf(pError, ErrorSize, "Error 303: Remove from clan failed. No rows updated.");
		str_copy(pResult->m_aaMessages[0], "Error 303: Unable to remove from clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	str_copy(pResult->m_aaMessages[0], "Removed from clan successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
}

bool CClanManager::ClanLeaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	// ClanLeave only uses the account id (for the player leaving) and clan id
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	str_copy(aBuf, "UPDATE accounts SET clanID = 0, auth_level = 0 WHERE id = ? AND clanID = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 401: Failed to prepare UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 401: Clan leave failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindInt(1, pData->m_AccountId);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 402: Failed to execute UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 402: Clan leave failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		snprintf(pError, ErrorSize, "Error 403: Clan leave failed. No rows updated.");
		str_copy(pResult->m_aaMessages[0], "Error 403: Unable to leave clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	str_copy(pResult->m_aaMessages[0], "You have left the clan successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
}

bool CClanManager::SetAuthLevelThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	str_copy(aBuf, "UPDATE accounts SET auth_level = ? WHERE name = ? AND clanID = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 501: Failed to prepare UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 501: Set auth level failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindInt(1, pData->m_NewAuthLevel);
	pSqlServer->BindString(2, pData->m_aUsername);
	pSqlServer->BindInt(3, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 502: Failed to execute UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 502: Set auth level failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		snprintf(pError, ErrorSize, "Error 503: Set auth level failed. No rows updated.");
		str_copy(pResult->m_aaMessages[0], "Error 503: Unable to set auth level.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	str_copy(pResult->m_aaMessages[0], "Auth level updated successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
}

bool CClanManager::RenameClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	str_copy(aBuf, "UPDATE clans SET name = ? WHERE id = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 601: Failed to prepare UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 601: Rename clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindString(1, pData->m_aNewClanName);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Error 602: Failed to execute UPDATE statement: %s", pError);
		str_copy(pResult->m_aaMessages[0], "Error 602: Rename clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		snprintf(pError, ErrorSize, "Error 603: Rename clan failed. No rows updated.");
		str_copy(pResult->m_aaMessages[0], "Error 603: Unable to rename clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	// update the in-memory clan vector
	if(pData->m_pClanManager)
	{
		auto &vec = pData->m_pClanManager->m_vClansData;
		for(auto &Clan : vec)
		{
			if(Clan.m_Id == pData->m_ClanId)
			{
				str_copy(Clan.m_ClanName, pData->m_aNewClanName, sizeof(Clan.m_ClanName));
				break;
			}
		}
	}
	str_copy(pResult->m_aaMessages[0], "Clan renamed successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
}

// loads clans data into a CClansData vector
bool CClanManager::LoadClansThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanListRequest *pRequest = static_cast<const CSqlClanListRequest *>(pGameData);
	char aBuf[1024];
	str_copy(aBuf, "SELECT id, name, level, experience FROM clans;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		snprintf(pError, ErrorSize, "Failed to prepare LOAD clans statement: %s", pError);
		return true;
	}

	bool End = false;
	CClanListResult *pResult = static_cast<CClanListResult *>(pGameData->m_pResult.get());
	while(!End)
	{
		if(pSqlServer->Step(&End, pError, ErrorSize))
		{
			snprintf(pError, ErrorSize, "Failed to execute LOAD clans statement: %s", pError);
			return true;
		}
		if(!End)
		{
			CClansData Data;
			Data.m_Id = pSqlServer->GetInt(1);
			pSqlServer->GetString(2, Data.m_ClanName, sizeof(Data.m_ClanName));
			Data.m_Level = pSqlServer->GetInt(3);
			Data.m_Experience = pSqlServer->GetInt(4);
			pResult->m_vClans.push_back(Data);
		}
	}

	if(pRequest->m_pClanManager)
	{
		pRequest->m_pClanManager->OnClansLoaded(pResult->m_vClans);
	}

	dbg_msg("clan", "Loaded %d clans into memory", (int)pResult->m_vClans.size());
	return false;
}

// helper functions because I need help (i'm going crazy)

int CClanManager::GetClanIdByName(const char *pClanName)
{
	for(const auto &Clan : m_vClansData)
	{
		if(str_comp(Clan.m_ClanName, pClanName) == 0)
			return Clan.m_Id;
	}
	return -1;
}

void CClanManager::UpdatePlayerClan(int ClientId, int NewClanId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(pPlayer)
	{
		pPlayer->m_Account.m_ClanId = NewClanId;
		pPlayer->m_Account.m_AuthLevel = 3; // as leader
	}
}

void CClanManager::ResetPlayersClan(int ClanId)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(pPlayer && pPlayer->m_Account.m_ClanId == ClanId)
		{
			pPlayer->m_Account.m_ClanId = 0;
			pPlayer->m_Account.m_AuthLevel = 0;
		}
	}
}
