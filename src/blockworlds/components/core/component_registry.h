#ifndef GAME_SERVER_BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_FACTORY_H
#define GAME_SERVER_BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_FACTORY_H

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
	using FnFactory = std::function<class CComponent*(class CGameContext*)>;

private:
	std::unordered_map<std::type_index, FnFactory> m_TypeToFactory;
	std::unordered_map<std::string, std::type_index> m_NameToType;
	std::unordered_map<std::type_index, std::string> m_TypeToName;

	std::unordered_map<std::type_index, std::unique_ptr<class CComponent>> m_Components;

public:
	template<typename T>
	void Register(const std::string &Name)
	{
		m_TypeToFactory[typeid(T)] = [](class CGameContext *pGameServer) { return new T(pGameServer); };
		m_NameToType.try_emplace(Name, typeid(T));
		m_TypeToName.try_emplace(typeid(T), Name);
	}

	template<typename T>
	CComponent *Create(class CGameContext *pGameServer)
	{
		return Create(typeid(T), pGameServer);
	}
	CComponent *Create(std::type_index Type, class CGameContext *pGameServer);
	CComponent *Create(const std::string &Name, class CGameContext *pGameServer);

	template<typename T>
	CComponent *Get()
	{
		return Get(typeid(T));
	}
	CComponent* Get(std::type_index Type);
	CComponent* Get(const std::string &Name);

	template<typename T>
	bool Remove()
	{
		return Remove(typeid(T));
	}
	bool Remove(std::type_index Type);
	bool Remove(const std::string &Name);

	std::vector<class CComponent*> Active();
	std::unordered_map<std::type_index, class CComponent*> All();

	template<typename T>
	std::string Name()
	{
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

#endif // GAME_SERVER_BLOCKWORLDS_COMPONENTS_CORE_COMPONENT_FACTORY_H
