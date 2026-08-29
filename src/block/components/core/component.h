#ifndef BLOCK_COMPONENTS_CORE_COMPONENT_H
#define BLOCK_COMPONENTS_CORE_COMPONENT_H

#include <base/vmath.h>

#include <engine/console.h>

#include <block/base.h>
#include <block/utils/memory.h>

#include <utility>
#include <vector>

// The Log/LogDebug helpers below forward a runtime format string into dbg_msg,
// which GCC cannot check; every caller passes a literal.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
#endif

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

#define CONSOLE_COMMAND(Name, Args, Callback, Help) \
	m_ConsoleCommands.push_back({Name, Args, CFGFLAG_SERVER | CFGFLAG_ANNOUNCE, \
		reinterpret_cast<IConsole::FCommandCallback>(Callback), \
		reinterpret_cast<void *>(this), \
		Help});

#define CHAT_COMMAND(Name, Args, Callback, Help) \
	m_ConsoleCommands.push_back({Name, Args, CFGFLAG_SERVER | CFGFLAG_CHAT | CFGFLAG_ANNOUNCE, \
		reinterpret_cast<IConsole::FCommandCallback>(Callback), \
		reinterpret_cast<void *>(this), \
		Help});

#define CHAIN_COMMAND(Name, Callback) \
	m_ChainCommands.push_back({Name, \
		Callback, \
		reinterpret_cast<void *>(this)});

class CComponent
{
	class CGameContext *m_pGameServer;

public:
	CGameContext *GameServer() const;
	class CServer *Server() const;
	class CConfig *Config() const;
	class CConsole *Console() const;
	class CComponentRegistry *Registry() const;

protected:
	struct SCommand
	{
		const char *m_pName;
		const char *m_pParams;
		int m_Flags;
		IConsole::FCommandCallback m_pfnCallback;
		void *m_pUserData;
		const char *m_pHelp;
	};
	std::vector<SCommand> m_ConsoleCommands;

	struct SCommandChain
	{
		const char *m_pName;
		IConsole::FChainCommandCallback m_pfnCallback;
		void *m_pUserData;
	};
	std::vector<SCommandChain> m_ChainCommands;

public:
	explicit CComponent(class CGameContext *pGameServer);
	virtual ~CComponent() = default;

	std::vector<SCommand> *GetConsoleCommands() { return &m_ConsoleCommands; }

	virtual const char *GetName() const = 0;
	virtual const char *GetPrintableName() const { return GetName(); }
	virtual bool IsDebug() const;
	virtual bool IsRequired() const { return false; }

	[[nodiscard]] virtual std::vector<CComponentAccessor<CComponent>> GetSubComponents() const { return {}; } // basically, it's set of components that are not registered, but require ticking, like events

	template<typename... TArgs>
	void Log(const char *pFmt, TArgs &&...Args) const
	{
		dbg_msg(GetName(), pFmt, std::forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	void LogDebug(const char *pFmt, TArgs &&...Args) const
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

	virtual void OnConsoleInit();
	virtual void OnConsoleTerminate();
	virtual void OnShutdown() {}

	virtual void OnSnapClientInfo(int ClientId, int SnappingClient, struct CNetObj_ClientInfo *pClientInfo) {}
	virtual void OnSnapPlayerInfo(int ClientId, int SnappingClient, struct CNetObj_PlayerInfo *pPlayerInfo) {}

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
	virtual void OnCharacterTakeDamage(vec2 Force, int Dmg, int From, int ClientId, int Weapon) {}
};

#endif // BLOCK_COMPONENTS_CORE_COMPONENT_H
