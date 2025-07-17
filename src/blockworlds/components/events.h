#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_H

#include "core/component.h"

#include <engine/console.h>

class CEvents final : public CComponent
{
public:
	static constexpr const char *GetNameStatic() { return "Events"; }
	[[nodiscard]] const char *GetName() const override { return GetNameStatic(); };
	[[nodiscard]] std::vector<CComponent *> GetSubComponents() const override;

protected:
	void OnEnable() override;
	void OnDisable() override;

	void OnConsoleInit() override;
	void OnConsoleTerminate() override;

	void OnPlayerDrop(int ClientId) override;
	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;

	void OnTick() override;

	static void ConEventsTest(IConsole::IResult *pResult, void *pUserData);

public:
	explicit CEvents(CGameContext *pGameServer);

private:
	class CEventComponent *m_pActiveEvent;
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_H
