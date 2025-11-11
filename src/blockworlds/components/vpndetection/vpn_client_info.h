#ifndef BLOCKWORLDS_COMPONENTS_VPN_CLIENT_INFO_H
#define BLOCKWORLDS_COMPONENTS_VPN_CLIENT_INFO_H

#include "vpn_service_result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * Per-client VPN detection information
 */
struct CVpnClientInfo
{
	int m_ClientId;
	std::string m_IpAddress;
	std::vector<std::shared_ptr<IVpnServiceResult>> m_Results;
	bool m_CheckInProgress;
	int64_t m_LastCheckTime;

	CVpnClientInfo();

	std::shared_ptr<IVpnServiceResult> GetResultByService(const char *pServiceName) const;
	bool IsBadIP() const;
	int GetAverageRiskScore() const;
};

#endif // BLOCKWORLDS_COMPONENTS_VPN_CLIENT_INFO_H
