#ifndef BLOCK_COMPONENTS_VPNDETECTION_VPN_SERVICE_RESULT_H
#define BLOCK_COMPONENTS_VPNDETECTION_VPN_SERVICE_RESULT_H

#include <cstdint>
#include <string>

/**
 * Base interface for VPN detection results
 */
struct IVpnServiceResult
{
	virtual ~IVpnServiceResult() = default;

	virtual const char *GetServiceName() const = 0;
	virtual bool IsBadIP() const = 0;
	virtual bool IsValid() const = 0;
	virtual int64_t GetTimestamp() const = 0;
	virtual const char *GetIpAddress() const = 0;
	virtual const char *GetAsn() const = 0;
	virtual const char *GetIsp() const = 0;
	virtual int GetRiskScore() const = 0;
	virtual const char *GetErrorMessage() const = 0;
	virtual int GetErrorCode() const = 0;
};

/**
 * Concrete VPN detection result implementation
 */
struct CVpnServiceResult : public IVpnServiceResult
{
	std::string m_ServiceName;
	std::string m_IpAddress;
	std::string m_Asn;
	std::string m_Isp;
	std::string m_ErrorMessage;
	bool m_IsBadIP;
	bool m_IsValid;
	int m_RiskScore;
	int m_ErrorCode;
	int64_t m_Timestamp;

	CVpnServiceResult() :
		m_IsBadIP(false),
		m_IsValid(false),
		m_RiskScore(-1),
		m_ErrorCode(0),
		m_Timestamp(0)
	{
	}

	const char *GetServiceName() const override { return m_ServiceName.c_str(); }
	const char *GetIpAddress() const override { return m_IpAddress.c_str(); }
	const char *GetAsn() const override { return m_Asn.c_str(); }
	const char *GetIsp() const override { return m_Isp.c_str(); }
	const char *GetErrorMessage() const override { return m_ErrorMessage.c_str(); }
	bool IsBadIP() const override { return m_IsBadIP; }
	bool IsValid() const override { return m_IsValid; }
	int GetRiskScore() const override { return m_RiskScore; }
	int GetErrorCode() const override { return m_ErrorCode; }
	int64_t GetTimestamp() const override { return m_Timestamp; }
};

#endif // BLOCK_COMPONENTS_VPNDETECTION_VPN_SERVICE_RESULT_H
