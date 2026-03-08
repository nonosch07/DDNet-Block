
CREATE TABLE IF NOT EXISTS `Blockworlds_accounts_autologin` (
  `account_id` int(11) NOT NULL,
  `token_hash` char(64) NOT NULL,
  `ip_hash`    char(64) NOT NULL,
  `expires_at` timestamp NOT NULL,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

ALTER TABLE `Blockworlds_accounts_autologin`
  ADD KEY IF NOT EXISTS `ix_autologin_lookup` (`token_hash`, `ip_hash`);

ALTER TABLE `Blockworlds_accounts_autologin`
  DROP FOREIGN KEY IF EXISTS `fk_autologin_core`;
ALTER TABLE `Blockworlds_accounts_autologin`
  ADD CONSTRAINT `fk_autologin_core`
    FOREIGN KEY (`account_id`)
    REFERENCES `Blockworlds_accounts_core` (`id`)
    ON DELETE CASCADE;

ALTER TABLE `Blockworlds_accounts_progress`
  ADD COLUMN IF NOT EXISTS `autologin_enabled` int(11) NOT NULL DEFAULT 1;
