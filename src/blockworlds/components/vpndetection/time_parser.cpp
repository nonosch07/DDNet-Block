#include "time_parser.h"

#include <blockworlds/bw_base.h>

#include <cctype>

namespace
{
	struct STimeUnitPattern
	{
		const char *m_PPattern;
		ETimeUnit m_Unit;
		int m_MinLength;
	};

	const STimeUnitPattern s_aTimeUnits[] = {
		{"day", TIME_UNIT_DAY, 3},
		{"hour", TIME_UNIT_HOUR, 4},
		{"hr", TIME_UNIT_HOUR, 2},
		{"minute", TIME_UNIT_MINUTE, 6},
		{"min", TIME_UNIT_MINUTE, 3},
		{"second", TIME_UNIT_SECOND, 6},
		{"sec", TIME_UNIT_SECOND, 3},
	};

	const char *SkipWhitespace(const char *p)
	{
		while(*p && (*p == ' ' || *p == '\t'))
			p++;
		return p;
	}

	bool MatchPrefixIgnoreCase(const char *pStr, const char *pPattern, int MinLen)
	{
		for(int i = 0; i < MinLen; i++)
		{
			if(tolower(pStr[i]) != tolower(pPattern[i]))
				return false;
		}
		return true;
	}

	bool ParseNumber(const char **ppStr, int *pOutNumber)
	{
		const char *p = *ppStr;
		if(!isdigit(*p))
			return false;

		int Number = 0;
		while(isdigit(*p))
		{
			Number = Number * 10 + (*p - '0');
			p++;
		}

		*pOutNumber = Number;
		*ppStr = p;
		return true;
	}

	ETimeUnit ParseTimeUnit(const char **ppStr)
	{
		const char *p = SkipWhitespace(*ppStr);
		char c = tolower(*p);

		if(c == 'd')
		{
			*ppStr = p + 1;
			return TIME_UNIT_DAY;
		}
		else if(c == 'h')
		{
			*ppStr = p + 1;
			return TIME_UNIT_HOUR;
		}
		else if(c == 'm')
		{
			*ppStr = p + 1;
			return TIME_UNIT_MINUTE;
		}
		else if(c == 's')
		{
			*ppStr = p + 1;
			return TIME_UNIT_SECOND;
		}

		for(const auto &Pattern : s_aTimeUnits)
		{
			if(MatchPrefixIgnoreCase(p, Pattern.m_PPattern, Pattern.m_MinLength))
			{
				p += Pattern.m_MinLength;
				if(*p == 's')
					p++;
				*ppStr = p;
				return Pattern.m_Unit;
			}
		}

		return TIME_UNIT_NONE;
	}
} // namespace

bool ParseTimeString(const char *pTimeStr, int *pOutSeconds)
{
	if(!pTimeStr || !pOutSeconds)
		return false;

	const char *p = SkipWhitespace(pTimeStr);
	if(*p == '\0')
		return false;

	int TotalSeconds = 0;
	bool FoundUnit = false;

	while(*p)
	{
		p = SkipWhitespace(p);
		if(*p == '\0')
			break;

		int Number = 0;
		if(!ParseNumber(&p, &Number))
		{
			if(!FoundUnit)
				return false;
			break;
		}

		ETimeUnit Unit = ParseTimeUnit(&p);
		if(Unit != TIME_UNIT_NONE)
		{
			TotalSeconds += Number * static_cast<int>(Unit);
			FoundUnit = true;
		}
		else if(!FoundUnit)
		{
			p = SkipWhitespace(p);
			if(*p != '\0')
				return false;
			TotalSeconds = Number * TIME_UNIT_MINUTE;
			FoundUnit = true;
			break;
		}
		else
		{
			return false;
		}
	}

	if(!FoundUnit)
		return false;

	*pOutSeconds = TotalSeconds;
	return true;
}

bool ParseTimeStringMinutes(const char *pTimeStr, int *pOutMinutes)
{
	int Seconds = 0;
	if(!ParseTimeString(pTimeStr, &Seconds))
		return false;

	*pOutMinutes = Seconds / 60;
	return true;
}
