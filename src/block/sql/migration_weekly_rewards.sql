ALTER TABLE `Block_accounts_progress` ADD COLUMN `weekly_day` int(11) DEFAULT 0;
ALTER TABLE `Block_accounts_progress` ADD COLUMN `weekly_last_claim` int(11) DEFAULT 0;
ALTER TABLE `Block_accounts_progress` ADD COLUMN `weekly_exp_boost_until` bigint(20) DEFAULT 0;
