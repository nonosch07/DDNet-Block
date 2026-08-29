#ifndef BLOCK_COMPONENTS_VPNDETECTION_VPN_SERVICE_REQUEST_H
#define BLOCK_COMPONENTS_VPNDETECTION_VPN_SERVICE_REQUEST_H

#include "vpn_service_result.h"

#include <memory>
#include <string>

class CVpnDetectionComponent;
class IVpnService;

/**
 * Base interface for VPN detection service requests
 */
struct IVpnServiceRequest
{
	virtual ~IVpnServiceRequest() = default;

	virtual const char *GetServiceName() const = 0;
	virtual const char *GetIpAddress() const = 0;
	virtual int GetClientId() const = 0;
	virtual std::shared_ptr<IVpnServiceResult> Execute() = 0;
};

/**
 * Concrete VPN service request implementation
 * Performs HTTP requests via cURL and delegates parsing to the service
 */
struct CVpnServiceRequest : public IVpnServiceRequest
{
	std::string m_ServiceName;
	std::string m_IpAddress;
	int m_ClientId;
	CVpnDetectionComponent *m_pComponent;
	IVpnService *m_pService;

	CVpnServiceRequest(
		const char *pServiceName,
		const char *pIpAddress,
		int ClientId,
		CVpnDetectionComponent *pComponent,
		IVpnService *pService);

	const char *GetServiceName() const override { return m_ServiceName.c_str(); }
	const char *GetIpAddress() const override { return m_IpAddress.c_str(); }
	int GetClientId() const override { return m_ClientId; }

	std::shared_ptr<IVpnServiceResult> Execute() override;

private:
	bool PerformHttpRequest(const char *pUrl, std::string &ResponseBody, std::string &ResponseHeaders, int &ResponseCode) const;
	int ParseRetryAfterMs(const std::string &ResponseHeaders, int ResponseCode) const;
};

#endif // BLOCK_COMPONENTS_VPNDETECTION_VPN_SERVICE_REQUEST_H
