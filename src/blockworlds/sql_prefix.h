#ifndef BLOCKWORLDS_SQL_PREFIX_H
#define BLOCKWORLDS_SQL_PREFIX_H

// I'm a lazy mozerfoker

#define BW_PREFIX "Blockworlds_"
#define BW_TBL(name) BW_PREFIX name

#define TBL_ACCOUNTS_CORE BW_TBL("accounts_core")
#define TBL_ACCOUNTS_PROGRESS BW_TBL("accounts_progress")
#define TBL_ACCOUNTS_INVENTORY BW_TBL("accounts_inventory")
#define TBL_ACCOUNTS_RANKED BW_TBL("accounts_ranked")
#define TBL_ACCOUNTS_BUSY BW_TBL("accounts_busy")
#define TBL_ACCOUNTS_AUTOLOGIN BW_TBL("accounts_autologin")
#define TBL_CLANS BW_TBL("clans")
#define TBL_WHOIS_CONNECTIONS BW_TBL("whois_connections")
// aggregated whois tables (SQLite-local)
#define TBL_WHOIS_AGG_NAMES_BY_IP BW_TBL("whois_names_by_ip")
#define TBL_WHOIS_AGG_IPS_BY_NAME BW_TBL("whois_ips_by_name")

#define BW_ENGINE_COLLATE " ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin"

#endif // BLOCKWORLDS_SQL_PREFIX_H
