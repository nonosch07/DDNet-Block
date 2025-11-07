#include "vpn_client_info.h"

#include <base/system.h>

CVpnClientInfo::CVpnClientInfo() :
	m_ClientId(-1),
	m_CheckInProgress(false),
	m_LastCheckTime(0)
{
}

std::shared_ptr<IVpnServiceResult> CVpnClientInfo::GetResultByService(const char *pServiceName) const
{
	for(const auto &pResult : m_Results)
	{
		if(pResult && str_comp(pResult->GetServiceName(), pServiceName) == 0)
		{
			return pResult;
		}
	}
	return nullptr;
}

bool CVpnClientInfo::IsBadIP() const
{
	for(const auto &pResult : m_Results)
	{
		if(pResult && pResult->IsValid() && pResult->IsBadIP())
		{
			return true;
		}
	}
	return false;
}

int CVpnClientInfo::GetAverageRiskScore() const
{
	int Total = 0;
	int Count = 0;
	
	for(const auto &pResult : m_Results)
	{
		if(pResult && pResult->IsValid())
		{
			int Score = pResult->GetRiskScore();
			if(Score >= 0)
			{
				Total += Score;
				Count++;
			}
		}
	}
	
	return Count > 0 ? Total / Count : -1;
}

