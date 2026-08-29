#ifndef BLOCK_CONFIG_H
#define BLOCK_CONFIG_H

#include <engine/console.h>

// Float configuration variables owned by Block.
//
// Upstream's config system only knows int, colour and string variables. Block has
// exactly two float cvars, which is far too little to justify threading a new
// variable type through config.h, config.cpp, client.cpp and the teehistorian
// (every consumer of config_variables.h has to define every macro). They are
// registered straight on the console instead, so they parse, clamp and print
// exactly like before under the same names, and upstream is left alone.
//
// The one behavioural difference to a real engine cvar: these are not part of
// the config save/dump or the teehistorian header.
class CBlockConfig
{
public:
	float m_SvBombTagStunDuration = 0.5f;
	float m_SvVpnGetipintelThreshold = 99.0f;

	void Register(IConsole *pConsole);

private:
	struct SVar
	{
		float *m_pValue;
		float m_Min;
		float m_Max;
		const char *m_pName;
		IConsole *m_pConsole;
	};
	static void ConVar(IConsole::IResult *pResult, void *pUserData);
	SVar m_aVars[2];
};

extern CBlockConfig g_BwConfig;

#endif // BLOCK_CONFIG_H
