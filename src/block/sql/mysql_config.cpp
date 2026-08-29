#include "mysql_config.h"

#include <engine/server/databases/connection_pool.h>

#include <mutex>

namespace
{
	std::mutex g_Mutex;
	CMysqlConfig g_Config;
	bool g_Have = false;
} // namespace

void RememberMysqlWriteConfig(const CMysqlConfig &Config)
{
	const std::lock_guard<std::mutex> Lock(g_Mutex);
	g_Config = Config;
	g_Have = true;
}

bool MysqlWriteConfig(CMysqlConfig *pOut)
{
	const std::lock_guard<std::mutex> Lock(g_Mutex);
	if(!g_Have)
		return false;
	*pOut = g_Config;
	return true;
}
