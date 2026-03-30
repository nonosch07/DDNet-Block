#ifndef BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_H
#define BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_H

#include <base/system.h>
#include <base/vmath.h>

#include <blockworlds/utils/memory.h>

#include <memory>
#include <typeindex>
#include <utility>
#include <vector>

#define DECLARE_COMPONENT(ClassName, Name) \
public: \
	explicit ClassName(CGameContext *pGameServer); \
	static const char *GetNameStatic() { return Name; } \
	const char *GetName() const override { return GetNameStatic(); } \
	using ThisComponent = ClassName; \
	\
	template<typename... TArgs> \
	static void Log(const char *pFmt, TArgs &&...Args) \
	{ \
		dbg_msg(ThisComponent::GetNameStatic(), pFmt, std::forward<TArgs>(Args)...); \
	}

class CComponent
{
	class CGameContext *m_pGameServer;

protected:
	CGameContext *GameServer() const;
	class IServer *Server() const;
	class CConfig *Config() const;
	class IConsole *Console() const;
	class CComponentRegistry *Registry() const;

public:
	explicit CComponent(class CGameContext *pGameServer);
	virtual ~CComponent() = default;

	// TODO
	//	struct SComponentDependency {
	//		std::type_index m_Type;
	//		bool m_Required;
	//	};
	//	virtual std::vector<SComponentDependency> GetDependencies() const { return {}; };
	//	virtual void InjectDependency(const std::type_index& Type, CComponent* pComponent) {};

	[[nodiscard]] virtual std::vector<ComponentAccessor<CComponent>> GetSubComponents() const { return {}; }; // basically, it's set of components that are not registered, but require ticking, like events

	[[nodiscard]] virtual const char *GetName() const = 0;
	[[nodiscard]] virtual bool IsDebug() const;

	template<typename... TArgs>
	void Log(const char *pFmt, TArgs &&... Args) const
	{
		dbg_msg(GetName(), pFmt, std::forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	void LogDebug(const char *pFmt, TArgs &&... Args) const
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

	virtual void OnSnapClientInfo(int ClientId, int SnappingClient, class CNetObj_ClientInfo *pClientInfo) {}
	virtual void OnSnapPlayerInfo(int ClientId, int SnappingClient, class CNetObj_PlayerInfo *pPlayerInfo) {}

	virtual void OnPlayerConnected(int ClientId) {}

	virtual void OnPlayerEntering(int ClientId) {}
	virtual void OnPlayerEnter(int ClientId) {}
	virtual void OnPlayerDropping(int ClientId) {}
	virtual void OnPlayerDrop(int ClientId) {}

	virtual bool OnClientJoin(int ClientId) { return false; }

	virtual void OnPlayerAuthorized(int ClientId, int Level) {}
	virtual void OnPlayerUnAuthorized(int ClientId) {}

	virtual void OnTick() {}
	virtual void OnPostTick() {}
	virtual void OnSnap(int SnappingClient) {}
	virtual void OnPostSnap() {}

	virtual void OnCharacterSpawn(int ClientId, vec2 SpawnPos) {}
	virtual void OnCharacterDeath(int KillerId, int ClientId, int Weapon) {}
	// called when TakeDamage is invoked on a character by a real attacker (From >= 0)
	virtual void OnCharacterTakeDamage(vec2 Force, vec2 Source, int Dmg, int From, int ClientId, int Weapon) {}
};

#endif // BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_H
