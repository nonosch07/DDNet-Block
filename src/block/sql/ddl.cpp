#include "ddl.h"

#include <engine/server/databases/connection.h>

#include <block/base.h>

bool IsSqliteConnection(IDbConnection *pSql)
{
	// no type tag on IDbConnection, but the two backends spell this differently
	const char *pInsertIgnore = pSql->InsertIgnore();
	return pInsertIgnore && str_comp(pInsertIgnore, "INSERT OR IGNORE") == 0;
}

bool RunDdl(IDbConnection *pSql, const char *pStmt, char *pError, int ErrorSize)
{
	if(!pSql->PrepareStatement(pStmt, pError, ErrorSize))
		return false;
	int Rows = 0;
	return pSql->ExecuteUpdate(&Rows, pError, ErrorSize);
}

bool EnsureColumn(IDbConnection *pSql, const char *pTable, const char *pColumn, const char *pDefinition, char *pError, int ErrorSize)
{
	// the answer is carried by whether a row comes back at all, so nothing has to
	// be read out of it -- one less place to get IDbConnection's 1-based column
	// indexing wrong
	if(!pSql->PrepareStatement(
		   "SELECT COLUMN_NAME FROM information_schema.COLUMNS"
		   " WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ? AND COLUMN_NAME = ?"
		   " LIMIT 1",
		   pError, ErrorSize))
		return false;
	pSql->BindString(1, pTable);
	pSql->BindString(2, pColumn);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;
	if(!End)
		return true; // the column is already there

	char aStmt[512];
	str_format(aStmt, sizeof(aStmt), "ALTER TABLE `%s` ADD COLUMN `%s` %s", pTable, pColumn, pDefinition);
	if(!RunDdl(pSql, aStmt, pError, ErrorSize))
		return false;
	dbg_msg("sql", "added missing column %s.%s", pTable, pColumn);
	return true;
}
