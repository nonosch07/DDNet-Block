#include <algorithm>
#include <cctype>
#include <engine/server/databases/connection.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <mutex>
#include <unordered_map>

#include "clans.h"
#include "engine/shared/config.h"
#include "engine/shared/protocol.h"

#define MAX_CLAN_NAME_LENGTH 12

void ToLowercase(char *str)
{
	for(; *str; ++str)
		*str = std::tolower(*str);
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

std::shared_ptr<CClanResult> CClanManager::NewSqlClanResult(int ClientId)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pCurPlayer)
		return nullptr;
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
	ExecClanThread(ClanLeaveThread, "clan leave", ClientId, "", pPlayer->GetPlayerName(), pPlayer->GetAccId(), pPlayer->m_Account.m_ClanId);
}

void CClanManager::SaveClan(int ClientId, int ClanId)
{
	ExecClanThread(SaveClanThread, "save clan", ClientId, "", "", 0, ClanId);
}

void CClanManager::ShowTopClans(int ClientId)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecClanThread(ShowTopClansThread, "show top clans thread", ClientId, "", "", 0, 0);
}

void CClanManager::SetAuthLevel(int ClientId, const char *AccountName, int NewAuthLevel, int ClanId)
{
	if(RateLimitPlayer(ClientId))
		return;
	ExecClanThread(SetAuthLevelThread, "auth level thread", ClientId, "", AccountName, 0, ClanId, NewAuthLevel);
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
	ExecClanThread(AssignClanThread, "assign clan", ClientId, "", AccountName, AccountId, ClanId);
}

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
static bool CheckClanPermission(IDbConnection *pSqlServer, int AccountId, int ClanId, int RequiredAuthLevel, char *pError, int ErrorSize)
{
	char aBuf[256];
	str_copy(aBuf, "SELECT clanID, auth_level FROM accounts WHERE id = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		return false;
	pSqlServer->BindInt(1, AccountId);
	bool End = false;
	if(pSqlServer->Step(&End, pError, ErrorSize))
		return false;
	if(End)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Permission denied: account not found.", ErrorSize);
		return false;
	}

	// Note: IDbConnection uses 1-based column indices (first column = 1)
	int DbClanId = pSqlServer->GetInt(1);
	int DbAuthLevel = pSqlServer->GetInt(2);

	if(DbClanId != ClanId || DbAuthLevel < RequiredAuthLevel)
	{
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

	str_copy(aBuf, "INSERT INTO clans (name) VALUES (?);", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 101: Failed to prepare INSERT statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 101: Clan creation failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindString(1, pData->m_aClanName);
	int NumInserted;
	if(pSqlServer->ExecuteUpdate(&NumInserted, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 102: Failed to execute INSERT statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 102: Clan creation failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumInserted != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 103: Clan creation failed. No rows inserted.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 103: Unable to create clan. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_copy(aBuf, "SELECT id FROM clans WHERE name = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 104: Failed to prepare SELECT statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 104: Clan creation issue. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindString(1, pData->m_aClanName);
	int ClanId = -1;
	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 105: Failed to execute SELECT statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 105: Clan creation issue. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(!End)
		ClanId = pSqlServer->GetInt(1);
	if(ClanId == -1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 106: Failed to retrieve Clan ID.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 106: Clan creation issue. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_copy(aBuf, "UPDATE accounts SET clanID = ?, auth_level = 3 WHERE id = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 107: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 107: Clan assignment failed. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindInt(1, ClanId);
	pSqlServer->BindInt(2, pData->m_AccountId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 108: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 108: Clan assignment failed. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 109: Account update failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 109: Failed to assign you as clan leader.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	// push the new skidibi clan data into the skibidi in-memory container
	{
		CClansData NewClan;
		NewClan.m_Id = ClanId;
		str_copy(NewClan.m_ClanName, pData->m_aClanName, MAX_CLAN_NAME_LENGTH);
		ToLowercase(NewClan.m_ClanName);
		NewClan.m_Level = 1;
		NewClan.m_Experience = 0;
		if(pData->m_pClanManager)
		{
			std::lock_guard<std::mutex> lock(g_ClansDataMutex);
			pData->m_pClanManager->m_vClansData.push_back(NewClan);
		}
	}

	// update the player's in-memory clan data
	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pData->m_pClanManager->UpdatePlayerClan(pData->m_ClientId, ClanId, 3);
	}
	str_copy(pResult->m_aaMessages[0], "Clan created successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
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
		return true;
	}

	str_copy(aBuf, "DELETE FROM clans WHERE id = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 101: Failed to prepare DELETE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 101: Clan deletion failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindInt(1, pData->m_ClanId);
	int NumDeleted;
	if(pSqlServer->ExecuteUpdate(&NumDeleted, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 102: Failed to execute DELETE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 102: Clan deletion failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumDeleted != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 103: Clan deletion failed. No rows deleted.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 103: Unable to delete clan. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_copy(aBuf, "UPDATE accounts SET clanID = 0, auth_level = 0 WHERE clanID = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 104: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 104: Failed to update player data. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindInt(1, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 105: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 105: Failed to update player data. Please try again.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated < 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 106: No players were updated. Clan members may not exist.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 106: Failed to update player data.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(pData->m_pClanManager)
	{
		CGameContext *pGameServer = pData->m_pClanManager->GameServer();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pPlayer = pGameServer->m_apPlayers[i];
			if(pPlayer && pPlayer->m_Account.m_ClanId == pData->m_ClanId)
			{
				pGameServer->SendChatTarget(i, "Your clan has been deleted.");
			}
		}
		pData->m_pClanManager->ResetPlayersClan(pData->m_ClanId);

		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
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

	str_copy(pResult->m_aaMessages[0], "Clan deleted successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
}
bool CClanManager::AssignClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::DIRECT);

	if(pData->m_AccountId != 0)
	{
		str_copy(aBuf, "UPDATE accounts SET clanID = ?, auth_level = 1 WHERE id = ?;", sizeof(aBuf));
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			if(pError && *pError)
			{
				char aTmp[1024];
				str_format(aTmp, sizeof(aTmp), "Error 201: Failed to prepare UPDATE statement: %s", pError);
				str_copy(pError, aTmp, ErrorSize);
			}
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
			if(pError && *pError)
			{
				char aTmp[1024];
				str_format(aTmp, sizeof(aTmp), "Error 201: Failed to prepare UPDATE statement: %s", pError);
				str_copy(pError, aTmp, ErrorSize);
			}
			str_copy(pResult->m_aaMessages[0], "Error 201: Assign clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
			return true;
		}
		pSqlServer->BindInt(1, pData->m_ClanId);
		pSqlServer->BindString(2, pData->m_aUsername);
	}

	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 202: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 202: Assign clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 203: Assign clan failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 203: Unable to assign clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pData->m_pClanManager->UpdatePlayerClan(pData->m_ClientId, pData->m_ClanId, 1);
	}

	{
		CGameContext *pGameServer = pData->m_pClanManager->GameServer();
		char aBroadcast[256];
		str_format(aBroadcast, sizeof(aBroadcast), "%s has joined the clan!", pData->m_aUsername);
		pGameServer->SendChatClan(pData->m_ClanId, aBroadcast);
	}

	// str_copy(pResult->m_aaMessages[0], "Clan assignment successful.", sizeof(pResult->m_aaMessages[0]));
	return false;
}

bool CClanManager::RemoveFromClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];
	pResult->SetVariant(CClanResult::DIRECT);

	if(!CheckClanPermission(pSqlServer, pData->m_AccountId, pData->m_ClanId, 2, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], pError, sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_copy(aBuf, "UPDATE accounts SET clanID = 0, auth_level = 0 WHERE name = ? AND clanID = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 301: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 301: Remove from clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindString(1, pData->m_aUsername);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 302: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 302: Remove from clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 303: Remove from clan failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 303: Unable to remove from clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pResult->SetVariant(CClanResult::DIRECT);
		pData->m_pClanManager->UpdatePlayerClan(pData->m_ClientId, 0, 0);
	}

	CGameContext *pGameServer = pData->m_pClanManager->GameServer();

	// -- notify the kicked player --
	int KickedClientId = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pTarget = pGameServer->m_apPlayers[i];
		if(pTarget && pTarget->IsLoggedIn() && str_comp(pTarget->m_Account.m_aName, pData->m_aUsername) == 0)
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
		pGameServer->SendChatClan(pData->m_ClanId, aBroadcast);
	}

	// str_copy(pResult->m_aaMessages[0], "Removed from clan successfully!", sizeof(pResult->m_aaMessages[0]));

	return false;
}

bool CClanManager::ClanLeaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::DIRECT);

	str_copy(aBuf, "UPDATE accounts SET clanID = 0, auth_level = 0 WHERE id = ? AND clanID = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 401: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 401: Clan leave failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindInt(1, pData->m_AccountId);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 402: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 402: Clan leave failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 403: Clan leave failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 403: Unable to leave clan.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pData->m_pClanManager->UpdatePlayerClan(pData->m_ClientId, 0, 0);
	}
	str_copy(pResult->m_aaMessages[0], "You have left the clan successfully!", sizeof(pResult->m_aaMessages[0]));
	return false;
}

bool CClanManager::SetAuthLevelThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::CLAN);

	if(!CheckClanPermission(pSqlServer, pData->m_AccountId, pData->m_ClanId, 3, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], pError, sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_copy(aBuf, "UPDATE accounts SET auth_level = ? WHERE name = ? AND clanID = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 501: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 501: Set auth level failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindInt(1, pData->m_NewAuthLevel);
	pSqlServer->BindString(2, pData->m_aUsername);
	pSqlServer->BindInt(3, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 502: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 502: Set auth level failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 503: Set auth level failed. No rows updated.", ErrorSize);
		str_copy(pResult->m_aaMessages[0], "Error 503: Unable to set auth level.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	if(pData->m_pClanManager && pData->m_ClientId >= 0)
	{
		pData->m_pClanManager->UpdatePlayerClan(pData->m_ClientId, pData->m_ClanId, pData->m_NewAuthLevel);
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
	str_copy(pResult->m_aaMessages[0], aMessage, sizeof(pResult->m_aaMessages[0]));

	return false;
}

bool CClanManager::RenameClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::DIRECT);

	if(!CheckClanPermission(pSqlServer, pData->m_AccountId, pData->m_ClanId, 3, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], pError, sizeof(pResult->m_aaMessages[0]));
		return true;
	}

	str_copy(aBuf, "UPDATE clans SET name = ? WHERE id = ?;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 601: Failed to prepare UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 601: Rename clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	pSqlServer->BindString(1, pData->m_aNewClanName);
	pSqlServer->BindInt(2, pData->m_ClanId);
	int NumUpdated;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error 602: Failed to execute UPDATE statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		str_copy(pResult->m_aaMessages[0], "Error 602: Rename clan failed. Please try again later.", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(NumUpdated != 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error 603: Rename clan failed. No rows updated.", ErrorSize);
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
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Failed to prepare LOAD clans statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		return true;
	}

	bool End = false;
	CClanListResult *pResult = static_cast<CClanListResult *>(pGameData->m_pResult.get());
	while(!End)
	{
		if(pSqlServer->Step(&End, pError, ErrorSize))
		{
			if(pError && *pError)
			{
				char aTmp[1024];
				str_format(aTmp, sizeof(aTmp), "Failed to execute LOAD clans statement: %s", pError);
				str_copy(pError, aTmp, ErrorSize);
			}
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
		std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
		if(g_ClanNameToId.count(nameLower))
		{
			dbg_msg("clan", "Duplicate clan name '%s' detected!", clan.m_ClanName);
			continue;
		}
		g_ClanIdMap[clan.m_Id] = clan;
		g_ClanNameToId[nameLower] = clan.m_Id;
	}
	dbg_msg("clan", "Loaded %d clans into memory (map size: %zu)", (int)m_vClansData.size(), g_ClanIdMap.size());
}

// helper functions because I need help (i'm going crazy)
int CClanManager::GetClanIdByName(const char *pClanName)
{
	std::string nameLower(pClanName);
	std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
	std::lock_guard<std::mutex> lock(g_ClansDataMutex);
	auto it = g_ClanNameToId.find(nameLower);
	if(it != g_ClanNameToId.end())
		return it->second;
	return -1;
}

const char *CClanManager::GetClanName(int ClanId)
{
	std::lock_guard<std::mutex> lock(g_ClansDataMutex);
	auto it = g_ClanIdMap.find(ClanId);
	if(it != g_ClanIdMap.end())
		return it->second.m_ClanName;
	return " ";
}

void CClanManager::UpdatePlayerClan(int ClientId, int ClanId, int AuthLevel)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	pPlayer->m_Account.m_ClanId = ClanId;
	pPlayer->m_Account.m_AuthLevel = AuthLevel;
	{
		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
		auto it = g_ClanIdMap.find(ClanId);
		pPlayer->m_Account.m_pClanData = (it != g_ClanIdMap.end()) ? &it->second : nullptr;
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
			GameServer()->SendChatTarget(-1, aBuf);
		}

		for(auto &vecClan : m_vClansData)
		{
			if(vecClan.m_Id == ClanId)
			{
				vecClan.m_Experience = Clan.m_Experience;
				vecClan.m_Level = Clan.m_Level;
				break;
			}
		}
	}
}

bool CClanManager::SaveClanThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	char aBuf[1024];

	if(pData->m_ClanId < 1)
	{
		if(pError && ErrorSize > 0)
			str_copy(pError, "Error: Couldn't retrieve clan ID!", ErrorSize);
		return true;
	}
	str_copy(aBuf, "UPDATE clans SET level = ?, experience = ? WHERE id = ?;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error: Failed to prepare SAVE clan statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		return true;
	}

	const CClansData *pClan = GetClanDataById(pData->m_ClanId, pData->m_pClanManager->GetClansData());
	if(!pClan)
	{
		if(pError && ErrorSize > 0)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error: Clan id %d not found in memory", pData->m_ClanId);
			str_copy(pError, aTmp, ErrorSize);
		}
		return true;
	}

	pSqlServer->BindInt(1, pClan->m_Level);
	pSqlServer->BindInt(2, pClan->m_Experience);
	pSqlServer->BindInt(3, pClan->m_Id);

	int NumUpdated = 0;
	if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		if(pError && *pError)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error: Failed to execute SAVE clan statement: %s", pError);
			str_copy(pError, aTmp, ErrorSize);
		}
		return true;
	}
	// allow 0 rows updated (if values didn’t change) without treating it as an error
	if(NumUpdated < 0)
	{
		if(pError && ErrorSize > 0)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error: SAVE clan failed with negative rows updated for clan id %d.", pClan->m_Id);
			str_copy(pError, aTmp, ErrorSize);
		}
		return true;
	}
	else if(NumUpdated == 0)
	{
		dbg_msg("clan", "Clan %d save: no changes made (0 rows updated).", pClan->m_Id);
	}
	else
	{
		dbg_msg("clan", "Clan %d saved successfully (rows updated: %d).", pClan->m_Id, NumUpdated);
	}

	int CurrentTick = pData->m_pClanManager->GameServer()->Server()->Tick();
	for(auto &Clan : pData->m_pClanManager->m_vClansData)
	{
		if(Clan.m_Id == pClan->m_Id)
		{
			Clan.m_LastSavedTick = CurrentTick;
			break;
		}
	}

	return false;
}

bool CClanManager::ShowTopClansThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	CClanResult *pResult = dynamic_cast<CClanResult *>(pGameData->m_pResult.get());
	pResult->SetVariant(CClanResult::DIRECT);
	char aBuf[512];
	str_copy(aBuf, "SELECT name, level, experience FROM clans ORDER BY level DESC, experience DESC LIMIT 10;", sizeof(aBuf));

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
		dbg_msg("top_clans", "SQL preparation failed: %s", pError);
		pResult->m_Success = false;
		return true;
	}

	int Line = 0;
	str_copy(pResult->m_aaMessages[Line], "---------- Top Clans ----------", sizeof(pResult->m_aaMessages[Line]));
	Line++;

	bool End = false;
	while(!pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		*pError = '\0';
		char aClanName[32];
		int Level = 0;

		pSqlServer->GetString(1, aClanName, sizeof(aClanName));
		if(*pError != '\0')
		{
			dbg_msg("top_clans", "Failed to retrieve clan name: %s", pError);
			str_copy(pResult->m_aaMessages[0], "Failed to retrieve clan name.", sizeof(pResult->m_aaMessages[0]));
			pResult->m_Success = false;
			return true;
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
		return true;
	}

	return false;
}
