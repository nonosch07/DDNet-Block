#ifndef GAME_SERVER_BLOCKWORLDS_COMPONENTS_EVENTS_H
#define GAME_SERVER_BLOCKWORLDS_COMPONENTS_EVENTS_H

#include "core/component.h"

#include <engine/console.h>

class CEvents final : public CComponent
{
public:
	static constexpr const char *GetNameStatic() { return "Events"; }
	[[nodiscard]] const char *GetName() const override { return GetNameStatic(); };

protected:
	void OnEnable() override;
	void OnDisable() override;

	void OnConsoleInit() override;
	void OnConsoleTerminate() override;

	void OnTick() override;

	static void ConEventsTest(IConsole::IResult *pResult, void *pUserData);

public:
	explicit CEvents(CGameContext *pGameServer);

};

#endif // GAME_SERVER_BLOCKWORLDS_COMPONENTS_EVENTS_H
