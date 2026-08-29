#include "config.h"

#include <engine/shared/config.h>

#include <block/base.h>

#include <algorithm>

CBlockConfig g_BwConfig;

void CBlockConfig::ConVar(IConsole::IResult *pResult, void *pUserData)
{
	SVar *pVar = static_cast<SVar *>(pUserData);
	if(pResult->NumArguments())
	{
		float Value = pResult->GetFloat(0);
		Value = std::clamp(Value, pVar->m_Min, pVar->m_Max);
		*pVar->m_pValue = Value;
	}
	else if(pVar->m_pConsole)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pVar->m_pName, *pVar->m_pValue);
		pVar->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
	}
}

void CBlockConfig::Register(IConsole *pConsole)
{
	m_aVars[0] = {&m_SvBombTagStunDuration, 0.0f, 10.0f, "sv_bombtag_stun_duration", pConsole};
	m_aVars[1] = {&m_SvVpnGetipintelThreshold, 0.0f, 100.0f, "sv_vpn_service_getipintel_threshold", pConsole};

	pConsole->Register("sv_bombtag_stun_duration", "?f", CFGFLAG_SERVER, ConVar, &m_aVars[0],
		"Duration of the stun when a player gets hit with a hammer (seconds)");
	pConsole->Register("sv_vpn_service_getipintel_threshold", "?f", CFGFLAG_SERVER, ConVar, &m_aVars[1],
		"Probability threshold (0.00-100.00) for marking IP as bad");
}
