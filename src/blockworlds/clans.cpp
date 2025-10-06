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

#define MAX_CLAN_NAME_LENGTH 32

void ToLowercase(char *str)
{
	for(; *str; ++str)
		*str = std::tolower(*str);
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

std::shared_ptr<CClanResult> CClanManager::NewSqlClanResult(int ClientId)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pCurPlayer || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;

	auto pResult = std::make_shared<CClanResult>();
	pCurPlayer->m_ClanQueryResult.push(pResult);
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
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	ExecClanThread(SetAuthLevelThread, "auth level thread", ClientId, "", AccountName, pPlayer->GetAccId(), ClanId, NewAuthLevel);
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
	ExecClanThread(RemoveFromClanThread, "remove from clan", ClientId, "", AccountName, pPlayer->GetAccId(), ClanId);
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

	// Copy & normalize clan name (trim spaces) without mutating original structure buffer.
	char aNormalizedName[MAX_CLAN_NAME_LENGTH + 1];
	str_copy(aNormalizedName, pData->m_aClanName, sizeof(aNormalizedName));
	TrimSpaces(aNormalizedName);
	if(str_length(aNormalizedName) < 3 || str_length(aNormalizedName) > MAX_CLAN_NAME_LENGTH)
	{
		str_copy(pResult->m_aaMessages[0], "Clan name must be between 3 and 32 characters (after trimming).", sizeof(pResult->m_aaMessages[0]));
		return false;
	}
	for(int i = 0; aNormalizedName[i]; i++)
	{
		char c = aNormalizedName[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' '))
		{
			str_copy(pResult->m_aaMessages[0], "Clan name can only contain letters, numbers, underscore, dash and spaces.", sizeof(pResult->m_aaMessages[0]));
			return false;
		}
	}

	// Case-insensitive uniqueness check
	str_copy(aBuf, "SELECT id FROM clans WHERE LOWER(name) = LOWER(?) LIMIT 1;", sizeof(aBuf));
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 100: Failed to validate clan name uniqueness.", sizeof(pResult->m_aaMessages[0]));
		return true; // treat as handled error
	}
	pSqlServer->BindString(1, aNormalizedName);
	int ExistingId = -1;
	bool EndCheck = false;
	if(pSqlServer->Step(&EndCheck, pError, ErrorSize))
	{
		str_copy(pResult->m_aaMessages[0], "Error 100: Failed to validate clan name uniqueness (step).", sizeof(pResult->m_aaMessages[0]));
		return true;
	}
	if(!EndCheck)
		ExistingId = pSqlServer->GetInt(1);
	if(ExistingId != -1)
	{
		str_copy(pResult->m_aaMessages[0], "Clan name already exists.", sizeof(pResult->m_aaMessages[0]));
		return false; // not a server error
	}

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
	pSqlServer->BindString(1, aNormalizedName);
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
	pSqlServer->BindString(1, aNormalizedName);
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

	str_format(aBuf, sizeof(aBuf), "UPDATE accounts SET clanID = ?, auth_level = %d WHERE id = ?;", (int)ClanAuthLevel::LEADER);
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
		str_copy(NewClan.m_ClanName, aNormalizedName, sizeof(NewClan.m_ClanName));
		// keep the stored name as given, but map keys are lowercase
		char aNameLower[MAX_CLAN_NAME_LENGTH + 1];
		str_copy(aNameLower, aNormalizedName, sizeof(aNameLower));
		ToLowercase(aNameLower);
		NewClan.m_Level = 1;
		NewClan.m_Experience = 0;
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
	str_copy(pResult->m_aaMessages[0], "Clan created successfully!", sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;
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

	str_format(aBuf, sizeof(aBuf), "UPDATE accounts SET clanID = 0, auth_level = %d WHERE clanID = ?;", (int)ClanAuthLevel::NONE);
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
				std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
				int id = it->m_Id;
				vec.erase(it);
				// erase from maps as well
				g_ClanIdMap.erase(id);
				g_ClanNameToId.erase(nameLower);
				break;
			}
		}
	}

	str_copy(pResult->m_aaMessages[0], "Clan deleted successfully!", sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;
	return false;
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
			return true;
		}
		int inviterAccId = pInviter->GetAccId();
		// only coleader or leader can invite
		if(!CheckClanPermission(pSqlServer, inviterAccId, pData->m_ClanId, (int)ClanAuthLevel::COLEADER, pError, ErrorSize))
		{
			str_copy(pResult->m_aaMessages[0], pError, sizeof(pResult->m_aaMessages[0]));
			return true;
		}
	}

	if(pData->m_AccountId != 0)
	{
		str_format(aBuf, sizeof(aBuf), "UPDATE accounts SET clanID = ?, auth_level = %d WHERE id = ?;", (int)ClanAuthLevel::MEMBER);
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
		str_format(aBuf, sizeof(aBuf), "UPDATE accounts SET clanID = ?, auth_level = %d WHERE name = ?;", (int)ClanAuthLevel::MEMBER);
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
		pGameServer->SendChatClan(pData->m_ClanId, aBroadcast);
	}

	// mark result as successful so the main thread processes it
	pResult->m_Success = true;
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

	str_format(aBuf, sizeof(aBuf), "UPDATE accounts SET clanID = 0, auth_level = %d WHERE name = ? AND clanID = ?;", (int)ClanAuthLevel::NONE);
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

	// Mark the SQL result as successful so the main thread processes it
	pResult->m_Success = true;

	return false;
}

bool CClanManager::ClanLeaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlClanRequest *pData = static_cast<const CSqlClanRequest *>(pGameData);
	CClanResult *pResult = static_cast<CClanResult *>(pGameData->m_pResult.get());
	char aBuf[1024];

	pResult->SetVariant(CClanResult::DIRECT);

	str_format(aBuf, sizeof(aBuf), "UPDATE accounts SET clanID = 0, auth_level = %d WHERE id = ? AND clanID = ?;", (int)ClanAuthLevel::NONE);
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
		pResult->m_Action = CClanResult::ACTION_UPDATE_PLAYER_BY_CLIENT;
		pResult->m_ActionClientId = pData->m_ClientId;
		pResult->m_ActionNewClanId = 0;
		pResult->m_ActionNewAuthLevel = static_cast<int>(ClanAuthLevel::NONE);
	}
	str_copy(pResult->m_aaMessages[0], "You have left the clan successfully!", sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;
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
	str_copy(pResult->m_aaMessages[0], aMessage, sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;

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

	// update the in-memory clan vector (protected by mutex)
	if(pData->m_pClanManager)
	{
		std::string oldNameLower;
		{
			std::lock_guard<std::mutex> lock(g_ClansDataMutex);
			auto &vec = pData->m_pClanManager->m_vClansData;
			for(auto &Clan : vec)
			{
				if(Clan.m_Id == pData->m_ClanId)
				{
					oldNameLower = Clan.m_ClanName;
					str_copy(Clan.m_ClanName, pData->m_aNewClanName, sizeof(Clan.m_ClanName));
					break;
				}
			}
			// update global maps
			auto it = g_ClanIdMap.find(pData->m_ClanId);
			if(it != g_ClanIdMap.end())
			{
				// remove old name mapping
				std::transform(oldNameLower.begin(), oldNameLower.end(), oldNameLower.begin(), ::tolower);
				if(!oldNameLower.empty())
					g_ClanNameToId.erase(oldNameLower);
				// set new name in id map and add name->id map
				it->second.m_ClanName[0] = '\0';
				str_copy(it->second.m_ClanName, pData->m_aNewClanName, sizeof(it->second.m_ClanName));
				std::string newLower(pData->m_aNewClanName);
				std::transform(newLower.begin(), newLower.end(), newLower.begin(), ::tolower);
				g_ClanNameToId[newLower] = pData->m_ClanId;
			}
		}
	}
	str_copy(pResult->m_aaMessages[0], "Clan renamed successfully!", sizeof(pResult->m_aaMessages[0]));
	pResult->m_Success = true;
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

	CClansData ClanCopy;
	if(!pData->m_pClanManager->GetClanSnapshotById(pData->m_ClanId, ClanCopy))
	{
		if(pError && ErrorSize > 0)
		{
			char aTmp[1024];
			str_format(aTmp, sizeof(aTmp), "Error: Clan id %d not found in memory", pData->m_ClanId);
			str_copy(pError, aTmp, ErrorSize);
		}
		return true;
	}

	pSqlServer->BindInt(1, ClanCopy.m_Level);
	pSqlServer->BindInt(2, ClanCopy.m_Experience);
	pSqlServer->BindInt(3, ClanCopy.m_Id);

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
			str_format(aTmp, sizeof(aTmp), "Error: SAVE clan failed with negative rows updated for clan id %d.", ClanCopy.m_Id);
			str_copy(pError, aTmp, ErrorSize);
		}
		return true;
	}
	else if(NumUpdated == 0)
	{
		dbg_msg("clan", "Clan %d save: no changes made (0 rows updated).", ClanCopy.m_Id);
	}
	else
	{
		dbg_msg("clan", "Clan %d saved successfully (rows updated: %d).", ClanCopy.m_Id, NumUpdated);
	}

	int CurrentTick = pData->m_pClanManager->GameServer()->Server()->Tick();
	// update the in-memory clan's last saved tick under mutex to avoid races with main thread
	{
		std::lock_guard<std::mutex> lock(g_ClansDataMutex);
		for(auto &Clan : pData->m_pClanManager->m_vClansData)
		{
			if(Clan.m_Id == ClanCopy.m_Id)
			{
				Clan.m_LastSavedTick = CurrentTick;
				break;
			}
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
