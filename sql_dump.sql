-- phpMyAdmin SQL Dump
-- version 5.2.1
-- https://www.phpmyadmin.net/
--
-- Host: 127.0.0.1
-- Generation Time: Mar 06, 2025 at 06:56 AM
-- Server version: 10.4.32-MariaDB
-- PHP Version: 8.0.30

SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
START TRANSACTION;
SET time_zone = "+00:00";

--
-- Database: `bw`
--

-- --------------------------------------------------------

--
-- Table structure for table `accounts`
--

CREATE TABLE `accounts` (
                            `id` int(11) NOT NULL,
                            `name` varchar(11) NOT NULL,
                            `password` varchar(256) NOT NULL,
                            `address` varchar(47) DEFAULT '0.0.0.0',
                            `is_logged_in` int(11) DEFAULT 0,
                            `vip` int(11) DEFAULT 0,
                            `pages` int(11) DEFAULT 0,
                            `level` int(11) DEFAULT 1,
                            `experience` int(11) DEFAULT 0,
                            `weaponkits` int(11) DEFAULT 0,
                            `ranking` int(11) DEFAULT 0,
                            `clanID` int(11) NOT NULL DEFAULT 0,
                            `auth_level` int(11) DEFAULT 0,
                            `blockpoints` int(11) DEFAULT 0,
                            `knockouts` varchar(256) DEFAULT '00000000000',
                            `gundesign` varchar(256) DEFAULT '0000000000',
                            `skinmani` varchar(256) DEFAULT '0000000000',
                            `extras` varchar(256) DEFAULT '00000000000000',
                            `registerdate` timestamp NOT NULL DEFAULT current_timestamp(),
                            `ranked_games` int(11) DEFAULT 0,
                            `ranked_kills` int(11) DEFAULT 0,
                            `ranked_deaths` int(11) DEFAULT 0,
                            `ranked_wins` int(11) DEFAULT 0,
                            `kills` int(11) DEFAULT 0,
                            `deaths` int(11) DEFAULT 0,
                            `tourney_win` int(11) DEFAULT 0,
                            `playtime` bigint(20) DEFAULT 0,
                            `killstreak` int(11) DEFAULT 0,
                            `last_name` varchar(16) DEFAULT 'nameless tee',
                            `last_skin` varchar(32) DEFAULT 'default',
                            `last_body_color` int(11) DEFAULT 0,
                            `last_feet_color` int(11) DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- --------------------------------------------------------

--
-- Table structure for table `accounts_busy`
--

CREATE TABLE `accounts_busy` (
                                 `account_id` int(11) NOT NULL,
                                 `server_id` varchar(32) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `clans`
--

CREATE TABLE `clans` (
                         `id` int(11) NOT NULL,
                         `name` varchar(32) NOT NULL,
                         `level` int(11) DEFAULT 1,
                         `experience` int(11) DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

--
-- Indexes for dumped tables
--

--
-- Indexes for table `accounts`
--
ALTER TABLE `accounts`
    ADD PRIMARY KEY (`id`);

--
-- Indexes for table `accounts_busy`
--
ALTER TABLE `accounts_busy`
    ADD UNIQUE KEY `account_id` (`account_id`);

--
-- Indexes for table `clans`
--
ALTER TABLE `clans`
    ADD PRIMARY KEY (`id`);

--
-- AUTO_INCREMENT for dumped tables
--

--
-- AUTO_INCREMENT for table `accounts`
--
ALTER TABLE `accounts`
    MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `clans`
--
ALTER TABLE `clans`
    MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- Constraints for dumped tables
--

--
-- Constraints for table `accounts_busy`
--
ALTER TABLE `accounts_busy`
    ADD CONSTRAINT `accounts_busy_account_id` FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE ON UPDATE CASCADE;
COMMIT;
