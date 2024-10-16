#include "connection.h"

IDbConnection::IDbConnection(const char *pPrefix)
{
	str_copy(m_aPrefix, pPrefix);
}

void IDbConnection::FormatCreateRace(char *aBuf, unsigned int BufferSize, bool Backup) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_race%s ("
		"  Map VARCHAR(128) COLLATE %s NOT NULL, "
		"  Name VARCHAR(%d) COLLATE %s NOT NULL, "
		"  Timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
		"  Time FLOAT DEFAULT 0, "
		"  Server CHAR(4), "
		"  cp1 FLOAT DEFAULT 0, cp2 FLOAT DEFAULT 0, cp3 FLOAT DEFAULT 0, "
		"  cp4 FLOAT DEFAULT 0, cp5 FLOAT DEFAULT 0, cp6 FLOAT DEFAULT 0, "
		"  cp7 FLOAT DEFAULT 0, cp8 FLOAT DEFAULT 0, cp9 FLOAT DEFAULT 0, "
		"  cp10 FLOAT DEFAULT 0, cp11 FLOAT DEFAULT 0, cp12 FLOAT DEFAULT 0, "
		"  cp13 FLOAT DEFAULT 0, cp14 FLOAT DEFAULT 0, cp15 FLOAT DEFAULT 0, "
		"  cp16 FLOAT DEFAULT 0, cp17 FLOAT DEFAULT 0, cp18 FLOAT DEFAULT 0, "
		"  cp19 FLOAT DEFAULT 0, cp20 FLOAT DEFAULT 0, cp21 FLOAT DEFAULT 0, "
		"  cp22 FLOAT DEFAULT 0, cp23 FLOAT DEFAULT 0, cp24 FLOAT DEFAULT 0, "
		"  cp25 FLOAT DEFAULT 0, "
		"  GameId VARCHAR(64), "
		"  DDNet7 BOOL DEFAULT FALSE, "
		"  PRIMARY KEY (Map, Name, Time, Timestamp, Server)"
		")",
		GetPrefix(), Backup ? "_backup" : "",
		BinaryCollate(), MAX_NAME_LENGTH_SQL, BinaryCollate());
}

void IDbConnection::FormatCreateTeamrace(char *aBuf, unsigned int BufferSize, const char *pIdType, bool Backup) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_teamrace%s ("
		"  Map VARCHAR(128) COLLATE %s NOT NULL, "
		"  Name VARCHAR(%d) COLLATE %s NOT NULL, "
		"  Timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
		"  Time FLOAT DEFAULT 0, "
		"  ID %s NOT NULL, " // VARBINARY(16) for MySQL and BLOB for SQLite
		"  GameId VARCHAR(64), "
		"  DDNet7 BOOL DEFAULT FALSE, "
		"  PRIMARY KEY (Id, Name)"
		")",
		GetPrefix(), Backup ? "_backup" : "",
		BinaryCollate(), MAX_NAME_LENGTH_SQL, BinaryCollate(), pIdType);
}

void IDbConnection::FormatCreateMaps(char *aBuf, unsigned int BufferSize) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_maps ("
		"  Map VARCHAR(128) COLLATE %s NOT NULL, "
		"  Server VARCHAR(32) COLLATE %s NOT NULL, "
		"  Mapper VARCHAR(128) COLLATE %s NOT NULL, "
		"  Points INT DEFAULT 0, "
		"  Stars INT DEFAULT 0, "
		"  Timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
		"  PRIMARY KEY (Map)"
		")",
		GetPrefix(), BinaryCollate(), BinaryCollate(), BinaryCollate());
}

void IDbConnection::FormatCreateSaves(char *aBuf, unsigned int BufferSize, bool Backup) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_saves%s ("
		"  Savegame TEXT COLLATE %s NOT NULL, "
		"  Map VARCHAR(128) COLLATE %s NOT NULL, "
		"  Code VARCHAR(128) COLLATE %s NOT NULL, "
		"  Timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
		"  Server CHAR(4), "
		"  DDNet7 BOOL DEFAULT FALSE, "
		"  SaveId VARCHAR(36) DEFAULT NULL, "
		"  PRIMARY KEY (Map, Code)"
		")",
		GetPrefix(), Backup ? "_backup" : "",
		BinaryCollate(), BinaryCollate(), BinaryCollate());
}

void IDbConnection::FormatCreatePoints(char *aBuf, unsigned int BufferSize) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_points ("
		"  Name VARCHAR(%d) COLLATE %s NOT NULL, "
		"  Points INT DEFAULT 0, "
		"  PRIMARY KEY (Name)"
		")",
		GetPrefix(), MAX_NAME_LENGTH_SQL, BinaryCollate());
}

void IDbConnection::FormatCreateAccounts(char *aBuf, unsigned int BufferSize) const
{
	snprintf(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS Accounts ("
		"  id INT AUTO_INCREMENT PRIMARY KEY, "
		"  name VARCHAR(11) BINARY NOT NULL, "
		"  password VARCHAR(256) BINARY NOT NULL, "
		"  address VARCHAR(47) DEFAULT '0.0.0.0', "
		"  is_logged_in INT DEFAULT 0, "
		"  vip INT DEFAULT 0, "
		"  pages INT DEFAULT 0, "
		"  level INT DEFAULT 1, "
		"  experience INT DEFAULT 0, "
		"  weaponkits INT DEFAULT 0, "
		"  ranking INT DEFAULT 0, "
		"  clan VARCHAR(32) BINARY DEFAULT NULL, "
		"  blockpoints INT DEFAULT 0, "
		"  knockouts VARCHAR(256) DEFAULT '0000000000000', "
		"  gundesign VARCHAR(256) DEFAULT '000000000000', "
		"  skinmani VARCHAR(256) DEFAULT '0000000000000', "
		"  extras VARCHAR(256) DEFAULT '00000000000000', "
		"  registerdate TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
		"  ranked_games INT DEFAULT 0, "
		"  ranked_kills INT DEFAULT 0, "
		"  ranked_deaths INT DEFAULT 0, "
		"  ranked_wins INT DEFAULT 0, "
		"  kills INT DEFAULT 0, "
		"  deaths INT DEFAULT 0, "
		"  tourney_win INT DEFAULT 0, "
		"  playtime BIGINT DEFAULT 0, "
		"  killstreak INT DEFAULT 0, "
		"  last_name VARCHAR(16) DEFAULT 'nameless tee', "
		"  last_skin VARCHAR(32) DEFAULT 'default', "
		"  last_body_color INT DEFAULT 0, "
		"  last_feet_color INT DEFAULT 0"
		") CHARACTER SET utf8mb4 COLLATE utf8mb4_bin;");

	dbg_msg("sql", "accounts table created");
}
