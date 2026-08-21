#include <blockworlds/bw_base.h>
#include <blockworlds/components/vpndetection/services/getipintel_service.h>
#include <blockworlds/components/vpndetection/services/iphub_service.h>
#include <blockworlds/components/vpndetection/services/iplocate_service.h>
#include <blockworlds/components/vpndetection/services/ipquery_service.h>
#include <blockworlds/components/vpndetection/services/proxycheck_service.h>
#include <blockworlds/components/vpndetection/services/vpnapi_service.h>
#include <blockworlds/components/vpndetection/vpn_cache.h>
#include <blockworlds/components/vpndetection/vpn_service_queue.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
	std::shared_ptr<CVpnServiceResult> MakeResult(const char *pIp, const char *pService, int64_t Timestamp, bool Bad = false)
	{
		auto pResult = std::make_shared<CVpnServiceResult>();
		pResult->m_IpAddress = pIp;
		pResult->m_ServiceName = pService;
		pResult->m_Timestamp = Timestamp;
		pResult->m_IsValid = true;
		pResult->m_IsBadIP = Bad;
		pResult->m_RiskScore = Bad ? 100 : 0;
		return pResult;
	}

	struct CTestVpnRequest : IVpnServiceRequest
	{
		std::string m_ServiceName;
		std::string m_IpAddress;
		int m_ClientId;

		CTestVpnRequest(const char *pServiceName, const char *pIpAddress, int ClientId) :
			m_ServiceName(pServiceName),
			m_IpAddress(pIpAddress),
			m_ClientId(ClientId)
		{
		}

		const char *GetServiceName() const override { return m_ServiceName.c_str(); }
		const char *GetIpAddress() const override { return m_IpAddress.c_str(); }
		int GetClientId() const override { return m_ClientId; }
		std::shared_ptr<IVpnServiceResult> Execute() override { return nullptr; }
	};

	std::shared_ptr<CTestVpnRequest> MakeRequest(const char *pIp, int ClientId)
	{
		return std::make_shared<CTestVpnRequest>("ipquery", pIp, ClientId);
	}
} // namespace

TEST(VpnCache, TtlRejectsExpiredAndLegacyTickTimestamps)
{
	CVpnCache Cache;
	Cache.SetTtlDays(14);

	Cache.Add(MakeResult("8.8.8.8", "ipquery", time_timestamp()));
	EXPECT_NE(Cache.Get("8.8.8.8", "ipquery"), nullptr);

	Cache.Add(MakeResult("8.8.4.4", "ipquery", time_timestamp() - 15 * 24 * 60 * 60));
	EXPECT_EQ(Cache.Get("8.8.4.4", "ipquery"), nullptr);

	Cache.Add(MakeResult("1.1.1.1", "ipquery", 12345));
	EXPECT_EQ(Cache.Get("1.1.1.1", "ipquery"), nullptr);
}

TEST(VpnCache, GetAllForIpOnlyReturnsFreshEntries)
{
	CVpnCache Cache;
	Cache.SetTtlDays(14);
	Cache.Add(MakeResult("8.8.8.8", "ipquery", time_timestamp()));
	Cache.Add(MakeResult("8.8.8.8", "proxycheck", time_timestamp() - 20 * 24 * 60 * 60));
	Cache.Add(MakeResult("8.8.8.8", "iplocate", time_timestamp()));

	std::vector<std::shared_ptr<IVpnServiceResult>> Results;
	Cache.GetAllForIP("8.8.8.8", Results);
	EXPECT_EQ(Results.size(), 2u);
}

TEST(VpnQueue, RejectsDuplicateAndRespectsMaxSize)
{
	SVpnServiceQueue Queue("ipquery", 100);
	EXPECT_TRUE(Queue.EnqueueRequest(MakeRequest("8.8.8.8", 1), 2));
	EXPECT_FALSE(Queue.EnqueueRequest(MakeRequest("8.8.8.8", 1), 2));
	EXPECT_TRUE(Queue.EnqueueRequest(MakeRequest("8.8.4.4", 2), 2));
	EXPECT_FALSE(Queue.EnqueueRequest(MakeRequest("1.1.1.1", 3), 2));
	EXPECT_EQ(Queue.GetQueueSize(), 2);
}

TEST(VpnQueue, RemovesQueuedRequestsForDroppedClient)
{
	SVpnServiceQueue Queue("ipquery", 100);
	EXPECT_TRUE(Queue.EnqueueRequest(MakeRequest("8.8.8.8", 1), 10));
	EXPECT_TRUE(Queue.EnqueueRequest(MakeRequest("8.8.4.4", 2), 10));
	EXPECT_TRUE(Queue.EnqueueRequest(MakeRequest("1.1.1.1", 1), 10));

	EXPECT_EQ(Queue.RemoveRequestsForClient(1), 2);
	ASSERT_EQ(Queue.GetQueueSize(), 1);
	EXPECT_STREQ(Queue.DequeueRequest()->GetIpAddress(), "8.8.4.4");
}

TEST(VpnServices, IPQueryParsesNestedRiskObject)
{
	CIPQueryService Service;
	auto Result = Service.ParseResponse("1.1.1.1",
		"{\"ip\":\"1.1.1.1\",\"isp\":{\"asn\":\"AS13335\",\"isp\":\"Cloudflare\"},\"risk\":{\"is_vpn\":false,\"is_proxy\":false,\"is_tor\":false,\"is_datacenter\":true,\"risk_score\":42}}",
		200);
	ASSERT_TRUE(Result->IsValid());
	EXPECT_TRUE(Result->IsBadIP());
	EXPECT_EQ(Result->GetRiskScore(), 42);
	EXPECT_STREQ(Result->GetAsn(), "AS13335");
}

TEST(VpnServices, GetIPIntelParsesProbability)
{
	CGetIPIntelService Service;
	Service.SetContactEmail("admin@example.com");
	Service.SetThreshold(0.90f);
	auto Result = Service.ParseResponse("8.8.8.8", "{\"result\":\"0.95\",\"status\":\"success\",\"asn\":\"AS15169\",\"country\":\"US\"}", 200);
	ASSERT_TRUE(Result->IsValid());
	EXPECT_TRUE(Result->IsBadIP());
	EXPECT_EQ(Result->GetRiskScore(), 95);
}

TEST(VpnServices, IPHubParsesBlock)
{
	CIPHubService Service;
	auto Result = Service.ParseResponse("8.8.8.8", "{\"ip\":\"8.8.8.8\",\"asn\":15169,\"isp\":\"GOOGLE\",\"block\":1}", 200);
	ASSERT_TRUE(Result->IsValid());
	EXPECT_TRUE(Result->IsBadIP());
	EXPECT_EQ(Result->GetRiskScore(), 100);
	EXPECT_STREQ(Result->GetAsn(), "AS15169");
}

TEST(VpnServices, IPLocateParsesPrivacy)
{
	CIPLocateService Service;
	auto Result = Service.ParseResponse("8.8.8.8",
		"{\"asn\":{\"asn\":\"AS15169\",\"name\":\"Google LLC\"},\"privacy\":{\"is_vpn\":true,\"is_proxy\":false,\"is_tor\":false,\"is_hosting\":true,\"is_anonymous\":true}}",
		200);
	ASSERT_TRUE(Result->IsValid());
	EXPECT_TRUE(Result->IsBadIP());
	EXPECT_STREQ(Result->GetIsp(), "Google LLC");
}

TEST(VpnServices, ProxyCheckParsesIpObject)
{
	CProxyCheckService Service;
	auto Result = Service.ParseResponse("8.8.8.8",
		"{\"status\":\"ok\",\"8.8.8.8\":{\"proxy\":\"yes\",\"vpn\":\"yes\",\"type\":\"VPN\",\"risk\":90,\"asn\":\"AS15169\",\"provider\":\"Google\"}}",
		200);
	ASSERT_TRUE(Result->IsValid());
	EXPECT_TRUE(Result->IsBadIP());
	EXPECT_EQ(Result->GetRiskScore(), 90);
}

TEST(VpnServices, VpnApiParsesSecurity)
{
	CVpnApiService Service;
	auto Result = Service.ParseResponse("8.8.8.8",
		"{\"security\":{\"vpn\":false,\"proxy\":true,\"tor\":false,\"relay\":false},\"network\":{\"autonomous_system_number\":\"AS15169\",\"autonomous_system_organization\":\"Google\"}}",
		200);
	ASSERT_TRUE(Result->IsValid());
	EXPECT_TRUE(Result->IsBadIP());
	EXPECT_STREQ(Result->GetAsn(), "AS15169");
}

TEST(VpnServices, TrimsConfigValuesBeforeBuildingEndpoints)
{
	char aVpnApiKey[] = " abc123 \t";
	CVpnApiService VpnApi;
	VpnApi.SetApiKeyPtr(aVpnApiKey);
	EXPECT_TRUE(VpnApi.IsConfigured());
	EXPECT_EQ(VpnApi.GetEndpoint("8.8.8.8"), "https://vpnapi.io/api/8.8.8.8?key=abc123");

	CGetIPIntelService GetIPIntel;
	GetIPIntel.SetContactEmail(" admin@example.com \n");
	GetIPIntel.SetFlags(" b ");
	GetIPIntel.SetOutputFlags(" b ");
	EXPECT_TRUE(GetIPIntel.IsConfigured());
	EXPECT_EQ(GetIPIntel.GetEndpoint("8.8.8.8"), "http://check.getipintel.net/check.php?ip=8.8.8.8&contact=admin@example.com&flags=b&oflags=b&format=json");
}

TEST(VpnServices, RateLimitedResponsesAreInvalid)
{
	CProxyCheckService Service;
	auto Result = Service.ParseResponse("8.8.8.8", "{}", 429);
	EXPECT_FALSE(Result->IsValid());
	EXPECT_EQ(Result->GetErrorCode(), -429);
}
