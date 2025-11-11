#ifndef BLOCKWORLDS_COMPONENTS_VPN_TIME_PARSER_H
#define BLOCKWORLDS_COMPONENTS_VPN_TIME_PARSER_H

/**
 * Parses human-readable time strings into seconds or minutes.
 * 
 * Supported formats:
 *   - Plain numbers: "60" (interpreted as minutes)
 *   - Single units: "1d", "5h", "30m", "45s"
 *   - Multiple units: "1d5h10m", "1 day 5 hours 10 minutes"
 *   - Word forms: "day/days", "hour/hours/hr/hrs", "minute/minutes/min", "second/seconds/sec"
 */

enum ETimeUnit
{
	TIME_UNIT_NONE = 0,
	TIME_UNIT_SECOND = 1,
	TIME_UNIT_MINUTE = 60,
	TIME_UNIT_HOUR = 3600,
	TIME_UNIT_DAY = 86400,
};

bool ParseTimeString(const char *pTimeStr, int *pOutSeconds);
bool ParseTimeStringMinutes(const char *pTimeStr, int *pOutMinutes);

#endif // BLOCKWORLDS_COMPONENTS_VPN_TIME_PARSER_H
