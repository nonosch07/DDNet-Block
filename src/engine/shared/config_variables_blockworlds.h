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
MACRO_CONFIG_INT(Sv1on1InviteCooldown, sv_1on1_invite_cooldown, 5, 0, 3600, CFGFLAG_SERVER, "Cooldown time (in seconds) a player must wait between 1on1 invites.")
MACRO_CONFIG_INT(Sv1on1MaxOutstandingInvitesPerSender, sv_1on1_max_outstanding_invites_per_sender, 3, 1, 100, CFGFLAG_SERVER, "Maximum number of outstanding 1on1 invites a single player can have.")

MACRO_CONFIG_INT(SvBpTransferCooldown, sv_bp_transfer_cooldown, 30, 0, 3600, CFGFLAG_SERVER, "Cooldown time (in seconds) between initiating blockpoint transfer offers.")
MACRO_CONFIG_INT(SvBpTransferExpiry, sv_bp_transfer_expiry, 30, 5, 600, CFGFLAG_SERVER, "Expiry time (in seconds) for a pending blockpoint transfer offer.")
MACRO_CONFIG_INT(SvBpTransferAmountCap, sv_bp_transfer_amount_cap, 3500, 1, 100000, CFGFLAG_SERVER, "Maximum allowed blockpoints in a single transfer offer.")
MACRO_CONFIG_INT(SvBpTransferMaxOutstandingPerSender, sv_bp_transfer_max_outstanding_per_sender, 3, 1, 100, CFGFLAG_SERVER, "Maximum number of outstanding blockpoint transfer offers a player can have to others.")
MACRO_CONFIG_INT(SvBpTransferAmountMin, sv_bp_transfer_amount_min, 100, 1, 100000, CFGFLAG_SERVER, "Minimum allowed blockpoints in a single transfer offer.")

MACRO_CONFIG_INT(Sv1on1InviteExpiry, sv_1on1_invite_expiry, 15, 5, 600, CFGFLAG_SERVER, "Expiry time (in seconds) for 1on1 invites.")
MACRO_CONFIG_INT(SvClanInviteCooldown, sv_clan_invite_cooldown, 60, 0, 3600, CFGFLAG_SERVER, "Cooldown time (in seconds) between clan invites from the same player.")
MACRO_CONFIG_INT(SvClanInviteExpiry, sv_clan_invite_expiry, 15, 5, 600, CFGFLAG_SERVER, "Expiry time (in seconds) for clan invites.")
MACRO_CONFIG_INT(SvClanInviteMaxOutstandingPerSender, sv_clan_invite_max_outstanding_per_sender, 5, 1, 100, CFGFLAG_SERVER, "Maximum number of outstanding clan invites a single player can have.")
MACRO_CONFIG_INT(SvShopRequestExpiry, sv_shop_request_expiry, 10, 5, 600, CFGFLAG_SERVER, "Expiry time (in seconds) for shop purchase requests.")

// Register/IP throttling configuration
MACRO_CONFIG_INT(SvRegisterCooldownPerIp, sv_register_cooldown_per_ip, 10, 0, 3600, CFGFLAG_SERVER, "Cooldown time (seconds) per IP between register attempts.")
MACRO_CONFIG_INT(SvRegisterIpAttemptWindow, sv_register_ip_attempt_window, 60, 1, 3600, CFGFLAG_SERVER, "Time window (seconds) in which register attempts per IP are counted.")
MACRO_CONFIG_INT(SvRegisterIpMaxAttempts, sv_register_ip_max_attempts, 5, 1, 100, CFGFLAG_SERVER, "Number of register attempts allowed per IP in the attempt window before temporary ban.")
MACRO_CONFIG_INT(SvRegisterIpBanSeconds, sv_register_ip_ban_seconds, 300, 0, 86400, CFGFLAG_SERVER, "Duration (seconds) to ban an IP after exceeding register attempts.")

// Experience and block-related settings:
MACRO_CONFIG_INT(SvBlockExperienceMultiplierStacking, sv_block_expirience_multiplier_stacking, 1, 1, NUM_EXP_CALC_METHODS, CFGFLAG_SERVER, "0-highest, 1-additive, 2-logarithmic, 3-multiplicative")
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

// Deathnote settings:
MACRO_CONFIG_INT(SvDeathNoteCoolDown, sv_deathnote_cooldown, 600, 60, 3600, CFGFLAG_SERVER, "Cooldown time (in seconds) a player must wait before reusing the Deathnote.")

// Shop server setting:
MACRO_CONFIG_INT(SvShopServer, sv_shop_server, 0, 0, 1, CFGFLAG_SERVER, "Set to 1 if this is a shop server; otherwise, set to 0.")

// Clans settings:
MACRO_CONFIG_INT(SvClanMinLevel, sv_clan_min_level, 25, 0, 100, CFGFLAG_SERVER, "Minimum player level required to create a clan.")
MACRO_CONFIG_INT(SvClanSaveInterval, sv_clan_save_interval, 600, 1, 3600, CFGFLAG_SERVER, "Time delay (in seconds) between successive clan saves.")

// events:
// LMB (last man blocking)
MACRO_CONFIG_INT(SvLMBInitialFreezeTime, sv_lmb_initial_freeze_time, 3, 0, 1000, CFGFLAG_SERVER, " Duration of character freeze after teleportation to the arena (seconds)")
MACRO_CONFIG_INT(SvLMBRegistrationTime, sv_lmb_registration_time, 30, 0, 1000, CFGFLAG_SERVER, "Duration of registration phase (seconds)")
MACRO_CONFIG_INT(SvLMBActiveTime, sv_lmb_active_time, 600, 0, 1000, CFGFLAG_SERVER, "Duration of active phase (seconds)")
MACRO_CONFIG_INT(SvLMBWinnerExpMultiplier, sv_lmb_winner_exp_multiplier, 200, 100, 10000, CFGFLAG_SERVER, "Exp multiplier for the winner (percents)")
MACRO_CONFIG_INT(SvLMBWinnerExpMultiplierDuration, sv_lmb_winner_exp_multiplier_duration, 5, 1, 60, CFGFLAG_SERVER, " Exp multiplier duration (minutes)") // make more precise?
MACRO_CONFIG_INT(SvLMBFreezeTimeout, sv_lmb_freeze_timeout, 150, 1, 50000, CFGFLAG_SERVER, "Duration of character freeze to be counted as loss (ticks, 50 ticks = 1 second)") // not related to lmb?
MACRO_CONFIG_INT(SvLMBBroadcastRate, sv_lmb_broadcast_rate, 50, 1, 500, CFGFLAG_SERVER, "Rate at which information broadcasts will be sent (ticks, 50 ticks = 1 second)") // not related to lmb?
MACRO_CONFIG_INT(SvLMBMinimumCandidates, sv_lmb_minimum_candidates, 8, 2, MAX_CLIENTS, CFGFLAG_SERVER, "Minimum amount of candidates to start the active phase")

// Weaponkits setting: persist whether weaponkits are allowed on this server
MACRO_CONFIG_INT(SvWeaponkitsAllowed, sv_weaponkits_allowed, 1, 0, 1, CFGFLAG_SERVER, "Allow (1) or disable (0) weaponkits on the server.")

MACRO_CONFIG_INT(SvWeaponkitsVoteCoolDown, sv_weaponkits_vote_cooldown, 15, 1, 60, CFGFLAG_SERVER, "Cooldown time (in minutes) a player must wait before reusing the weaponkits vote.")
MACRO_CONFIG_INT(SvLmbVoteCoolDown, sv_lmb_vote_cooldown, 30, 1, 99999, CFGFLAG_SERVER, "Cooldown time (in minutes) a player must wait before reusing the LMB vote.")

MACRO_CONFIG_INT(SvNoHammerOnUnfreeze, sv_no_hammer_on_unfreeze, 1, 0, 1, CFGFLAG_SERVER, "Prevent instant hammer fire immediately after unfreeze (1=on, 0=off)")

// TDM (Team Deathmatch)
MACRO_CONFIG_INT(SvTDMRegistrationTime, sv_tdm_registration_time, 30, 0, 1000, CFGFLAG_SERVER, "Duration of TDM registration phase (seconds)")
MACRO_CONFIG_INT(SvTDMActiveTime, sv_tdm_active_time, 600, 0, 3600, CFGFLAG_SERVER, "Duration of TDM active phase (seconds)")
MACRO_CONFIG_INT(SvTDMMinimumCandidates, sv_tdm_minimum_candidates, 8, 2, MAX_CLIENTS, CFGFLAG_SERVER, "Minimum amount of candidates to start TDM")
MACRO_CONFIG_INT(SvTDMMaximumCandidates, sv_tdm_maximum_candidates, 16, 2, MAX_CLIENTS, CFGFLAG_SERVER, "Maximum amount of participants for TDM")
MACRO_CONFIG_INT(SvTDMBroadcastRate, sv_tdm_broadcast_rate, 50, 1, 500, CFGFLAG_SERVER, "Rate at which TDM information broadcasts will be sent (ticks)")
MACRO_CONFIG_INT(SvTDMFreezeTime, sv_tdm_freeze_time, 3, 5, 30, CFGFLAG_SERVER, "Specifies the time required for a player to be frozen in order to die")

// VIP flag experience bonus
MACRO_CONFIG_INT(SvVipFlagExpMultiplier, sv_flag_exp_multiplier, 200, 100, 10000, CFGFLAG_SERVER, "Exp multiplier granted by the VIP flag (percents)")
MACRO_CONFIG_INT(SvVipFlagExpDuration, sv_flag_exp_duration, 5, 1, 1440, CFGFLAG_SERVER, "Duration for VIP flag experience bonus (minutes)")

// 1on1 draw detection settings
MACRO_CONFIG_INT(Sv1on1DrawFreezeGrace, sv_1on1_draw_freeze_grace, 3, 0, 120, CFGFLAG_SERVER, "1on1: Grace period after round start before counting simultaneous freeze (seconds)")
MACRO_CONFIG_INT(Sv1on1DrawFreezeStalemate, sv_1on1_draw_freeze_stalemate, 5, 1, 300, CFGFLAG_SERVER, "1on1: Both players frozen this long (after grace) => draw (seconds)")
MACRO_CONFIG_INT(Sv1on1DrawDeathTickTolerance, sv_1on1_draw_death_tick_tolerance, 1, 0, 50, CFGFLAG_SERVER, "1on1: Max tick diff to treat dual death as draw")
MACRO_CONFIG_INT(Sv1on1DrawDeathExtendedWindow, sv_1on1_draw_death_extended_window, 50, 0, 2000, CFGFLAG_SERVER, "1on1: Extended tick window for dual death draw (ticks)")
