-- Migration: Add weekly rewards columns to accounts_progress
-- Run this on the production database before deploying the new server version.
-- These ALTER TABLE statements are safe to run multiple times (will error harmlessly if columns already exist).

ALTER TABLE `Blockworlds_accounts_progress` ADD COLUMN `weekly_day` int(11) DEFAULT 0;
ALTER TABLE `Blockworlds_accounts_progress` ADD COLUMN `weekly_last_claim` int(11) DEFAULT 0;
ALTER TABLE `Blockworlds_accounts_progress` ADD COLUMN `weekly_exp_boost_until` bigint(20) DEFAULT 0;
