/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) ;
#endif

MACRO_CONFIG_STR(SvServerId, sv_server_id, 32, "unique_id", CFGFLAG_SERVER, "Unique identifier for the server instance. Mainly used for login state.")

MACRO_CONFIG_INT(SvAccountsystem, sv_account_system, 1, 0, 1, CFGFLAG_SERVER, "Toggle for enabling (1) or disabling (0) the account system.")
MACRO_CONFIG_INT(Sv1on1system, sv_1on1_system, 1, 0, 1, CFGFLAG_SERVER, "Toggle for enabling (1) or disabling (0) the one-on-one (1v1) system.")

// 1on1 invite configuration
MACRO_CONFIG_INT(Sv1on1InviteCooldown, sv_1on1_invite_cooldown, 10, 1, 3600, CFGFLAG_SERVER, "Cooldown time (in seconds) a player must wait between 1on1 invites.")
MACRO_CONFIG_INT(Sv1on1MaxOutstandingInvitesPerSender, sv_1on1_max_outstanding_invites_per_sender, 1, 1, 100, CFGFLAG_SERVER, "Maximum number of outstanding 1on1 invites a single player can have.")

MACRO_CONFIG_INT(SvBpTransferCooldown, sv_bp_transfer_cooldown, 30, 0, 3600, CFGFLAG_SERVER, "Cooldown time (in seconds) between initiating blockpoint transfer offers.")
MACRO_CONFIG_INT(SvBpTransferExpiry, sv_bp_transfer_expiry, 10, 5, 600, CFGFLAG_SERVER, "Expiry time (in seconds) for a pending blockpoint transfer offer.")
MACRO_CONFIG_INT(SvBpTransferAmountCap, sv_bp_transfer_amount_cap, 3500, 1, 100000, CFGFLAG_SERVER, "Maximum allowed blockpoints in a single transfer offer.")
MACRO_CONFIG_INT(SvBpTransferMaxOutstandingPerSender, sv_bp_transfer_max_outstanding_per_sender, 1, 1, 100, CFGFLAG_SERVER, "Maximum number of outstanding blockpoint transfer offers a player can have to others.")
MACRO_CONFIG_INT(SvBpTransferAmountMin, sv_bp_transfer_amount_min, 50, 1, 100000, CFGFLAG_SERVER, "Minimum allowed blockpoints in a single transfer offer.")

// daily blockpoint transfer caps (0 = disabled)
MACRO_CONFIG_INT(SvBpTransferDailyAmountCap, sv_bp_transfer_daily_amount_cap, 10000, 0, 100000000, CFGFLAG_SERVER, "Maximum total blockpoints a player can send in transfers per day (UTC); 0 disables the cap.")
MACRO_CONFIG_INT(SvBpTransferDailyCountCap, sv_bp_transfer_daily_count_cap, 5, 0, 100000000, CFGFLAG_SERVER, "Maximum number of transfers a player can complete per day (UTC); 0 disables the cap.")

MACRO_CONFIG_INT(Sv1on1InviteExpiry, sv_1on1_invite_expiry, 10, 5, 600, CFGFLAG_SERVER, "Expiry time (in seconds) for 1on1 invites.")
MACRO_CONFIG_INT(SvClanInviteCooldown, sv_clan_invite_cooldown, 60, 0, 3600, CFGFLAG_SERVER, "Cooldown time (in seconds) between clan invites from the same player.")
MACRO_CONFIG_INT(SvClanInviteExpiry, sv_clan_invite_expiry, 10, 5, 600, CFGFLAG_SERVER, "Expiry time (in seconds) for clan invites.")
MACRO_CONFIG_INT(SvClanInviteMaxOutstandingPerSender, sv_clan_invite_max_outstanding_per_sender, 5, 1, 100, CFGFLAG_SERVER, "Maximum number of outstanding clan invites a single player can have.")
MACRO_CONFIG_INT(SvShopRequestExpiry, sv_shop_request_expiry, 10, 5, 600, CFGFLAG_SERVER, "Expiry time (in seconds) for shop purchase requests.")

// Register/IP throttling configuration
MACRO_CONFIG_INT(SvRegisterCooldownPerIp, sv_register_cooldown_per_ip, 10, 0, 3600, CFGFLAG_SERVER, "Cooldown time (seconds) per IP between register attempts.")
MACRO_CONFIG_INT(SvRegisterIpAttemptWindow, sv_register_ip_attempt_window, 60, 1, 3600, CFGFLAG_SERVER, "Time window (seconds) in which register attempts per IP are counted.")
MACRO_CONFIG_INT(SvRegisterIpMaxAttempts, sv_register_ip_max_attempts, 5, 1, 100, CFGFLAG_SERVER, "Number of register attempts allowed per IP in the attempt window before temporary ban.")
MACRO_CONFIG_INT(SvRegisterIpBanSeconds, sv_register_ip_ban_seconds, 0, 0, 86400, CFGFLAG_SERVER, "Duration (seconds) to ban an IP after exceeding register attempts.")

// Experience and block-related settings:
MACRO_CONFIG_INT(SvBlockExperienceMultiplierStacking, sv_block_expirience_multiplier_stacking, 1, 0, NUM_EXP_CALC_METHODS - 1, CFGFLAG_SERVER, "0-highest, 1-additive, 2-logarithmic, 3-multiplicative")
MACRO_CONFIG_INT(SvBlockExperience, sv_block_experience, 1, 0, 0, CFGFLAG_SERVER, "Experience points awarded for each registered block.")
MACRO_CONFIG_INT(SvBlockFreezedInterval, sv_block_freezed, 3, 0, 0, CFGFLAG_SERVER, "Duration (in seconds) a player must remain frozen to be considered for blocking.")
MACRO_CONFIG_INT(SvBlockResetUnfreezedInterval, sv_block_reset_unfreezed, 3, 0, 0, CFGFLAG_SERVER, "Time (in seconds) a player must remain unfrozen before losing the enemy’s block impact.")
MACRO_CONFIG_INT(SvBlockResetNoImpactInterval, sv_block_reset_no_impact, 2, 0, 0, CFGFLAG_SERVER, "Time (in seconds) after unfreezing during which an enemy’s impact is still counted.")
MACRO_CONFIG_INT(SvBlockImpactIntervalToResist, sv_block_impact_interval_to_resist, 5, 0, 0, CFGFLAG_SERVER, "Duration (in seconds) enemy impact is registered before resistance can be applied.")
MACRO_CONFIG_INT(SvBlockUnfreezeNoImpactInterval, sv_block_unfreeze_no_impact_interval, 1, 0, 0, CFGFLAG_SERVER, "Time threshold (in seconds) for a recent impact to count towards blocking after unfreeze.")
MACRO_CONFIG_INT(SvBlockKillInterval, sv_block_kill_interval, 5, 0, 0, CFGFLAG_SERVER, "Time (in seconds) since a player's death during which blocking is still in effect.")
MACRO_CONFIG_INT(SvKillStreakCount, sv_kill_streak_count, 5, 1, 10, CFGFLAG_SERVER, "Number of consecutive kills required to register a kill streak.")
MACRO_CONFIG_INT(SvAllowExpFromSameIp, sv_allow_exp_from_same_ip, 0, 0, 1, CFGFLAG_SERVER, "Enable (1) or disable (0) awarding experience points for players connecting from the same IP. (Recommended: off for production.)")

MACRO_CONFIG_INT(SvIgnoreAfkKills, sv_ignore_afk_kills, 1, 0, 1, CFGFLAG_SERVER, "When enabled (1), kills of AFK or paused players will not award experience or killstreaks.")
MACRO_CONFIG_INT(SvIgnoreClanmateKills, sv_ignore_clanmate_kills, 1, 0, 1, CFGFLAG_SERVER, "When enabled (1), kills between players of the same clan will not award experience or killstreaks.")
MACRO_CONFIG_INT(SvBlockMinAliveTime, sv_block_min_alive_time, 20, 1, 120, CFGFLAG_SERVER, "Minimum time (in seconds) a player must stay alive to earn experience.")
MACRO_CONFIG_INT(SvBlockInterval, sv_block_interval, 0, 0, 1, CFGFLAG_SERVER, "Interval (in seconds) between two countable fights.")

// Anti-farming / PvP EXP integrity settings
MACRO_CONFIG_INT(SvMinActivePlayersForExp, sv_min_active_players_for_exp, 8, 0, MAX_CLIENTS, CFGFLAG_SERVER, "Minimum number of active (non-afk, non-spec, playing) players required to award PvP/block EXP")
MACRO_CONFIG_INT(SvExpTargetFullPlayers, sv_exp_target_full_players, 8, 0, MAX_CLIENTS, CFGFLAG_SERVER, "Player count at which population scaling reaches 100% EXP")
MACRO_CONFIG_INT(SvExpVictimRecentActionSec, sv_exp_victim_recent_action_sec, 6, 0, 120, CFGFLAG_SERVER, "Victim must have acted within this many seconds to grant EXP")
MACRO_CONFIG_INT(SvExpKillerRecentActionSec, sv_exp_killer_recent_action_sec, 4, 0, 120, CFGFLAG_SERVER, "Killer must have acted within this many seconds to receive EXP")
MACRO_CONFIG_INT(SvExpMaxSameVictim, sv_exp_max_same_victim, 3, 1, 50, CFGFLAG_SERVER, "Maximum EXP-granting kills on the same victim within the configured window")
MACRO_CONFIG_INT(SvExpSameVictimWindowSec, sv_exp_same_victim_window_sec, 180, 10, 86400, CFGFLAG_SERVER, "Time window (seconds) for counting repeated kills on the same victim")
MACRO_CONFIG_INT(SvExpMinUniqueRatioPercent, sv_exp_min_unique_ratio_percent, 40, 0, 100, CFGFLAG_SERVER, "Minimum unique victim ratio (percent) in recent kills before applying diversity penalty")
MACRO_CONFIG_INT(SvExpLoopDetectionWindowSec, sv_exp_loop_detection_window_sec, 90, 5, 3600, CFGFLAG_SERVER, "Window (seconds) to inspect for reciprocal kill loop patterns")
MACRO_CONFIG_INT(SvExpLoopMinAlternations, sv_exp_loop_min_alternations, 3, 1, 20, CFGFLAG_SERVER, "Minimum A-B-A-B alternations to flag a farming loop")
MACRO_CONFIG_INT(SvExpLevelDiffSoftCap, sv_exp_level_diff_soft_cap, 6, 0, 100, CFGFLAG_SERVER, "Level difference after which EXP starts decaying for high-level killers vs low-level victims")
MACRO_CONFIG_INT(SvExpLevelDiffDecayKPercent, sv_exp_level_diff_decay_k_percent, 400, 1, 10000, CFGFLAG_SERVER, "Decay constant (percent) used in exp(-Δ/K) scaling (K = value/100)")
MACRO_CONFIG_INT(SvExpDailySoftCap, sv_exp_daily_soft_cap, 5000, 0, 1000000, CFGFLAG_SERVER, "Daily soft cap for PvP EXP before diminishing returns")
MACRO_CONFIG_INT(SvExpMinSessionMinutes, sv_exp_min_session_minutes, 3, 0, 120, CFGFLAG_SERVER, "Minimum connection time (minutes) for both players to allow EXP")
MACRO_CONFIG_INT(SvExpMaxAfkVictimRatioPercent, sv_exp_max_afk_victim_ratio_percent, 30, 0, 100, CFGFLAG_SERVER, "Maximum percent of recent kills on AFK victims before EXP suppression")
MACRO_CONFIG_INT(SvDebugAntifarm, sv_debug_antifarm, 0, 0, 1, CFGFLAG_SERVER, "Enable verbose anti-farm debug messages to killers (1=on)")

// weekend EXP bonus (server-local time)
MACRO_CONFIG_INT(SvWeekendExpEnabled, sv_weekend_exp_enabled, 1, 0, 1, CFGFLAG_SERVER, "Enable weekend EXP bonus (Saturday/Sunday)")
MACRO_CONFIG_INT(SvWeekendExpMultiplier, sv_weekend_exp_multiplier, 200, 100, 10000, CFGFLAG_SERVER, "Weekend EXP multiplier (percent), e.g. 200 = 2x")

// Deathnote settings:
MACRO_CONFIG_INT(SvDeathNoteCoolDown, sv_deathnote_cooldown, 600, 60, 3600, CFGFLAG_SERVER, "Cooldown time (in seconds) a player must wait before reusing the Deathnote.")

// Shop server setting:
MACRO_CONFIG_INT(SvShopServer, sv_shop_server, 0, 0, 1, CFGFLAG_SERVER, "Set to 1 if this is a shop server; otherwise, set to 0.")

// Clans settings:
MACRO_CONFIG_INT(SvClanMinLevel, sv_clan_min_level, 25, 0, 100, CFGFLAG_SERVER, "Minimum player level required to create a clan.")
MACRO_CONFIG_INT(SvClanSaveInterval, sv_clan_save_interval, 600, 1, 3600, CFGFLAG_SERVER, "Time delay (in seconds) between successive clan saves.")
MACRO_CONFIG_INT(SvClanMaxMembers, sv_clan_max_members, 25, 1, 1000, CFGFLAG_SERVER, "Maximum number of members per clan (including leaders & co-leaders).")
MACRO_CONFIG_INT(SvClanConfirmExpiry, sv_clan_confirm_expiry, 15, 5, 600, CFGFLAG_SERVER, "Expiry time (in seconds) for clan confirmations like delete/kick/rename.")
MACRO_CONFIG_INT(SvClanCreatePrice, sv_clan_create_price, 500, 0, 1000000, CFGFLAG_SERVER, "Blockpoints cost to create a clan (0 = free).")
MACRO_CONFIG_INT(SvClanRenamePrice, sv_clan_rename_price, 5000, 0, 1000000, CFGFLAG_SERVER, "Blockpoints cost to rename a clan (0 = free).")

MACRO_CONFIG_INT(SvGroundHookPenaltyDelay, sv_ground_hook_penalty_delay, 5, 1, 999999, CFGFLAG_SERVER, "Seconds allowed to hook ground before freeze penalty is applied")
MACRO_CONFIG_INT(SvGroundHookPenalty, sv_ground_hook_penalty, 3, 1, 999999, CFGFLAG_SERVER, "Seconds the freeze penalty will last when applied to a player")

// LMB (last man blocking)
MACRO_CONFIG_INT(SvLMBInitialFreezeTime, sv_lmb_initial_freeze_time, 3, 0, 1000, CFGFLAG_SERVER, " Duration of character freeze after teleportation to the arena (seconds)")
MACRO_CONFIG_INT(SvLMBRegistrationTime, sv_lmb_registration_time, 60, 0, 1000, CFGFLAG_SERVER, "Duration of registration phase (seconds)")
MACRO_CONFIG_INT(SvLMBActiveTime, sv_lmb_active_time, 600, 0, 1000, CFGFLAG_SERVER, "Duration of active phase (seconds)")
MACRO_CONFIG_INT(SvLMBWinnerExpMultiplier, sv_lmb_winner_exp_multiplier, 200, 100, 10000, CFGFLAG_SERVER, "Exp multiplier for the winner (percents)")
MACRO_CONFIG_INT(SvLMBWinnerExpMultiplierDuration, sv_lmb_winner_exp_multiplier_duration, 5, 1, 60, CFGFLAG_SERVER, " Exp multiplier duration (minutes)") // make more precise?
MACRO_CONFIG_INT(SvLMBFreezeTimeout, sv_lmb_freeze_timeout, 5, 1, 50000, CFGFLAG_SERVER, "Seconds a player must remain frozen to be eliminated in LMB")
MACRO_CONFIG_INT(SvLMBBroadcastRate, sv_lmb_broadcast_rate, 10, 1, 500, CFGFLAG_SERVER, "Rate at which information broadcasts will be sent (ticks, 50 ticks = 1 second)") // not related to lmb?
MACRO_CONFIG_INT(SvLMBMinimumCandidates, sv_lmb_minimum_candidates, 8, 2, MAX_CLIENTS, CFGFLAG_SERVER, "Minimum amount of candidates to start the active phase")
MACRO_CONFIG_INT(SvLMBBlockpointsReward, sv_lmb_blockpoints_reward, 50, 1, 10000, CFGFLAG_SERVER, "Blockpoints reward for winning the LMB event")
MACRO_CONFIG_INT(SvLMBPagesReward, sv_lmb_pages_reward, 3, 1, 15, CFGFLAG_SERVER, "Pages reward for winning the LMB event")

// Weaponkits setting: persist whether weaponkits are allowed on this server
MACRO_CONFIG_INT(SvWeaponkitsAllowed, sv_weaponkits_allowed, 1, 0, 1, CFGFLAG_SERVER, "Allow (1) or disable (0) weaponkits on the server.")

MACRO_CONFIG_INT(SvWeaponkitsVoteCoolDown, sv_weaponkits_vote_cooldown, 15, 1, 60, CFGFLAG_SERVER, "Cooldown time (in minutes) a player must wait before reusing the weaponkits vote.")
MACRO_CONFIG_INT(SvEventVoteCoolDown, sv_lmb_vote_cooldown, 30, 1, 99999, CFGFLAG_SERVER, "Cooldown time (in minutes) a player must wait before starting an event vote (LMB/TDM).")

MACRO_CONFIG_INT(SvNoHammerOnUnfreeze, sv_no_hammer_on_unfreeze, 1, 0, 1, CFGFLAG_SERVER, "Prevent instant hammer fire immediately after unfreeze (1=on, 0=off)")

// TDM (Team Deathmatch)
MACRO_CONFIG_INT(SvTDMRegistrationTime, sv_tdm_registration_time, 30, 0, 1000, CFGFLAG_SERVER, "Duration of TDM registration phase (seconds)")
MACRO_CONFIG_INT(SvTDMActiveTime, sv_tdm_active_time, 600, 0, 3600, CFGFLAG_SERVER, "Duration of TDM active phase (seconds)")
MACRO_CONFIG_INT(SvTDMMinimumCandidates, sv_tdm_minimum_candidates, 8, 2, MAX_CLIENTS, CFGFLAG_SERVER, "Minimum amount of candidates to start TDM")
MACRO_CONFIG_INT(SvTDMMaximumCandidates, sv_tdm_maximum_candidates, 16, 2, MAX_CLIENTS, CFGFLAG_SERVER, "Maximum amount of participants for TDM")
MACRO_CONFIG_INT(SvTDMBroadcastRate, sv_tdm_broadcast_rate, 10, 1, 500, CFGFLAG_SERVER, "Rate at which TDM information broadcasts will be sent (ticks)")
MACRO_CONFIG_INT(SvTDMFreezeTime, sv_tdm_freeze_time, 3, 5, 30, CFGFLAG_SERVER, "Specifies the time required for a player to be frozen in order to die")
MACRO_CONFIG_INT(SvTDMFreezeTimeKill, sv_tdm_freeze_time_kill, 5, 5, 30, CFGFLAG_SERVER, "Specifies the time required for a frozen player to die")
MACRO_CONFIG_INT(SvTDMWinnerExp, sv_tdm_winner_exp, 5, 0, 10000, CFGFLAG_SERVER, "Experience rewarded to each winning TDM participant")
MACRO_CONFIG_INT(SvTDMWinnerBlockpoints, sv_tdm_winner_blockpoints, 25, 0, 100000, CFGFLAG_SERVER, "Blockpoints rewarded to winning TDM participants")

// 1on1 draw detection settings
MACRO_CONFIG_INT(Sv1on1DrawFreezeGrace, sv_1on1_draw_freeze_grace, 3, 0, 120, CFGFLAG_SERVER, "1on1: Grace period after round start before counting simultaneous freeze (seconds)")
MACRO_CONFIG_INT(Sv1on1DrawFreezeStalemate, sv_1on1_draw_freeze_stalemate, 5, 1, 300, CFGFLAG_SERVER, "1on1: Both players frozen this long (after grace) => draw (seconds)")
MACRO_CONFIG_INT(Sv1on1DrawDeathTickTolerance, sv_1on1_draw_death_tick_tolerance, 1, 0, 50, CFGFLAG_SERVER, "1on1: Max tick diff to treat dual death as draw")
MACRO_CONFIG_INT(Sv1on1DrawDeathExtendedWindow, sv_1on1_draw_death_extended_window, 50, 0, 2000, CFGFLAG_SERVER, "1on1: Extended tick window for dual death draw (ticks)")
MACRO_CONFIG_INT(Sv1on1BroadcastRate, sv_1on1_broadcast_rate, 10, 1, 500, CFGFLAG_SERVER, "Rate at which 1on1 score broadcasts will be sent (ticks, 50 ticks = 1 second)")

MACRO_CONFIG_INT(SvPasswordPbkdf2Iter, sv_password_pbkdf2_iter, 120000, 10000, 2000000, CFGFLAG_SERVER, "PBKDF2 iteration count for account password hashing")

// Discord integration
MACRO_CONFIG_INT(SvDiscordEnabled, sv_discord_enabled, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Enable Discord webhook sending")
MACRO_CONFIG_STR(SvDiscordWebhookUsername, sv_discord_webhook_username, 127, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Override webhook username (optional)")
MACRO_CONFIG_STR(SvDiscordWebhookAvatar, sv_discord_webhook_avatar, 255, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Override webhook avatar URL (optional)")
MACRO_CONFIG_INT(SvDiscordTts, sv_discord_tts, 0, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Send messages as TTS")
MACRO_CONFIG_STR(SvDiscordThreadId, sv_discord_thread_id, 127, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Thread ID to post under (optional)")

MACRO_CONFIG_STR(SvDiscordWebhookUrl1on1, sv_discord_webhook_url_1on1, 512, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Discord webhook URL for 1on1 events")
MACRO_CONFIG_STR(SvDiscordWebhookUrlLogs, sv_discord_webhook_url_bp_logs, 512, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Discord webhook URL used for blockpoint transfer logs")
MACRO_CONFIG_STR(SvDiscordWebhookUrlLmb, sv_discord_webhook_url_lmb, 512, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Discord webhook URL for LMB events")
MACRO_CONFIG_STR(SvDiscordWebhookUrlChat, sv_discord_webhook_url_chat, 512, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Discord webhook URL for chat relay")
MACRO_CONFIG_STR(SvDiscordWebhookUrlCmd, sv_discord_webhook_url_cmd, 512, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Discord webhook URL for chat commands")
MACRO_CONFIG_STR(SvDiscordWebhookUrlChatFilter, sv_discord_webhook_url_chatfilter, 512, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Discord webhook URL for chat filter mutes")

// Whois admin tool
MACRO_CONFIG_INT(SvWhoisRetentionMonths, sv_whois_retention_months, 6, 0, 120, CFGFLAG_SERVER, "Delete whois records older than this many months (0 = keep forever)")
MACRO_CONFIG_INT(SvWhoisSnapshotMinutes, sv_whois_snapshot_minutes, 10, 0, 1440, CFGFLAG_SERVER, "Interval in minutes for whois snapshots (0 = disable)")
MACRO_CONFIG_STR(SvWhoisDbPath, sv_whois_db_path, 64, "whois.sqlite", CFGFLAG_SERVER, "Path to the whois SQLite database file")
MACRO_CONFIG_INT(SvWhoisMaxRows, sv_whois_max_rows, 100, 10, 100000, CFGFLAG_SERVER, "Max rows returned for whois queries (exact IP or name)")
MACRO_CONFIG_INT(SvWhoisPrefixMaxIps, sv_whois_prefix_max_ips, 50, 1, 100000, CFGFLAG_SERVER, "Max number of IPs to show in whois prefix mode")
MACRO_CONFIG_INT(SvWhoisPrefixNamesPerIp, sv_whois_prefix_names_per_ip, 10, 1, 100000, CFGFLAG_SERVER, "Max number of names to show per IP in whois prefix mode")
MACRO_CONFIG_INT(SvWhoisCooldownSec, sv_whois_cooldown_sec, 0, 0, 300, CFGFLAG_SERVER, "Cooldown in seconds between whois commands per admin (0 = no cooldown)")

MACRO_CONFIG_INT(SvShowAuthedUsers, sv_show_authed_users, 0, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Show authed users (admin/mod) as green in the scoreboard")

// rcon command sending performance tuning
MACRO_CONFIG_INT(SvSendRconCmdsPerTick, sv_send_rcon_cmds_per_tick, 32, 1, 256, CFGFLAG_SERVER, "Number of rcon commands to send per tick per client")
MACRO_CONFIG_INT(SvSendRconCmdsClientsPerTick, sv_send_rcon_cmds_clients_per_tick, 4, 1, MAX_CLIENTS, CFGFLAG_SERVER, "Number of clients to update with rcon commands per tick")

// chat filter settings
MACRO_CONFIG_INT(SvChatfilterMuteHours, sv_chatfilter_mute_hours, 12, 0, 168, CFGFLAG_SERVER, "Hours to mute a player when a filtered word is used (0 = disable muting)")
MACRO_CONFIG_STR(SvChatfilterWordsFile, sv_chatfilter_words_file, 255, "data/chatfilter_words.txt", CFGFLAG_SERVER, "Path or filename of the chat filter word list (resolved relative to the executable directory)")
