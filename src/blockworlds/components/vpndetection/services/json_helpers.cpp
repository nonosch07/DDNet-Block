#include "json_helpers.h"

#include <algorithm>
#include <base/system.h>

namespace JsonHelpers {
bool ParseString(const char *pJson, const char *pKey, char *pOut, int OutSize)
{
	char aSearchKey[128];
	str_format(aSearchKey, sizeof(aSearchKey), "\"%s\":\"", pKey);

	const char *pStart = str_find(pJson, aSearchKey);
	if(!pStart)
		return false;

	pStart += str_length(aSearchKey);
	const char *pEnd = str_find(pStart, "\"");
	if(!pEnd)
		return false;

	int Len = std::min((int)(pEnd - pStart), OutSize - 1);
	str_copy(pOut, pStart, Len + 1);
	return true;
}

bool ParseBool(const char *pJson, const char *pKey, bool &Out)
{
	char aSearchKey[128];
	str_format(aSearchKey, sizeof(aSearchKey), "\"%s\":", pKey);

	const char *pStart = str_find(pJson, aSearchKey);
	if(!pStart)
		return false;

	pStart += str_length(aSearchKey);

	// Skip whitespace
	while(*pStart == ' ' || *pStart == '\t' || *pStart == '\n' || *pStart == '\r')
		pStart++;

	if(str_startswith(pStart, "true"))
	{
		Out = true;
		return true;
	}
	else if(str_startswith(pStart, "false"))
	{
		Out = false;
		return true;
	}

	return false;
}

bool ParseInt(const char *pJson, const char *pKey, int &Out)
{
	char aSearchKey[128];
	str_format(aSearchKey, sizeof(aSearchKey), "\"%s\":", pKey);

	const char *pStart = str_find(pJson, aSearchKey);
	if(!pStart)
		return false;

	pStart += str_length(aSearchKey);
	Out = str_toint(pStart);
	return true;
}

bool ParseFloat(const char *pJson, const char *pKey, float &Out)
{
	char aSearchKey[128];
	str_format(aSearchKey, sizeof(aSearchKey), "\"%s\":", pKey);

	const char *pStart = str_find(pJson, aSearchKey);
	if(!pStart)
		return false;

	pStart += str_length(aSearchKey);
	Out = str_tofloat(pStart);
	return true;
}
} // namespace JsonHelpers
