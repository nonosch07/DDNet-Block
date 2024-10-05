#include <cstddef>
#include <engine/server/databases/connection.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "accounts.h"

// Credits: Strongly inspired by ChillerDragon's account system.

CAdminCommandResult::CAdminCommandResult()
{
	SetVariant(Variant::DIRECT, NULL);
}

void CAdminCommandResult::SetVariant(Variant v, const CSqlAdminCommandRequest *pRequest)
{
	if(pRequest)
	{
		m_AdminClientId = pRequest->m_AdminClientId;
		m_TargetAccountId = pRequest->m_TargetAccountId;
		m_State = pRequest->m_State;
		m_Type = pRequest->m_Type;
		str_copy(m_aUsername, pRequest->m_aUsername, sizeof(m_aUsername));
		str_copy(m_aPassword, pRequest->m_aPassword, sizeof(m_aPassword));
	}
	else
	{
		m_AdminClientId = -1;
		m_TargetAccountId = -1;
		m_State = -1;
		m_Type = DIRECT;
		m_aUsername[0] = '\0';
		m_aPassword[0] = '\0';
	}
	m_MessageKind = v;
	switch(v)
	{
	case FREEZE_ACC:
	case MODERATOR:
	case SUPER_MODERATOR:
	case SUPPORTER:
	case DIRECT:
	case ALL:
		for(auto &aMessage : m_aaMessages)
			aMessage[0] = 0;
		break;
	case BROADCAST:
		m_aBroadcast[0] = 0;
		break;
	case LOG_ONLY:
		break;
	}
}

CAccountResult::CAccountResult()
{
	SetVariant(Variant::DIRECT);
}


CAccounts::CAccounts(CGameContext *pGameServer, CDbConnectionPool *pPool) :
	m_pPool(pPool),
	m_pGameServer(pGameServer),
	m_pServer(pGameServer->Server())
{
}

std::shared_ptr<CAdminCommandResult> CAccounts::NewSqlAdminCommandResult(int ClientId)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientId];
	if(pCurPlayer->m_AdminCommandQueryResult != nullptr)
		return nullptr;
	pCurPlayer->m_AdminCommandQueryResult = std::make_shared<CAdminCommandResult>();
	return pCurPlayer->m_AdminCommandQueryResult;
}

void CAccounts::ExecAdminThread(
	bool (*pFuncPtr)(IDbConnection *, const ISqlData *, char *pError, int ErrorSize),
	const char *pThreadName,
	int AdminClientId,
	int TargetAccountId,
	int State,
	CAdminCommandResult::Variant Type,
	const char *pUsername,
	const char *pPassword,
	const char *pQuery)
{
	auto pResult = NewSqlAdminCommandResult(AdminClientId);
	if(pResult == nullptr)
		return;
	auto Tmp = std::make_unique<CSqlAdminCommandRequest>(pResult);
	Tmp->m_AdminClientId = AdminClientId;
	Tmp->m_TargetAccountId = TargetAccountId;
	Tmp->m_State = State;
	Tmp->m_Type = Type;
	str_copy(Tmp->m_aUsername, pUsername, sizeof(Tmp->m_aUsername));
	str_copy(Tmp->m_aPassword, pPassword, sizeof(Tmp->m_aPassword));
	str_copy(Tmp->m_aQuery, pQuery, sizeof(Tmp->m_aQuery));

	m_pPool->Execute(pFuncPtr, std::move(Tmp), pThreadName);
}

std::shared_ptr<CAccountResult> CAccounts::NewSqlAccountResult(int ClientId)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientId];
	if(pCurPlayer->m_AccountQueryResult != nullptr)
		return nullptr;
	pCurPlayer->m_AccountQueryResult = std::make_shared<CAccountResult>();
	return pCurPlayer->m_AccountQueryResult;
}

void CAccounts::ExecUserThread(
	bool (*pFuncPtr)(IDbConnection *, const ISqlData *, char *pError, int ErrorSize),
	const char *pThreadName,
	int ClientId,
	const char *pUsername,
	const char *pPassword,
	const char *pNewPassword,
    int AccountId,
	CAccountData *pAccountData)
{
	auto pResult = NewSqlAccountResult(ClientId);
	if(pResult == nullptr)
		return;
	auto Tmp = std::make_unique<CSqlAccountRequest>(pResult);
	Tmp->m_ClientId = ClientId;
	str_copy(Tmp->m_aUsername, pUsername, sizeof(Tmp->m_aUsername));
	str_copy(Tmp->m_aPassword, pPassword, sizeof(Tmp->m_aPassword));
	str_copy(Tmp->m_aNewPassword, pNewPassword, sizeof(Tmp->m_aNewPassword));
    if(pAccountData)
    {
		Tmp->m_AccountData = *pAccountData;
    }
	else
    {
		Tmp->m_AccountData = CAccountData();
    }

	m_pPool->Execute(pFuncPtr, std::move(Tmp), pThreadName);
}

void CAccounts::Save(int ClientId, CAccountData *pAccountData)
{
    ExecUserThread(SaveThread, "save user", ClientId, "", "", "", 0, pAccountData);
}

bool CAccounts::SaveThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
    const CSqlAccountRequest *pData = dynamic_cast<const CSqlAccountRequest *>(pGameData);
    CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());
    pResult->SetVariant(CAccountResult::LOG_ONLY);

    char aBuf[2048];
    str_copy(aBuf,
        "UPDATE Accounts SET "
        "address = ?, is_logged_in = ?, vip = ?, pages = ?, level = ?, experience = ?, weaponkits = ?, ranking = ?,"
        "blockpoints = ?, knockouts = ?, gundesign = ?, skinmani = ?, extras = ?, ranked_games = ?,"
        "ranked_kills = ?, ranked_deaths = ?, ranked_wins = ?, kills = ?, deaths = ?, tourney_win = ?, playtime = ?, killstreak = ?,"
        "last_name = ?, last_skin = ?, last_body_color = ?, last_feet_color = ? "
        "WHERE id = ?;",
        sizeof(aBuf));

    if (pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
    {
        dbg_msg("sql", "PrepareStatement failed: %s", pError);
        return true;
    }

    const CAccountData *pAcc = &pData->m_AccountData;
    int Index = 1;
    pSqlServer->BindString(Index++, pAcc->m_aAddress);
    pSqlServer->BindInt(Index++, pAcc->m_IsLoggedIn);
    pSqlServer->BindInt(Index++, pAcc->m_Vip);
    pSqlServer->BindInt(Index++, pAcc->m_Pages);
    pSqlServer->BindInt(Index++, pAcc->m_Level);
    pSqlServer->BindInt(Index++, pAcc->m_Experience);
    pSqlServer->BindInt(Index++, pAcc->m_Weaponkits);
    pSqlServer->BindInt(Index++, pAcc->m_Ranking);
    pSqlServer->BindInt(Index++, pAcc->m_Blockpoints);
    pSqlServer->BindString(Index++, pAcc->m_aKnockouts);
    pSqlServer->BindString(Index++, pAcc->m_aGundesign);
    pSqlServer->BindString(Index++, pAcc->m_aSkinmani);
    pSqlServer->BindString(Index++, pAcc->m_aExtras);
    pSqlServer->BindInt(Index++, pAcc->m_RankedGames);
    pSqlServer->BindInt(Index++, pAcc->m_RankedKills);
    pSqlServer->BindInt(Index++, pAcc->m_RankedDeaths);
    pSqlServer->BindInt(Index++, pAcc->m_RankedWins);
    pSqlServer->BindInt(Index++, pAcc->m_Kills);
    pSqlServer->BindInt(Index++, pAcc->m_Deaths);
    pSqlServer->BindInt(Index++, pAcc->m_TourneyWin);
    pSqlServer->BindInt64(Index++, pAcc->m_Playtime);
    pSqlServer->BindInt(Index++, pAcc->m_Killstreak);
    pSqlServer->BindString(Index++, pAcc->m_aLastName);
    pSqlServer->BindString(Index++, pAcc->m_aLastSkin);
    pSqlServer->BindInt(Index++, pAcc->m_LastBodyColor);
    pSqlServer->BindInt(Index++, pAcc->m_LastFeetColor);
    pSqlServer->BindInt(Index++, pAcc->m_Id);

    int NumUpdated;

    if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		return true;
	}
	return false;
}


void CAccounts::Login(int ClientId, const char *pUsername, const char *pPassword)
{
	ExecUserThread(LoginThread, "login user", ClientId, pUsername, pPassword, "", 0, NULL);
}

bool CAccounts::LoginThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
    const CSqlAccountRequest *pRequestData = dynamic_cast<const CSqlAccountRequest *>(pGameData);
    CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());

    // Hash the input password
    SHA256_DIGEST HashedPassword = CGameContext::HashPassword(pRequestData->m_aPassword);
    char aHashedPassword[SHA256_DIGEST_LENGTH * 2 + 1];
    for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        sprintf(&aHashedPassword[i * 2], "%02x", HashedPassword.data[i]);

    char aBuf[2048];
    str_copy(aBuf,
        "SELECT "
        "id, name, password, address, is_logged_in, vip, pages, level, experience, weaponkits, ranking, "
        "clan, blockpoints, knockouts, gundesign, skinmani, extras, registerdate, ranked_games, "
        "ranked_kills, ranked_deaths, ranked_wins, kills, deaths, tourney_win, playtime, killstreak, "
        "last_name, last_skin, last_body_color, last_feet_color "
        "FROM Accounts "
        "WHERE name = ? AND password = ?;", sizeof(aBuf));

    if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
    {
        return true;
    }

    pSqlServer->BindString(1, pRequestData->m_aUsername);
    pSqlServer->BindString(2, aHashedPassword);

    bool End;
    if(pSqlServer->Step(&End, pError, ErrorSize))
    {
        return true;
    }

    if (!End)
    {
        if(pSqlServer->GetInt(5)) // IsLoggedIn
        {
            pResult->SetVariant(CAccountResult::LOGGED_IN_ALREADY);
            pResult->m_Account.m_Id = pSqlServer->GetInt(1);
            return false;
        }
        pResult->SetVariant(CAccountResult::LOGIN_INFO);
        pResult->m_Account.m_Id = pSqlServer->GetInt(1); // id
        pSqlServer->GetString(2, pResult->m_Account.m_aName, sizeof(pResult->m_Account.m_aName)); // name
        pSqlServer->GetString(3, pResult->m_Account.m_aPassword, sizeof(pResult->m_Account.m_aPassword)); // password
        pSqlServer->GetString(4, pResult->m_Account.m_aAddress, sizeof(pResult->m_Account.m_aAddress)); // address
        pResult->m_Account.m_IsLoggedIn = pSqlServer->GetInt(5); // is_logged_in
        pResult->m_Account.m_Vip = pSqlServer->GetInt(6); // vip
        pResult->m_Account.m_Pages = pSqlServer->GetInt(7); // pages
        pResult->m_Account.m_Level = pSqlServer->GetInt(8); // level
        pResult->m_Account.m_Experience = pSqlServer->GetInt(9); // experience
        pResult->m_Account.m_Weaponkits = pSqlServer->GetInt(10); // weaponkits
        pResult->m_Account.m_Ranking = pSqlServer->GetInt(11); // ranking

        char aClan[32]; 
        if(pSqlServer->IsNull(12))
        {
            pResult->m_Account.m_aClan[0] = '\0';
        }
        else
        {
            pSqlServer->GetString(12, aClan, sizeof(aClan));
            strncpy(pResult->m_Account.m_aClan, aClan, sizeof(pResult->m_Account.m_aClan));
            pResult->m_Account.m_aClan[sizeof(pResult->m_Account.m_aClan) - 1] = '\0';
        }

        pResult->m_Account.m_Blockpoints = pSqlServer->GetInt(13); // blockpoints
        pSqlServer->GetString(14, pResult->m_Account.m_aKnockouts, sizeof(pResult->m_Account.m_aKnockouts)); // knockouts
        pSqlServer->GetString(15, pResult->m_Account.m_aGundesign, sizeof(pResult->m_Account.m_aGundesign)); // gundesign
        pSqlServer->GetString(16, pResult->m_Account.m_aSkinmani, sizeof(pResult->m_Account.m_aSkinmani)); // skinmani
        pSqlServer->GetString(17, pResult->m_Account.m_aExtras, sizeof(pResult->m_Account.m_aExtras)); // extras
        pSqlServer->GetString(18, pResult->m_Account.m_RegisterDate, sizeof(pResult->m_Account.m_RegisterDate)); // registerdate
        pResult->m_Account.m_RankedGames = pSqlServer->GetInt(19); // ranked_games
        pResult->m_Account.m_RankedKills = pSqlServer->GetInt(20); // ranked_kills
        pResult->m_Account.m_RankedDeaths = pSqlServer->GetInt(21); // ranked_deaths
        pResult->m_Account.m_RankedWins = pSqlServer->GetInt(22); // ranked_wins
        pResult->m_Account.m_Kills = pSqlServer->GetInt(23); // kills
        pResult->m_Account.m_Deaths = pSqlServer->GetInt(24); // deaths
        pResult->m_Account.m_TourneyWin = pSqlServer->GetInt(25); // tourney_win
        pResult->m_Account.m_Playtime = pSqlServer->GetInt64(26); // playtime
        pResult->m_Account.m_Killstreak = pSqlServer->GetInt(27); // killstreak
        pSqlServer->GetString(28, pResult->m_Account.m_aLastName, sizeof(pResult->m_Account.m_aLastName)); // last_name
        pSqlServer->GetString(29, pResult->m_Account.m_aLastSkin, sizeof(pResult->m_Account.m_aLastSkin)); // last_skin
        pResult->m_Account.m_LastBodyColor = pSqlServer->GetInt(30); // last_body_color
        pResult->m_Account.m_LastFeetColor = pSqlServer->GetInt(31); // last_feet_color


        dbg_msg("clandata","%s", pResult->m_Account.m_aClan);
    }
    else
    {
        pResult->SetVariant(CAccountResult::LOGIN_WRONG_PASS);
    }
    return false;
}


void CAccounts::ExecuteSql(const char *pQuery)
{
	auto Tmp = std::make_unique<CSqlStringData>();
	str_copy(Tmp->m_aString, pQuery, sizeof(Tmp->m_aString));

	m_pPool->ExecuteWrite(ExecuteSqlThread, std::move(Tmp), "execute sql query");
}

bool CAccounts::ExecuteSqlThread(IDbConnection *pSqlServer, const ISqlData *pGameData, Write w, char *pError, int ErrorSize)
{
	if(w != Write::NORMAL && w != Write::NORMAL_FAILED)
	{
		dbg_assert(false, "ExecuteSqlThread failed to write");
		return true;
	}

	const CSqlStringData *pData = dynamic_cast<const CSqlStringData *>(pGameData);

	if(pSqlServer->PrepareStatement(pData->m_aString, pError, ErrorSize))
	{
		return true;
	}

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		dbg_assert(false, "ExecuteSqlThread did not step");
		return true;
	}
	return !End;
}

void CAccounts::Register(int ClientId, const char *pUsername, const char *pPassword)
{
	ExecUserThread(RegisterThread, "register user", ClientId, pUsername, pPassword, "", 0, NULL);
}

bool CAccounts::RegisterThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
    const CSqlAccountRequest *pData = static_cast<const CSqlAccountRequest *>(pGameData);
    CAccountResult *pResult = static_cast<CAccountResult *>(pGameData->m_pResult.get());
    pResult->SetVariant(CAccountResult::REGISTER);

    char aBuf[2048];
    str_copy(aBuf, "SELECT name FROM Accounts WHERE name = ?;", sizeof(aBuf));

    if (pSqlServer->PrepareStatement(aBuf, pError, ErrorSize)) {
        snprintf(pError, ErrorSize, "Failed to prepare SELECT statement: %s", pError);
        return true;
    }

    pSqlServer->BindString(1, pData->m_aUsername);

    printf("Executing SELECT statement to check if username exists: %s\n", pData->m_aUsername);

    bool End;
    if (pSqlServer->Step(&End, pError, ErrorSize)) {
        snprintf(pError, ErrorSize, "Failed to execute SELECT statement: %s", pError);
        return true;
    }

    bool UsernameExists = !End;

    if (UsernameExists) {
        str_copy(pResult->m_aaMessages[0], "This username already exists.", sizeof(pResult->m_aaMessages[0]));
        return false;
    } else {
        SHA256_DIGEST HashedPassword = CGameContext::HashPassword(pData->m_aPassword);
        char aHashedPassword[SHA256_DIGEST_LENGTH * 2 + 1];
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
            sprintf(&aHashedPassword[i * 2], "%02x", HashedPassword.data[i]);

        snprintf(aBuf, sizeof(aBuf), "INSERT INTO Accounts (name, password) VALUES (?, ?);");

        if (pSqlServer->PrepareStatement(aBuf, pError, ErrorSize)) {
            snprintf(pError, ErrorSize, "Failed to prepare INSERT statement: %s", pError);
            return true;
        }

        pSqlServer->BindString(1, pData->m_aUsername);
        pSqlServer->BindString(2, aHashedPassword);

        int NumInserted;
        if (pSqlServer->ExecuteUpdate(&NumInserted, pError, ErrorSize)) {
            snprintf(pError, ErrorSize, "Failed to execute INSERT statement: %s", pError);
            return true;
        }

        if (NumInserted == 1) {
            str_copy(pResult->m_aaMessages[0], "Account registered successfully!", sizeof(pResult->m_aaMessages[0]));
            str_copy(pResult->m_aaMessages[1], "Please log in with /login <name> <pass>", sizeof(pResult->m_aaMessages[1]));
        } else {
            snprintf(pError, ErrorSize, "Account registration failed. No rows inserted.");
            return true;
        }
    }

    return false;
}

void CAccounts::SetLoggedIn(int ClientId, int LoggedIn, int AccountId)
{
    auto Tmp = std::make_unique<CSqlSetLoginData>();
    Tmp->m_LoggedIn = LoggedIn;
    Tmp->m_AccountId = AccountId;

    dbg_msg("account", "ClientId=%d, LoggedIn=%d, AccountId=%d - Executing SetLoggedInThread", ClientId, LoggedIn, AccountId);

    m_pPool->ExecuteWrite(SetLoggedInThread, std::move(Tmp), "set logged in");
}

bool CAccounts::SetLoggedInThread(IDbConnection *pSqlServer, const ISqlData *pGameData, Write w, char *pError, int ErrorSize)
{
    const CSqlSetLoginData *pData = dynamic_cast<const CSqlSetLoginData *>(pGameData);
    if (!pData)
    {
        str_copy(pError, "Invalid data type.", ErrorSize);
        dbg_msg("sql", "SetLoggedInThread: Invalid data type.");
        return true;
    }

    char aBuf[512];

    if (w == Write::NORMAL)
    {
        snprintf(aBuf, sizeof(aBuf), "UPDATE Accounts SET is_logged_in = ? WHERE id = ?;");

        dbg_msg("sql", "SetLoggedInThread: Preparing statement: %s", aBuf);

        if (pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
        {
            str_copy(pError, "Failed to prepare UPDATE statement.", ErrorSize);
            dbg_msg("sql", "SetLoggedInThread: Failed to prepare UPDATE statement: %s", pError);
            return true;
        }

        pSqlServer->BindInt(1, pData->m_LoggedIn);
        pSqlServer->BindInt(2, pData->m_AccountId);

        dbg_msg("sql", "SetLoggedInThread: Bound parameters - LoggedIn=%d, AccountId=%d", pData->m_LoggedIn, pData->m_AccountId);

        int NumUpdated;
        if (pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
        {
            return true;
        }
        return false;
    }
    else if (w == Write::NORMAL_FAILED)
    {
        dbg_msg("sql", "SetLoggedInThread: Write::NORMAL_FAILED state reached.");
        return false;
    }
    return false;
}

void CAccounts::ChangePassword(int ClientId, const char *pUsername, const char *pOldPassword, const char *pNewPassword)
{
    ExecUserThread(ChangePasswordThread, "change password", ClientId, pUsername, pOldPassword, pNewPassword, 0, NULL);
}

bool CAccounts::ChangePasswordThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
    const CSqlAccountRequest *pData = dynamic_cast<const CSqlAccountRequest *>(pGameData);
    CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());
    pResult->SetVariant(CAccountResult::DIRECT);

    char aBuf[2048];
    str_copy(aBuf, "SELECT password FROM Accounts WHERE name = ?;", sizeof(aBuf));

    if (pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
    {
        return true;
    }

    pSqlServer->BindString(1, pData->m_aUsername);

    bool End;
    if (pSqlServer->Step(&End, pError, ErrorSize))
    {
        return true;
    }

    if (End)
    {
        str_copy(pResult->m_aaMessages[0], "Username not found.", sizeof(pResult->m_aaMessages[0]));
        return false;
    }

    char aStoredPasswordHash[SHA256_DIGEST_LENGTH * 2 + 1];
    pSqlServer->GetString(1, aStoredPasswordHash, sizeof(aStoredPasswordHash));

    SHA256_DIGEST HashedOldPassword = CGameContext::HashPassword(pData->m_aPassword);
    char aHashedOldPassword[SHA256_DIGEST_LENGTH * 2 + 1];
    for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        sprintf(&aHashedOldPassword[i * 2], "%02x", HashedOldPassword.data[i]);

    if (str_comp(aHashedOldPassword, aStoredPasswordHash) != 0)
    {
        str_copy(pResult->m_aaMessages[0], "Old password is incorrect.", sizeof(pResult->m_aaMessages[0]));
        return false;
    }

    SHA256_DIGEST HashedNewPassword = CGameContext::HashPassword(pData->m_aNewPassword);
    char aHashedNewPassword[SHA256_DIGEST_LENGTH * 2 + 1];
    for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        sprintf(&aHashedNewPassword[i * 2], "%02x", HashedNewPassword.data[i]);

    str_copy(aBuf, "UPDATE Accounts SET password = ? WHERE name = ?;", sizeof(aBuf));

    if (pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
    {
        return true;
    }

    pSqlServer->BindString(1, aHashedNewPassword);
    pSqlServer->BindString(2, pData->m_aUsername);

    int NumUpdated;
    if (pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
    {
        return true;
    }

    if (NumUpdated == 1)
    {
        str_copy(pResult->m_aaMessages[0], "Successfully changed your password.", sizeof(pResult->m_aaMessages[0]));
    }
    else
    {
        str_copy(pResult->m_aaMessages[0], "Password change failed, please try again or contact an administrator", sizeof(pResult->m_aaMessages[0]));
    }

    return false;
}

void CAccounts::ShowTopLevel(int ClientId)
{
    ExecUserThread(ShowTopLevelThread, "show top level thread", ClientId, "", "", "", 0, NULL);
}

bool CAccounts::ShowTopLevelThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
    CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());

    char aBuf[512];
    str_copy(aBuf, "SELECT last_name, level FROM Accounts ORDER BY level DESC LIMIT 10;", sizeof(aBuf));

    if (pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
    {
        str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
        dbg_msg("top_level", "SQL preparation failed: %s", pError);
        pResult->m_Success = false;
        return true;
    }

    int Line = 0;
    str_copy(pResult->m_aaMessages[Line], "------------ Global Top Level ------------", sizeof(pResult->m_aaMessages[Line]));
    Line++;

    bool End = false;
    while (!pSqlServer->Step(&End, pError, ErrorSize) && !End)
    {
        *pError = '\0';

        char aLastName[MAX_NAME_LENGTH];
        int Level;

        pSqlServer->GetString(1, aLastName, sizeof(aLastName));

        if (*pError != '\0')
        {
            dbg_msg("top_level", "Failed to retrieve Last Name: %s", pError);
            str_copy(pResult->m_aaMessages[0], "Failed to retrieve account last name.", sizeof(pResult->m_aaMessages[0]));
            pResult->m_Success = false;
            return true;
        }

        Level = pSqlServer->GetInt(2);

        char aBuf[128];
        str_format(aBuf, sizeof(aBuf), "%d. %s : %d", Line, aLastName, Level);
        str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));
        dbg_msg("top_level", "Retrieved: %s", aBuf);
        Line++;

        if (Line >= CAccountResult::MAX_MESSAGES)
            break;
    }

    if (Line == 1)
    {
        str_copy(pResult->m_aaMessages[Line], "No players found.", sizeof(pResult->m_aaMessages[Line]));
        pResult->m_Success = false;
    }
    else
    {
        pResult->m_Success = true;
    }

    if (*pError != '\0')
    {
        dbg_msg("top_level", "SQL stepping failed: %s", pError);
        str_copy(pResult->m_aaMessages[Line], "Error retrieving top level players.", sizeof(pResult->m_aaMessages[Line]));
        pResult->m_Success = false;
        return true;
    }

    return false; 
}

void CAccounts::ShowTopBlockpoints(int ClientId)
{
    ExecUserThread(ShowTopBlockpointsThread, "show top blockpoints thread", ClientId, "", "", "", 0, NULL);
}

bool CAccounts::ShowTopBlockpointsThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
    CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());
    pResult->SetVariant(CAccountResult::TOP_MESSAGES);

    char aBuf[512];
    str_copy(aBuf, "SELECT last_name, level FROM Accounts ORDER BY level DESC LIMIT 10;", sizeof(aBuf));

    if (pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
    {
        str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
        dbg_msg("top_level", "SQL preparation failed: %s", pError);
        pResult->m_Success = false;
        return true;
    }

    int Line = 0;
    str_copy(pResult->m_aaMessages[Line], "---------- Global Top Blockpoints ----------", sizeof(pResult->m_aaMessages[Line]));
    Line++;

    bool End = false;
    while (!pSqlServer->Step(&End, pError, ErrorSize) && !End)
    {
        *pError = '\0';

        char aLastName[MAX_NAME_LENGTH];
        int Level;

        pSqlServer->GetString(1, aLastName, sizeof(aLastName)); 

        if (*pError != '\0')
        {
            dbg_msg("top_level", "Failed to retrieve Last Name: %s", pError);
            str_copy(pResult->m_aaMessages[0], "Failed to retrieve account last name.", sizeof(pResult->m_aaMessages[0]));
            pResult->m_Success = false;
            return true;
        }

        Level = pSqlServer->GetInt(2);

        char aBuf[128];
        str_format(aBuf, sizeof(aBuf), "%d. %s - %d", Line, aLastName, Level);
        str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));
        dbg_msg("top_level", "Retrieved: %s", aBuf);
        Line++;

        if (Line >= CAccountResult::MAX_MESSAGES)
            break;
    }

    if (Line == 1)
    {
        str_copy(pResult->m_aaMessages[Line], "No players found.", sizeof(pResult->m_aaMessages[Line]));
        pResult->m_Success = false;
    }
    else
    {
        pResult->m_Success = true;
    }

    if (*pError != '\0')
    {
        dbg_msg("top_level", "SQL stepping failed: %s", pError); 
        str_copy(pResult->m_aaMessages[Line], "Error retrieving top level players.", sizeof(pResult->m_aaMessages[Line]));
        pResult->m_Success = false;
        return true;
    }

    return false;
}


void CAccounts::ShowTopKillStreak(int ClientId)
{
    ExecUserThread(ShowTopKillStreaksThread, "show top killstreak thread", ClientId, "", "", "", 0, NULL);
}

bool CAccounts::ShowTopKillStreaksThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
    CAccountResult *pResult = dynamic_cast<CAccountResult *>(pGameData->m_pResult.get());
    pResult->SetVariant(CAccountResult::TOP_MESSAGES);

    char aBuf[512];
    str_copy(aBuf, "SELECT last_name, level FROM Accounts ORDER BY level DESC LIMIT 10;", sizeof(aBuf));

    if (pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
    {
        str_copy(pResult->m_aaMessages[0], "Failed to prepare SQL statement.", sizeof(pResult->m_aaMessages[0]));
        dbg_msg("top_blockpoints", "SQL preparation failed: %s", pError);
        pResult->m_Success = false;
        return true;
    }

    int Line = 0;
    str_copy(pResult->m_aaMessages[Line], "--------- Global Top Killstreak ---------", sizeof(pResult->m_aaMessages[Line]));
    Line++;

    bool End = false;
    while (!pSqlServer->Step(&End, pError, ErrorSize) && !End)
    {
        *pError = '\0';

        char aLastName[MAX_NAME_LENGTH];
        int Level;

        pSqlServer->GetString(1, aLastName, sizeof(aLastName));

        if (*pError != '\0')
        {
            dbg_msg("top_level", "Failed to retrieve Last Name: %s", pError);
            str_copy(pResult->m_aaMessages[0], "Failed to retrieve account last name.", sizeof(pResult->m_aaMessages[0]));
            pResult->m_Success = false;
            return true;
        }

        Level = pSqlServer->GetInt(2);

        char aBuf[128];
        str_format(aBuf, sizeof(aBuf), "%d. %s : %d", Line, aLastName, Level);
        str_copy(pResult->m_aaMessages[Line], aBuf, sizeof(pResult->m_aaMessages[Line]));
        dbg_msg("top_killstreak", "Retrieved: %s", aBuf);
        Line++;

        if (Line >= CAccountResult::MAX_MESSAGES)
            break;
    }

    // just in case no results were found
    if (Line == 1)
    {
        str_copy(pResult->m_aaMessages[Line], "No players found.", sizeof(pResult->m_aaMessages[Line]));
        pResult->m_Success = false;
    }
    else
    {
        pResult->m_Success = true;
    }

    if (*pError != '\0')
    {
        dbg_msg("top_level", "SQL stepping failed: %s", pError);
        str_copy(pResult->m_aaMessages[Line], "Error retrieving top level players.", sizeof(pResult->m_aaMessages[Line]));
        pResult->m_Success = false;
        return true;
    }

    return false;
}