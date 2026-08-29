// Buying a week of VIP in the shop.
//
// The rules that matter: it costs blockpoints, it cannot be stacked, it runs on
// wall-clock time so a week is a week whether or not the player logs in, and an
// admin-granted VIP (which has no expiry) must never be taken away by the
// expiry tick.

#include <block/accounts.h>
#include <block/common.h>
#include <block/cosmetics/cosmetics.h>
#include <block/player.h>
#include <block/shop/storemanager.h>
#include <gtest/gtest.h>

// a bare CBlockPlayer is enough: the VIP rules are pure account state
class BlockVip : public ::testing::Test // NOLINT(readability-identifier-naming)
{
public:
	CBlockPlayer m_Block;
	static const long long NOW = 1000000;
};

TEST_F(BlockVip, BuyingGrantsExactlyOneWeek)
{
	EXPECT_FALSE(m_Block.HasVip());

	m_Block.GrantTimedVip(VIP_WEEK_SECONDS, NOW);

	EXPECT_TRUE(m_Block.HasVip());
	EXPECT_EQ(m_Block.GetPlayerVipUntil(), NOW + VIP_WEEK_SECONDS);
	EXPECT_EQ(VIP_WEEK_SECONDS, 7 * 24 * 60 * 60);
}

TEST_F(BlockVip, ItExpiresOnceTheWeekIsUp)
{
	m_Block.GrantTimedVip(VIP_WEEK_SECONDS, NOW);

	// one second before the end it is still active
	EXPECT_FALSE(m_Block.ExpireVipIfDue(NOW + VIP_WEEK_SECONDS - 1));
	EXPECT_TRUE(m_Block.HasVip());

	EXPECT_TRUE(m_Block.ExpireVipIfDue(NOW + VIP_WEEK_SECONDS));
	EXPECT_FALSE(m_Block.HasVip());
	EXPECT_EQ(m_Block.GetPlayerVipUntil(), 0) << "the expiry must be cleared too, or it re-expires every tick";

	// and it only reports the expiry once
	EXPECT_FALSE(m_Block.ExpireVipIfDue(NOW + VIP_WEEK_SECONDS + 1));
}

TEST_F(BlockVip, AnAdminGrantedVipNeverExpires)
{
	// what `vip_player` does: VIP with no expiry
	m_Block.SetPlayerVip(1);
	ASSERT_EQ(m_Block.GetPlayerVipUntil(), 0);

	EXPECT_FALSE(m_Block.ExpireVipIfDue(NOW + 10 * VIP_WEEK_SECONDS));
	EXPECT_TRUE(m_Block.HasVip()) << "an admin VIP was taken away by the expiry tick";
}

TEST_F(BlockVip, BuyingAgainExtendsInsteadOfLosingTheRemainder)
{
	m_Block.GrantTimedVip(VIP_WEEK_SECONDS, NOW);
	// halfway through, a second week is added on top of what is left
	const long long Halfway = NOW + VIP_WEEK_SECONDS / 2;
	m_Block.GrantTimedVip(VIP_WEEK_SECONDS, Halfway);

	EXPECT_EQ(m_Block.GetPlayerVipUntil(), NOW + 2 * VIP_WEEK_SECONDS)
		<< "the remaining time was thrown away instead of being extended";
}

TEST_F(BlockVip, BuyingAfterItLapsedStartsAFreshWeek)
{
	m_Block.GrantTimedVip(VIP_WEEK_SECONDS, NOW);
	ASSERT_TRUE(m_Block.ExpireVipIfDue(NOW + VIP_WEEK_SECONDS));

	const long long Later = NOW + 100 * VIP_WEEK_SECONDS;
	m_Block.GrantTimedVip(VIP_WEEK_SECONDS, Later);
	EXPECT_EQ(m_Block.GetPlayerVipUntil(), Later + VIP_WEEK_SECONDS)
		<< "an expired VIP must not extend from its old, long-past expiry";
}

TEST(BlockVipShop, TheWeekCostsFifteenHundredAndHasNoLevelGate)
{
	CCosmeticsHandler Cosmetics;
	int Price = 0, Level = -1;
	vec2 PreviewPos;
	ASSERT_TRUE(Cosmetics.ShopInfoUtility(CCosmeticsHandler::UTILITY_VIP_WEEK, Price, Level, PreviewPos));
	EXPECT_EQ(Price, 1500);
	EXPECT_EQ(Level, 0);
}

TEST(BlockVipShop, TheItemIsNamedRatherThanFallingBackToUtilityItem)
{
	EXPECT_STREQ(CShop::UtilityName(CCosmeticsHandler::UTILITY_VIP_WEEK), "VIP (1 week)");
	// the shop menu and the purchase flow must agree on every utility name
	EXPECT_STREQ(CShop::UtilityName(CCosmeticsHandler::UTILITY_WEAPONKIT), "Weapon Kit");
	EXPECT_STREQ(CShop::UtilityName(CCosmeticsHandler::UTILITY_PASSIVE_REMOVER), "Passive Remover");
}
