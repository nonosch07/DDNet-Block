#ifndef BLOCK_SQL_DDL_H
#define BLOCK_SQL_DDL_H

class IDbConnection;

/// Whether this connection is the local SQLite file rather than a MySQL server.
///
/// Block's account and clan tables are MySQL-only -- the statements below use
/// AUTO_INCREMENT, ENGINE and information_schema. The pool hands out a SQLite
/// connection whenever no MySQL write server is configured, so anything that
/// emits MySQL DDL has to check first: running it there fails, and a failed
/// write puts the whole pool into fail mode.
bool IsSqliteConnection(IDbConnection *pSql);

/// Runs one statement that yields no rows, such as CREATE TABLE.
///
/// Returns true on success. Note that the pool's job callbacks use the opposite
/// convention, so a caller inside one has to translate.
bool RunDdl(IDbConnection *pSql, const char *pStmt, char *pError, int ErrorSize);

/// Adds a column to a table that does not have it yet, and does nothing when it
/// is already there.
///
/// MySQL has no portable `ADD COLUMN IF NOT EXISTS`. Running the ALTER blindly
/// and swallowing its error would work, but it would swallow the errors worth
/// seeing too, so the column is looked up in information_schema first.
///
/// MySQL only: SQLite has no information_schema.
bool EnsureColumn(IDbConnection *pSql, const char *pTable, const char *pColumn, const char *pDefinition, char *pError, int ErrorSize);

#endif // BLOCK_SQL_DDL_H
