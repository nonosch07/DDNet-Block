#include "component_registry.h"

CComponentRegistry g_ComponentRegistry;

CComponentAccessor<CComponent> CComponentRegistry::Create(std::type_index Type, class CGameContext *pGameServer, bool InitConsole)
{
	if(m_RequiredComponents.contains(Type) && m_Components.contains(Type))
		return nullptr;

	auto Result = m_Components.try_emplace(Type, m_TypeToFactory[Type](pGameServer));
	if(!Result.second)
		return nullptr;

	auto pComponent = Result.first->second;
	pComponent->OnEnable();
	if(InitConsole)
		pComponent->OnConsoleInit();

	return CComponentAccessor(pComponent);
}
CComponentAccessor<CComponent> CComponentRegistry::Create(const std::string &Name, class CGameContext *pGameServer, bool InitConsole)
{
	auto Result = m_NameToType.find(Name);
	if(Result == m_NameToType.end())
		return nullptr;
	return Create(Result->second, pGameServer, InitConsole);
}

CComponentAccessor<CComponent> CComponentRegistry::Get(std::type_index Type)
{
	if(auto it = m_Components.find(Type); it != m_Components.end())
		return CComponentAccessor(it->second);
	return nullptr;
}
CComponentAccessor<CComponent> CComponentRegistry::Get(const std::string &Name)
{
	auto Result = m_NameToType.find(Name);
	if(Result == m_NameToType.end())
		return nullptr;
	return Get(Result->second);
}

bool CComponentRegistry::Remove(std::type_index Type)
{
	if(m_RequiredComponents.contains(Type))
		return false;
	if(const auto it = m_Components.find(Type); it != m_Components.end())
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

std::vector<CComponentAccessor<CComponent>> CComponentRegistry::Active()
{
	std::vector<CComponentAccessor<CComponent>> vComponents;
	vComponents.reserve(m_Components.size());
	for(auto &[Type, pComponent] : m_Components)
	{
		vComponents.emplace_back(pComponent);
		if(auto vSubComponents = pComponent->GetSubComponents(); !vSubComponents.empty())
			vComponents.insert(vComponents.end(), std::make_move_iterator(vSubComponents.begin()), std::make_move_iterator(vSubComponents.end()));
	}
	return vComponents;
}

std::vector<CComponentAccessor<CComponent>> CComponentRegistry::Required()
{
	std::vector<CComponentAccessor<CComponent>> vComponents;
	vComponents.reserve(m_Components.size());
	for(auto &[Type, pComponent] : m_Components)
	{
		if(!m_RequiredComponents.contains(Type))
			continue;
		vComponents.emplace_back(pComponent);
		if(auto vSubComponents = pComponent->GetSubComponents(); !vSubComponents.empty())
			vComponents.insert(vComponents.end(), std::make_move_iterator(vSubComponents.begin()), std::make_move_iterator(vSubComponents.end()));
	}
	return vComponents;
}

std::unordered_map<std::type_index, CComponentAccessor<CComponent>> CComponentRegistry::All()
{
	std::unordered_map<std::type_index, CComponentAccessor<CComponent>> vComponents;
	vComponents.reserve(m_TypeToFactory.size());
	for(auto &[Type, Factory] : m_TypeToFactory)
	{
		if(auto it = m_Components.find(Type); it != m_Components.end())
			vComponents.emplace(Type, it->second);
		else
			vComponents.emplace(Type, nullptr);
	}
	return vComponents;
}
