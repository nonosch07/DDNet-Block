#include "vpn_cache.h"

#include <blockworlds/bw_base.h>
#include <engine/external/json-parser/json.h>

#include <fstream>
#include <sstream>

namespace {
constexpr int64_t MIN_VALID_UNIX_TIMESTAMP = 1600000000; // Reject old time_get() tick values in legacy cache files.

// Escapes a string for safe embedding inside a JSON double-quoted value.
// Handles ", \, and control characters so a corrupt ISP/ASN name can never
// produce malformed JSON that fails to reload on the next server start.
std::string JsonEscapeString(const std::string &Str)
{
	std::string Result;
	Result.reserve(Str.size());
	for(unsigned char c : Str)
	{
		switch(c)
		{
		case '"': Result += "\\\""; break;
		case '\\': Result += "\\\\"; break;
		case '\n': Result += "\\n"; break;
		case '\r': Result += "\\r"; break;
		case '\t': Result += "\\t"; break;
		default:
			if(c < 0x20)
			{
				char aBuf[8];
				str_format(aBuf, sizeof(aBuf), "\\u%04x", (unsigned int)c);
				Result += aBuf;
			}
			else
				Result += (char)c;
			break;
		}
	}
	return Result;
}
} // namespace

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

void CVpnCache::SetTtlDays(int Days)
{
	if(Days < 1)
		Days = 1;
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_TtlSeconds = (int64_t)Days * 24 * 60 * 60;
	PruneExpiredLocked();
}

bool CVpnCache::IsTimestampFresh(int64_t Timestamp) const
{
	if(Timestamp < MIN_VALID_UNIX_TIMESTAMP)
		return false;

	const int64_t Now = time_timestamp();
	if(Timestamp > Now + 24 * 60 * 60)
		return false;

	return Now - Timestamp <= m_TtlSeconds;
}

bool CVpnCache::IsExpired(std::shared_ptr<IVpnServiceResult> pResult) const
{
	if(!pResult || !pResult->IsValid())
		return true;

	std::lock_guard<std::mutex> Lock(m_Mutex);
	return !IsTimestampFresh(pResult->GetTimestamp());
}

std::shared_ptr<IVpnServiceResult> CVpnCache::Get(const char *pIpAddress, const char *pServiceName)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);

	std::string CacheKey = MakeCacheKey(pIpAddress, pServiceName);
	auto It = m_Cache.find(CacheKey);

	if(It != m_Cache.end())
	{
		if(!It->second || !It->second->IsValid() || !IsTimestampFresh(It->second->GetTimestamp()))
		{
			m_Cache.erase(It);
			return nullptr;
		}
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
			if(Entry.second && Entry.second->IsValid() && IsTimestampFresh(Entry.second->GetTimestamp()))
				Results.push_back(Entry.second);
		}
	}
}

void CVpnCache::PruneExpiredLocked()
{
	for(auto It = m_Cache.begin(); It != m_Cache.end();)
	{
		if(!It->second || !It->second->IsValid() || !IsTimestampFresh(It->second->GetTimestamp()))
			It = m_Cache.erase(It);
		else
			++It;
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

		if(pResult->m_IsValid && !pResult->m_ServiceName.empty() && !pResult->m_IpAddress.empty() && IsTimestampFresh(pResult->m_Timestamp))
		{
			m_Cache[pKey] = pResult;
		}
	}

	json_value_free(pRoot);
	PruneExpiredLocked();
	return true;
}

bool CVpnCache::Save(const char *pFilename)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	PruneExpiredLocked();

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
		if(!pResult || !pResult->IsValid() || !IsTimestampFresh(pResult->GetTimestamp()))
			continue;

		if(!First)
			File << ",\n";
		First = false;

		// All string fields are JSON-escaped so an unusual ISP/ASN name (e.g.
		// one containing a quote or backslash) can never corrupt the cache file.
		File << "  \"" << Entry.first << "\": {\n";
		File << "    \"service\": \"" << JsonEscapeString(pResult->GetServiceName()) << "\",\n";
		File << "    \"ip\": \"" << JsonEscapeString(pResult->GetIpAddress()) << "\",\n";
		File << "    \"asn\": \"" << JsonEscapeString(pResult->GetAsn()) << "\",\n";
		File << "    \"isp\": \"" << JsonEscapeString(pResult->GetIsp()) << "\",\n";
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
