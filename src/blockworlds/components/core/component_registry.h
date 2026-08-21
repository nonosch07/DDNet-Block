#pragma once

#include <blockworlds/bw_base.h>
#include <blockworlds/components/core/component.h>
#include <blockworlds/utils/memory.h>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <typeindex>

class CComponentRegistry final
{
public:
	using FnFactory = std::function<std::shared_ptr<CComponent>(class CGameContext *)>;

private:
	std::unordered_map<std::type_index, FnFactory> m_TypeToFactory;
	std::unordered_map<std::string, std::type_index> m_NameToType;
	std::unordered_map<std::type_index, std::string> m_TypeToName;

	std::set<std::type_index> m_RequiredComponents;

	std::unordered_map<std::type_index, std::shared_ptr<CComponent>> m_Components;

public:
	template<typename T>
	void Register(const std::string &Name, bool Required = false)
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CServerComponent");
		m_TypeToFactory[typeid(T)] = [](class CGameContext *pGameServer) { return std::make_shared<T>(pGameServer); };
		m_NameToType.try_emplace(Name, typeid(T));
		m_TypeToName.try_emplace(typeid(T), Name);
		if(Required)
			m_RequiredComponents.emplace(typeid(T));
	}

	template<typename T>
	CComponentAccessor<T> Create(class CGameContext *pGameServer, bool InitConsole = true)
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent");
		return CComponentAccessor(std::static_pointer_cast<T>(Create(typeid(T), pGameServer).m_pPtr, InitConsole));
	}
	CComponentAccessor<CComponent> Create(std::type_index Type, class CGameContext *pGameServer, bool InitConsole = true);
	CComponentAccessor<CComponent> Create(const std::string &Name, class CGameContext *pGameServer, bool InitConsole = true);

	std::vector<CComponentAccessor<CComponent>> CreateRequired(class CGameContext *pGameServer)
	{
		std::vector<CComponentAccessor<CComponent>> vCreated;
		for(auto TComponent : m_RequiredComponents)
			vCreated.emplace_back(Create(TComponent, pGameServer, false));
		return vCreated;
	}

	template<typename T>
	CComponentAccessor<T> Get()
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent");
		return CComponentAccessor<T>(std::static_pointer_cast<T>(Get(typeid(T)).m_pPtr));
	}
	CComponentAccessor<CComponent> Get(std::type_index Type);
	CComponentAccessor<CComponent> Get(const std::string &Name);

	template<typename T>
	bool Remove()
	{
		return Remove(typeid(T));
	}
	bool Remove(std::type_index Type);
	bool Remove(const std::string &Name);

	std::vector<CComponentAccessor<CComponent>> Required();
	std::vector<CComponentAccessor<CComponent>> Active();
	std::unordered_map<std::type_index, CComponentAccessor<CComponent>> All();

	template<typename T>
	std::string Name()
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent");
		return Name(typeid(T));
	}
	std::string Name(std::type_index Type)
	{
		auto it = m_TypeToName.find(Type);
		return it != m_TypeToName.end() ? it->second : "";
	}
	std::type_index Type(const std::string &Name)
	{
		auto it = m_NameToType.find(Name);
		return it != m_NameToType.end() ? it->second : typeid(nullptr);
	}
};

extern CComponentRegistry g_ComponentRegistry;
