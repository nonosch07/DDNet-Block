#ifndef BLOCKWORLDS_COMPONENTS_PROMISES_H
#define BLOCKWORLDS_COMPONENTS_PROMISES_H

#include <engine/shared/config.h>

#include <blockworlds/components/core/component.h>

struct SPromise
{
	int m_ExecuteTick;
	std::weak_ptr<void> m_pUserData;
	std::function<void(std::shared_ptr<void>)> m_Callback;
};

class CPromises final : public CComponent
{
public:
	static constexpr const char *GetNameStatic() { return "Promises"; }
	[[nodiscard]] const char *GetName() const override { return GetNameStatic(); }

protected:
	void OnTick() override;
	void OnShutdown() override;
	void OnDisable() override;

public:
	explicit CPromises(CGameContext *pGameServer);

	const SPromise *AddPromise(int ExecuteTick, std::weak_ptr<void> pUserData, std::function<void(std::shared_ptr<void>)> FnCallback);

private:
	std::vector<SPromise> m_Promises;
	unsigned long GetCallbackHash(std::function<void(std::shared_ptr<void>)> FnCallback);
};

#endif // BLOCKWORLDS_COMPONENTS_PROMISES_H
