#ifndef GAME_SERVER_BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_H
#define GAME_SERVER_BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_H

#include <base/vmath.h>
#include <base/system.h>

#include <typeindex>
#include <utility>
#include <vector>

class CComponent
{
	class CGameContext *m_pGameServer;

protected:
	CGameContext *GameServer() const;
	class IServer *Server() const;
	class CConfig *Config() const;
	class IConsole *Console() const;

public:
	CComponent(class CGameContext *pGameServer);
	virtual ~CComponent() = default;

	// TODO
//	struct SComponentDependency {
//		std::type_index m_Type;
//		bool m_Required;
//	};
//	virtual std::vector<SComponentDependency> GetDependencies() const { return {}; };
//	virtual void InjectDependency(const std::type_index& Type, CComponent* pComponent) {};

	[[nodiscard]] virtual const char *GetName() const = 0;
	[[nodiscard]] virtual bool IsDebug() const = 0;

	template<typename... TArgs>
	void Log(const char* pFmt, TArgs&&... Args) const
	{
		dbg_msg(GetName(), pFmt, std::forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	void LogDebug(const char* pFmt, TArgs&&... Args) const
	{
		if(IsDebug())
		{
			char aFrom[128];
			str_format(aFrom, sizeof(aFrom), "%s/debug", GetName());
			dbg_msg(aFrom, pFmt, std::forward<TArgs>(Args)...);
		}
	}

	virtual void OnEnable() {}
	virtual void OnDisable() {}

	virtual void OnConsoleInit() {}
	virtual void OnConsoleTerminate() {}
	virtual void OnShutdown() {}

	virtual void OnPlayerEntering(int ClientId) {}
	virtual void OnPlayerEnter(int ClientId) {}
	virtual void OnPlayerDropping(int ClientId) {}
	virtual void OnPlayerDrop(int ClientId) {}

	virtual void OnPlayerAuthorized(int ClientId, int Level) {}
	virtual void OnPlayerUnAuthorized(int ClientId) {}

	virtual void OnTick() {}
	virtual void OnPostTick() {}
	virtual void OnSnap(int SnappingClient) {}
	virtual void OnPostSnap() {}

	virtual void OnCharacterSpawn(int ClientId, vec2 SpawnPos) {}
	virtual void OnCharacterDeath(int KillerId, int ClientId, int Weapon) {}
};

#endif // GAME_SERVER_BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_H
