#include "1on1arenazone.h"

#include <base/system.h>
#include <cctype>


static std::string BuildDisplayName(const char *pLayerName)
{
	// strip leading "1on1_" prefix (5 chars)
	const char *pSuffix = pLayerName;
	if(str_comp_nocase_num(pLayerName, "1on1_", 5) == 0)
		pSuffix = pLayerName + 5;

	// capitalize first character, replace underscores with spaces
	std::string name;
	bool nextUpper = true;
	for(const char *p = pSuffix; *p; ++p)
	{
		if(*p == '_')
		{
			name += ' ';
			nextUpper = false;
		}
		else if(nextUpper)
		{
			name += (char)std::toupper((unsigned char)*p);
			nextUpper = false;
		}
		else
		{
			name += *p;
		}
	}
	if(name.empty())
		name = "Arena";
	return name;
}

C1on1ArenaZone::C1on1ArenaZone(CGameContext *pGameServer, const char *pLayerName, int arenaIndex) :
	IZone(pGameServer, -1),
	m_DisplayName(BuildDisplayName(pLayerName)),
	m_ArenaIndex(arenaIndex)
{
}
