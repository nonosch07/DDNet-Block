#ifndef BLOCK_COMPONENTS_CLIENTDETECT_CLIENT_DETECT_H
#define BLOCK_COMPONENTS_CLIENTDETECT_CLIENT_DETECT_H

#include <engine/console.h>

#include <block/components/core/component.h>

class CClientDetectComponent final : public CComponent
{
	DECLARE_COMPONENT(CClientDetectComponent, "clientdetect")

private:
	static void ConStatusClient(IConsole::IResult *pResult, void *pUserData);
};

#endif
