#ifndef BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_REGISTRY_H
#define BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_REGISTRY_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeindex>

#include <base/system.h>
#include <blockworlds/components/core/component.h>

class CComponentRegistry final
{
public:
	using FnFactory = std::function<std::shared_ptr<CComponent>(class CGameContext*)>;

private:
	std::unordered_map<std::type_index, FnFactory> m_TypeToFactory;
	std::unordered_map<std::string, std::type_index> m_NameToType;
	std::unordered_map<std::type_index, std::string> m_TypeToName;

	std::unordered_map<std::type_index, std::shared_ptr<CComponent>> m_Components;

public:
	template<typename T>
	void Register(const std::string &Name)
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent");
		m_TypeToFactory[typeid(T)] = [](class CGameContext *pGameServer) { return std::make_shared<T>(pGameServer); };
		m_NameToType.try_emplace(Name, typeid(T));
		m_TypeToName.try_emplace(typeid(T), Name);
	}

	template<typename T>
	std::shared_ptr<T> Create(class CGameContext *pGameServer)
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent");
		return std::static_pointer_cast<T>(Create(typeid(T), pGameServer));
	}
	std::shared_ptr<CComponent> Create(std::type_index Type, class CGameContext *pGameServer);
	std::shared_ptr<CComponent> Create(const std::string &Name, class CGameContext *pGameServer);

	template<typename T>
	std::shared_ptr<T> Get()
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent");
		return std::static_pointer_cast<T>(Get(typeid(T)));
	}
	std::shared_ptr<CComponent> Get(std::type_index Type);
	std::shared_ptr<CComponent> Get(const std::string &Name);

	template<typename T>
	bool Remove()
	{
		return Remove(typeid(T));
	}
	bool Remove(std::type_index Type);
	bool Remove(const std::string &Name);

	std::vector<std::shared_ptr<CComponent>> Active();
	std::unordered_map<std::type_index, std::shared_ptr<CComponent>> All();

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

#endif // BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_REGISTRY_H
