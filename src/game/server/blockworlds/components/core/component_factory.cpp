#include "component_factory.h"

CComponentFactory g_ComponentRegistry;

CComponent *CComponentFactory::Create(std::type_index Type, class CGameContext *pGameServer)
{
	auto Result = m_Components.try_emplace(Type, m_TypeToFactory[Type](pGameServer));
	if(!Result.second)
		return nullptr;

	auto pComponent = Result.first->second.get();
	pComponent->OnEnable();
	pComponent->OnConsoleInit(); // we call it here since console is already initialized at any point of component creation

	return pComponent;
}
CComponent *CComponentFactory::Create(const std::string &Name, class CGameContext *pGameServer)
{
	auto Result = m_NameToType.find(Name);
	if(Result == m_NameToType.end())
		return nullptr;
	return Create(Result->second, pGameServer);
}

CComponent *CComponentFactory::Get(std::type_index Type)
{
	if (auto it = m_Components.find(Type); it != m_Components.end())
		return it->second.get();
	return nullptr;
}
CComponent *CComponentFactory::Get(const std::string &Name)
{
	auto Result = m_NameToType.find(Name);
	if(Result == m_NameToType.end())
		return nullptr;
	return Get(Result->second);
}

bool CComponentFactory::Remove(std::type_index Type)
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
bool CComponentFactory::Remove(const std::string &Name)
{
	auto Result = m_NameToType.find(Name);
	if(Result == m_NameToType.end())
		return false;
	return Remove(Result->second);
}

std::vector<CComponent*> CComponentFactory::Active()
{
	std::vector<CComponent*> vComponents;
	vComponents.reserve(m_Components.size());
	for(auto &item : m_Components)
		vComponents.push_back(item.second.get());
	return vComponents;
}

std::unordered_map<std::type_index, class CComponent*> CComponentFactory::All()
{
	std::unordered_map<std::type_index, class CComponent*> vComponents;
	vComponents.reserve(m_TypeToFactory.size());
	for(auto &item : m_TypeToFactory)
	{
		if(auto it = m_Components.find(item.first); it != m_Components.end())
			vComponents.emplace(item.first, it->second.get());
		else
			vComponents.emplace(item.first, nullptr);
	}
	return vComponents;
}
