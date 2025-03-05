-- SQL Dump for Accounts and Clans Tables

CREATE TABLE IF NOT EXISTS Clans (
  id INT AUTO_INCREMENT PRIMARY KEY,
  name VARCHAR(32) BINARY NOT NULL,
  level INT DEFAULT 1,
  experience INT DEFAULT 0
) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin AUTO_INCREMENT=1;

CREATE TABLE IF NOT EXISTS Accounts (
  id INT AUTO_INCREMENT PRIMARY KEY,
  name VARCHAR(11) BINARY NOT NULL,
  password VARCHAR(256) BINARY NOT NULL,
  address VARCHAR(47) DEFAULT '0.0.0.0',
  is_logged_in INT DEFAULT 0,
  vip INT DEFAULT 0,
  pages INT DEFAULT 0,
  level INT DEFAULT 1,
  experience INT DEFAULT 0,
  weaponkits INT DEFAULT 0,
  ranking INT DEFAULT 0,
  clanID INT NOT NULL DEFAULT 0, -- 0 indicates no clan membership
  auth_level INT DEFAULT 0,      -- 0 = none, 1 = member, 2 = co-leader, 3 = leader
  blockpoints INT DEFAULT 0,
  knockouts VARCHAR(11) DEFAULT '00000000000',
  gundesign VARCHAR(10) DEFAULT '0000000000',
  skinmani VARCHAR(10) DEFAULT '0000000000',
  extras VARCHAR(256) DEFAULT '00000000000000',
  registerdate TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  ranked_games INT DEFAULT 0,
  ranked_kills INT DEFAULT 0,
  ranked_deaths INT DEFAULT 0,
  ranked_wins INT DEFAULT 0,
  kills INT DEFAULT 0,
  deaths INT DEFAULT 0,
  tourney_win INT DEFAULT 0,
  playtime BIGINT DEFAULT 0,
  killstreak INT DEFAULT 0,
  last_name VARCHAR(16) DEFAULT 'nameless tee',
  last_skin VARCHAR(32) DEFAULT 'default',
  last_body_color INT DEFAULT 0,
  last_feet_color INT DEFAULT 0
) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin;
