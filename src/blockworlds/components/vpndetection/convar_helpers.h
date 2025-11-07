#ifndef BLOCKWORLDS_COMPONENTS_VPN_CONVAR_HELPERS_H
#define BLOCKWORLDS_COMPONENTS_VPN_CONVAR_HELPERS_H

#include <engine/console.h>
#include <functional>

template<typename T>
struct SConVarData
{
	T *pValue;
	T MinValue;
	T MaxValue;
	T DefaultValue;
	IConsole *pConsole;
	std::function<void(T)> OnChange;
};

struct SConVarStringData
{
	char *pValue;
	int MaxLength;
	const char *pDefaultValue;
	IConsole *pConsole;
	std::function<void(const char *)> OnChange;
};

inline void ConVarBoolCallback(IConsole::IResult *pResult, void *pUserData)
{
	auto *pData = static_cast<SConVarData<bool> *>(pUserData);
	
	if(pResult->NumArguments())
	{
		bool NewValue = pResult->GetInteger(0) != 0;
		*pData->pValue = NewValue;
		if(pData->OnChange)
			pData->OnChange(NewValue);
	}
	else
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Value: %d", *pData->pValue ? 1 : 0);
		pData->pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
	}
}

inline void ConVarIntCallback(IConsole::IResult *pResult, void *pUserData)
{
	auto *pData = static_cast<SConVarData<int> *>(pUserData);
	
	if(pResult->NumArguments())
	{
		int Value = pResult->GetInteger(0);
		if(Value < pData->MinValue)
			Value = pData->MinValue;
		if(Value > pData->MaxValue)
			Value = pData->MaxValue;
		*pData->pValue = Value;
		if(pData->OnChange)
			pData->OnChange(Value);
	}
	else
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Value: %d", *pData->pValue);
		pData->pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
	}
}

inline void ConVarFloatCallback(IConsole::IResult *pResult, void *pUserData)
{
	auto *pData = static_cast<SConVarData<float> *>(pUserData);
	
	if(pResult->NumArguments())
	{
		float Value = pResult->GetFloat(0);
		if(Value < pData->MinValue)
			Value = pData->MinValue;
		if(Value > pData->MaxValue)
			Value = pData->MaxValue;
		*pData->pValue = Value;
		if(pData->OnChange)
			pData->OnChange(Value);
	}
	else
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Value: %.2f", *pData->pValue);
		pData->pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
	}
}

inline void ConVarStringCallback(IConsole::IResult *pResult, void *pUserData)
{
	auto *pData = static_cast<SConVarStringData *>(pUserData);
	
	if(pResult->NumArguments())
	{
		const char *pStr = pResult->GetString(0);
		str_copy(pData->pValue, pStr, pData->MaxLength);
		if(pData->OnChange)
			pData->OnChange(pStr);
	}
	else
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Value: %s", pData->pValue[0] ? pData->pValue : "(not set)");
		pData->pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "config", aBuf);
	}
}

#define CONVAR_BOOL(var, def) \
	new SConVarData<bool>{&var, false, true, def, Console(), nullptr}

#define CONVAR_INT(var, def, min, max) \
	new SConVarData<int>{&var, min, max, def, Console(), nullptr}

#define CONVAR_FLOAT(var, def, min, max) \
	new SConVarData<float>{&var, min, max, def, Console(), nullptr}

#define CONVAR_STRING(var, def) \
	new SConVarStringData{var, sizeof(var), def, Console(), nullptr}

#define CONVAR_BOOL_ONCHANGE(var, def, callback) \
	new SConVarData<bool>{&var, false, true, def, Console(), callback}

#define CONVAR_INT_ONCHANGE(var, def, min, max, callback) \
	new SConVarData<int>{&var, min, max, def, Console(), callback}

#define CONVAR_FLOAT_ONCHANGE(var, def, min, max, callback) \
	new SConVarData<float>{&var, min, max, def, Console(), callback}

#define CONVAR_STRING_ONCHANGE(var, def, callback) \
	new SConVarStringData{var, sizeof(var), def, Console(), callback}

#endif // BLOCKWORLDS_COMPONENTS_VPN_CONVAR_HELPERS_H

