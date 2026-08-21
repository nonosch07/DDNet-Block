#include "bw_player.h"

#include <blockworlds/accounts.h>
#include <blockworlds/bw_base.h>
#include <blockworlds/clans.h>
#include <blockworlds/common.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/cosmetics/cosmetics.h>
#include <blockworlds/cosmetics/specials/ball.h>
#include <blockworlds/cosmetics/specials/crown.h>
#include <blockworlds/cosmetics/specials/epiccircle.h>
#include <blockworlds/cosmetics/specials/flag.h>
#include <blockworlds/cosmetics/specials/halo.h>
#include <blockworlds/whois.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <blockworlds/bw_context.h>
#include <blockworlds/bw_util.h>
#include <algorithm>
#include <blockworlds/discord/webhook.h>

// Forwarders: the moved code reads exactly as it did when it lived in CPlayer.
CGameContext *CBwPlayer::GameServer() const { return m_pPlayer->GameServer(); }
IServer *CBwPlayer::Server() const { return m_pPlayer->Server(); }
int CBwPlayer::GetCid() const { return m_pPlayer->GetCid(); }
CCharacter *CBwPlayer::GetCharacter() const { return m_pPlayer->GetCharacter(); }

int CBwPlayer::GetClanLevel()
{
	CClansData Tmp;
	if(m_Account.m_ClanId > 0 && GameServer()->Bw().Clans()->GetClanSnapshotById(m_Account.m_ClanId, Tmp))
		return Tmp.m_Level;
	return 0;
}

int CBwPlayer::GetClanExperience()
{
	CClansData Tmp;
	if(m_Account.m_ClanId > 0 && GameServer()->Bw().Clans()->GetClanSnapshotById(m_Account.m_ClanId, Tmp))
		return Tmp.m_Experience;
	return 0;
}

void CBwPlayer::GiveFlag(int DurationMinutes)
{
	// remove existing flag if present
	if(m_pFlagEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pFlagEntity);
		m_pFlagEntity = nullptr;
		m_FlagExpireTick = 0;
	}

	// spawn new flag entity attached to player
	m_pFlagEntity = new CFlag(&GameServer()->m_World, GetCid(), 0);
	if(DurationMinutes > 0)
		m_FlagExpireTick = Server()->Tick() + DurationMinutes * 60 * Server()->TickSpeed();
}

void CBwPlayer::GiveTemporarySpecial(int SpecialIndex, int DurationMinutes)
{
	if(SpecialIndex < 0 || SpecialIndex >= CCosmeticsHandler::NUM_SPECIALS)
		return;

	if(m_CurrentSpecial != -1 && m_pSpecialEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
		m_pSpecialEntity = nullptr;
		m_CurrentSpecial = -1;
		m_SpecialExpireTick = 0;
	}

	if(SpecialIndex == CCosmeticsHandler::SPECIAL_BALL)
	{
		extern class CBall *CreateBall(CGameWorld *, vec2, int);
		m_pSpecialEntity = new CBall(&GameServer()->m_World, GetCharacter() ? GetCharacter()->m_Pos : Player()->m_ViewPos, GetCid());
	}
	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_CROWN)
	{
		m_pSpecialEntity = new CCrown(&GameServer()->m_World, GetCid());
	}
	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_EPICCIRCLE)
	{
		m_pSpecialEntity = new CEpicCircle(&GameServer()->m_World, GetCharacter() ? GetCharacter()->m_Pos : Player()->m_ViewPos, GetCid());
	}
	else
		return;

	m_CurrentSpecial = SpecialIndex;
	if(DurationMinutes > 0)
		m_SpecialExpireTick = Server()->Tick() + DurationMinutes * Server()->TickSpeed();
}

void CBwPlayer::BWProcessAccountsResult(CAccountResult &Result)
{
	if(Result.m_Success)
	{
		switch(Result.m_MessageKind)
		{
		case CAccountResult::DIRECT:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->Bw().SendChatTarget(GetCid(), aMessage);
			}
			break;

		case CAccountResult::ALL:
		{
			bool PrimaryMessage = true;
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;

				if(GameServer()->ProcessSpamProtection(GetCid()) && PrimaryMessage)
					break;

				GameServer()->SendChat(-1, TEAM_ALL, aMessage, -1);
				PrimaryMessage = false;
			}
			break;
		}

		case CAccountResult::TOP_MESSAGES:
			if(m_CaptureTopToMenu)
			{
				// capture into buffer for inline leaderboard rendering
				m_TopMessagesCount = 0;
				for(auto &aMessage : Result.m_aaMessages)
				{
					if(aMessage[0] == 0 || m_TopMessagesCount >= TOP_MAX_LINES)
						break;
					// truncate to safe line length for vote menu
					str_copy(m_aTopMessages[m_TopMessagesCount], aMessage, TOP_MAX_LINE_LEN);
					m_TopMessagesCount++;
				}
				m_CaptureTopToMenu = false; // one-shot capture
				// refresh the vote menu to display the data inline
				GameServer()->Bw().ClearVotes(GetCid());
				GameServer()->ProgressVoteOptions(GetCid());
				GameServer()->Bw().SendCosmeticsVoteOptions(GetCid());
			}
			else
			{
				for(auto &aMessage : Result.m_aaMessages)
				{
					if(aMessage[0] == 0)
						break;
					GameServer()->Bw().SendChatTarget(GetCid(), aMessage);
				}
			}
			break;

		case CAccountResult::BROADCAST:
			break;

		case CAccountResult::LOGGED_IN_ALREADY:
			if(str_comp(Result.m_aLoginServer, g_Config.m_SvServerId) == 0)
			{
				GameServer()->Bw().SendChatTarget(GetCid(), "Account is already being used on this server.");
				break;
			}
			GameServer()->Bw().SendChatTarget(GetCid(), "Account is already being used on other server.");
			break;

		case CAccountResult::LOGIN_WRONG_PASS:
			GameServer()->Bw().SendChatTarget(GetCid(), "Wrong username or password.");
			break;

		case CAccountResult::LOGIN_INFO:
			m_Account = Result.m_Account;
			OnPlayerLogin(); // Call the login handler
			break;

		case CAccountResult::REGISTER:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					continue;
				GameServer()->Bw().SendChatTarget(GetCid(), aMessage);
			}
			// GameServer()->RegisterBanCheck(GetCid());
			break;

		case CAccountResult::LOG_ONLY:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				// dbg_msg("account", "%s", aMessage);
			}
			break;
		}
	}
}

void CBwPlayer::BWProcessClansResult(CClanResult &Result)
{
	CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Bw().Http());
	const char *pLogsUrl = g_Config.m_SvDiscordWebhookUrlLogs[0] ? g_Config.m_SvDiscordWebhookUrlLogs : nullptr;
	bool discordConfigured = Discord.IsConfigured(pLogsUrl);

	// capture Top Clans into the inline leaderboard buffer if requested
	if(m_CaptureTopToMenu && Result.m_Success)
	{
		const char *pFirst = Result.m_aaMessages[0];
		if(pFirst && pFirst[0] && str_find_nocase(pFirst, "Top Clans"))
		{
			m_TopMessagesCount = 0;
			for(int i = 0; i < CClanResult::MAX_MESSAGES && m_TopMessagesCount < TOP_MAX_LINES; ++i)
			{
				if(Result.m_aaMessages[i][0] == '\0')
					break;
				str_copy(m_aTopMessages[m_TopMessagesCount], Result.m_aaMessages[i], TOP_MAX_LINE_LEN);
				m_TopMessagesCount++;
			}
			m_CaptureTopToMenu = false;
			// refresh the vote menu to display the data inline
			GameServer()->Bw().ClearVotes(GetCid());
			GameServer()->ProgressVoteOptions(GetCid());
			GameServer()->Bw().SendCosmeticsVoteOptions(GetCid());
			return;
		}
	}

	if(Result.m_Success || Result.m_aaMessages[0][0] != '\0')
	{
		// First, apply any deferred actions requested by the SQL worker thread
		switch(Result.m_Action)
		{
		case CClanResult::ACTION_UPDATE_PLAYER_BY_CLIENT:
			if(Result.m_ActionClientId >= 0 && Result.m_ActionClientId < MAX_CLIENTS)
			{
				CPlayer *pTarget = GameServer()->m_apPlayers[Result.m_ActionClientId];
				if(pTarget)
				{
					const char *pPlayerName = pTarget->Bw().GetPlayerName();

					pTarget->Bw().m_Account.m_ClanId = Result.m_ActionNewClanId;
					pTarget->Bw().m_Account.m_AuthLevel = static_cast<ClanAuthLevel>(Result.m_ActionNewAuthLevel);

					if(discordConfigured && Result.m_Success)
					{
						if(Result.m_ActionNewClanId > 0 && Result.m_ActionNewAuthLevel == static_cast<int>(ClanAuthLevel::LEADER))
						{
							std::string clanName = GameServer()->Bw().Clans()->GetClanNameCopy(Result.m_ActionNewClanId);
							char aMsg[512];
							str_format(aMsg, sizeof(aMsg), "[CLAN] Created: %s (cid=%d) created clan '%s' (id=%d)", pPlayerName, Result.m_ActionClientId, clanName.c_str(), Result.m_ActionNewClanId);
							CDiscordWebhook::SSendOptions Opt;
							Opt.m_pWebhookUrl = pLogsUrl;
							Discord.Send(aMsg, Opt);
						}
					}
				}
			}
			break;
		case CClanResult::ACTION_UPDATE_PLAYER_BY_NAME:
			if(Result.m_ActionPlayerName[0] != '\0')
			{
				// find target player by name to capture previous clan for logging
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					CPlayer *pTarget = GameServer()->m_apPlayers[i];
					if(pTarget && pTarget->Bw().IsLoggedIn() && str_comp(pTarget->Bw().m_Account.m_aName, Result.m_ActionPlayerName) == 0)
					{
						int prevClan = pTarget->Bw().GetClanId();
						const char *pPlayerName = pTarget->Bw().GetPlayerName();
						pTarget->Bw().m_Account.m_ClanId = Result.m_ActionNewClanId;
						pTarget->Bw().m_Account.m_AuthLevel = static_cast<ClanAuthLevel>(Result.m_ActionNewAuthLevel);

						if(discordConfigured && Result.m_Success)
						{
							if(Result.m_ActionNewClanId > 0 && prevClan == 0)
							{
								std::string clanName = GameServer()->Bw().Clans()->GetClanNameCopy(Result.m_ActionNewClanId);
								char aMsg[512];
								str_format(aMsg, sizeof(aMsg), "[CLAN] Joined: %s joined clan '%s' (id=%d)", pPlayerName, clanName.c_str(), Result.m_ActionNewClanId);
								CDiscordWebhook::SSendOptions Opt;
								Opt.m_pWebhookUrl = pLogsUrl;
								Discord.Send(aMsg, Opt);
							}
							else if(Result.m_ActionNewClanId == 0 && prevClan > 0)
							{
								std::string clanName = GameServer()->Bw().Clans()->GetClanNameCopy(prevClan);
								char aMsg[512];
								str_format(aMsg, sizeof(aMsg), "[CLAN] Removed: %s was removed/left clan '%s' (id=%d)", pPlayerName, clanName.c_str(), prevClan);
								CDiscordWebhook::SSendOptions Opt;
								Opt.m_pWebhookUrl = pLogsUrl;
								Discord.Send(aMsg, Opt);
							}
						}
						break;
					}
				}
			}
			break;
		case CClanResult::ACTION_UPDATE_TWO_PLAYERS:
		{
			// update target by name
			if(Result.m_ActionPlayerName[0] != '\0')
			{
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					CPlayer *pTarget = GameServer()->m_apPlayers[i];
					if(pTarget && pTarget->Bw().IsLoggedIn() && str_comp(pTarget->Bw().m_Account.m_aName, Result.m_ActionPlayerName) == 0)
					{
						int prevClan = pTarget->Bw().GetClanId();
						const char *pPlayerName = pTarget->Bw().GetPlayerName();
						pTarget->Bw().m_Account.m_ClanId = Result.m_ActionNewClanId;
						pTarget->Bw().m_Account.m_AuthLevel = static_cast<ClanAuthLevel>(Result.m_ActionNewAuthLevel2);

						// discord logging for promotion
						if(discordConfigured && Result.m_Success)
						{
							if(Result.m_ActionNewClanId > 0 && prevClan == 0 && Result.m_ActionNewAuthLevel2 == static_cast<int>(ClanAuthLevel::LEADER))
							{
								std::string clanName = GameServer()->Bw().Clans()->GetClanNameCopy(Result.m_ActionNewClanId);
								char aMsg[512];
								str_format(aMsg, sizeof(aMsg), "[CLAN] Transferred: %s is now leader of '%s' (id=%d)", pPlayerName, clanName.c_str(), Result.m_ActionNewClanId);
								CDiscordWebhook::SSendOptions Opt;
								Opt.m_pWebhookUrl = pLogsUrl;
								Discord.Send(aMsg, Opt);
							}
						}
						break;
					}
				}
			}

			// update issuer by client id
			if(Result.m_ActionClientId >= 0 && Result.m_ActionClientId < MAX_CLIENTS)
			{
				CPlayer *pIssuer = GameServer()->m_apPlayers[Result.m_ActionClientId];
				if(pIssuer)
				{
					pIssuer->Bw().m_Account.m_ClanId = Result.m_ActionNewClanId;
					pIssuer->Bw().m_Account.m_AuthLevel = static_cast<ClanAuthLevel>(Result.m_ActionNewAuthLevel);

					// discord logging for demotion
					if(discordConfigured && Result.m_Success)
					{
						if(Result.m_ActionNewClanId > 0 && Result.m_ActionNewAuthLevel == static_cast<int>(ClanAuthLevel::COLEADER))
						{
							char aMsg[512];
							str_format(aMsg, sizeof(aMsg), "[CLAN] Transferred: %s was demoted to co-leader of clan id %d", pIssuer->Bw().GetPlayerName(), Result.m_ActionNewClanId);
							CDiscordWebhook::SSendOptions Opt;
							Opt.m_pWebhookUrl = pLogsUrl;
							Discord.Send(aMsg, Opt);
						}
					}
				}
			}
			break;
		}
		case CClanResult::ACTION_RESET_CLAN_PLAYERS:
			if(Result.m_ActionResetClanId > 0)
			{
				// log clan deletion
				if(discordConfigured && Result.m_Success)
				{
					std::string clanName = GameServer()->Bw().Clans()->GetClanNameCopy(Result.m_ActionResetClanId);
					char aMsg[512];
					str_format(aMsg, sizeof(aMsg), "[CLAN] Deleted: Clan '%s' (id=%d) was deleted", clanName.c_str(), Result.m_ActionResetClanId);
					CDiscordWebhook::SSendOptions Opt;
					Opt.m_pWebhookUrl = pLogsUrl;
					Discord.Send(aMsg, Opt);
				}
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					CPlayer *pTarget = GameServer()->m_apPlayers[i];
					if(pTarget && pTarget->Bw().IsLoggedIn() && pTarget->Bw().m_Account.m_ClanId == Result.m_ActionResetClanId)
					{
						pTarget->Bw().m_Account.m_ClanId = 0;
						pTarget->Bw().m_Account.m_AuthLevel = ClanAuthLevel::NONE;
					}
				}
			}
			break;
		case CClanResult::ACTION_NOTIFY_CLAN_RENAME:
			if(GetClanId() > 0)
			{
				char aBuf[192];
				const char *pOld = Result.m_ActionOldClanName[0] ? Result.m_ActionOldClanName : "<old>";
				const char *pNew = Result.m_ActionNewClanName[0] ? Result.m_ActionNewClanName : "<new>";
				str_format(aBuf, sizeof(aBuf), "Clan renamed: '%s' -> '%s'", pOld, pNew);
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					CPlayer *pMember = GameServer()->m_apPlayers[i];
					if(pMember && pMember->Bw().IsLoggedIn() && pMember->Bw().GetClanId() == GetClanId())
					{
						GameServer()->Bw().SendChatTarget(i, aBuf);
					}
				}
				if(discordConfigured && Result.m_Success)
				{
					char aMsg[512];
					str_format(aMsg, sizeof(aMsg), "[CLAN] Renamed: '%s' -> '%s'", pOld, pNew);
					CDiscordWebhook::SSendOptions Opt;
					Opt.m_pWebhookUrl = pLogsUrl;
					Discord.Send(aMsg, Opt);
				}
			}
			break;
		default:
			break;
		}

		if(Result.m_Success && Result.m_ActionChargeClientId >= 0 && Result.m_ActionChargeAmount > 0)
		{
			int cid = Result.m_ActionChargeClientId;
			if(cid >= 0 && cid < MAX_CLIENTS)
			{
				CPlayer *pCharger = GameServer()->m_apPlayers[cid];
				if(pCharger && pCharger->Bw().IsLoggedIn())
				{
					int cost = Result.m_ActionChargeAmount;
					if(pCharger->Bw().GetPlayerBlockpoints() >= cost)
					{
						pCharger->Bw().SetPlayerBlockpoints(pCharger->Bw().GetPlayerBlockpoints() - cost);
						GameServer()->Bw().Accounts()->Save(cid, &pCharger->Bw().m_Account);
						char aBuf[128];
						if(cost == g_Config.m_SvClanRenamePrice)
							str_format(aBuf, sizeof(aBuf), "You paid %d blockpoints to rename the clan.", cost);
						else if(cost == g_Config.m_SvClanCreatePrice)
							str_format(aBuf, sizeof(aBuf), "You paid %d blockpoints to create the clan.", cost);
						else
							str_format(aBuf, sizeof(aBuf), "You paid %d blockpoints.", cost);
						GameServer()->Bw().SendChatTarget(cid, aBuf);
					}
					else
					{
						if(Result.m_ActionChargeAmount == g_Config.m_SvClanRenamePrice)
							GameServer()->Bw().SendChatTarget(cid, "Rename fee could not be charged due to insufficient blockpoints after rename.");
						else if(Result.m_ActionChargeAmount == g_Config.m_SvClanCreatePrice)
							GameServer()->Bw().SendChatTarget(cid, "Creation fee could not be charged due to insufficient blockpoints after creation.");
						else
							GameServer()->Bw().SendChatTarget(cid, "Fee could not be charged due to insufficient blockpoints.");
					}
				}
			}
		}

		switch(Result.m_MessageKind)
		{
		case CClanResult::DIRECT:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->Bw().SendChatTarget(GetCid(), aMessage);
			}
			break;
		case CClanResult::ALL:
		case CClanResult::BROADCAST:
		case CClanResult::DELETE:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->Bw().SendChatTarget(GetCid(), aMessage);
			}
			break;
		case CClanResult::CLAN:
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				CPlayer *pMember = GameServer()->m_apPlayers[i];
				if(pMember && pMember->Bw().IsLoggedIn() && pMember->Bw().GetClanId() == GetClanId())
				{
					for(auto &aMessage : Result.m_aaMessages)
					{
						if(aMessage[0] == 0)
							break;
						GameServer()->Bw().SendChatTarget(i, aMessage);
					}
				}
			}
			break;
		}
		}
	}
}

void CBwPlayer::BWProcessAdminCommandResult(CAdminCommandResult &Result)
{
	if(Result.m_Success) // SQL request was successful
	{
		switch(Result.m_MessageKind)
		{
		case CAdminCommandResult::FREEZE_ACC:
		{
			break;
		}
		case CAdminCommandResult::MODERATOR:
		{
			break;
		}
		case CAdminCommandResult::SUPER_MODERATOR:
		{
			break;
		}
		case CAdminCommandResult::SUPPORTER:
		{
			break;
		}
		case CAdminCommandResult::DIRECT:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->Bw().SendChatTarget(GetCid(), aMessage);
			}
			break;
		case CAdminCommandResult::ALL:
		{
			bool PrimaryMessage = true;
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;

				if(GameServer()->ProcessSpamProtection(GetCid()) && PrimaryMessage)
					break;

				GameServer()->SendChat(-1, TEAM_ALL, aMessage, -1);
				PrimaryMessage = false;
			}
			break;
		}
		case CAdminCommandResult::BROADCAST:
			// if(Result.m_aBroadcast[0] != 0)
			// 	GameServer()->Bw().SendBroadcast(Result.m_aBroadcast, -1);
			break;
		case CAdminCommandResult::LOG_ONLY:
			for(auto &aMessage : Result.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				// dbg_msg("account", "%s", aMessage);
			}
			break;
		}
	}
}

void CBwPlayer::OnPlayerLogin()
{
	// KillCharacter(); - no need to kill since we allow using /login only at spawn
	GameServer()->Bw().SendChatTarget(GetCid(), "Login successfully");

	// Log account info to whois now that we have it (join event fires before login)
	if(GameServer()->Bw().WhoIs())
		GameServer()->Bw().WhoIs()->LogJoin(GetCid(), "login");

	GameServer()->Bw().ClearVotes(GetCid());
	GameServer()->ProgressVoteOptions(GetCid());
	GameServer()->Bw().SendCosmeticsVoteOptions(GetCid());

	if(GetPlayerVip())
	{
		for(int i = 0; i < CCosmeticsHandler::NUM_SPECIALS; ++i)
		{
			m_aSpecialsOwned[i] = '1';
		}
	}

	// --- Weekly rewards ---
	ProcessWeeklyReward();

	// --- Apply stored x2 EXP boost if still active ---
	if(m_Account.m_WeeklyExpBoostUntil > 0)
	{
		long long NowPlaytime = m_Account.m_Playtime;
		if(NowPlaytime < m_Account.m_WeeklyExpBoostUntil)
		{
			// Remaining boost in seconds of playtime
			int RemainingSeconds = (int)(m_Account.m_WeeklyExpBoostUntil - NowPlaytime);
			int RemainingMinutes = RemainingSeconds / 60;
			if(RemainingMinutes > 0)
			{
				AddExpMultiplier(200, RemainingMinutes);
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "[Weekly] x2 EXP boost active! %dh %dm remaining (ingame time)", RemainingMinutes / 60, RemainingMinutes % 60);
				GameServer()->Bw().SendChatTarget(GetCid(), aBuf);
			}
		}
		else
		{
			// Boost expired, clear it
			SetWeeklyExpBoostUntil(0);
		}
	}

	if(m_Account.m_Id > 0)
	{
		bool Dirty = false;
		const char *pCurName = Server()->ClientName(GetCid());
		if(pCurName[0] && str_comp(pCurName, m_Account.m_aLastName) != 0)
		{
			str_copy(m_Account.m_aLastName, pCurName, sizeof(m_Account.m_aLastName));
			Dirty = true;
		}
		const char *pSkin = Player()->m_TeeInfos.m_aSkinName;
		if(pSkin[0] && str_comp(pSkin, m_Account.m_aLastSkin) != 0)
		{
			str_copy(m_Account.m_aLastSkin, pSkin, sizeof(m_Account.m_aLastSkin));
			Dirty = true;
		}
		if(Player()->m_TeeInfos.m_ColorBody != m_Account.m_LastBodyColor)
		{
			m_Account.m_LastBodyColor = Player()->m_TeeInfos.m_ColorBody;
			Dirty = true;
		}
		if(Player()->m_TeeInfos.m_ColorFeet != m_Account.m_LastFeetColor)
		{
			m_Account.m_LastFeetColor = Player()->m_TeeInfos.m_ColorFeet;
			Dirty = true;
		}
		char aIp[48] = {0};
		BwClientAddr(Server(), GetCid(), aIp, sizeof(aIp));
		if(aIp[0])
		{
			bool Invalid = false;
			for(int i = 0; aIp[i]; ++i)
			{
				unsigned char c = (unsigned char)aIp[i];
				if(c < 0x20 || c > 0x7E)
				{
					Invalid = true;
					break;
				}
			}
			if(!Invalid)
			{
				str_copy(m_Account.m_aAddress, aIp, sizeof(m_Account.m_aAddress));
				Dirty = true;
			}
		}
		if(Dirty)
		{
			m_Account.m_DirtyCore = true;
			m_PendingLoginCoreSave = true;
			m_PendingLoginSaveTick = Server()->Tick();
		}
	}
}

void CBwPlayer::OnPlayerSave(bool Logout)
{
	// dbg_msg("account", "saving account '%s' CID=%d AccountId=%d Logout=%d", Server()->ClientName(GetCid()), GetCid(), m_Account.m_Id, Logout);

	if(!m_Account.m_Id)
		return;

	char aName[32];
	str_copy(aName, Server()->ClientName(GetCid()), sizeof(aName));

	if(str_comp(aName, m_Account.m_aLastName) != 0)
	{
		str_format(m_Account.m_aLastName, sizeof(m_Account.m_aLastName), "%s", aName);
		m_Account.m_DirtyCore = true;
	}

	char aIp[48] = {0};
	BwClientAddr(Server(), GetCid(), aIp, sizeof(aIp));
	if(aIp[0])
	{
		bool Invalid = false;
		for(int i = 0; aIp[i]; ++i)
		{
			unsigned char c = (unsigned char)aIp[i];
			if(c < 0x20 || c > 0x7E)
			{
				Invalid = true;
				break;
			}
		}
		if(Invalid)
		{
			str_copy(aIp, "npc", sizeof(aIp)); // fallback safe token
		}
		if(str_comp(aIp, m_Account.m_aAddress) != 0)
		{
			str_copy(m_Account.m_aAddress, aIp, sizeof(m_Account.m_aAddress));
			m_Account.m_DirtyCore = true;
		}
	}

	int ColorFeet = GameServer()->m_apPlayers[GetCid()]->m_TeeInfos.m_ColorFeet;

	if(ColorFeet != m_Account.m_LastFeetColor)
	{
		m_Account.m_LastFeetColor = ColorFeet;
		m_Account.m_DirtyCore = true;
	}

	int ColorBody = GameServer()->m_apPlayers[GetCid()]->m_TeeInfos.m_ColorBody;

	if(ColorBody != m_Account.m_LastBodyColor)
	{
		m_Account.m_LastBodyColor = ColorBody;
		m_Account.m_DirtyCore = true;
	}

	const char *aSkinName = GameServer()->m_apPlayers[GetCid()]->m_TeeInfos.m_aSkinName;

	if(str_comp(aSkinName, m_Account.m_aLastSkin) != 0)
	{
		str_format(m_Account.m_aLastSkin, sizeof(m_Account.m_aLastSkin), "%s", aSkinName);
		m_Account.m_DirtyCore = true;
	}

	GameServer()->Bw().Accounts()->Save(GetCid(), &m_Account);
	if(Logout)
		GameServer()->Bw().Accounts()->Logout(GetCid(), m_Account.m_Id);
}

void CBwPlayer::OnPlayerLogout()
{
	if(!IsLoggedIn())
		return;
	if(GameServer()->Bw().Accounts() && GameServer()->Bw().Accounts()->ShutdownFlushActive())
	{
		// dbg_msg("account", "skip duplicate logout save for AccountId=%d during shutdown", GetAccId());
		m_Account = CAccountData();
		return;
	}

	GameServer()->Bw().ClearVotes(GetCid());
	GameServer()->ProgressVoteOptions(GetCid());
	OnPlayerSave(true);
	// dbg_msg("account", "logging out AccountId=%d", GetAccId());

	if(GetClanId() > 0)
	{
		GameServer()->Bw().Clans()->SaveClan(GetCid(), GetClanId());
	}

	// clear cosmetics and save account
	ClearCosmetics();
	SetSkinMani(-1);
	SetGunDesign(-1);
	SetKnockout(-1);

	// clear account data
	m_Account = CAccountData();
}

void CBwPlayer::ClearCosmetics()
{
	// remove any active special entity
	if(m_pSpecialEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
		m_pSpecialEntity = nullptr;
	}
	m_CurrentSpecial = -1;
	m_SpecialExpireTick = 0;

	// remove any active flag entity
	if(m_pFlagEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pFlagEntity);
		m_pFlagEntity = nullptr;
	}
	m_FlagExpireTick = 0;

	// reset owned specials
	for(int i = 0; i < CCosmeticsHandler::NUM_SPECIALS; ++i)
		m_aSpecialsOwned[i] = '0';
	m_aSpecialsOwned[CCosmeticsHandler::NUM_SPECIALS] = '\0';

	// reset active cosmetics
	m_CurrentSkinMani = -1;
	m_CurrentGunDesign = -1;
	m_CurrentKnockout = -1;
}

void CBwPlayer::DisableCosmeticsForEvent()
{
	// remove active special entity (but keep ownership)
	if(m_pSpecialEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
		m_pSpecialEntity = nullptr;
	}
	m_CurrentSpecial = -1;
	m_SpecialExpireTick = 0;

	// remove flag entity
	if(m_pFlagEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pFlagEntity);
		m_pFlagEntity = nullptr;
	}
	m_FlagExpireTick = 0;

	// reset active visual slots (not ownership)
	m_CurrentSkinMani = -1;
	m_CurrentGunDesign = -1;
	m_CurrentKnockout = -1;
}

const char *CBwPlayer::GetPlayerSpecials()
{
	return m_aSpecialsOwned;
}

bool CBwPlayer::ToggleSpecial(int SpecialIndex)
{
	if(SpecialIndex < 0 || SpecialIndex >= CCosmeticsHandler::NUM_SPECIALS)
		return false;

	if(m_CurrentSpecial == SpecialIndex)
	{
		if(m_pSpecialEntity)
		{
			GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
			m_pSpecialEntity = nullptr;
		}
		m_CurrentSpecial = -1;
		return true;
	}

	if(m_CurrentSpecial != -1 && m_pSpecialEntity)
	{
		GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
		m_pSpecialEntity = nullptr;
		m_CurrentSpecial = -1;
	}

	if(SpecialIndex == CCosmeticsHandler::SPECIAL_BALL)
	{
		extern class CBall *CreateBall(CGameWorld *, vec2, int);
		// fallback: use direct constructor if available
		m_pSpecialEntity = new CBall(&GameServer()->m_World, GetCharacter() ? GetCharacter()->m_Pos : Player()->m_ViewPos, GetCid());
	}
	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_CROWN)
	{
		m_pSpecialEntity = new CCrown(&GameServer()->m_World, GetCid());
	}
	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_EPICCIRCLE)
	{
		m_pSpecialEntity = new CEpicCircle(&GameServer()->m_World, GetCharacter() ? GetCharacter()->m_Pos : Player()->m_ViewPos, GetCid());
	}

	else if(SpecialIndex == CCosmeticsHandler::SPECIAL_HALO)
	{
		m_pSpecialEntity = new CHalo(&GameServer()->m_World, GetCid());
	}
	else
		return false;

	m_CurrentSpecial = SpecialIndex;
	return true;
}

void CBwPlayer::ProcessWeeklyReward()
{
	if(!IsLoggedIn())
		return;

	time_t t = time(nullptr);
	struct tm tmres;
	time_localtime_safe(&t, &tmres);
	int TodayDate = (tmres.tm_year + 1900) * 10000 + (tmres.tm_mon + 1) * 100 + tmres.tm_mday;

	int LastClaim = GetWeeklyLastClaim();

	// Already claimed today (this account)
	if(LastClaim == TodayDate)
		return;

	// IP-based abuse guard: if another account from the same IP already claimed today, deny
	{
		char aAddrWithPort[NETADDR_MAXSTRSIZE];
		BwClientAddr(GameServer()->Server(), GetCid(), aAddrWithPort, sizeof(aAddrWithPort));
		// strip port: truncate at the last ':'
		char aIpKey[NETADDR_MAXSTRSIZE];
		str_copy(aIpKey, aAddrWithPort, sizeof(aIpKey));
		char *pLastColon = (char *)str_rchr(aIpKey, ':');
		if(pLastColon)
			*pLastColon = '\0';

		auto it = GameServer()->Bw().m_WeeklyRewardClaimedByIp.find(aIpKey);
		if(it != GameServer()->Bw().m_WeeklyRewardClaimedByIp.end() && it->second == TodayDate)
		{
			GameServer()->Bw().SendChatTarget(GetCid(), "[Daily Reward] You have already claimed today's reward!");
			return;
		}
	}

	// Check if streak is broken (more than 1 day gap)
	int Day = GetWeeklyDay();
	if(LastClaim > 0)
	{
		// Calculate day difference (approximate, works for consecutive days)
		int LastYear = LastClaim / 10000;
		int LastMonth = (LastClaim / 100) % 100;
		int LastDay = LastClaim % 100;

		struct tm tmLast = {};
		tmLast.tm_year = LastYear - 1900;
		tmLast.tm_mon = LastMonth - 1;
		tmLast.tm_mday = LastDay;
		tmLast.tm_hour = 12;
		time_t tLast = mktime(&tmLast);

		struct tm tmToday = {};
		tmToday.tm_year = tmres.tm_year;
		tmToday.tm_mon = tmres.tm_mon;
		tmToday.tm_mday = tmres.tm_mday;
		tmToday.tm_hour = 12;
		time_t tToday = mktime(&tmToday);

		double diffDays = difftime(tToday, tLast) / 86400.0;
		if(diffDays > 1.5) // More than 1 day gap -> reset streak
			Day = 0;
	}
	else
	{
		Day = 0; // First ever claim
	}

	// day is 0-indexed (0=day1, 1=day2, ..., 6=day7)
	// After day 7 (index 6), cycle back to day 0
	if(Day >= 7)
		Day = 0;

	// Grant the reward for current day
	char aBuf[256];
	switch(Day)
	{
	case 0: // Day 1: 15 BP
		SetPlayerBlockpoints(GetPlayerBlockpoints() + 15);
		str_copy(aBuf, "[Weekly Day 1/7] +15 Blockpoints! Come back tomorrow for more!", sizeof(aBuf));
		break;
	case 1: // Day 2: 30 BP
		SetPlayerBlockpoints(GetPlayerBlockpoints() + 30);
		str_copy(aBuf, "[Weekly Day 2/7] +30 Blockpoints!", sizeof(aBuf));
		break;
	case 2: // Day 3: 3 weaponkits
		SetPlayerWeaponkits(GetPlayerWeaponkits() + 3);
		str_copy(aBuf, "[Weekly Day 3/7] +3 Weaponkits!", sizeof(aBuf));
		break;
	case 3: // Day 4: 50 BP
		SetPlayerBlockpoints(GetPlayerBlockpoints() + 50);
		str_copy(aBuf, "[Weekly Day 4/7] +50 Blockpoints!", sizeof(aBuf));
		break;
	case 4: // Day 5: 5 weaponkits
		SetPlayerWeaponkits(GetPlayerWeaponkits() + 5);
		str_copy(aBuf, "[Weekly Day 5/7] +5 Weaponkits!", sizeof(aBuf));
		break;
	case 5: // Day 6: 2 hours of passive time
	{
		int PassiveSeconds = 2 * 3600; // 2 hours in seconds
		SetPlayerPassive(GetPlayerPassive() + PassiveSeconds);
		str_copy(aBuf, "[Weekly Day 6/7] +2 hours of Passive Protection!", sizeof(aBuf));
		break;
	}
	case 6: // Day 7: 150 BP + x2 EXP for 2h of ingame time
	{
		SetPlayerBlockpoints(GetPlayerBlockpoints() + 150);
		// x2 EXP boost for 2h of account playtime (in seconds)
		long long BoostEndPlaytime = GetPlayerPlaytime() + 2LL * 3600;
		SetWeeklyExpBoostUntil(BoostEndPlaytime);
		// Activate the multiplier for 2h of real playtime (120 minutes)
		AddExpMultiplier(200, 120);
		str_copy(aBuf, "[Weekly Day 7/7] +150 Blockpoints + x2 EXP for 2h (ingame time)! Streak complete!", sizeof(aBuf));
		break;
	}
	default:
		return;
	}

	GameServer()->Bw().SendChatTarget(GetCid(), aBuf);

	// Advance day and save
	SetWeeklyDay(Day + 1);
	SetWeeklyLastClaim(TodayDate);

	// Record this IP as having claimed today
	{
		char aAddrWithPort[NETADDR_MAXSTRSIZE];
		BwClientAddr(GameServer()->Server(), GetCid(), aAddrWithPort, sizeof(aAddrWithPort));
		char aIpKey[NETADDR_MAXSTRSIZE];
		str_copy(aIpKey, aAddrWithPort, sizeof(aIpKey));
		char *pLastColon = (char *)str_rchr(aIpKey, ':');
		if(pLastColon)
			*pLastColon = '\0';
		GameServer()->Bw().m_WeeklyRewardClaimedByIp[aIpKey] = TodayDate;
	}

	OnPlayerSave(false);
}

void CBwPlayer::AddPlayerExp(int Amount, bool ApplyMultiplier)
{
	// apply player-specific multipliers and optional weekend bonus
	float TotalMult = 1.0f;
	if(ApplyMultiplier)
		TotalMult *= GetExpMultiplier();
	if(g_Config.m_SvWeekendExpEnabled)
	{
		time_t t = time(nullptr);
		struct tm tmres;
		time_localtime_safe(&t, &tmres);
		int wday = tmres.tm_wday; // 0=Sunday, 6=Saturday
		if(wday == 0 || wday == 6)
		{
			float weekendMult = (float)g_Config.m_SvWeekendExpMultiplier / 100.0f;
			if(weekendMult < 0.01f)
				weekendMult = 0.01f;
			TotalMult *= weekendMult;
		}
	}
	Amount = (int)((float)Amount * TotalMult);

	// Clamp to prevent overflow / absurd values
	if(Amount < 0)
		Amount = 0;
	if(Amount > 10000)
		Amount = 10000;

	m_SessionExpGained += Amount;
	m_Account.m_Experience += Amount;

	m_Account.m_DirtyProgress = true;

	// Process level-ups iteratively (not recursively) to avoid stack overflow
	int LevelsGained = 0;
	const int MaxLevelsPerCall = 100; // safety cap
	int RemainingExp = GetPlayerExperience();
	while(RemainingExp >= NeededAccountExp(GetPlayerLevel()) && LevelsGained < MaxLevelsPerCall)
	{
		RemainingExp -= NeededAccountExp(GetPlayerLevel());
		SetPlayerLevel(GetPlayerLevel() + 1);
		LevelsGained++;
	}
	SetPlayerExperience(std::max(RemainingExp, 0));

	if(LevelsGained > 0)
	{
		CPlayer *pPlayer = GameServer()->Bw().GetPlayer(GetCid());
		if(pPlayer && pPlayer->GetCharacter())
		{
			pPlayer->GetCharacter()->SetEmote(EMOTE_HAPPY, Server()->Tick() + 2 * Server()->TickSpeed());
			CCharacter *pChar = pPlayer->GetCharacter();
			GameServer()->CreateFinishEffect(pChar->GetPos(), pChar->TeamMask());
			GameServer()->CreateSound(pChar->GetPos(), SOUND_CTF_CAPTURE, -1);
		}

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[LevelUp+]: You are now level %d!", GetPlayerLevel());
		GameServer()->Bw().SendChatTarget(GetCid(), aBuf);

		// Check all levels gained for milestone rewards
		int StartLevel = GetPlayerLevel() - LevelsGained;
		for(int l = StartLevel + 1; l <= GetPlayerLevel(); l++)
		{
			if(l % 50 == 0)
			{
				SetPlayerBlockpoints(GetPlayerBlockpoints() + 300);
				GameServer()->Bw().SendChatTarget(GetCid(), "[LevelUp+]: You've received 300bp !");
			}
		}

		OnPlayerSave(false);
	}
}

void CBwPlayer::AddExpMultiplier(int ModifierPercent, int Duration)
{
	auto it = m_ExpModifiers.find(ModifierPercent);
	if(it == m_ExpModifiers.end())
		m_ExpModifiers.emplace(ModifierPercent, Server()->Tick() + Duration * 60 * Server()->TickSpeed());
	else
		it->second += Duration * 60 * Server()->TickSpeed();

	CalculateExpMultiplier();
}
void CBwPlayer::CalculateExpMultiplier()
{
	if(m_ExpModifiers.empty())
	{
		m_CurrentExpMultiplier = 1.0f;
		return;
	}

	float Multiplier = 0.0f;
	switch(g_Config.m_SvBlockExperienceMultiplierStacking)
	{
	case HIGHEST:
	{
		int Highest = 0;
		for(const auto &ExpMultiplier : m_ExpModifiers)
			Highest = Highest > ExpMultiplier.first ? Highest : ExpMultiplier.first;
		m_CurrentExpMultiplier = (float)Highest / 100.0f;
		break;
	}
	case ADDITIVE:
	{
		Multiplier = 1.0f;
		for(const auto &ExpMultiplier : m_ExpModifiers)
			Multiplier += (ExpMultiplier.first - 100) / 100.0f;
		m_CurrentExpMultiplier = Multiplier;
		break;
	}
	case LOGARITHMIC:
	{
		Multiplier = 1.0f;
		for(const auto &ExpMultiplier : m_ExpModifiers)
			Multiplier *= (ExpMultiplier.first) / 100.0f;
		// Keep baseline at 1.0 and apply diminishing returns via logarithm
		m_CurrentExpMultiplier = 1.0f + log2f(Multiplier);
		if(m_CurrentExpMultiplier < 1.0f)
			m_CurrentExpMultiplier = 1.0f;
		break;
	}
	case MULTIPLICATIVE:
	{
		Multiplier = 1.0f;
		for(const auto &ExpMultiplier : m_ExpModifiers)
			Multiplier *= (ExpMultiplier.first) / 100.0f;
		m_CurrentExpMultiplier = Multiplier;
		break;
	}
	default:
		m_CurrentExpMultiplier = 1.0f;
		break;
	}
}


void CBwPlayer::SendBroadcastImp(const char *pMessage)
{
	// Only send Broadcast if message is different or timed out
	if(!str_comp(m_BroadcastData.m_aMessage, pMessage) && m_BroadcastData.m_SentTick + Server()->TickSpeed() * 9 > Server()->Tick())
		return;

	str_copy(m_BroadcastData.m_aMessage, pMessage, sizeof(m_BroadcastData.m_aMessage));
	m_BroadcastData.m_SentTick = Server()->Tick();

	GameServer()->Bw().SendBroadcast(pMessage, GetCid());
}


void CBwPlayer::Tick()
{
	if(!m_AccountQueryResult.empty() && m_AccountQueryResult.front() && m_AccountQueryResult.front()->m_Completed)
	{
		BWProcessAccountsResult(*m_AccountQueryResult.front());
		m_AccountQueryResult.pop();
	}
	if(m_PendingLoginCoreSave && IsLoggedIn())
	{
		if(Server()->Tick() > m_PendingLoginSaveTick)
		{
			GameServer()->Bw().Accounts()->Save(GetCid(), &m_Account);
			// dbg_msg("account", "login snapshot saved for AccountId=%d (deferred)", m_Account.m_Id);
			m_PendingLoginCoreSave = false;
		}
	}
	if(!m_AdminCommandQueryResult.empty() && m_AdminCommandQueryResult.front() && m_AdminCommandQueryResult.front()->m_Completed)
	{
		BWProcessAdminCommandResult(*m_AdminCommandQueryResult.front());
		m_AdminCommandQueryResult.pop();
	}
	if(!m_ClanQueryResult.empty() && m_ClanQueryResult.front() && m_ClanQueryResult.front()->m_Completed)
	{
		BWProcessClansResult(*m_ClanQueryResult.front());
		m_ClanQueryResult.pop();
	}

	if(m_TelekinesisEnabled && GetCharacter() && GetCharacter()->IsAlive())
	{
		const CNetObj_PlayerInput &Input = GetCharacter()->Bw().GetLatestInput();
		bool Firing = (Input.m_Fire & 1) != 0;

		if(Firing)
		{
			// cursor position in world coordinates
			vec2 CursorWorldPos = GetCharacter()->m_Pos + vec2(Input.m_TargetX, Input.m_TargetY);

			if(m_TelekinesisTarget == -1)
			{
				// try to grab a player near cursor
				float ClosestDist = 32.0f;
				int ClosestId = -1;
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(i == GetCid())
						continue;
					CCharacter *pChar = GameServer()->GetPlayerChar(i);
					if(!pChar || !pChar->IsAlive())
						continue;
					float Dist = distance(CursorWorldPos, pChar->m_Pos);
					if(Dist < ClosestDist)
					{
						ClosestDist = Dist;
						ClosestId = i;
					}
				}
				if(ClosestId != -1)
				{
					m_TelekinesisTarget = ClosestId;
				}
			}

			if(m_TelekinesisTarget != -1)
			{
				CCharacter *pTarget = GameServer()->GetPlayerChar(m_TelekinesisTarget);
				if(pTarget && pTarget->IsAlive())
				{
					// mark target as held; gravity=0 + position override happen in Character tick
					pTarget->Bw().m_TelekinesisHeld = true;
					pTarget->Bw().m_TelekinesisTargetPos = CursorWorldPos;
					pTarget->Bw().Core().m_Pos = CursorWorldPos;
					pTarget->Bw().Core().m_Vel = vec2(0, 0);
					pTarget->m_Pos = CursorWorldPos;
				}
				else
				{
					// target died or disconnected
					m_TelekinesisTarget = -1;
				}
			}
		}
		else
		{
			// fire released: drop the target
			m_TelekinesisTarget = -1;
		}
	}
	else if(m_TelekinesisEnabled && !GetCharacter()) // ?
	{
		m_TelekinesisTarget = -1;
	}


	if(m_ClanSaveCooldown + Server()->TickSpeed() * g_Config.m_SvClanSaveInterval < Server()->Tick())
	{
		if(IsLoggedIn() && GetClanId() > 0)
		{
			GameServer()->Bw().Clans()->SaveClan(GetCid(), GetClanId());
			m_ClanSaveCooldown = Server()->Tick();
		}
	}

	{
		bool RecalculationNeeded = false;
		auto it = m_ExpModifiers.begin();
		for(; it != m_ExpModifiers.end();)
		{
			if(it->second <= Server()->Tick())
			{
				it = m_ExpModifiers.erase(it);
				RecalculationNeeded = true;
			}
			else
			{
				++it;
			}
		}

		// expire temporary special if its time has come
		if(m_SpecialExpireTick && Server()->Tick() >= m_SpecialExpireTick)
		{
			if(m_pSpecialEntity)
			{
				GameServer()->m_World.RemoveEntity(m_pSpecialEntity);
				m_pSpecialEntity = nullptr;
			}
			m_CurrentSpecial = -1;
			m_SpecialExpireTick = 0;
		}

		// expire flag reward if needed
		if(m_pFlagEntity && m_FlagExpireTick && Server()->Tick() >= m_FlagExpireTick)
		{
			GameServer()->m_World.RemoveEntity(m_pFlagEntity);
			m_pFlagEntity = nullptr;
			m_FlagExpireTick = 0;
		}
		if(RecalculationNeeded)
			CalculateExpMultiplier();
	}
}


void CBwPlayer::Reset()
{
	m_LastLMBVoteCall = 0;
	// reset leaderboard capture state
	m_CaptureTopToMenu = false;
	m_CaptureTopCategory = -1;
	m_TopMessagesCount = 0;
	for(int i = 0; i < TOP_MAX_LINES; ++i)
		m_aTopMessages[i][0] = '\0';

	GameServer()->Bw().BlockTracker().StopTrackPlayer(GetCid());
	m_ExpModifiers.clear();
	CalculateExpMultiplier();
	m_SpecialExpireTick = 0;
	m_pFlagEntity = nullptr;
	m_FlagExpireTick = 0;

	// initialize owned specials default (VIP check will apply when player logs in)
	for(int i = 0; i < CCosmeticsHandler::NUM_SPECIALS; ++i)
		m_aSpecialsOwned[i] = '0';
	m_aSpecialsOwned[CCosmeticsHandler::NUM_SPECIALS] = '\0';
}

void CBwPlayer::OnDisconnect()
{
	ClearCosmetics();
}
