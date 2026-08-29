-- Block_accounts_inventory.passive_removers
--
-- schema.sql and the account code (CAccounts::LoginThread / SaveThread) have
-- expected this column for a while, but no migration ever shipped for it, so
-- databases created before it was added fail every login with
--   Unknown column 'i.passive_removers' in 'SELECT'
--
-- Safe to run more than once only if the column is absent; MySQL/MariaDB will
-- report "Duplicate column name" if it is already there, which can be ignored.

ALTER TABLE `Block_accounts_inventory` ADD COLUMN `passive_removers` int(11) NOT NULL DEFAULT 0;
