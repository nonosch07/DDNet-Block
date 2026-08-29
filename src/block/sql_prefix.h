#ifndef BLOCK_SQL_PREFIX_H
#define BLOCK_SQL_PREFIX_H

// I'm a lazy mozerfoker

#define BLOCK_PREFIX "Block_"
#define BLOCK_TBL(name) BLOCK_PREFIX name

#define TBL_ACCOUNTS_CORE BLOCK_TBL("accounts_core")
#define TBL_ACCOUNTS_PROGRESS BLOCK_TBL("accounts_progress")
#define TBL_ACCOUNTS_INVENTORY BLOCK_TBL("accounts_inventory")
#define TBL_ACCOUNTS_RANKED BLOCK_TBL("accounts_ranked")
#define TBL_ACCOUNTS_BUSY BLOCK_TBL("accounts_busy")
#define TBL_CLANS BLOCK_TBL("clans")
#define TBL_WHOIS_CONNECTIONS BLOCK_TBL("whois_connections")
// aggregated whois tables (SQLite-local)
#define TBL_WHOIS_AGG_NAMES_BY_IP BLOCK_TBL("whois_names_by_ip")
#define TBL_WHOIS_AGG_IPS_BY_NAME BLOCK_TBL("whois_ips_by_name")

#define BLOCK_ENGINE_COLLATE " ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin"

#endif // BLOCK_SQL_PREFIX_H
