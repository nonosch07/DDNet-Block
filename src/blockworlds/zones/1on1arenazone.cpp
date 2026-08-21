#include "1on1arenazone.h"

#include <blockworlds/bw_base.h>

#include <cctype>

static std::string BuildDisplayName(const char *pLayerName)
{
	// strip leading "1on1_" prefix (5 chars)
	const char *pSuffix = pLayerName;
	if(str_comp_nocase_num(pLayerName, "1on1_", 5) == 0)
		pSuffix = pLayerName + 5;

	// capitalize first character, replace underscores with spaces
	std::string Name;
	bool NextUpper = true;
	for(const char *p = pSuffix; *p; ++p)
	{
		if(*p == '_')
		{
			Name += ' ';
			NextUpper = false;
		}
		else if(NextUpper)
		{
			Name += (char)std::toupper((unsigned char)*p);
			NextUpper = false;
		}
		else
		{
			Name += *p;
		}
	}
	if(Name.empty())
		Name = "Arena";
	return Name;
}

C1on1ArenaZone::C1on1ArenaZone(CGameContext *pGameServer, const char *pLayerName, int ArenaIndex) :
	IZone(pGameServer, -1),
	m_DisplayName(BuildDisplayName(pLayerName)),
	m_ArenaIndex(ArenaIndex)
{
}
