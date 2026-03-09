-- phpMyAdmin SQL Dump
-- version 5.2.2deb1
-- https://www.phpmyadmin.net/
--
-- Hôte : localhost:3306
-- Généré le : ven. 24 oct. 2025 à 15:41
-- Version du serveur : 11.8.3-MariaDB-0+deb13u1 from Debian
-- Version de PHP : 8.4.11

SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
START TRANSACTION;
SET time_zone = "+00:00";


/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8mb4 */;

--
-- Base de données : `testdb`
--

-- --------------------------------------------------------

--
-- Structure de la table `Blockworlds_accounts_busy`
--

CREATE TABLE `Blockworlds_accounts_busy` (
  `account_id` int(11) NOT NULL,
  `server_id` varchar(32) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Structure de la table `Blockworlds_accounts_core`
--

CREATE TABLE `Blockworlds_accounts_core` (
  `id` int(11) NOT NULL,
  `name` varchar(11) NOT NULL,
  `password` varchar(256) NOT NULL,
  `address` varchar(47) DEFAULT '0.0.0.0',
  `registerdate` timestamp NOT NULL DEFAULT current_timestamp(),
  `last_name` varchar(16) DEFAULT 'nameless tee',
  `last_skin` varchar(32) DEFAULT 'default',
  `last_body_color` int(11) DEFAULT 0,
  `last_feet_color` int(11) DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- --------------------------------------------------------

--
-- Structure de la table `Blockworlds_accounts_inventory`
--

CREATE TABLE `Blockworlds_accounts_inventory` (
  `account_id` int(11) NOT NULL,
  `vip` int(11) DEFAULT 0,
  `pages` int(11) DEFAULT 0,
  `weaponkits` int(11) DEFAULT 0,
  `knockouts` varchar(256) DEFAULT '00000000000',
  `gundesign` varchar(256) DEFAULT '0000000000',
  `skinmani` varchar(256) DEFAULT '0000000000'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- --------------------------------------------------------

--
-- Structure de la table `Blockworlds_accounts_progress`
--

CREATE TABLE `Blockworlds_accounts_progress` (
  `account_id` int(11) NOT NULL,
  `level` int(11) DEFAULT 1,
  `experience` int(11) DEFAULT 0,
  `ranking` int(11) DEFAULT 0,
  `clanID` int(11) DEFAULT 0,
  `auth_level` int(11) DEFAULT 0,
  `blockpoints` int(11) DEFAULT 0,
  `passive` int(11) DEFAULT 0,
  `kills` int(11) DEFAULT 0,
  `deaths` int(11) DEFAULT 0,
  `tourney_win` int(11) DEFAULT 0,
  `playtime` bigint(20) DEFAULT 0,
  `killstreak` int(11) DEFAULT 0,
  `weekly_day` int(11) DEFAULT 0,
  `weekly_last_claim` int(11) DEFAULT 0,
  `weekly_exp_boost_until` bigint(20) DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- --------------------------------------------------------

--
-- Structure de la table `Blockworlds_accounts_ranked`
--

CREATE TABLE `Blockworlds_accounts_ranked` (
  `account_id` int(11) NOT NULL,
  `ranked_games` int(11) DEFAULT 0,
  `ranked_kills` int(11) DEFAULT 0,
  `ranked_deaths` int(11) DEFAULT 0,
  `ranked_wins` int(11) DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- --------------------------------------------------------

--
-- Structure de la table `Blockworlds_clans`
--

CREATE TABLE `Blockworlds_clans` (
  `id` int(11) NOT NULL,
  `name` varchar(32) NOT NULL,
  `level` int(11) DEFAULT 1,
  `experience` int(11) DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

--
-- Index pour les tables déchargées
--

--
-- Index pour la table `Blockworlds_accounts_busy`
--
ALTER TABLE `Blockworlds_accounts_busy`
  ADD UNIQUE KEY `ux_busy_account` (`account_id`);

--
-- Index pour la table `Blockworlds_accounts_core`
--
ALTER TABLE `Blockworlds_accounts_core`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `ux_accounts_core_name` (`name`);

--
-- Index pour la table `Blockworlds_accounts_inventory`
--
ALTER TABLE `Blockworlds_accounts_inventory`
  ADD PRIMARY KEY (`account_id`);

--
-- Index pour la table `Blockworlds_accounts_progress`
--
ALTER TABLE `Blockworlds_accounts_progress`
  ADD PRIMARY KEY (`account_id`),
  ADD KEY `ix_progress_level` (`level`),
  ADD KEY `ix_progress_blockpoints` (`blockpoints`),
  ADD KEY `ix_progress_killstreak` (`killstreak`),
  ADD KEY `ix_progress_ranking` (`ranking`),
  ADD KEY `ix_progress_clan_auth` (`clanID`,`auth_level`);

--
-- Index pour la table `Blockworlds_accounts_ranked`
--
ALTER TABLE `Blockworlds_accounts_ranked`
  ADD PRIMARY KEY (`account_id`),
  ADD KEY `ix_ranked_wins` (`ranked_wins`),
  ADD KEY `ix_ranked_kills` (`ranked_kills`);

--
-- Index pour la table `Blockworlds_clans`
--
ALTER TABLE `Blockworlds_clans`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `ux_clans_name` (`name`);

--
-- AUTO_INCREMENT pour les tables déchargées
--

--
-- AUTO_INCREMENT pour la table `Blockworlds_accounts_core`
--
ALTER TABLE `Blockworlds_accounts_core`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT pour la table `Blockworlds_clans`
--
ALTER TABLE `Blockworlds_clans`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- Contraintes pour les tables déchargées
--

--
-- Contraintes pour la table `Blockworlds_accounts_busy`
--
ALTER TABLE `Blockworlds_accounts_busy`
  ADD CONSTRAINT `fk_busy_core` FOREIGN KEY (`account_id`) REFERENCES `Blockworlds_accounts_core` (`id`) ON DELETE CASCADE;

--
-- Contraintes pour la table `Blockworlds_accounts_inventory`
--
ALTER TABLE `Blockworlds_accounts_inventory`
  ADD CONSTRAINT `fk_acc_inv_core` FOREIGN KEY (`account_id`) REFERENCES `Blockworlds_accounts_core` (`id`) ON DELETE CASCADE;

--
-- Contraintes pour la table `Blockworlds_accounts_progress`
--
ALTER TABLE `Blockworlds_accounts_progress`
  ADD CONSTRAINT `fk_acc_prog_core` FOREIGN KEY (`account_id`) REFERENCES `Blockworlds_accounts_core` (`id`) ON DELETE CASCADE;

--
-- Contraintes pour la table `Blockworlds_accounts_ranked`
--
ALTER TABLE `Blockworlds_accounts_ranked`
  ADD CONSTRAINT `fk_acc_ranked_core` FOREIGN KEY (`account_id`) REFERENCES `Blockworlds_accounts_core` (`id`) ON DELETE CASCADE;
COMMIT;

/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
