#include "json_helpers.h"

#include <algorithm>
#include <base/system.h>
#include <engine/external/json-parser/json.h>

namespace JsonHelpers {
const json_value *GetObjectItem(const json_value *pObject, const char *pKey)
{
	if(!pObject || !pKey || pObject->type != json_object)
		return nullptr;

	for(unsigned int i = 0; i < pObject->u.object.length; i++)
	{
		if(str_comp(pObject->u.object.values[i].name, pKey) == 0)
			return pObject->u.object.values[i].value;
	}

	return nullptr;
}

const json_value *GetPath(const json_value *pRoot, const char *pPath)
{
	if(!pRoot || !pPath || !pPath[0])
		return nullptr;

	const json_value *pCurrent = pRoot;
	const char *pSegmentStart = pPath;
	while(*pSegmentStart)
	{
		const char *pSegmentEnd = pSegmentStart;
		while(*pSegmentEnd && *pSegmentEnd != '.')
			pSegmentEnd++;

		char aSegment[128];
		const int Len = std::min((int)(pSegmentEnd - pSegmentStart), (int)sizeof(aSegment) - 1);
		str_copy(aSegment, pSegmentStart, Len + 1);

		pCurrent = GetObjectItem(pCurrent, aSegment);
		if(!pCurrent)
			return nullptr;

		pSegmentStart = *pSegmentEnd == '.' ? pSegmentEnd + 1 : pSegmentEnd;
	}

	return pCurrent;
}

bool ParseString(const json_value *pRoot, const char *pKey, char *pOut, int OutSize)
{
	if(!pRoot || !pOut || OutSize <= 0)
		return false;
	pOut[0] = '\0';

	const json_value *pValue = GetPath(pRoot, pKey);
	if(!pValue || pValue->type == json_none)
		return false;

	if(pValue->type == json_string)
	{
		str_copy(pOut, pValue->u.string.ptr, OutSize);
		return true;
	}
	if(pValue->type == json_integer)
	{
		str_format(pOut, OutSize, "%lld", (long long)pValue->u.integer);
		return true;
	}
	if(pValue->type == json_double)
	{
		str_format(pOut, OutSize, "%.2f", pValue->u.dbl);
		return true;
	}
	if(pValue->type == json_boolean)
	{
		str_copy(pOut, pValue->u.boolean ? "true" : "false", OutSize);
		return true;
	}
	return false;
}

bool ParseBool(const json_value *pRoot, const char *pKey, bool &Out)
{
	if(!pRoot)
		return false;

	const json_value *pValue = GetPath(pRoot, pKey);
	if(!pValue || pValue->type == json_none)
		return false;

	if(pValue->type == json_boolean)
	{
		Out = pValue->u.boolean != 0;
		return true;
	}
	if(pValue->type == json_string)
	{
		Out = str_comp_nocase(pValue->u.string.ptr, "true") == 0 ||
		      str_comp_nocase(pValue->u.string.ptr, "yes") == 0 ||
		      str_comp(pValue->u.string.ptr, "1") == 0;
		return true;
	}
	if(pValue->type == json_integer)
	{
		Out = pValue->u.integer != 0;
		return true;
	}
	return false;
}

bool ParseInt(const json_value *pRoot, const char *pKey, int &Out)
{
	if(!pRoot)
		return false;

	const json_value *pValue = GetPath(pRoot, pKey);
	if(!pValue || pValue->type == json_none)
		return false;

	if(pValue->type == json_integer)
	{
		Out = (int)pValue->u.integer;
		return true;
	}
	if(pValue->type == json_double)
	{
		Out = (int)pValue->u.dbl;
		return true;
	}
	if(pValue->type == json_string)
	{
		Out = str_toint(pValue->u.string.ptr);
		return true;
	}
	return false;
}

bool ParseFloat(const json_value *pRoot, const char *pKey, float &Out)
{
	if(!pRoot)
		return false;

	const json_value *pValue = GetPath(pRoot, pKey);
	if(!pValue || pValue->type == json_none)
		return false;

	if(pValue->type == json_double)
	{
		Out = (float)pValue->u.dbl;
		return true;
	}
	if(pValue->type == json_integer)
	{
		Out = (float)pValue->u.integer;
		return true;
	}
	if(pValue->type == json_string)
	{
		Out = str_tofloat(pValue->u.string.ptr);
		return true;
	}
	return false;
}

bool ParseString(const char *pJson, const char *pKey, char *pOut, int OutSize)
{
	if(!pJson || !pOut || OutSize <= 0)
		return false;
	pOut[0] = '\0';

	json_value *pRoot = json_parse(pJson, str_length(pJson));
	if(!pRoot)
		return false;

	const bool Result = ParseString(pRoot, pKey, pOut, OutSize);
	json_value_free(pRoot);
	return Result;
}

bool ParseBool(const char *pJson, const char *pKey, bool &Out)
{
	if(!pJson)
		return false;

	json_value *pRoot = json_parse(pJson, str_length(pJson));
	if(!pRoot)
		return false;

	const bool Result = ParseBool(pRoot, pKey, Out);
	json_value_free(pRoot);
	return Result;
}

bool ParseInt(const char *pJson, const char *pKey, int &Out)
{
	if(!pJson)
		return false;

	json_value *pRoot = json_parse(pJson, str_length(pJson));
	if(!pRoot)
		return false;

	const bool Result = ParseInt(pRoot, pKey, Out);
	json_value_free(pRoot);
	return Result;
}

bool ParseFloat(const char *pJson, const char *pKey, float &Out)
{
	if(!pJson)
		return false;

	json_value *pRoot = json_parse(pJson, str_length(pJson));
	if(!pRoot)
		return false;

	const bool Result = ParseFloat(pRoot, pKey, Out);
	json_value_free(pRoot);
	return Result;
}

} // namespace JsonHelpers
