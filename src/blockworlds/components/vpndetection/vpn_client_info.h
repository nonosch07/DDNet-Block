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
	// Number of queued/running service requests started when the client joined
	int m_PendingChecks;
	// Time (time_get) at which the withheld join message is broadcast anyway, 0 when
	// the join message of this client is not being withheld
	int64_t m_JoinMsgHoldExpire;

	CVpnClientInfo();

	std::shared_ptr<IVpnServiceResult> GetResultByService(const char *pServiceName) const;
	bool IsBadIP() const;
	int GetAverageRiskScore() const;
};

#endif // BLOCKWORLDS_COMPONENTS_VPN_CLIENT_INFO_H
