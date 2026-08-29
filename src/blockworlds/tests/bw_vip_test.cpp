// Buying a week of VIP in the shop.
//
// The rules that matter: it costs blockpoints, it cannot be stacked, it runs on
// wall-clock time so a week is a week whether or not the player logs in, and an
// admin-granted VIP (which has no expiry) must never be taken away by the
// expiry tick.

#include <blockworlds/accounts.h>
#include <blockworlds/bw_player.h>
#include <blockworlds/common.h>
#include <blockworlds/cosmetics/cosmetics.h>
#include <blockworlds/shop/storemanager.h>
#include <gtest/gtest.h>

// a bare CBwPlayer is enough: the VIP rules are pure account state
class BwVip : public ::testing::Test // NOLINT(readability-identifier-naming)
{
public:
	CBwPlayer m_Bw;
	static const long long NOW = 1000000;
};

TEST_F(BwVip, BuyingGrantsExactlyOneWeek)
{
	EXPECT_FALSE(m_Bw.HasVip());

	m_Bw.GrantTimedVip(VIP_WEEK_SECONDS, NOW);

	EXPECT_TRUE(m_Bw.HasVip());
	EXPECT_EQ(m_Bw.GetPlayerVipUntil(), NOW + VIP_WEEK_SECONDS);
	EXPECT_EQ(VIP_WEEK_SECONDS, 7 * 24 * 60 * 60);
}

TEST_F(BwVip, ItExpiresOnceTheWeekIsUp)
{
	m_Bw.GrantTimedVip(VIP_WEEK_SECONDS, NOW);

	// one second before the end it is still active
	EXPECT_FALSE(m_Bw.ExpireVipIfDue(NOW + VIP_WEEK_SECONDS - 1));
	EXPECT_TRUE(m_Bw.HasVip());

	EXPECT_TRUE(m_Bw.ExpireVipIfDue(NOW + VIP_WEEK_SECONDS));
	EXPECT_FALSE(m_Bw.HasVip());
	EXPECT_EQ(m_Bw.GetPlayerVipUntil(), 0) << "the expiry must be cleared too, or it re-expires every tick";

	// and it only reports the expiry once
	EXPECT_FALSE(m_Bw.ExpireVipIfDue(NOW + VIP_WEEK_SECONDS + 1));
}

TEST_F(BwVip, AnAdminGrantedVipNeverExpires)
{
	// what `vip_player` does: VIP with no expiry
	m_Bw.SetPlayerVip(1);
	ASSERT_EQ(m_Bw.GetPlayerVipUntil(), 0);

	EXPECT_FALSE(m_Bw.ExpireVipIfDue(NOW + 10 * VIP_WEEK_SECONDS));
	EXPECT_TRUE(m_Bw.HasVip()) << "an admin VIP was taken away by the expiry tick";
}

TEST_F(BwVip, BuyingAgainExtendsInsteadOfLosingTheRemainder)
{
	m_Bw.GrantTimedVip(VIP_WEEK_SECONDS, NOW);
	// halfway through, a second week is added on top of what is left
	const long long Halfway = NOW + VIP_WEEK_SECONDS / 2;
	m_Bw.GrantTimedVip(VIP_WEEK_SECONDS, Halfway);

	EXPECT_EQ(m_Bw.GetPlayerVipUntil(), NOW + 2 * VIP_WEEK_SECONDS)
		<< "the remaining time was thrown away instead of being extended";
}

TEST_F(BwVip, BuyingAfterItLapsedStartsAFreshWeek)
{
	m_Bw.GrantTimedVip(VIP_WEEK_SECONDS, NOW);
	ASSERT_TRUE(m_Bw.ExpireVipIfDue(NOW + VIP_WEEK_SECONDS));

	const long long Later = NOW + 100 * VIP_WEEK_SECONDS;
	m_Bw.GrantTimedVip(VIP_WEEK_SECONDS, Later);
	EXPECT_EQ(m_Bw.GetPlayerVipUntil(), Later + VIP_WEEK_SECONDS)
		<< "an expired VIP must not extend from its old, long-past expiry";
}

TEST(BwVipShop, TheWeekCostsFifteenHundredAndHasNoLevelGate)
{
	CCosmeticsHandler Cosmetics;
	int Price = 0, Level = -1;
	vec2 PreviewPos;
	ASSERT_TRUE(Cosmetics.ShopInfoUtility(CCosmeticsHandler::UTILITY_VIP_WEEK, Price, Level, PreviewPos));
	EXPECT_EQ(Price, 1500);
	EXPECT_EQ(Level, 0);
}

TEST(BwVipShop, TheItemIsNamedRatherThanFallingBackToUtilityItem)
{
	EXPECT_STREQ(CShop::UtilityName(CCosmeticsHandler::UTILITY_VIP_WEEK), "VIP (1 week)");
	// the shop menu and the purchase flow must agree on every utility name
	EXPECT_STREQ(CShop::UtilityName(CCosmeticsHandler::UTILITY_WEAPONKIT), "Weapon Kit");
	EXPECT_STREQ(CShop::UtilityName(CCosmeticsHandler::UTILITY_PASSIVE_REMOVER), "Passive Remover");
}
