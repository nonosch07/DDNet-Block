#ifndef BLOCK_SQL_MYSQL_CONFIG_H
#define BLOCK_SQL_MYSQL_CONFIG_H

struct CMysqlConfig;

/// Remembers the credentials of the MySQL write server as it is registered.
///
/// Block subsystems that keep a connection of their own -- whois, whose scans
/// are slow enough that running them through the shared pool would hold up
/// account saves -- have no other way to reach them: the pool owns its
/// connections and does not hand out the configuration it was built from.
void RememberMysqlWriteConfig(const CMysqlConfig &Config);

/// Fills pOut with the remembered credentials. False when the server was
/// started without a MySQL write database, which is the signal to fall back to
/// a local SQLite file.
bool MysqlWriteConfig(CMysqlConfig *pOut);

#endif // BLOCK_SQL_MYSQL_CONFIG_H
