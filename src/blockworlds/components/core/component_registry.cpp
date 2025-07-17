#include "component_registry.h"

CComponentRegistry g_ComponentRegistry;

ComponentAccessor<CComponent> CComponentRegistry::Create(std::type_index Type, class CGameContext *pGameServer)
{
	auto Result = m_Components.try_emplace(Type, m_TypeToFactory[Type](pGameServer));
	if(!Result.second)
		return nullptr;

	auto pComponent = Result.first->second;
	pComponent->OnEnable();
	pComponent->OnConsoleInit(); // we call it here since console is already initialized at any point of component creation

	return ComponentAccessor(pComponent);
}
ComponentAccessor<CComponent> CComponentRegistry::Create(const std::string &Name, class CGameContext *pGameServer)
{
	auto Result = m_NameToType.find(Name);
	if(Result == m_NameToType.end())
		return nullptr;
	return Create(Result->second, pGameServer);
}

ComponentAccessor<CComponent> CComponentRegistry::Get(std::type_index Type)
{
	if (auto it = m_Components.find(Type); it != m_Components.end())
		return ComponentAccessor(it->second);
	return nullptr;
}
ComponentAccessor<CComponent> CComponentRegistry::Get(const std::string &Name)
{
	auto Result = m_NameToType.find(Name);
	if(Result == m_NameToType.end())
		return nullptr;
	return Get(Result->second);
}

bool CComponentRegistry::Remove(std::type_index Type)
{
	if (auto it = m_Components.find(Type); it != m_Components.end())
	{
		it->second->OnConsoleTerminate();
		it->second->OnDisable();
		m_Components.erase(it);
		return true;
	}
	return false;
}
bool CComponentRegistry::Remove(const std::string &Name)
{
	auto Result = m_NameToType.find(Name);
	if(Result == m_NameToType.end())
		return false;
	return Remove(Result->second);
}

std::vector<ComponentAccessor<CComponent>> CComponentRegistry::Active()
{
	std::vector<ComponentAccessor<CComponent>> vComponents;
	vComponents.reserve(m_Components.size());
	for(auto &item : m_Components)
	{
		vComponents.emplace_back(item.second);
		if(auto vSubComponents = item.second->GetSubComponents(); !vSubComponents.empty())
			vComponents.insert(vComponents.end(), std::make_move_iterator(vSubComponents.begin()), std::make_move_iterator(vSubComponents.end()));
	}
	return vComponents;
}

std::unordered_map<std::type_index, ComponentAccessor<CComponent>> CComponentRegistry::All()
{
	std::unordered_map<std::type_index, ComponentAccessor<CComponent>> vComponents;
	vComponents.reserve(m_TypeToFactory.size());
	for(auto &item : m_TypeToFactory)
	{
		if(auto it = m_Components.find(item.first); it != m_Components.end())
			vComponents.emplace(item.first, it->second);
		else
			vComponents.emplace(item.first, nullptr);
	}
	return vComponents;
}
