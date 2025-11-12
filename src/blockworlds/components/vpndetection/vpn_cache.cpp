#include "vpn_cache.h"

#include <base/system.h>
#include <engine/external/json-parser/json.h>

#include <fstream>
#include <sstream>

std::string CVpnCache::MakeCacheKey(const char *pIpAddress, const char *pServiceName)
{
	std::string Key = pIpAddress;
	Key += ':';
	Key += pServiceName;
	return Key;
}

void CVpnCache::Add(std::shared_ptr<IVpnServiceResult> pResult)
{
	if(!pResult || !pResult->IsValid())
		return;

	if(str_comp(pResult->GetServiceName(), "local") == 0)
		return;

	std::lock_guard<std::mutex> Lock(m_Mutex);
	std::string CacheKey = MakeCacheKey(pResult->GetIpAddress(), pResult->GetServiceName());
	m_Cache[CacheKey] = pResult;
}

std::shared_ptr<IVpnServiceResult> CVpnCache::Get(const char *pIpAddress, const char *pServiceName)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);

	std::string CacheKey = MakeCacheKey(pIpAddress, pServiceName);
	auto It = m_Cache.find(CacheKey);

	if(It != m_Cache.end())
	{
		return It->second;
	}

	return nullptr;
}

void CVpnCache::GetAllForIP(const char *pIpAddress, std::vector<std::shared_ptr<IVpnServiceResult>> &Results)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);

	std::string IpPrefix = std::string(pIpAddress) + ':';
	for(const auto &Entry : m_Cache)
	{
		if(Entry.first.compare(0, IpPrefix.length(), IpPrefix) == 0)
		{
			Results.push_back(Entry.second);
		}
	}
}

bool CVpnCache::Load(const char *pFilename)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_Cache.clear();

	std::ifstream File(pFilename);
	if(!File.is_open())
	{
		return false;
	}

	std::stringstream Buffer;
	Buffer << File.rdbuf();
	File.close();

	if(File.fail())
	{
		return false;
	}

	std::string Content = Buffer.str();

	if(Content.empty())
	{
		return false;
	}

	json_value *pRoot = json_parse(Content.c_str(), Content.length());
	if(!pRoot)
	{
		return false;
	}

	if(pRoot->type != json_object)
	{
		json_value_free(pRoot);
		return false;
	}

	for(unsigned int i = 0; i < pRoot->u.object.length; i++)
	{
		const char *pKey = pRoot->u.object.values[i].name;
		json_value *pEntry = pRoot->u.object.values[i].value;

		if(pEntry->type != json_object)
			continue;

		auto pResult = std::make_shared<CVpnServiceResult>();

		for(unsigned int j = 0; j < pEntry->u.object.length; j++)
		{
			const char *pFieldName = pEntry->u.object.values[j].name;
			json_value *pFieldValue = pEntry->u.object.values[j].value;

			if(str_comp(pFieldName, "service") == 0 && pFieldValue->type == json_string)
				pResult->m_ServiceName = pFieldValue->u.string.ptr;
			else if(str_comp(pFieldName, "ip") == 0 && pFieldValue->type == json_string)
				pResult->m_IpAddress = pFieldValue->u.string.ptr;
			else if(str_comp(pFieldName, "asn") == 0 && pFieldValue->type == json_string)
				pResult->m_Asn = pFieldValue->u.string.ptr;
			else if(str_comp(pFieldName, "isp") == 0 && pFieldValue->type == json_string)
				pResult->m_Isp = pFieldValue->u.string.ptr;
			else if(str_comp(pFieldName, "is_bad") == 0 && pFieldValue->type == json_boolean)
				pResult->m_IsBadIP = pFieldValue->u.boolean;
			else if(str_comp(pFieldName, "risk_score") == 0 && pFieldValue->type == json_integer)
				pResult->m_RiskScore = (int)pFieldValue->u.integer;
			else if(str_comp(pFieldName, "timestamp") == 0 && pFieldValue->type == json_integer)
				pResult->m_Timestamp = pFieldValue->u.integer;
			else if(str_comp(pFieldName, "valid") == 0 && pFieldValue->type == json_boolean)
				pResult->m_IsValid = pFieldValue->u.boolean;
		}

		if(pResult->m_IsValid && !pResult->m_ServiceName.empty() && !pResult->m_IpAddress.empty())
		{
			m_Cache[pKey] = pResult;
		}
	}

	json_value_free(pRoot);
	return true;
}

bool CVpnCache::Save(const char *pFilename)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);

	std::ofstream File(pFilename);
	if(!File.is_open())
	{
		return false;
	}

	File << "{\n";

	bool First = true;
	for(const auto &Entry : m_Cache)
	{
		const auto &pResult = Entry.second;
		if(!pResult || !pResult->IsValid())
			continue;

		if(!First)
			File << ",\n";
		First = false;

		File << "  \"" << Entry.first << "\": {\n";
		File << "    \"service\": \"" << pResult->GetServiceName() << "\",\n";
		File << "    \"ip\": \"" << pResult->GetIpAddress() << "\",\n";
		File << "    \"asn\": \"" << pResult->GetAsn() << "\",\n";
		File << "    \"isp\": \"" << pResult->GetIsp() << "\",\n";
		File << "    \"is_bad\": " << (pResult->IsBadIP() ? "true" : "false") << ",\n";
		File << "    \"risk_score\": " << pResult->GetRiskScore() << ",\n";
		File << "    \"timestamp\": " << pResult->GetTimestamp() << ",\n";
		File << "    \"valid\": true\n";
		File << "  }";
	}

	File << "\n}\n";
	File.close();

	if(File.fail())
	{
		return false;
	}

	return true;
}

void CVpnCache::Clear()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_Cache.clear();
}

int CVpnCache::GetEntryCount() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return (int)m_Cache.size();
}
