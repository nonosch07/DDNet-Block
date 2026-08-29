-- Expiry of a VIP bought in the shop, as a UNIX timestamp.
-- 0 means it never expires, which is what an admin-granted VIP keeps, so the
-- expiry tick leaves those alone.
ALTER TABLE `Block_accounts_inventory` ADD COLUMN `vip_until` bigint(20) NOT NULL DEFAULT 0;
