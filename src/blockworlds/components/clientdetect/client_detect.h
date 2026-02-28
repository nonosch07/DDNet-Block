#ifndef BLOCKWORLDS_COMPONENTS_CLIENTDETECT_CLIENT_DETECT_H
#define BLOCKWORLDS_COMPONENTS_CLIENTDETECT_CLIENT_DETECT_H

#include <blockworlds/components/core/component.h>
#include <engine/console.h>

class CClientDetectComponent final : public CComponent
{
public:
	static const char *GetNameStatic() { return "clientdetect"; }

public:
	explicit CClientDetectComponent(class CGameContext *pGameServer);

public:
	const char *GetName() const override { return GetNameStatic(); }
	void OnConsoleInit() override;
	void OnConsoleTerminate() override;

private:
	static void ConStatusClient(IConsole::IResult *pResult, void *pUserData);
};

#endif
