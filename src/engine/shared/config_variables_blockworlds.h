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

// Experience and block-related settings:
MACRO_CONFIG_INT(SvBlockExperienceMultiplierStacking, sv_block_expirience_multiplier_stacking, 1, 1, 4, CFGFLAG_SERVER, "1-highest, 2-additive, 3-logarithmic, 4-multiplicative")
MACRO_CONFIG_INT(SvBlockExperience, sv_block_experience, 3, 0, 0, CFGFLAG_SERVER, "Experience points awarded for each registered block.")
MACRO_CONFIG_INT(SvBlockFreezedInterval, sv_block_freezed, 3, 0, 0, CFGFLAG_SERVER, "Duration (in seconds) a player must remain frozen to be considered for blocking.")
MACRO_CONFIG_INT(SvBlockResetUnfreezedInterval, sv_block_reset_unfreezed, 3, 0, 0, CFGFLAG_SERVER, "Time (in seconds) a player must remain unfrozen before losing the enemy’s block impact.")
MACRO_CONFIG_INT(SvBlockResetNoImpactInterval, sv_block_reset_no_impact, 2, 0, 0, CFGFLAG_SERVER, "Time (in seconds) after unfreezing during which an enemy’s impact is still counted.")
MACRO_CONFIG_INT(SvBlockImpactIntervalToResist, sv_block_impact_interval_to_resist, 5, 0, 0, CFGFLAG_SERVER, "Duration (in seconds) enemy impact is registered before resistance can be applied.")
MACRO_CONFIG_INT(SvBlockUnfreezeNoImpactInterval, sv_block_unfreeze_no_impact_interval, 1, 0, 0, CFGFLAG_SERVER, "Time threshold (in seconds) for a recent impact to count towards blocking after unfreeze.")
MACRO_CONFIG_INT(SvBlockKillInterval, sv_block_kill_interval, 5, 0, 0, CFGFLAG_SERVER, "Time (in seconds) since a player's death during which blocking is still in effect.")
MACRO_CONFIG_INT(SvKillStreakCount, sv_kill_streak_count, 5, 1, 10, CFGFLAG_SERVER, "Number of consecutive kills required to register a kill streak.")
MACRO_CONFIG_INT(SvAllowExpFromSameIp, sv_allow_exp_from_same_ip, 0, 0, 1, CFGFLAG_SERVER, "Enable (1) or disable (0) awarding experience points for players connecting from the same IP. (Recommended: off for production.)")
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

// TDM (Team Deathmatch)
MACRO_CONFIG_INT(SvTDMRegDuration, sv_tdm_reg_duration, 10, 0, 1000, CFGFLAG_SERVER, "Specifies the duration during which TDM registration is possible in seconds")
MACRO_CONFIG_INT(SvTDMFreezeTime, sv_tdm_freeze_time, 3, 5, 30, CFGFLAG_SERVER, "Specifies the time required for a player to be frozen in order to die")
MACRO_CONFIG_INT(SvTDMTournamentTime, sv_tdm_tournament_time, 1200, 600, 1200, CFGFLAG_SERVER, "Specifies the maximum duration of a tournament (in seconds)")
MACRO_CONFIG_INT(SvTDMMinPlayer, sv_tdm_min_player, 15, 12, 18, CFGFLAG_SERVER, "Specifies the minimum number of players required to start")
