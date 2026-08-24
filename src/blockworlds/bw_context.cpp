#include "bw_context.h"

#include <base/bytes.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/server.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>

#include <generated/protocol7.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gamemodes/ddnet.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/version.h>

#include <blockworlds/accounts.h>
#include <blockworlds/bw_config.h>
#include <blockworlds/bw_gamecontroller.h>
#include <blockworlds/bw_player.h>
#include <blockworlds/bw_util.h>
#include <blockworlds/clans.h>
#include <blockworlds/common.h>
#include <blockworlds/components/agones/agones.h>
#include <blockworlds/components/ai/ai_bot.h>
#include <blockworlds/components/chatfilter/chat_filter.h>
#include <blockworlds/components/clientdetect/client_detect.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/components/events/1on1.h>
#include <blockworlds/components/events/tdm.h>
#include <blockworlds/components/oneonone_manager.h>
#include <blockworlds/components/port_proxy/port_proxy.h>
#include <blockworlds/components/promises.h>
#include <blockworlds/components/requests.h>
#include <blockworlds/components/vpndetection/vpn_detection.h>
#include <blockworlds/discord/webhook.h>
#include <blockworlds/votes/votemanager.h>
#include <blockworlds/whois.h>
#include <blockworlds/zones/passivezone.h>

#include <algorithm>

CBlockworlds::CBlockworlds(CGameContext *pGameServer) :
	m_pGameServer(pGameServer),
	m_BlockTracker(pGameServer)
{
}

CBlockworlds::~CBlockworlds() = default;

IServer *CBlockworlds::Server() const { return m_pGameServer->Server(); }
IConsole *CBlockworlds::Console() const { return m_pGameServer->Console(); }
IEngine *CBlockworlds::Engine() const { return m_pGameServer->Engine(); }
void CBlockworlds::Teleport(CCharacter *pChr, vec2 Pos) { m_pGameServer->Teleport(pChr, Pos); }

// Free helper the vote-menu code shares (was a file-static in gamecontext.cpp).
static void SetVoteDescriptionAtIndex(int *pIndex, const char *pStr, CNetMsg_Sv_VoteOptionListAdd *pOptionMsg)
{
	switch(*pIndex)
	{
	case 0: pOptionMsg->m_pDescription0 = pStr; break;
	case 1: pOptionMsg->m_pDescription1 = pStr; break;
	case 2: pOptionMsg->m_pDescription2 = pStr; break;
	case 3: pOptionMsg->m_pDescription3 = pStr; break;
	case 4: pOptionMsg->m_pDescription4 = pStr; break;
	case 5: pOptionMsg->m_pDescription5 = pStr; break;
	case 6: pOptionMsg->m_pDescription6 = pStr; break;
	case 7: pOptionMsg->m_pDescription7 = pStr; break;
	case 8: pOptionMsg->m_pDescription8 = pStr; break;
	case 9: pOptionMsg->m_pDescription9 = pStr; break;
	case 10: pOptionMsg->m_pDescription10 = pStr; break;
	case 11: pOptionMsg->m_pDescription11 = pStr; break;
	case 12: pOptionMsg->m_pDescription12 = pStr; break;
	case 13: pOptionMsg->m_pDescription13 = pStr; break;
	case 14: pOptionMsg->m_pDescription14 = pStr; break;
	}
	(*pIndex)++;
}

void CBlockworlds::BW_OnTick()
{
	// Only run component-based events now
	/*if(auto events = g_ComponentRegistry.Get<CEvents>(); events)
	{
		auto subs = events->GetSubComponents();
		for(auto &sub : subs)
		{
			CEventComponent *pEv = dynamic_cast<CEventComponent *>(sub.operator->());
			if(pEv)
				pEv->OnTick();
		}
	}*/

	if(m_pClans)
	{
		m_pClans->AutosaveTick();
	}

	// periodic top 3 session players broadcast (kills, deaths, best streak)
	// Uses BlockTracker hourly stats - not account-bound, covers all ingame players.
	if(g_Config.m_SvSessionStatsEnabled)
	{
		int64_t IntervalTicks = (int64_t)Server()->TickSpeed() * std::clamp(g_Config.m_SvSessionStatsInterval, 60, 86400);
		if(m_LastBestPlayerBroadcast == 0)
			m_LastBestPlayerBroadcast = Server()->Tick();
		if(Server()->Tick() - m_LastBestPlayerBroadcast >= IntervalTicks)
		{
			m_LastBestPlayerBroadcast = Server()->Tick();

			struct SSessionEntry
			{
				int m_ClientId;
				int m_Kills;
				int m_Deaths;
				int m_BestStreak;
			};

			SSessionEntry aEntries[MAX_CLIENTS];
			int EntryCount = 0;

			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				CPlayer *p = GameServer()->m_apPlayers[i];
				if(!p || !p->IsPlaying())
					continue;
				if(p->GetTeam() == TEAM_SPECTATORS)
					continue;

				const CBlockTracker::SHourlyStats &HS = BlockTracker().GetHourlyStats(i);
				if(!HS.m_Active)
					continue;

				int Kills = std::clamp(HS.m_Kills, 0, 999999);
				int Deaths = std::clamp(HS.m_Deaths, 0, 999999);
				int BestStreak = std::clamp(HS.m_BestStreak, 0, 9999);

				// score = kills + best streak bonus; must have at least 1 kill
				int Score = Kills + BestStreak * 2;
				if(Score <= 0)
					continue;

				aEntries[EntryCount].m_ClientId = i;
				aEntries[EntryCount].m_Kills = Kills;
				aEntries[EntryCount].m_Deaths = Deaths;
				aEntries[EntryCount].m_BestStreak = BestStreak;
				EntryCount++;
			}

			// sort descending by composite score
			for(int i = 0; i < EntryCount - 1; ++i)
			{
				for(int j = i + 1; j < EntryCount; ++j)
				{
					int ScoreI = aEntries[i].m_Kills + aEntries[i].m_BestStreak * 2;
					int ScoreJ = aEntries[j].m_Kills + aEntries[j].m_BestStreak * 2;
					if(ScoreJ > ScoreI)
					{
						SSessionEntry Tmp = aEntries[i];
						aEntries[i] = aEntries[j];
						aEntries[j] = Tmp;
					}
				}
			}

			int ShowCount = std::min(EntryCount, 3);
			if(ShowCount > 0)
			{
				GameServer()->SendChat(-1, TEAM_ALL, "------ Top Players This Hour ------");
				for(int i = 0; i < ShowCount; ++i)
				{
					const SSessionEntry &e = aEntries[i];
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf),
						"#%d %s | %d Kills | %d Deaths | Best Streak: %d",
						i + 1, Server()->ClientName(e.m_ClientId),
						e.m_Kills, e.m_Deaths, e.m_BestStreak);
					GameServer()->SendChat(-1, TEAM_ALL, aBuf);
				}
			}

			// reset hourly stats so each broadcast window is independent
			BlockTracker().ResetAllHourlyStats();
		}
	}
}

void CBlockworlds::ClearVotes(int ClientID) const
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	if(!pPlayer)
		return;
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, ClientID);
	pPlayer->m_SendVoteIndex = 0;
}

void CBlockworlds::ConSendSound(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Sound = pResult->GetInteger(0);
	int Target = pResult->GetInteger(1);
	pSelf->CreateSoundGlobal(Sound, Target);
}

void CBlockworlds::ConSetWeaponkits(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments() == 0)
	{
		pSelf->SendChat(-1, TEAM_ALL, pSelf->Bw().m_WeaponkitsAllowed ? "Weaponkits allowed: true" : "Weaponkits allowed: false");
		return;
	}
	const char *pArg = pResult->GetString(0);
	if(str_comp(pArg, "1") == 0 || str_comp_nocase(pArg, "on") == 0 || str_comp_nocase(pArg, "true") == 0)
		pSelf->Bw().m_WeaponkitsAllowed = true;
	else if(str_comp(pArg, "0") == 0 || str_comp_nocase(pArg, "off") == 0 || str_comp_nocase(pArg, "false") == 0)
		pSelf->Bw().m_WeaponkitsAllowed = false;
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Usage: set_weaponkits_allowed [0|1]");
		return;
	}

	g_Config.m_SvWeaponkitsAllowed = pSelf->Bw().m_WeaponkitsAllowed ? 1 : 0;
	if(pSelf->ConfigManager())
		pSelf->ConfigManager()->Save();

	pSelf->SendChat(-1, TEAM_ALL, pSelf->Bw().m_WeaponkitsAllowed ? "Weaponkits are now allowed on this server." : "Weaponkits are now disabled on this server.");

	pSelf->Bw().UpdateWeaponkitsVoteOption();
}

void CBlockworlds::ConWhoisId(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	if(ClientId < 0)
	{
		// econ
	}
	else if(pSelf->Server()->GetAuthedState(ClientId) <= 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Permission denied");
		return;
	}
	int64_t Now = pSelf->Server()->Tick();
	if(ClientId >= 0 && pSelf->Bw().m_aWhoisCooldown[ClientId] && Now < pSelf->Bw().m_aWhoisCooldown[ClientId])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Please wait before using whois again");
		return;
	}
	if(!pSelf->Bw().m_pWhoIs)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Whois not available");
		return;
	}
	if(pResult->NumArguments() < 1)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] Incorrect command usage. Correct format: whois_id <client id> [/32|/24|/16]");
		return;
	}
	int Target = pResult->GetInteger(0);
	if(!CheckClientId(Target))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] Invalid client id. Correct format: whois_id <client id> [/32|/24|/16]");
		return;
	}
	if(!pSelf->m_apPlayers[Target])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] Target client not online");
		return;
	}

	char aIp[NETADDR_MAXSTRSIZE] = {0};
	BwClientAddr(pSelf->Server(), Target, aIp, sizeof(aIp));
	if(aIp[0] == '\0')
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] Could not obtain IP for target client");
		return;
	}

	int Cutoff = 0;
	const char *pMask = pResult->NumArguments() >= 2 ? pResult->GetString(1) : nullptr;
	if(pMask && *pMask)
	{
		if(pMask[0] != '/')
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] CIDR mask must start with '/'. Allowed: /32, /24, /16");
			return;
		}
		int Bits = str_toint(pMask + 1);
		if(Bits == 32)
			Cutoff = 0;
		else if(Bits == 24)
			Cutoff = 1;
		else if(Bits == 16)
			Cutoff = 2;
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] Unsupported CIDR mask. Allowed: /32, /24, /16");
			return;
		}
	}

	auto pRes = std::make_shared<CWhoIsResult>();
	pRes->m_TargetClientId = ClientId;
	str_copy(pRes->m_aTag, "whois", sizeof(pRes->m_aTag));
	pSelf->Bw().m_vWhoisResults.push_back(pRes);
	pSelf->Bw().m_pWhoIs->CmdWhoisStr(ClientId, /*Mode=*/0, Cutoff, aIp, pRes);
	if(ClientId >= 0)
	{
		int Sec = std::clamp(g_Config.m_SvWhoisCooldownSec, 0, 300);
		pSelf->Bw().m_aWhoisCooldown[ClientId] = Sec > 0 ? Now + Sec * pSelf->Server()->TickSpeed() : 0;
	}
}

void CBlockworlds::ConWhoisIp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	if(ClientId < 0)
	{
		// econ
	}
	else if(pSelf->Server()->GetAuthedState(ClientId) <= 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Permission denied");
		return;
	}
	int64_t Now = pSelf->Server()->Tick();
	if(ClientId >= 0 && pSelf->Bw().m_aWhoisCooldown[ClientId] && Now < pSelf->Bw().m_aWhoisCooldown[ClientId])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Please wait before using whois again");
		return;
	}
	if(!pSelf->Bw().m_pWhoIs)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Whois not available");
		return;
	}
	if(pResult->NumArguments() < 1)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] Incorrect command usage. Correct format: whois_ip <IP> [/32|/24|/16]");
		return;
	}
	const char *pIp = pResult->GetString(0);
	if(!pIp || !*pIp)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] IP must not be empty. Correct format: whois_ip <IP> [/32|/24|/16]");
		return;
	}
	int Cutoff = 0;
	const char *pMask = pResult->NumArguments() >= 2 ? pResult->GetString(1) : nullptr;
	if(pMask && *pMask)
	{
		if(pMask[0] != '/')
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] CIDR mask must start with '/'. Allowed: /32, /24, /16");
			return;
		}
		int Bits = str_toint(pMask + 1);
		if(Bits == 32)
			Cutoff = 0;
		else if(Bits == 24)
			Cutoff = 1;
		else if(Bits == 16)
			Cutoff = 2;
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] Unsupported CIDR mask. Allowed: /32, /24, /16");
			return;
		}
	}
	auto pRes = std::make_shared<CWhoIsResult>();
	pRes->m_TargetClientId = ClientId;
	str_copy(pRes->m_aTag, "whois", sizeof(pRes->m_aTag));
	pSelf->Bw().m_vWhoisResults.push_back(pRes);
	pSelf->Bw().m_pWhoIs->CmdWhoisStr(ClientId, /*Mode=*/0, Cutoff, pIp, pRes);
	if(ClientId >= 0)
	{
		int Sec = std::clamp(g_Config.m_SvWhoisCooldownSec, 0, 300);
		pSelf->Bw().m_aWhoisCooldown[ClientId] = Sec > 0 ? Now + Sec * pSelf->Server()->TickSpeed() : 0;
	}
}

void CBlockworlds::ConWhoisName(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	if(ClientId < 0)
	{
		// econ, no cooldown (sry pac)
	}
	else if(pSelf->Server()->GetAuthedState(ClientId) <= 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Permission denied");
		return;
	}
	int64_t Now = pSelf->Server()->Tick();
	if(ClientId >= 0 && pSelf->Bw().m_aWhoisCooldown[ClientId] && Now < pSelf->Bw().m_aWhoisCooldown[ClientId])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Please wait before using whois again");
		return;
	}
	if(!pSelf->Bw().m_pWhoIs)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Whois not available");
		return;
	}
	if(pResult->NumArguments() < 1)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] Incorrect command usage. Correct format: whois_name <name or pattern>");
		return;
	}
	const char *pName = pResult->GetString(0);
	if(!pName || !*pName)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "[Error] Name must not be empty. Correct format: whois_name <name or pattern>");
		return;
	}
	auto pRes = std::make_shared<CWhoIsResult>();
	pRes->m_TargetClientId = ClientId;
	str_copy(pRes->m_aTag, "whois", sizeof(pRes->m_aTag));
	pSelf->Bw().m_vWhoisResults.push_back(pRes);
	// Mode=1 for name
	pSelf->Bw().m_pWhoIs->CmdWhoisStr(ClientId, /*Mode=*/1, /*Cutoff=*/0, pName, pRes);
	if(ClientId >= 0)
	{
		int Sec = std::clamp(g_Config.m_SvWhoisCooldownSec, 0, 300);
		pSelf->Bw().m_aWhoisCooldown[ClientId] = Sec > 0 ? Now + Sec * pSelf->Server()->TickSpeed() : 0;
	}
}

void CBlockworlds::ConWhoisPurge(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->m_ClientId;
	if(ClientId >= 0 && pSelf->Server()->GetAuthedState(ClientId) <= 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Permission denied");
		return;
	}
	if(!pSelf->Bw().m_pWhoIs)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", "Whois not available");
		return;
	}
	int Months = g_Config.m_SvWhoisRetentionMonths;
	if(pResult->NumArguments() >= 1)
		Months = std::clamp(pResult->GetInteger(0), 0, 120);
	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), "Scheduling immediate whois purge (>%d months)", Months);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "whois", aMsg);
	pSelf->Bw().m_pWhoIs->PurgeNow(Months);
}

void CBlockworlds::CreateExplosionVisual(vec2 Pos, CClientMask Mask) const
{
	// purely visual: grenade-explosion particle + sound, no force/damage applied
	CNetEvent_Explosion *pEvent = GameServer()->m_Events.Create<CNetEvent_Explosion>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
	GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE, Mask);
}

void CBlockworlds::CreateStripline(char *pDst, int DstSize, const char *pTitle)
{
	int TitleLen = str_length(pTitle);
	int StripSideLen = fmin(15, (DstSize / 2) - TitleLen - 3);

	mem_zero(pDst, DstSize);

	for(int i = 0; i < StripSideLen; i++)
		str_append(pDst, "#", DstSize);

	str_append(pDst, " ", DstSize);
	str_append(pDst, pTitle, DstSize);
	str_append(pDst, " ", DstSize);

	for(int i = 0; i < StripSideLen; i++)
		str_append(pDst, "#", DstSize);
}

CPlayer *CBlockworlds::GetPlayer(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return nullptr;
	return GameServer()->m_apPlayers[ClientID];
}

CPlayer *CBlockworlds::GetPlayerByName(const char *pName) const
{
	CPlayer *pPlayer = nullptr;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && !str_comp(pName, Server()->ClientName(i)))
		{
			pPlayer = GameServer()->m_apPlayers[i];
			break;
		}
	}
	return pPlayer;
}

bool CBlockworlds::HandleCosmeticsVote(const CNetMsg_Cl_CallVote *pMsg, int ClientId) const
{
	CPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer)
		return false;

	if(!g_Config.m_SvVotemenuEnabled)
		return false;

	// block vote menu interactions for 1on1 players and active event participants
	// during prep, allow through so the duel config menu still works
	if(auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>())
	{
		if(auto Match = Mgr->GetMatchForPlayer(ClientId))
			if(Match->GetState() != COneOnOneEvent::EEventState::Preparation)
				return true; // consume silently
	}
	if(auto Events = g_ComponentRegistry.Get<CEvents>(); Events)
	{
		if(auto Active = Events->GetActiveEvent(); Active)
		{
			const auto &Parts = Active->Participants();
			if(std::find(Parts.begin(), Parts.end(), ClientId) != Parts.end())
				return true;
		}
	}

	return g_VoteManager.HandleVote(pPlayer, pMsg->m_pValue, ClientId, GameServer());
}

SHA256_DIGEST CBlockworlds::HashPassword(const char *pPassword)
{
	SHA256_CTX Sha256Ctx;
	sha256_init(&Sha256Ctx);
	sha256_update(&Sha256Ctx, pPassword, str_length(pPassword));
	return sha256_finish(&Sha256Ctx);
}

void CBlockworlds::HoldJoinMessage(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameServer()->m_apPlayers[ClientId])
		return;

	GameServer()->m_apPlayers[ClientId]->Bw().m_EntryChecksPending = true;
}

void CBlockworlds::PreShutdownFlush()
{
	static bool s_Ran = false;
	if(s_Ran)
		return;
	s_Ran = true;
	int AccountsQueued = 0;
	std::vector<std::shared_ptr<ISqlResult>> Results;
	Results.reserve(256);
	if(m_pAccounts)
	{
		m_pAccounts->BeginShutdownFlush();
		m_pAccounts->BeginShutdownCollection(Results);
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			CPlayer *pPl = GameServer()->m_apPlayers[i];
			if(!pPl || !pPl->Bw().IsLoggedIn())
				continue;
			CAccountData &Acc = pPl->Bw().m_Account;
			int64_t SessionTicks = Server()->Tick() - pPl->m_JoinTick;
			if(SessionTicks > 0)
			{
				int64_t SessionSeconds = SessionTicks / Server()->TickSpeed();
				if(SessionSeconds > 0)
				{
					Acc.m_Playtime += SessionSeconds;
					Acc.m_DirtyProgress = true;
				}
			}
			const char *pCurName = Server()->ClientName(i);
			if(str_comp(pCurName, Acc.m_aLastName) != 0)
			{
				str_copy(Acc.m_aLastName, pCurName, sizeof(Acc.m_aLastName));
				Acc.m_DirtyCore = true;
			}
			if(str_comp(pPl->m_TeeInfos.m_aSkinName, Acc.m_aLastSkin) != 0)
			{
				str_copy(Acc.m_aLastSkin, pPl->m_TeeInfos.m_aSkinName, sizeof(Acc.m_aLastSkin));
				Acc.m_DirtyCore = true;
			}
			if(pPl->m_TeeInfos.m_ColorBody != Acc.m_LastBodyColor)
			{
				Acc.m_LastBodyColor = pPl->m_TeeInfos.m_ColorBody;
				Acc.m_DirtyCore = true;
			}
			if(pPl->m_TeeInfos.m_ColorFeet != Acc.m_LastFeetColor)
			{
				Acc.m_LastFeetColor = pPl->m_TeeInfos.m_ColorFeet;
				Acc.m_DirtyCore = true;
			}
			char aIp[48] = {0};
			BwClientAddr(Server(), i, aIp, sizeof(aIp));
			if(aIp[0] && str_comp(aIp, Acc.m_aAddress) != 0)
			{
				str_copy(Acc.m_aAddress, aIp, sizeof(Acc.m_aAddress));
				Acc.m_DirtyCore = true;
			}
			m_pAccounts->Save(i, &Acc);
			AccountsQueued++;
		}

		m_pAccounts->ClearLogins();
		m_pAccounts->EndShutdownCollection();
	}
	int ClansQueued = 0;
	if(m_pClans)
	{
		m_pClans->BeginShutdownCollection(Results);
		ClansQueued = m_pClans->SaveAllClansOnShutdown();
		m_pClans->EndShutdownCollection();
	}
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "pre-shutdown flush: queued %d account saves, %d clan saves", AccountsQueued, ClansQueued);
	dbg_msg("shutdown", "%s", aBuf);

	int64_t Start = time_get();
	const int64_t Timeout = time_freq() * 5;
	int LastPct = -1;
	while(time_get() - Start < Timeout)
	{
		int PendingLocal = 0;
		for(auto &r : Results)
			if(r && !r->m_Completed.load(std::memory_order_relaxed))
				PendingLocal++;
		int Total = (int)Results.size();
		int Done = Total - PendingLocal;
		int Pct = Total ? (Done * 100 / Total) : 100;
		if(PendingLocal == 0)
			break;
		if(Pct != LastPct && (time_get() - Start) > time_freq() / 4)
		{
			dbg_msg("shutdown", "flush progress: %d%% (%d/%d done)", Pct, Done, Total);
			LastPct = Pct;
		}
		for(int Spin = 0; Spin < 2000; ++Spin)
		{
#if defined(CONF_FAMILY_WINDOWS)
			_ReadWriteBarrier();
#else
			asm volatile("");
#endif
		}
		thread_yield();
	}
	// final synchronous flush for any remaining dirty accounts
	if(m_pAccounts)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			CPlayer *pPl = GameServer()->m_apPlayers[i];
			if(!pPl || !pPl->Bw().IsLoggedIn())
				continue;
			CAccountData &Acc = pPl->Bw().m_Account;
			if(!Acc.m_DirtyCore && !Acc.m_DirtyProgress && !Acc.m_DirtyInventory && !Acc.m_DirtyRanked)
				continue;
			bool Ok = m_pAccounts->SyncSaveBlocking(i, Acc, 500);
			if(Ok)
			{
				Acc.m_DirtyCore = Acc.m_DirtyProgress = Acc.m_DirtyInventory = Acc.m_DirtyRanked = false;
				dbg_msg("shutdown", "final sync save ok for account %d", Acc.m_Id);
			}
			else
			{
				dbg_msg("shutdown", "final sync save FAILED for account %d", Acc.m_Id);
			}
		}
	}
	int Pending = 0;
	for(auto &r : Results)
		if(r && !r->m_Completed.load(std::memory_order_relaxed))
			Pending++;
	if(Pending)
	{
		dbg_msg("shutdown", "pre-shutdown flush: %d queries still pending after timeout", Pending);
	}
}

bool CBlockworlds::DeferVote(const char *pDescription, const char *pCommand)
{
	if(m_ComponentsQueue.empty())
		return false;
	m_vDeferredVotes.emplace_back(pDescription, pCommand);
	return true;
}

void CBlockworlds::ProcessComponentsQueue()
{
	while(!m_ComponentsQueue.empty())
	{
		auto ComponentName = m_ComponentsQueue.front();
		m_ComponentsQueue.pop();

		auto pComponent = g_ComponentRegistry.Create(ComponentName, GameServer());
		if(!pComponent)
		{
			dbg_msg("Components", "Component creation failed: %s", ComponentName.c_str());
			continue;
		}
		dbg_msg("Components", "Component created: %s (%p)", pComponent->GetName(), &*pComponent);
	}

	// The components have registered their commands now, so the votes that were
	// waiting on them can be validated. Take the list first: the queue is empty
	// at this point, so anything still invalid reports its error normally
	// instead of being held again.
	std::vector<std::pair<std::string, std::string>> vVotes;
	vVotes.swap(m_vDeferredVotes);
	for(const auto &[Description, Command] : vVotes)
		GameServer()->AddVote(Description.c_str(), Command.c_str());
}

void CBlockworlds::RegisterBlockworldsChatCommands() const
{
	Console()->Register("register", "s[username] s[password]", CFGFLAG_CHAT, ConRegister, GameServer(), "Create a new account.");
	Console()->Register("login", "s[username] s[password]", CFGFLAG_CHAT, ConLogin, GameServer(), "Log in to your account.");

	Console()->Register("logout", "", CFGFLAG_CHAT, ConAccountLogout, GameServer(), "Log out of your MySQL account.");
	Console()->Register("password", "s[oldpassword] s[newpassword]", CFGFLAG_CHAT, ConChangePassword, GameServer(), "Change your account password.");
	Console()->Register("exp", "", CFGFLAG_CHAT, ConExp, GameServer(), "Display your current experience progress.");

	Console()->Register("blockpoints", "", CFGFLAG_CHAT, ConDisplayBlockpoints, GameServer(), "Show your current blockpoints.");
	Console()->Register("bp", "", CFGFLAG_CHAT, ConDisplayBlockpoints, GameServer(), "Show your current blockpoints.");
	Console()->Register("give_bp", "s[player name] i[amount]", CFGFLAG_CHAT, ConGiveBlockpointsRequest, GameServer(), "Offer to transfer blockpoints to another player (requires their acceptance).");
	Console()->Register("accept_bp", "?s[player name]", CFGFLAG_CHAT, ConAcceptBlockpointsRequest, GameServer(), "Accept a pending blockpoints transfer.");
	Console()->Register("decline_bp", "?s[player name]", CFGFLAG_CHAT, ConDeclineBlockpointsRequest, GameServer(), "Decline a pending blockpoints transfer.");
	Console()->Register("profile", "?s[username]", CFGFLAG_CHAT, ConDisplayProfile, GameServer(), "Display your own or another player's profile.");
	Console()->Register("getcid", "s[player name]", CFGFLAG_CHAT, ConGetCid, GameServer(), "Get a player's client id by name.");

	Console()->Register("deathnote", "s[username]", CFGFLAG_CHAT, ConDeathnote, GameServer(), "Use one of your deathnote pages.");
	Console()->Register("passiveremover", "s[username]", CFGFLAG_CHAT, ConPassiveRemover, GameServer(), "Use a passive remover on a player to strip their passive.");
	Console()->Register("weapons", "", CFGFLAG_CHAT, ConWeaponKit, GameServer(), "Display how many weapon kits you have.");
	Console()->Register("weaponkit", "", CFGFLAG_CHAT, ConWeaponKit, GameServer(), "Display how many weapon kits you have.");

	Console()->Register("toplevel", "", CFGFLAG_CHAT, ConDisplayTopLevel, GameServer(), "Show the leaderboard of top-level players.");
	Console()->Register("topbp", "", CFGFLAG_CHAT, ConDisplayTopBlockpoints, GameServer(), "Display the leaderboard of top blockpoints.");
	Console()->Register("topks", "", CFGFLAG_CHAT, ConDisplayTopKillStreak, GameServer(), "Show the leaderboard for top kill streaks.");
	Console()->Register("topclans", "", CFGFLAG_CHAT, ConDisplayTopClans, GameServer(), "Display the top clans leaderboard.");
	// acc_integrity removed (not used)

	Console()->Register("contributors", "", CFGFLAG_CHAT, ConContributors, GameServer(), "Show thanks to Blockworlds contributors.");
	// upstream dropped /credits entirely; Blockworlds keeps its own
	Console()->Register("credits", "", CFGFLAG_CHAT, ConCredits, GameServer(), "Show who made this server.");

	Console()->Register("yes", "", CFGFLAG_CHAT, ConShopPurchase, GameServer(), "Confirm the pending shop purchase.");
	Console()->Register("no", "", CFGFLAG_CHAT, ConShopDecline, GameServer(), "Cancel the pending shop purchase.");

	Console()->Register("clan_create", "s[clanname]", CFGFLAG_CHAT, ConClanCreate, GameServer(), "Create a new clan with the given name.");
	Console()->Register("clan_delete", "", CFGFLAG_CHAT, ConClanDelete, GameServer(), "Delete your clan (leaders only).");
	Console()->Register("clan_leave", "", CFGFLAG_CHAT, ConClanLeave, GameServer(), "Leave your current clan.");
	Console()->Register("clan_kick", "s[username]", CFGFLAG_CHAT, ConClanRemove, GameServer(), "Remove the specified user from your clan.");
	Console()->Register("clan_role", "s[username] s[role]", CFGFLAG_CHAT, ConClanRole, GameServer(), "Assign a clan role to a member (member | coleader). Leader transfer: /clan_transfer.");
	Console()->Register("clan_rename", "s[newname]", CFGFLAG_CHAT, ConClanRename, GameServer(), "Rename your clan (leader only).");
	Console()->Register("clan_transfer", "s[newname]", CFGFLAG_CHAT, ConClanTransfer, GameServer(), "Transfer leadership to another member.");

	Console()->Register("clan_invite", "s[username]", CFGFLAG_CHAT, ConClanInvite, GameServer(), "Invite a user to join your clan.");
	Console()->Register("clan_accept", "", CFGFLAG_CHAT, ConClanAccept, GameServer(), "Accept a pending clan invitation.");
	Console()->Register("clan_decline", "", CFGFLAG_CHAT, ConClanDecline, GameServer(), "Decline a pending clan invitation.");

	// confirmations for delete/kick
	Console()->Register("clan_yes", "", CFGFLAG_CHAT, ConClanYes, GameServer(), "Confirm the last pending clan action (delete/kick).");
	Console()->Register("clan_no", "", CFGFLAG_CHAT, ConClanNo, GameServer(), "Cancel the last pending clan action (delete/kick).");

	Console()->Register("clan_exp", "", CFGFLAG_CHAT, ConClanExp, GameServer(), "Display the current experience progress of your clan.");

	Console()->Register("clan", "", CFGFLAG_CHAT, ConClanHelp, GameServer(), "Show clan system information and commands.");
	Console()->Register("account", "", CFGFLAG_CHAT, ConAccountHelp, GameServer(), "Show account system information and commands.");
	Console()->Register("clan_list", "", CFGFLAG_CHAT, ConClanList, GameServer(), "List members of your clan (up to 25).");

	// events
	Console()->Register("1on1", "s[player name] ?i[wager]", CFGFLAG_CHAT, Con1on1, GameServer(), "Fight against another player");
	Console()->Register("accept", "?r[player name]", CFGFLAG_CHAT, Con1on1Accept, GameServer(), "Accept the 1vs1 request from player r");
	Console()->Register("decline", "?r[player name]", CFGFLAG_CHAT, Con1on1Decline, GameServer(), "Decline the 1vs1 request from player r");
	Console()->Register("ready", "", CFGFLAG_CHAT, Con1on1Ready, GameServer(), "Ready up for 1on1 match during warmup");
	Console()->Register("leave", "", CFGFLAG_CHAT, ConLeaveEvent, GameServer(), "Leave current event");

	Console()->Register("pages", "", CFGFLAG_CHAT, ConDisplayPages, GameServer(), "Show how many deathnote pages you have.");

	Console()->Register("passive", "", CFGFLAG_CHAT, ConPassive, GameServer(), "Shows how many seconds of passive protection you have left");
}

void CBlockworlds::ReleaseJoinMessage(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameServer()->m_apPlayers[ClientId])
		return;

	GameServer()->m_apPlayers[ClientId]->Bw().m_EntryChecksPending = false;
	SendPendingJoinMessage(ClientId);
}

void CBlockworlds::RemoveVoteByDescription(const char *pDescription) const
{
	CVoteOptionServer *pOption = GameServer()->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp(pOption->m_aDescription, pDescription) == 0)
		{
			--GameServer()->m_NumVoteOptions;

			CHeap *pVoteOptionHeap = new CHeap();
			CVoteOptionServer *pVoteOptionFirst = nullptr;
			CVoteOptionServer *pVoteOptionLast = nullptr;
			int NumVoteOptions = GameServer()->m_NumVoteOptions;
			for(CVoteOptionServer *pSrc = GameServer()->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
			{
				if(pSrc == pOption)
					continue;

				int Len = str_length(pSrc->m_aCommand);
				CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len, alignof(CVoteOptionServer));
				pDst->m_pNext = nullptr;
				pDst->m_pPrev = pVoteOptionLast;
				if(pDst->m_pPrev)
					pDst->m_pPrev->m_pNext = pDst;
				pVoteOptionLast = pDst;
				if(!pVoteOptionFirst)
					pVoteOptionFirst = pDst;

				str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
				str_copy(pDst->m_aCommand, pSrc->m_aCommand, Len + 1);
			}

			delete GameServer()->m_pVoteOptionHeap;
			GameServer()->m_pVoteOptionHeap = pVoteOptionHeap;
			GameServer()->m_pVoteOptionFirst = pVoteOptionFirst;
			GameServer()->m_pVoteOptionLast = pVoteOptionLast;
			GameServer()->m_NumVoteOptions = NumVoteOptions;
			return;
		}
		pOption = pOption->m_pNext;
	}
}

void CBlockworlds::SendChatAccount(int AccountId, const char *pText, int VersionFlags) const
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
			continue;

		if(pPlayer->Bw().GetAccId() != AccountId)
			continue;

		SendChatTarget(i, pText, VersionFlags);
	}
}

void CBlockworlds::SendChatClan(int ClanId, const char *pText, int VersionFlags, int From) const
{
	CNetMsg_Sv_Chat Msg;
	// mark as team chat so clients render it in the team (green) color
	Msg.m_Team = 1;
	Msg.m_ClientId = From;
	Msg.m_pMessage = pText;

	if(g_Config.m_SvDemoChat)
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, SERVER_DEMO_CLIENT);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer || !pPlayer->Bw().IsLoggedIn())
			continue;

		if(pPlayer->Bw().GetClanId() != ClanId)
			continue;

		if(!((Server()->IsSixup(i) && (VersionFlags & CGameContext::FLAG_SIXUP)) ||
			   (!Server()->IsSixup(i) && (VersionFlags & CGameContext::FLAG_SIX))))
			continue;

		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
	}
}

void CBlockworlds::SendCosmeticsVoteOptions(int ClientID) const
{
	if(!g_Config.m_SvVotemenuEnabled)
		return;

	// don't send the menu to active event participants or 1on1 players (but allow during prep for duel config)
	if(auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>())
	{
		if(auto Match = Mgr->GetMatchForPlayer(ClientID))
			if(Match->GetState() != COneOnOneEvent::EEventState::Preparation)
				return;
	}
	if(auto Events = g_ComponentRegistry.Get<CEvents>(); Events)
	{
		if(auto Active = Events->GetActiveEvent(); Active)
		{
			const auto &Parts = Active->Participants();
			if(std::find(Parts.begin(), Parts.end(), ClientID) != Parts.end())
				return;
		}
	}

	CPlayer *pPlayer = GetPlayer(ClientID);

	g_VoteManager.SendOptions(pPlayer, ClientID, Server(), GameServer());
}

void CBlockworlds::SendPendingJoinMessage(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameServer()->m_apPlayers[ClientId])
		return;

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer->Bw().m_JoinMsgPending || pPlayer->Bw().m_EntryChecksPending)
		return;

	pPlayer->Bw().m_JoinMsgPending = false;
	// 0.7 clients only get the message from us if they did not print it themselves
	if(auto *pBwController = dynamic_cast<CGameControllerBW *>(GameServer()->m_pController))
	{
		pBwController->SendJoinMessage(pPlayer,
			CGameContext::FLAG_SIX | (pPlayer->Bw().m_JoinMsgSilentForSixup ? CGameContext::FLAG_SIXUP : 0));
	}
}

void CBlockworlds::SetVoteDescriptionAtIndex(int *pIndex, const char *pStr, CNetMsg_Sv_VoteOptionListAdd *pOptionMsg)
{
	// forward to the existing free function to keep logic in one place
	::SetVoteDescriptionAtIndex(pIndex, pStr, pOptionMsg);
}

void CBlockworlds::UpdateLMBVoteOption() const
{
	RemoveVoteByDescription("Start LMB event");

	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);

	for(auto &pPlayer : GameServer()->m_apPlayers)
	{
		if(pPlayer)
			pPlayer->m_SendVoteIndex = 0;
	}

	// Create a vote that triggers starting the LMB event via the events subsystem
	// GameServer()->AddVote("Start LMB event", "events_start lmb");
}

void CBlockworlds::UpdateWeaponkitsVoteOption() const
{
	RemoveVoteByDescription("Allow weaponkits");
	RemoveVoteByDescription("Disable weaponkits");

	if(m_WeaponkitsAllowed)
		GameServer()->AddVote("Disable weaponkits", "set_weaponkits_allowed 0");
	else
		GameServer()->AddVote("Allow weaponkits", "set_weaponkits_allowed 1");
}

bool CBlockworlds::isInEvent(int pPlayerID)
{
	// 1on1 manager is checked first (supports multiple concurrent 1on1s)
	if(auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>(); Mgr)
	{
		if(Mgr->GetMatchForPlayer(pPlayerID))
			return true;
	}

	if(auto Events = g_ComponentRegistry.Get<CEvents>(); Events)
	{
		for(auto &Sub : Events->GetSubComponents())
		{
			CEventComponent *pEv = dynamic_cast<CEventComponent *>(Sub.operator->());
			if(!pEv)
				continue;
			const auto &Parts = pEv->Participants();
			if(std::find(Parts.begin(), Parts.end(), pPlayerID) != Parts.end())
				return true;
		}
	}
	return false;
}

int CBlockworlds::GetTilePositions(int TileID, CGameContext *pSelf, std::vector<vec2> &Result)
{ // use a vector reference as a parameter
	// std::vector<vec2> result; // no need to declare a local vector
	if(TileID < 0 || TileID > 255)
		return 0;

	int Length = pSelf->Collision()->GetWidth() * pSelf->Collision()->GetHeight(); // get the length of the pointer array
	int FoundIndex = 0;
	for(int i = 0; i < Length; i++)
	{ // loop through all indices
		if(pSelf->Collision()->GetTileIndex(i) == TileID)
		{ // check if it matches the tile id

			int X = pSelf->Collision()->GetPos(i).x; // get the x coordinate from the index
			int Y = pSelf->Collision()->GetPos(i).y; // get the y coordinate from the index
			Result.emplace_back(X, Y); // use push_back to add elements to the vector
			FoundIndex++;
		}
	}
	// *_result = result; // no need to assign the vector to another pointer
	return FoundIndex;
	// return 0;
}

int CBlockworlds::GetSwitchTilePositions(int Type, int Delay, int Number, CGameContext *pSelf, std::vector<vec2> &Result)
{ // use a vector reference as a parameter
	// std::vector<vec2> result; // no need to declare a local vector
	if(Type < 0 || Type > 255)
	{
		return 0;
	}
	CMapItemLayerTilemap *SwitchLayer = pSelf->Collision()->Layers()->SwitchLayer();
	int Length = SwitchLayer->m_Width * SwitchLayer->m_Height; // get the length of the pointer array
	int FoundIndex = 0;
	for(int i = 0; i < Length; i++)
	{ // loop through all indices
		if(Type != -1)
		{
			if(pSelf->Collision()->GetSwitchType(i) != Type)
				continue;
		}
		if(Delay != -1 && pSelf->Collision()->GetSwitchDelay(i) != Delay)
			continue;
		if(Number != -1 && pSelf->Collision()->GetSwitchNumber(i) != Number)
			continue;

		int X = pSelf->Collision()->GetPos(i).x; // get the x coordinate from the index
		int Y = pSelf->Collision()->GetPos(i).y; // get the y coordinate from the index
		Result.emplace_back(X, Y); // use push_back to add elements to the vector
		FoundIndex++;
		// dbg_msg("Dummy-Shop", "switch found (ID: %d | Delay/Type: %d)", pSelf->Collision()->GetSwitchNumber(i), pSelf->Collision()->GetSwitchDelay(i));
		// }
	}

	// *_result = result; // no need to assign the vector to another pointer
	return FoundIndex;
	// return 0;
}

int CBlockworlds::GetNextClientID() const
{
	int ClientID = -1;
	for(int i = 0; i < g_Config.m_SvMaxClients; i++)
	{
		if(GameServer()->m_apPlayers[i])
			continue;

		ClientID = i;
		break;
	}

	return ClientID;
}

// ---------------------------------------------------------------------------
// Hooks called from upstream code. Each corresponds to one marked call site.
// ---------------------------------------------------------------------------

void CBlockworlds::OnTickEarly()
{
	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnTick();
}

void CBlockworlds::OnTickAfterController()
{
	BW_OnTick();
	m_Animations.Tick();
	m_ZoneManager.Tick();
	if(g_Config.m_SvShopServer)
	{
		if(m_ShopPreview.GameServer() == nullptr)
			m_ShopPreview.Init(GameServer());
		m_ShopPreview.Tick();
	}
}

void CBlockworlds::OnPostTick()
{
	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnPostTick();

	if(m_pWhoIs)
	{
		m_pWhoIs->SnapshotTick();
		m_pWhoIs->DrainAndPrintResults(); // print maintenance (purge) outputs
		for(auto It = m_vWhoisResults.begin(); It != m_vWhoisResults.end();)
		{
			auto &pRes = *It;
			if(pRes && pRes->m_Completed)
			{
				for(const auto &Line : pRes->m_vLines)
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pRes->m_aTag, Line.c_str());
				It = m_vWhoisResults.erase(It);
			}
			else
				++It;
		}
	}
}

bool CBlockworlds::SkipVoteParticipant(int ClientId) const
{
	// players in 1on1 prep don't participate in server votes
	auto pVoteMgr = g_ComponentRegistry.Get<COneOnOneManager>();
	if(!pVoteMgr)
		return false;
	if(auto Match = pVoteMgr->GetMatchForPlayer(ClientId))
		return Match->GetState() == COneOnOneEvent::EEventState::Preparation;
	return false;
}

void CBlockworlds::OnPlayerTick(int ClientId) const
{
	// runs once per second, from inside CGameContext::OnTick's player loop
	if(Server()->Tick() % Server()->TickSpeed() != 0)
		return;

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	CBwPlayer &Bw = pPlayer->Bw();

	if(Bw.IsLoggedIn())
	{
		Bw.SetPlayerPlaytime(Bw.GetPlayerPlaytime() + 1);

		int PassiveTime = Bw.GetPlayerPassive();
		if(PassiveTime > 0)
		{
			int NewPassive = PassiveTime - 1;
			Bw.SetPlayerPassive(NewPassive);
			if(NewPassive == 0)
				SendChatTarget(ClientId, "Your account passive protection has expired.");
		}
	}
	if(Bw.m_LocalPassiveDuration > 0)
	{
		Bw.m_LocalPassiveDuration--;
		if(Bw.m_LocalPassiveDuration == 0)
			SendChatTarget(ClientId, "Your passive protection has expired.");
	}
	if(Bw.m_PassiveRaceCooldown > 0)
	{
		Bw.m_PassiveRaceCooldown--;
		if(Bw.m_PassiveRaceCooldown == 0 && Bw.m_PassivePendingGrant)
		{
			Bw.m_PassivePendingGrant = false;
			if(!Bw.IsLoggedIn())
				Bw.m_LocalPassiveDuration = 7200;
			else
				Bw.SetPlayerPassive(7200);
			SendChatTarget(ClientId, "Wayblock Protection is now active for 2 hours!");
		}
	}
	if(Bw.m_PassiveRemoverUseCooldown > 0)
		Bw.m_PassiveRemoverUseCooldown--;
	if(Bw.m_RandomCosmeticDuration > 0)
	{
		Bw.m_RandomCosmeticDuration--;
		if(Bw.m_RandomCosmeticDuration == 0)
		{
			Bw.SetSkinMani(-1);
			Bw.SetKnockout(-1);
			Bw.SetGunDesign(-1);
			Bw.m_RandomCosmeticSkinmani = -1;
			Bw.m_RandomCosmeticKnockout = -1;
			Bw.m_RandomCosmeticGundesign = -1;
			SendChatTarget(ClientId, "Your random cosmetics have expired.");
		}
	}
}

/// True when the client takes part in the running event, whatever it is.
static bool IsInRunningEvent(int ClientId, const char **ppEventName = nullptr)
{
	auto pEvents = g_ComponentRegistry.Get<CEvents>();
	if(!pEvents)
		return false;
	auto pActive = pEvents->GetActiveEvent();
	if(!pActive || pActive->GetState() != CEventComponent::EEventState::Active)
		return false;
	const auto &Participants = pActive->Participants();
	if(std::find(Participants.begin(), Participants.end(), ClientId) == Participants.end())
		return false;
	if(ppEventName)
		*ppEventName = pActive->GetName();
	return true;
}

bool CBlockworlds::OnTeamChat(int ClientId, const char *pMessage) const
{
	CPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer)
		return true;

	if(!pPlayer->Bw().IsLoggedIn() || pPlayer->Bw().GetClanId() <= 0)
	{
		SendChatTarget(ClientId, "You must be logged in and in a clan to use clan chat");
		return true; // refusing is still consuming: never fall back to public chat
	}

	if(IsInRunningEvent(ClientId))
	{
		SendChatTarget(ClientId, "Clan chat is disabled in events.");
		return true;
	}

	char aCensored[256];
	GameServer()->CensorMessage(aCensored, pMessage, sizeof(aCensored));
	SendChatClan(pPlayer->Bw().GetClanId(), aCensored, 3 /* FLAG_SIX | FLAG_SIXUP */, ClientId);
	return true;
}

bool CBlockworlds::OnPublicChat(int ClientId, const char *pMessage) const
{
	// LMB and TDM are silent for the players in them, so nobody can coordinate
	// or stall; admins keep their voice
	if(Server()->GetAuthedState(ClientId) == 0)
	{
		const char *pEventName = nullptr;
		if(IsInRunningEvent(ClientId, &pEventName) && pEventName &&
			(str_comp(pEventName, "LMB") == 0 || str_comp(pEventName, "tdm") == 0))
			return true;
	}

	if(auto pChatFilter = g_ComponentRegistry.Get<CChatFilterComponent>())
	{
		if(pChatFilter->CheckAndMaybeMute(ClientId, pMessage))
			return true;
	}
	return false;
}

bool CBlockworlds::OwnsVoteUi(int ClientId) const
{
	auto pManager = g_ComponentRegistry.Get<COneOnOneManager>();
	if(!pManager)
		return false;
	auto pMatch = pManager->GetMatchForPlayer(ClientId);
	return pMatch && pMatch->GetState() == COneOnOneEvent::EEventState::Preparation;
}

bool CBlockworlds::VoteOnCooldown(int ClientId, const char *pCmd)
{
	if(!pCmd || !GetPlayer(ClientId))
		return false;

	const int64_t Now = Server()->Tick();
	// returns true and complains when the cooldown has not elapsed, otherwise
	// stamps it and lets the vote through
	const auto Check = [&](int64_t *pLast, int64_t Cooldown, const char *pWhat) {
		if(*pLast && Now < *pLast + Cooldown)
		{
			const int64_t Remaining = (*pLast + Cooldown - Now + Server()->TickSpeed() - 1) / Server()->TickSpeed();
			char aTime[64];
			FormatDuration((int)Remaining, aTime, sizeof(aTime));
			char aBuf[192];
			str_format(aBuf, sizeof(aBuf), "You must wait %s before calling %s again.", aTime, pWhat);
			SendChatTarget(ClientId, aBuf);
			return true;
		}
		*pLast = Now;
		return false;
	};

	if(str_find(pCmd, "set_weaponkits_allowed"))
	{
		// this one is configured in minutes
		return Check(&m_LastGlobalWeaponkitsVoteCall,
			(int64_t)Server()->TickSpeed() * 60 * g_Config.m_SvWeaponkitsVoteCoolDown, "the weaponkits vote");
	}

	if(str_find(pCmd, "events_start lmb") || str_find(pCmd, "events_start tdm") ||
		str_find(pCmd, "events_start zcatch") || str_find(pCmd, "events_start bombtag"))
	{
		return Check(&m_LastGlobalEventVoteCall,
			(int64_t)Server()->TickSpeed() * g_Config.m_SvEventVoteCoolDown, "an event start vote");
	}
	return false;
}

void CBlockworlds::FormatDuration(int Seconds, char *pBuf, size_t Size)
{
	const int Hours = Seconds / 3600;
	const int Minutes = (Seconds % 3600) / 60;
	const int Rest = Seconds % 60;
	const auto Plural = [](int N) { return N == 1 ? "" : "s"; };

	if(Hours > 0 && Minutes > 0)
		str_format(pBuf, Size, "%d hour%s %d minute%s", Hours, Plural(Hours), Minutes, Plural(Minutes));
	else if(Hours > 0)
		str_format(pBuf, Size, "%d hour%s", Hours, Plural(Hours));
	else if(Minutes > 0 && Rest > 0)
		str_format(pBuf, Size, "%d minute%s %d second%s", Minutes, Plural(Minutes), Rest, Plural(Rest));
	else if(Minutes > 0)
		str_format(pBuf, Size, "%d minute%s", Minutes, Plural(Minutes));
	else
		str_format(pBuf, Size, "%d second%s", Rest, Plural(Rest));
}

bool CBlockworlds::IsChatBlocked(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameServer()->m_apPlayers[ClientId])
		return false;
	if(Server()->GetAuthedState(ClientId) > 0)
		return false;
	const char *pEventName = nullptr;
	return IsInRunningEvent(ClientId, &pEventName) && pEventName &&
	       (str_comp(pEventName, "LMB") == 0 || str_comp(pEventName, "tdm") == 0);
}

bool CBlockworlds::OnJoinSpectators(int ClientId) const
{
	if(auto pEvents = g_ComponentRegistry.Get<CEvents>())
	{
		if(auto pActive = pEvents->GetActiveEvent();
			pActive && pActive->GetState() == CEventComponent::EEventState::Active)
		{
			const auto &Participants = pActive->Participants();
			if(std::find(Participants.begin(), Participants.end(), ClientId) != Participants.end())
			{
				// going to spec mid-event is a ragequit, not a team change
				pActive->Leave(ClientId);
				return true;
			}
		}
	}

	if(auto pManager = g_ComponentRegistry.Get<COneOnOneManager>())
	{
		if(auto pMatch = pManager->GetMatchForPlayer(ClientId))
		{
			if(pMatch->GetState() == COneOnOneEvent::EEventState::Preparation)
			{
				SendChatTarget(ClientId, "You can't join spectators during 1on1 preparation. Use /leave to cancel.");
				return true;
			}
			if(pMatch->GetState() == COneOnOneEvent::EEventState::Active)
			{
				pMatch->Leave(ClientId);
				return true;
			}
		}
	}
	return false;
}

bool CBlockworlds::BlocksSelfKill(int ClientId)
{
	if(auto pManager = g_ComponentRegistry.Get<COneOnOneManager>())
	{
		if(auto pMatch = pManager->GetMatchForPlayer(ClientId))
		{
			// no killing your way out of the configuration phase; once the match
			// is running a self-kill is just a lost round
			return pMatch->GetState() == COneOnOneEvent::EEventState::Preparation;
		}
	}

	if(!isInEvent(ClientId))
		return false;
	if(auto pEvents = g_ComponentRegistry.Get<CEvents>())
	{
		if(auto pActive = pEvents->GetActiveEvent())
			return !pActive->AllowKillCommandFor(ClientId);
	}
	return true;
}

bool CBlockworlds::OnWhisper(int ClientId, int VictimId, const char *pMessage) const
{
	// silence during LMB and TDM covers whispers in both directions, or a
	// participant could simply whisper their way around it
	const auto SilencedByEvent = [](int Cid) {
		const char *pEventName = nullptr;
		return IsInRunningEvent(Cid, &pEventName) && pEventName &&
		       (str_comp(pEventName, "LMB") == 0 || str_comp(pEventName, "tdm") == 0);
	};

	if(Server()->GetAuthedState(ClientId) == 0 && SilencedByEvent(ClientId))
	{
		SendChatTarget(ClientId, "You cannot whisper while participating in events.");
		return true;
	}

	if(auto pChatFilter = g_ComponentRegistry.Get<CChatFilterComponent>())
	{
		if(pChatFilter->CheckAndMaybeMute(ClientId, pMessage))
			return true;
	}

	if(Server()->GetAuthedState(VictimId) == 0 && SilencedByEvent(VictimId))
	{
		SendChatTarget(ClientId, "This person is participating in an event and cannot receive whispers.");
		return true;
	}
	return false;
}

void CBlockworlds::OnPublicChatSent(int ClientId, const char *pCensoredMessage, const char *pMessage) const
{
	// a censored message is not what the player typed, so it is not relayed
	if(str_comp_num(pCensoredMessage, pMessage, str_length(pCensoredMessage)) != 0)
		return;

	const char *pChatUrl = g_Config.m_SvDiscordWebhookUrlChat[0] ? g_Config.m_SvDiscordWebhookUrlChat : nullptr;
	CDiscordWebhook Discord(GameServer()->Engine(), Http());
	if(!Discord.IsConfigured(pChatUrl))
		return;

	char aMsg[600];
	const char *pName = Server()->ClientName(ClientId);
	str_format(aMsg, sizeof(aMsg), "**%s**: %s", pName ? pName : "<unknown>", pCensoredMessage);
	CDiscordWebhook::SSendOptions Options;
	Options.m_pWebhookUrl = pChatUrl;
	Discord.Send(aMsg, Options);
}

bool CBlockworlds::OverrideSpawnPos(int ClientId, vec2 *pSpawnPos)
{
	// a duel puts its two players where the match says: facing each other in the
	// prep zone while it is being configured, in the arena once it is running
	if(auto pManager = g_ComponentRegistry.Get<COneOnOneManager>())
	{
		if(auto pMatch = pManager->GetMatchForPlayer(ClientId))
		{
			const auto &Reservation = pMatch->GetSpawnReservation();
			const int Idx = (ClientId == pMatch->m_Player1ID) ? Reservation.m_Pos1Idx : Reservation.m_Pos2Idx;
			std::vector<vec2> Positions;
			if(pMatch->GetState() == COneOnOneEvent::EEventState::Preparation)
				Positions = ZoneManager()->Get1on1PrepPositions();
			else if(pMatch->GetState() == COneOnOneEvent::EEventState::Active)
				Positions = pMatch->GetArenaSpawnPositions();

			if(!Positions.empty())
			{
				// the reservation keeps the two players off each other's slot; a
				// stale index still has to land somewhere inside the arena
				*pSpawnPos = Positions[(Idx >= 0 && Idx < (int)Positions.size()) ? Idx : 0];
				return true;
			}
		}
	}

	// event participants spawn on their event's start quads
	auto pEvents = g_ComponentRegistry.Get<CEvents>();
	if(!pEvents)
		return false;
	auto pActive = pEvents->GetActiveEvent();
	if(!pActive || !pActive->GetName())
		return false;
	const auto &Participants = pActive->Participants();
	if(std::find(Participants.begin(), Participants.end(), ClientId) == Participants.end())
		return false;

	const char *pQuadLayer = nullptr;
	if(str_comp(pActive->GetName(), "tdm") == 0)
		pQuadLayer = "tdm_spawn";
	else if(str_comp(pActive->GetName(), "lmb") == 0)
		pQuadLayer = "lmb_spawn";
	else if(str_comp(pActive->GetName(), "zcatch") == 0)
		pQuadLayer = "zcb_spawn";
	else if(str_comp(pActive->GetName(), "zcatch_grenade") == 0)
		pQuadLayer = "zcg_spawn";
	if(!pQuadLayer)
		return false;

	const std::vector<vec2> StartPositions = ZoneManager()->GetNamedQuadCenters(pQuadLayer);
	if(StartPositions.empty())
		return false;
	// spread respawns over the start quads instead of stacking everyone on one
	*pSpawnPos = StartPositions[secure_rand_below((int)StartPositions.size())];
	return true;
}

void CBlockworlds::OnPlayerEnterMenu(int ClientId) const
{
	if(auto pEvents = g_ComponentRegistry.Get<CEvents>())
	{
		if(auto pActive = pEvents->GetActiveEvent())
		{
			const auto &Participants = pActive->Participants();
			if(std::find(Participants.begin(), Participants.end(), ClientId) != Participants.end())
			{
				// nothing to vote on while you are playing
				ClearVotes(ClientId);
				return;
			}
		}
	}

	// resend from scratch so a reconnecting dummy cannot end up out of sync
	ClearVotes(ClientId);
	GameServer()->ProgressVoteOptions(ClientId);
	SendCosmeticsVoteOptions(ClientId);
}

bool CBlockworlds::OnCharacterDie(CCharacter *pChr, int Killer)
{
	const int Victim = pChr->GetPlayer()->GetCid();

	// the tracker announces a block itself, so a blocked death must not also
	// produce the ordinary kill message
	const bool Blocked = BlockTracker().OnPlayerKill(Victim);
	BlockTracker().OnPlayerDeath(Victim);
	if(Blocked)
		return true;

	// events keep their own scoreboards and would be drowned in kill messages
	auto pEvents = g_ComponentRegistry.Get<CEvents>();
	if(!pEvents)
		return false;
	// Killer is -1 for world and server deaths
	const CPlayer *pKiller = (Killer >= 0 && Killer < MAX_CLIENTS) ? GameServer()->m_apPlayers[Killer] : nullptr;
	for(const auto &Sub : pEvents->GetSubComponents())
	{
		auto *pEvent = dynamic_cast<CEventComponent *>(Sub.operator->());
		if(!pEvent)
			continue;
		const auto &Participants = pEvent->Participants();
		const auto Involved = [&](int Cid) {
			return std::find(Participants.begin(), Participants.end(), Cid) != Participants.end();
		};
		if((pKiller && Involved(pKiller->GetCid())) || Involved(Victim))
			return true;
	}
	return false;
}

const char *CBlockworlds::ServerInfoClan(int ClientId)
{
	static char s_aClanName[BW_CLAN_NAME_BUFFER_SIZE];
	s_aClanName[0] = '\0';

	CPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer || !pPlayer->Bw().IsLoggedIn() || pPlayer->Bw().GetClanId() <= 0 || !Clans())
		return s_aClanName;

	CClansData Clan{};
	if(Clans()->GetClanSnapshotById(pPlayer->Bw().GetClanId(), Clan))
		str_copy(s_aClanName, Clan.m_ClanName, sizeof(s_aClanName));
	return s_aClanName;
}

int CBlockworlds::ServerInfoScore(int ClientId) const
{
	CPlayer *pPlayer = GetPlayer(ClientId);
	return (pPlayer && pPlayer->Bw().IsLoggedIn()) ? pPlayer->Bw().GetPlayerLevel() : 0;
}

void CBlockworlds::LogModeration(int ExecutorId, const char *pFmt, ...) const
{
	// econ and the console have no client id and nothing to attribute
	if(ExecutorId < 0)
		return;

	char aMsg[512];
	va_list Args;
	va_start(Args, pFmt);
	str_format_v(aMsg, sizeof(aMsg), pFmt, Args);
	va_end(Args);
	CDiscordWebhook::SendRconLog(GameServer()->Engine(), Http(), "%s", aMsg);
}

void CBlockworlds::OnRaceFinish(CPlayer *pPlayer) const
{
	CBwPlayer &Bw = pPlayer->Bw();
	if(!Bw.IsLoggedIn() || g_Config.m_SvRaceFinishExp <= 0)
		return;

	const int Now = Server()->Tick();
	const int CooldownTicks = g_Config.m_SvRaceFinishExpCooldown * Server()->TickSpeed();
	const bool OnCooldown = CooldownTicks > 0 && Bw.m_LastRaceFinishExpTick > 0 &&
				Now - Bw.m_LastRaceFinishExpTick < CooldownTicks;
	const bool SessionCapped = g_Config.m_SvRaceFinishExpMaxPerSession > 0 &&
				   Bw.m_RaceFinishExpCount >= g_Config.m_SvRaceFinishExpMaxPerSession;

	if(OnCooldown)
	{
		const int SecondsLeft = (CooldownTicks - (Now - Bw.m_LastRaceFinishExpTick)) / Server()->TickSpeed() + 1;
		SendChatTarget(pPlayer->GetCid(), "Race EXP on cooldown (%ds remaining).", SecondsLeft);
		return;
	}
	if(SessionCapped)
		return;

	const int Amount = std::clamp(g_Config.m_SvRaceFinishExp, 1, 1000);
	Bw.AddPlayerExp(Amount);
	Bw.m_LastRaceFinishExpTick = Now;
	Bw.m_RaceFinishExpCount++;
	SendChatTarget(pPlayer->GetCid(), "+%d EXP for finishing the race!", Amount);
}

void CBlockworlds::OnCharacterDied(CCharacter *pChr, int Killer, int Weapon)
{
	if(auto *pPassiveZone = dynamic_cast<CPassiveZone *>(ZoneManager()->GetZone(ZONE_PASSIVE)))
		pPassiveZone->OnCharacterDeath(pChr);

	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnCharacterDeath(Killer, pChr->GetPlayer()->GetCid(), Weapon);
}

void CBlockworlds::OnCharacterTakeDamage(CCharacter *pChar, vec2 Force, int Dmg, int From, int Weapon)
{
	if(From < 0 || Weapon < 0 || !pChar->GetPlayer())
		return;
	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnCharacterTakeDamage(Force, Dmg, From, pChar->GetPlayer()->GetCid(), Weapon);
}

bool CBlockworlds::ExplosionSkipsTarget(int Owner, CCharacter *pTarget) const
{
	// a passive/protected shooter only affects itself, and passive/protected
	// bystanders are never affected by someone else's explosion
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(Owner);
	const int TargetCid = pTarget->GetPlayer()->GetCid();
	if(pOwnerChar && (pOwnerChar->Core()->m_Passive || pOwnerChar->Core()->m_Protected))
		return TargetCid != Owner;
	return TargetCid != Owner && (pTarget->Core()->m_Protected || pTarget->Core()->m_Passive);
}

bool CBlockworlds::OnSnapProjectile(int Type, int Owner, vec2 Pos, vec2 Direction, int EntityId, int SnappingClient)
{
	if(Type != WEAPON_GUN)
		return false;
	// a grenade outlives its owner, so the shooter may already be gone
	if(Owner < 0 || !GameServer()->m_apPlayers[Owner])
		return false;
	// a viewer who turned cosmetics off sees the plain bullet, but only for
	// other people's shots -- your own gundesign is yours to look at
	if(SnappingClient != SERVER_DEMO_CLIENT && Owner != SnappingClient)
	{
		CPlayer *pSnap = GameServer()->m_apPlayers[SnappingClient];
		if(pSnap && pSnap->Bw().m_HideCosmetics)
			return false;
	}
	return Cosmetics()->SnapGundesign(Owner, Pos, Direction, EntityId, SnappingClient);
}

bool CBlockworlds::OnProjectileGunImpact(int Owner, vec2 Pos, vec2 Direction, CCharacter *pTargetChr)
{
	// some gundesigns make the tee they hit react
	if(pTargetChr && Owner >= 0 && GameServer()->m_apPlayers[Owner])
	{
		if(GameServer()->m_apPlayers[Owner]->Bw().GetGunDesign() == CCosmeticsHandler::GUNDESIGN_HEART)
			GameServer()->SendEmoticon(pTargetChr->GetPlayer()->GetCid(), EMOTICON_HEARTS, -1);
	}
	return Cosmetics()->DoGundesign(Owner, Pos, Direction);
}

CCharacter *CBlockworlds::FilterHitTarget(CCharacter *pOwnerChar, CCharacter *pTargetChr) const
{
	if(pOwnerChar && pOwnerChar->Core()->BwNoContact())
		return nullptr;
	if(pTargetChr && pTargetChr->Core()->BwNoContact())
		return nullptr;
	return pTargetChr;
}

CCharacter *CBlockworlds::IntersectLaserTarget(vec2 Pos0, vec2 Pos1, vec2 &NewPos, CCharacter *pOwnerChar, int CollideWith, bool DontHitSelf) const
{
	const bool SelfPassive = pOwnerChar && pOwnerChar->Core()->BwNoContact();
	auto Skip = [&](CCharacter *pChar) {
		// a passive shooter hits nobody but itself, and only when the laser is
		// allowed to come back to it at all
		if(SelfPassive)
			return DontHitSelf || pChar != pOwnerChar;
		if(pChar->Core()->BwNoContact())
			return true;
		return pOwnerChar && DontHitSelf && pChar == pOwnerChar;
	};

	// same closest-point search as CGameWorld::IntersectEntity, with the skip
	// predicate in place of upstream's single pNotThis pointer
	float ClosestLen = distance(Pos0, Pos1) * 100.0f;
	CCharacter *pClosest = nullptr;
	for(CEntity *pEntity = GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pEntity; pEntity = pEntity->TypeNext())
	{
		CCharacter *pChar = static_cast<CCharacter *>(pEntity);
		if(Skip(pChar))
			continue;
		if(CollideWith != -1 && !pChar->CanCollide(CollideWith))
			continue;

		vec2 IntersectPos;
		if(!closest_point_on_line(Pos0, Pos1, pChar->m_Pos, IntersectPos))
			continue;
		if(distance(pChar->m_Pos, IntersectPos) >= pChar->GetProximityRadius())
			continue;
		const float Len = distance(Pos0, IntersectPos);
		if(Len < ClosestLen)
		{
			NewPos = IntersectPos;
			ClosestLen = Len;
			pClosest = pChar;
		}
	}
	return pClosest;
}

void CBlockworlds::OnLaserHit(int Owner, CCharacter *pHit)
{
	if(pHit)
		BlockTracker().OnPlayerImpacted(pHit->GetOwnerId(), Owner);
}

void CBlockworlds::SnapLaserObject(const CSnapContext &Context, int SnapId, const vec2 &To, const vec2 &From,
	int StartTick, int Owner, int LaserType, int Subtype, int SwitchNumber, int Flags) const
{
	// Mirrors CGameContext::SnapLaserObject, which hardcodes m_Flags = 0.
	// Keep in sync if upstream changes the laser snap format.
	if(Context.GetClientVersion() >= VERSION_DDNET_MULTI_LASER)
	{
		CNetObj_DDNetLaser Laser = {};
		Laser.m_ToX = (int)To.x;
		Laser.m_ToY = (int)To.y;
		Laser.m_FromX = (int)From.x;
		Laser.m_FromY = (int)From.y;
		Laser.m_StartTick = StartTick;
		Laser.m_Owner = Owner;
		Laser.m_Type = LaserType;
		Laser.m_Subtype = Subtype;
		Laser.m_SwitchNumber = SwitchNumber;
		Laser.m_Flags = Flags;
		if(!Server()->Translate(Laser.m_Owner, Context.ClientId()))
			Laser.m_Owner = -1;
		Server()->SnapNewItem(SnapId, Laser);
	}
	else
	{
		CNetObj_Laser Laser = {};
		Laser.m_X = (int)To.x;
		Laser.m_Y = (int)To.y;
		Laser.m_FromX = (int)From.x;
		Laser.m_FromY = (int)From.y;
		Laser.m_StartTick = StartTick;
		Server()->SnapNewItem(SnapId, Laser);
	}
}

void CBlockworlds::OnCharacterSpawn(CCharacter *pChr) const
{
	CPlayer *pPlayer = pChr->GetPlayer();
	if(!pPlayer)
		return;

	// Prefer component-based events
	if(auto Events = g_ComponentRegistry.Get<CEvents>(); Events)
	{
		auto vSubs = Events->GetSubComponents();
		for(auto &Sub : vSubs)
		{
			CEventComponent *pEv = dynamic_cast<CEventComponent *>(Sub.operator->());
			if(!pEv)
				continue;
			// A savegame restore is not a real spawn and must not enter events.
			if((pEv->GetState() == CEventComponent::EEventState::Active ||
				   pEv->GetState() == CEventComponent::EEventState::Registration) &&
				m_SuppressSpawnEvent)
				continue;
			pEv->OnCharacterSpawn(pPlayer->GetCid(), pChr->m_Pos);
		}
	}

	pChr->Bw().m_CurrentKillingSpree = 0;

	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnCharacterSpawn(pPlayer->GetCid(), pChr->m_Pos);
}

int CBlockworlds::SnapPlayerScore(int SnappingClient, CPlayer *pPlayer) const
{
	CBwPlayer &Bw = pPlayer->Bw();
	const int ClientId = pPlayer->GetCid();
	int Score;

	// This is the time sent to the player while ingame (do not confuse to the one
	// reported to the master server). Due to clients expecting this as a negative
	// value, we have to make sure it's negative.
	// Special numbers: -9999 means no time and isn't displayed in the scoreboard.
	if(Bw.m_Score.has_value())
	{
		// Shift the time by a second if the player actually took 9999 seconds.
		if(Bw.m_Score.value() == 9999)
			Score = -10000;
		else
			Score = -Bw.m_Score.value();
	}
	else
	{
		Score = -9999;
	}

	// send 0 if times of others are not shown
	if(SnappingClient != ClientId && g_Config.m_SvHideScore)
		Score = -9999;

	bool ScoreSetFromEvent = false;
	// Prefer manager-based 1on1 matches for per-player scoring/participant checks
	if(auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>(); Mgr)
	{
		if(auto Match = Mgr->GetMatchForPlayer(ClientId); Match)
		{
			auto vParts = Match->Participants();
			bool IsParticipant = std::find(vParts.begin(), vParts.end(), ClientId) != vParts.end();

			if(auto EvScore = Match->GetScoreOf(ClientId); EvScore.has_value())
			{
				Score = EvScore.value();
				Bw.m_Score = EvScore;
				Server()->SetClientScore(ClientId, Score);
				ScoreSetFromEvent = true;
			}
			else if(IsParticipant)
			{
				Score = 0;
				Bw.m_Score = 0;
				Server()->SetClientScore(ClientId, Score);
				ScoreSetFromEvent = true;
			}
		}
	}

	// fallback to global active event if no manager match was found
	if(!ScoreSetFromEvent)
	{
		if(auto Events = g_ComponentRegistry.Get<CEvents>(); Events)
		{
			if(auto Active = Events->GetActiveEvent())
			{
				const auto &vParts = Active->Participants();
				bool IsParticipant = std::find(vParts.begin(), vParts.end(), ClientId) != vParts.end();

				if(auto EvScore = Active->GetScoreOf(ClientId); EvScore.has_value())
				{
					Score = EvScore.value();
					Bw.m_Score = EvScore;
					Server()->SetClientScore(ClientId, Score);
					ScoreSetFromEvent = true;
				}
				else if(IsParticipant)
				{
					Score = 0;
					Bw.m_Score = 0;
					Server()->SetClientScore(ClientId, Score);
					ScoreSetFromEvent = true;
				}
			}
		}
	}

	if(!ScoreSetFromEvent)
	{
		if(Bw.IsLoggedIn())
		{
			// Canonical server-side score uses this player's own display preference
			// (drives internal sorting / SetClientScore).
			int OwnScore;
			switch(Bw.m_ScoreDisplayMode)
			{
			case 1: OwnScore = Bw.GetPlayerBlockpoints(); break;
			default: OwnScore = Bw.GetPlayerLevel(); break;
			}
			Bw.m_Score = OwnScore;
			Server()->SetClientScore(ClientId, OwnScore);

			// The value written into the snap packet uses the VIEWER's preference,
			// so each client sees the metric they chose in the vote menu.
			int ViewerMode = Bw.m_ScoreDisplayMode; // fallback for self-snap / demo
			// SERVER_DEMO_CLIENT is -1, so the >= 0 test already covers it
			if(SnappingClient >= 0 && SnappingClient < MAX_CLIENTS && SnappingClient != ClientId)
			{
				if(CPlayer *pSnapper = GameServer()->m_apPlayers[SnappingClient])
					ViewerMode = pSnapper->Bw().m_ScoreDisplayMode;
			}
			switch(ViewerMode)
			{
			case 1: Score = Bw.GetPlayerBlockpoints(); break;
			default: Score = Bw.GetPlayerLevel(); break;
			}
		}
		else
		{
			Bw.m_Score = Score = 0;
			Server()->SetClientScore(ClientId, Score);
		}
	}

	return Score;
}

void CBlockworlds::OnSnapGameInfo(int SnappingClient, CNetObj_GameInfo *pGameInfo)
{
	if(SnappingClient < 0)
		return;
	auto EventsAccessor = g_ComponentRegistry.Get<CEvents>();
	if(!EventsAccessor)
		return;
	auto pEv = EventsAccessor->GetActiveEvent();
	if(!pEv)
		return;

	// Switch TDM participants to the vanilla team view, for their own snapshot only
	if(pEv->GetState() == CEventComponent::EEventState::Active && str_comp(pEv->GetName(), "tdm") == 0)
	{
		const auto &vParts = pEv->Participants();
		if(std::find(vParts.begin(), vParts.end(), SnappingClient) != vParts.end())
		{
			// force teams for UI (Join Blue/Red) without affecting others
			pGameInfo->m_GameFlags |= GAMEFLAG_TEAMS;
		}
	}
}

void CBlockworlds::OnSnapGameInfoEx(int SnappingClient, CNetObj_GameInfoEx *pGameInfoEx)
{
	// BW is its own gametype, not DDRace/DDNet, and has no race timer.
	pGameInfoEx->m_Flags &= ~(GAMEINFOFLAG_TIMESCORE |
				  GAMEINFOFLAG_GAMETYPE_RACE |
				  GAMEINFOFLAG_GAMETYPE_DDRACE |
				  GAMEINFOFLAG_GAMETYPE_DDNET);
	pGameInfoEx->m_Flags |= GAMEINFOFLAG_GAMETYPE_BLOCK_WORLDS;

	if(SnappingClient < 0)
		return;
	auto EventsAccessor = g_ComponentRegistry.Get<CEvents>();
	if(!EventsAccessor)
		return;
	if(auto pEv = EventsAccessor->GetActiveEvent(); pEv && !pEv->AllowZoomFor(SnappingClient))
		pGameInfoEx->m_Flags &= ~GAMEINFOFLAG_ALLOW_ZOOM;
}

void CBlockworlds::AddIpMuteSilent(const NETADDR *pAddr, int Secs, const char *pReason) const
{
	if(Secs <= 0)
		return;
	// Silent: no chat announcement, which is why this does not go through
	// CGameContext::TryMute.
	GameServer()->m_Mutes.Mute(pAddr, Secs, pReason ? pReason : "", "", false);
}

int CBlockworlds::GetRemainingMuteSecondsPublic(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameServer()->m_apPlayers[ClientId])
		return 0;

	const NETADDR *pAddr = Server()->ClientAddr(ClientId);
	if(auto Mute = GameServer()->m_Mutes.IsMuted(pAddr, false); Mute.has_value())
	{
		const int Expires = (Mute.value().m_Expire - Server()->Tick()) / Server()->TickSpeed();
		return std::max(Expires, 0);
	}
	return 0;
}

void CBlockworlds::SendBroadcast(int To, const char *pText) const
{
	GameServer()->Bw().SendBroadcast(pText, To);
}

void CBlockworlds::SendChatCmdGroupStart(int ClientId) const
{
	CNetMsg_Sv_CommandInfoGroupStart Msg;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
}

void CBlockworlds::SendChatCmdGroupEnd(int ClientId) const
{
	CNetMsg_Sv_CommandInfoGroupEnd Msg;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
}

void CBlockworlds::SendChatCmdAdd(const IConsole::ICommandInfo *pCommandInfo, int ClientId) const
{
	if(!pCommandInfo)
		return;
	const char *pName = pCommandInfo->Name();
	if(Server()->IsSixup(ClientId))
	{
		if(!str_comp_nocase(pName, "w") || !str_comp_nocase(pName, "whisper"))
			return;
		if(!str_comp_nocase(pName, "r"))
			pName = "rescue";

		protocol7::CNetMsg_Sv_CommandInfo Msg;
		Msg.m_pName = pName;
		Msg.m_pArgsFormat = pCommandInfo->Params();
		Msg.m_pHelpText = pCommandInfo->Help();
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
		return;
	}

	CNetMsg_Sv_CommandInfo Msg;
	Msg.m_pName = pName;
	Msg.m_pArgsFormat = pCommandInfo->Params();
	Msg.m_pHelpText = pCommandInfo->Help();
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
}

void CBlockworlds::SendChatCmdRem(const IConsole::ICommandInfo *pCommandInfo, int ClientId) const
{
	if(!pCommandInfo || Server()->IsSixup(ClientId))
		return;
	CNetMsg_Sv_CommandInfoRemove Msg;
	Msg.m_pName = pCommandInfo->Name();
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
}

bool CBlockworlds::AllowServerVoteStreaming(int ClientId) const
{
	(void)ClientId;
	return false;
}

void CBlockworlds::SendVoteListHeader(int ClientId)
{
	static const char *s_pHeader = "\xE2\x95\xAD\xE2\x94\x80 \xEA\x9C\xB1\xE1\xB4\x87\xCA\x80\xCA\x8B\xE1\xB4\x87\xCA\x80 \xE1\xB4\xA0\xE1\xB4\x8F\xE1\xB4\x9B\xE1\xB4\x87\xEA\x9C\xB1 \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80";
	int Index = 0;
	CNetMsg_Sv_VoteOptionListAdd HeaderMsg;
	HeaderMsg.m_pDescription0 = "";
	HeaderMsg.m_pDescription1 = "";
	HeaderMsg.m_pDescription2 = "";
	HeaderMsg.m_pDescription3 = "";
	HeaderMsg.m_pDescription4 = "";
	HeaderMsg.m_pDescription5 = "";
	HeaderMsg.m_pDescription6 = "";
	HeaderMsg.m_pDescription7 = "";
	HeaderMsg.m_pDescription8 = "";
	HeaderMsg.m_pDescription9 = "";
	HeaderMsg.m_pDescription10 = "";
	HeaderMsg.m_pDescription11 = "";
	HeaderMsg.m_pDescription12 = "";
	HeaderMsg.m_pDescription13 = "";
	HeaderMsg.m_pDescription14 = "";
	SetVoteDescriptionAtIndex(&Index, s_pHeader, &HeaderMsg);
	HeaderMsg.m_NumOptions = Index;
	Server()->SendPackMsg(&HeaderMsg, MSGFLAG_VITAL, ClientId);
}

void CBlockworlds::BotJoin(int BotId, const char *pName) const
{
	CServer *pServer = static_cast<CServer *>(Server());
	if(BotId < 0 || BotId >= Server()->MaxClients())
		return;
	CServer::CClient &Client = pServer->m_aClients[BotId];
	if(Client.m_State != CServer::CClient::STATE_EMPTY)
		return;

	// Mirrors CServer::UpdateDebugDummies' add path.
	CServer::NewClientCallback(BotId, pServer, false);
	Client.m_DebugDummy = true;

	// Synthetic unique-local address, same scheme upstream uses for dummies.
	Client.m_DebugDummyAddr.type = NETTYPE_IPV6;
	Client.m_DebugDummyAddr.ip[0] = 0xfd;
	secure_random_fill(&Client.m_DebugDummyAddr.ip[1], 5);
	Client.m_DebugDummyAddr.ip[6] = 0xc0;
	Client.m_DebugDummyAddr.ip[7] = 0xde;
	Client.m_DebugDummyAddr.ip[8] = 0x00;
	Client.m_DebugDummyAddr.ip[9] = 0x00;
	Client.m_DebugDummyAddr.ip[10] = 0x00;
	Client.m_DebugDummyAddr.ip[11] = 0x00;
	uint_to_bytes_be(&Client.m_DebugDummyAddr.ip[12], BotId);
	Client.m_DebugDummyAddr.port = secure_rand_below(65535 - 1024) + 1024;
	net_addr_str(&Client.m_DebugDummyAddr, Client.m_aDebugDummyAddrString.data(), Client.m_aDebugDummyAddrString.size(), true);
	net_addr_str(&Client.m_DebugDummyAddr, Client.m_aDebugDummyAddrStringNoPort.data(), Client.m_aDebugDummyAddrStringNoPort.size(), false);

	GameServer()->OnClientConnected(BotId, nullptr);
	Client.m_State = CServer::CClient::STATE_INGAME;
	Client.m_DDNetVersion = DDNET_VERSION_NUMBER;
	Client.m_GotDDNetVersionPacket = true;
	Client.m_DDNetVersionSettled = true;
	pServer->SetClientName(BotId, pName);

	if(GameServer()->m_apPlayers[BotId])
		GameServer()->m_apPlayers[BotId]->Bw().m_IsNpc = true;

	GameServer()->OnClientEnter(BotId);
}

void CBlockworlds::BotLeave(int BotId, bool Silent) const
{
	CServer *pServer = static_cast<CServer *>(Server());
	if(BotId < 0 || BotId >= Server()->MaxClients())
		return;
	if(!pServer->m_aClients[BotId].m_DebugDummy)
		return;
	CServer::DelClientCallback(BotId, Silent ? "" : "Bot left", pServer);
}

void CBlockworlds::RedirectClient(int ClientId, int Port, bool Force) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(!Force)
	{
		Server()->RedirectClient(ClientId, Port);
		return;
	}

	CServer *pServer = static_cast<CServer *>(Server());
	if(pServer->m_aClients[ClientId].m_State == CServer::CClient::STATE_EMPTY)
		return;

	log_info("bw-redirect", "forcing client to another port, cid=%d port=%d", ClientId, Port);

	CMsgPacker Msg(NETMSG_REDIRECT, true);
	Msg.AddInt(Port);
	pServer->SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientId);
	// deliberately no STATE_REDIRECTED and no drop timer: the proxy keeps the slot
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void CBlockworlds::OnConstruct(bool FirstInit)
{
	// Vote cooldown timestamps are tick based; a map change or soft reload resets
	// the tick counter, so they always have to be cleared here.
	m_LastGlobalWeaponkitsVoteCall = 0;
	m_LastGlobalEventVoteCall = 0;

	if(!FirstInit)
		return;

	m_WeaponkitsAllowed = g_Config.m_SvWeaponkitsAllowed;

	// required
	g_ComponentRegistry.Register<COneOnOneManager>(COneOnOneManager::GetNameStatic(), true);

	// common
	g_ComponentRegistry.Register<CPromises>(CPromises::GetNameStatic());
	g_ComponentRegistry.Register<CEvents>(CEvents::GetNameStatic());
	g_ComponentRegistry.Register<CRequests>(CRequests::GetNameStatic());
	g_ComponentRegistry.Register<CAiBotComponent>(CAiBotComponent::GetNameStatic());
	g_ComponentRegistry.Register<CChatFilterComponent>(CChatFilterComponent::GetNameStatic());
	g_ComponentRegistry.Register<CClientDetectComponent>(CClientDetectComponent::GetNameStatic());
	g_ComponentRegistry.Register<CVpnDetectionComponent>(CVpnDetectionComponent::GetNameStatic());
	g_ComponentRegistry.Register<CPortProxy>(CPortProxy::GetNameStatic());
	g_ComponentRegistry.Register<CAgonesComponent>(CAgonesComponent::GetNameStatic());
}

void CBlockworlds::OnDestruct()
{
	delete m_pAccounts;
	m_pAccounts = nullptr;
	delete m_pClans;
	m_pClans = nullptr;
	delete m_pWhoIs;
	m_pWhoIs = nullptr;
}

void CBlockworlds::OnConsoleInit()
{
	m_pHttp = GameServer()->Kernel()->RequestInterface<IHttp>();

	Console()->Register("send_sound", "i[sound id] i[player id]", CFGFLAG_SERVER, ConSendSound, GameServer(), "Send a sound to a player (-1 = everyone)");
	Console()->Register("set_weaponkits_allowed", "s[value]", CFGFLAG_SERVER, ConSetWeaponkits, GameServer(), "Set whether weaponkits are allowed (0/1)");

	// Admin-only whois commands
	Console()->Register("whois_ip", "r[ip] ?r[cidr]", CFGFLAG_SERVER, ConWhoisIp, GameServer(), "whois_ip <IP> [/32|/24|/16]");
	Console()->Register("whois_id", "v[id] ?r[cidr]", CFGFLAG_SERVER, ConWhoisId, GameServer(), "whois_id <client id> [/32|/24|/16]");
	Console()->Register("whois_name", "r[name]", CFGFLAG_SERVER, ConWhoisName, GameServer(), "whois_name <name or pattern>");
	Console()->Register("whois_purge", "?i[retention_months]", CFGFLAG_SERVER, ConWhoisPurge, GameServer(), "Force-run whois retention purge now (optional override months, default=sv_whois_retention_months)");

	// BW's rcon commands. These used to be interleaved into upstream's
	// CGameContext::RegisterDDRaceCommands().
	Console()->Register("rcon_weapons", "", CFGFLAG_SERVER | CMDFLAG_TEST, CGameContext::ConWeapons, GameServer(), "Gives all weapons to you");
	Console()->Register("change_name", "v[id] r[name]", CFGFLAG_SERVER, ConChangeName, GameServer(), "Change a player's displayed name");

	Console()->Register("status_acc", "?r[name]", CFGFLAG_SERVER, ConStatusAccounts, GameServer(), "List logged-in accounts containing name or all accounts");

	// Admin helpers for account/IP management
	Console()->Register("ip_bans", "", CFGFLAG_SERVER, ConIpBans, GameServer(), "List active IP bans (admin)");
	Console()->Register("ip_ban_clear", "s[ip]", CFGFLAG_SERVER, ConIpBanClear, GameServer(), "Clear IP ban (admin)");
	Console()->Register("list_outstanding_invites", "i[clientid]", CFGFLAG_SERVER, ConListOutstandingInvites, GameServer(), "List outstanding invites for a client (admin)");

	Console()->Register("component_list", "", CFGFLAG_SERVER, ConComponentList, GameServer(), "List of all components and active sub-components");
	Console()->Register("component_plug", "r[name]", CFGFLAG_SERVER, ConComponentPlug, GameServer(), "Plug-in component");
	Console()->Register("component_unplug", "r[name]", CFGFLAG_SERVER, ConComponentUnPlug, GameServer(), "Un-plug component");

	// admin account modification commands
	Console()->Register("give_pages", "v[id] i[amount]", CFGFLAG_SERVER, ConGivePages, GameServer(), "Give pages to player id");
	Console()->Register("give_level", "v[id] i[amount]", CFGFLAG_SERVER, ConGiveLevel, GameServer(), "Give levels to player id");
	Console()->Register("give_experience", "v[id] i[amount]", CFGFLAG_SERVER, ConGiveExperience, GameServer(), "Give experience to player id");
	Console()->Register("give_weaponkits", "v[id] i[amount]", CFGFLAG_SERVER, ConGiveWeaponkits, GameServer(), "Give weaponkits to player id");
	Console()->Register("give_blockpoints", "v[id] i[amount]", CFGFLAG_SERVER, ConGiveBlockpoints, GameServer(), "Give blockpoints to player id");
	Console()->Register("give_passive", "v[id] i[seconds]", CFGFLAG_SERVER, ConGivePassive, GameServer(), "Give passive seconds to player id");
	Console()->Register("set_acc_password", "s[name] s[newpass]", CFGFLAG_SERVER, ConAdminSetPassword, GameServer(), "Set an account password by account name");
	Console()->Register("vip_player", "v[id] i[0|1]", CFGFLAG_SERVER, ConSetVip, GameServer(), "Set or remove VIP for player id (1=set, 0=remove)");
	Console()->Register("vip_account", "s[name] i[0|1]", CFGFLAG_SERVER, ConSetVipAccount, GameServer(), "Set or remove VIP for account name (offline-capable) (1=set, 0=remove)");

	// cosmetic toggling commands with prefix c_
	Console()->Register("c_set_gundesign", "i[type] ?v[id]", CFGFLAG_SERVER, ConSetGunDesignCosmetic, GameServer(), "Set players' gun design cosmetic");
	Console()->Register("c_set_knockout", "i[type] ?v[id]", CFGFLAG_SERVER, ConSetKnockoutCosmetic, GameServer(), "Set players' knockout cosmetic");
	Console()->Register("c_set_skinmani", "i[type] ?v[id]", CFGFLAG_SERVER, ConSetSkinManiCosmetic, GameServer(), "Set players' skinmani cosmetic");
	Console()->Register("c_set_special", "i[type] ?v[id]", CFGFLAG_SERVER, ConSetSpecialCosmetic, GameServer(), "Set players' special cosmetic");

	Console()->Register("banhammer", "v[id]", CFGFLAG_SERVER, ConBanhammer, GameServer(), "Give a player the banhammer (one-time use, bans the next player they hammer)");
	Console()->Register("set_pages", "v[id] i[amount]", CFGFLAG_SERVER, ConSetPages, GameServer(), "Set pages for player id");
	Console()->Register("set_level", "v[id] i[amount]", CFGFLAG_SERVER, ConSetLevel, GameServer(), "Set level for player id");
	Console()->Register("set_experience", "v[id] i[amount]", CFGFLAG_SERVER, ConSetExperience, GameServer(), "Set experience for player id");
	Console()->Register("set_weaponkits", "v[id] i[amount]", CFGFLAG_SERVER, ConSetWeaponkitsAdmin, GameServer(), "Set weaponkits for player id");
	Console()->Register("set_blockpoints", "v[id] i[amount]", CFGFLAG_SERVER, ConSetBlockpoints, GameServer(), "Set blockpoints for player id");
	Console()->Register("set_passive", "v[id] i[seconds]", CFGFLAG_SERVER, ConSetPassive, GameServer(), "Set passive seconds for player id");

	Console()->Register("telekinesis", "", CFGFLAG_SERVER, ConTelekinesis, GameServer(), "Toggle telekinesis mode: hold fire to grab and move players with your cursor");
	Console()->Register("knockout", "?r[name|id]", CFGFLAG_SERVER, ConKnockout, GameServer(), "Trigger a knockout effect at your cursor position (no args to list all)");
	Console()->Register("whois_account", "r[account name]", CFGFLAG_SERVER, ConWhoisAccount, GameServer(), "List all names ever seen on an account (from whois log)");

	g_BwConfig.Register(Console());
	RegisterBlockworldsChatCommands();

	UpdateWeaponkitsVoteOption();
	UpdateLMBVoteOption();
}

void CBlockworlds::OnInit()
{
	if(!m_pHttp)
		m_pHttp = GameServer()->Kernel()->RequestInterface<IHttp>();

	g_ComponentRegistry.CreateRequired(GameServer());
	for(auto &pComponent : g_ComponentRegistry.Required())
		dbg_msg("Components", "Required Component created: %s (%p)", pComponent->GetName(), &*pComponent);
	ProcessComponentsQueue();

	CServer *pServer = static_cast<CServer *>(Server());
	if(!m_pAccounts)
		m_pAccounts = new CAccounts(GameServer(), pServer->DbPool());
	if(!m_pClans)
		m_pClans = new CClanManager(GameServer(), pServer->DbPool());
	if(!m_pWhoIs)
		m_pWhoIs = new CWhoIs(GameServer(), pServer->DbPool());

	m_Animations.Init(GameServer());
	m_CosmeticsHandler.Init(GameServer());
	m_ZoneManager.Init(GameServer());
	if(g_Config.m_SvShopServer)
		m_ShopPreview.Init(GameServer());

	m_pAccounts->ClearLogins();
	m_pClans->LoadAllClans();
}

void CBlockworlds::OnShutdown()
{
	// clean up shop NPC players before shutdown to prevent stale pointers on reload
	if(m_ShopPreview.GameServer())
		m_ShopPreview.Init(GameServer());

	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnShutdown();

	// Blockworlds persistence. PreShutdownFlush normally does this earlier; this
	// is the fallback for shutdown paths that never reached it.
	static bool s_FlushDetected = false;
	if(m_pAccounts && m_pAccounts->ShutdownFlushActive())
		s_FlushDetected = true;
	if(s_FlushDetected)
		return;

	int QueuedAccountSaves = 0;
	if(m_pAccounts)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			CPlayer *pPl = GameServer()->m_apPlayers[i];
			if(!pPl || !pPl->Bw().IsLoggedIn())
				continue;
			CAccountData &Acc = pPl->Bw().m_Account;
			str_copy(Acc.m_aLastName, Server()->ClientName(i), sizeof(Acc.m_aLastName));
			str_copy(Acc.m_aLastSkin, pPl->TeeInfos().m_aSkinName, sizeof(Acc.m_aLastSkin));
			Acc.m_LastBodyColor = pPl->TeeInfos().m_ColorBody;
			Acc.m_LastFeetColor = pPl->TeeInfos().m_ColorFeet;
			int64_t SessionTicks = Server()->Tick() - pPl->m_JoinTick;
			if(SessionTicks > 0)
			{
				int64_t SessionSeconds = SessionTicks / Server()->TickSpeed();
				if(SessionSeconds > 0)
					Acc.m_Playtime += SessionSeconds;
			}
			m_pAccounts->Save(i, &Acc);
			QueuedAccountSaves++;
		}
		m_pAccounts->ClearLogins();
	}
	int QueuedClanSaves = 0;
	if(m_pClans)
		QueuedClanSaves = m_pClans->SaveAllClansOnShutdown();
	if(QueuedAccountSaves || QueuedClanSaves)
		dbg_msg("shutdown", "shutdown (fallback): queued %d account saves, %d clan saves", QueuedAccountSaves, QueuedClanSaves);
}

void CBlockworlds::OnClientConnected(int ClientId)
{
	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnPlayerConnected(ClientId);
}

void CBlockworlds::OnSetAuthed(int ClientId, int Level) const
{
	for(const auto &Component : g_ComponentRegistry.Active())
	{
		if(Level == AUTHED_NO)
			Component->OnPlayerUnAuthorized(ClientId);
		else
			Component->OnPlayerAuthorized(ClientId, Level);
	}
	// admin-only cosmetics go away with the rcon level that granted them
	if(Level == AUTHED_NO && GetPlayer(ClientId))
		GetPlayer(ClientId)->Bw().ClearCosmetics();
}

void CBlockworlds::OnPostSnap()
{
	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnPostSnap();
}

void CBlockworlds::OnClientEnter(int ClientId)
{
	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnPlayerEntering(ClientId);

	// inform about weekend EXP bonus
	if(g_Config.m_SvWeekendExpEnabled)
	{
		time_t Now = time(nullptr);
		struct tm TmRes;
		time_localtime_safe(&Now, &TmRes);
		const int WeekDay = TmRes.tm_wday; // 0=Sun,6=Sat
		if(WeekDay == 0 || WeekDay == 6)
		{
			int Mult = std::max(g_Config.m_SvWeekendExpMultiplier, 100);
			SendChatTarget(ClientId, "Weekend bonus: x%.2g EXP is active!", Mult / 100.0f);
		}
	}

	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnPlayerEnter(ClientId);

	// Announce the join now unless a component (VPN detection) is still checking
	// the client, in which case it releases the message once the client is cleared.
	SendPendingJoinMessage(ClientId);

	if(m_pWhoIs)
		m_pWhoIs->LogJoin(ClientId);
}

void CBlockworlds::OnClientDrop(int ClientId, const char *pReason)
{
	// Clear bw votemenu
	g_VoteManager.ClearClient(ClientId);

	if(m_pWhoIs)
		m_pWhoIs->LogLeave(ClientId);

	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnPlayerDropping(ClientId);

	if(auto Requests = g_ComponentRegistry.Get<CRequests>())
		Requests->CancelRequestsInvolving(ClientId, std::nullopt, "player disconnected");

	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnPlayerDrop(ClientId);
}

void CBlockworlds::OnSnap(int SnappingClient)
{
	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnSnap(SnappingClient);

	m_ZoneManager.Snap(SnappingClient);
	m_Animations.Snap(SnappingClient);
}

// A client that was never announced (entry checks such as VPN detection are still
// running) leaves without any notification either.
bool CBlockworlds::IsSilentDrop(int ClientId) const
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	return pPlayer && pPlayer->Bw().m_EntryChecksPending;
}

void CBlockworlds::SendChatTarget(int To, const char *pText) const
{
	GameServer()->SendChatTarget(To, pText);
}

void CBlockworlds::SendChatTeam(int Team, const char *pText) const
{
	GameServer()->SendChatTeam(Team, pText);
}

void CBlockworlds::SendBroadcast(const char *pText, int ClientId, bool IsImportant) const
{
	GameServer()->SendBroadcast(pText, ClientId, IsImportant);
}

void CBlockworlds::ConChangeName(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim();

	if(Victim < 0 || Victim >= MAX_CLIENTS || !pSelf->m_apPlayers[Victim])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "change_name", "Client id not found.");
		return;
	}

	const char *pNewName = pResult->GetString(1);
	if(!pNewName || !pNewName[0])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "change_name", "Name must not be empty.");
		return;
	}

	char aOldName[MAX_NAME_LENGTH];
	str_copy(aOldName, pSelf->Server()->ClientName(Victim), sizeof(aOldName));
	pSelf->Server()->SetClientName(Victim, pNewName);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "'%s' was renamed to '%s'", aOldName, pSelf->Server()->ClientName(Victim));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "change_name", aBuf);
}

bool CBlockworlds::OnCallVote(const CNetMsg_Cl_CallVote *pMsg, int ClientId) const
{
	// A vote-menu entry navigates a page; it is not a server vote and must not
	// be rate limited or looked up in the server vote list.
	if(HandleCosmeticsVote(pMsg, ClientId))
		return true;

	// players in 1on1 prep can't call server votes
	if(auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>())
	{
		if(auto Match = Mgr->GetMatchForPlayer(ClientId))
			if(Match->GetState() == COneOnOneEvent::EEventState::Preparation)
				return true;
	}

	return false;
}

bool CBlockworlds::OnVote(const CNetMsg_Cl_Vote *pMsg, int ClientId)
{
	// Route to the private 1on1 duel vote handler for players in the config phase
	if(pMsg->m_Vote == 0)
		return false;

	auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>();
	if(!Mgr)
		return false;

	auto Match = Mgr->GetMatchForPlayer(ClientId);
	if(!Match || !Match->IsInConfigPhase())
		return false;

	Match->OnDuelVote(ClientId, pMsg->m_Vote);
	return true;
}

void CBlockworlds::OnSnapClientInfo(int ClientId, int SnappingClient, CNetObj_ClientInfo *pClientInfo)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	CBwPlayer &Bw = pPlayer->Bw();

	// clan tag comes from the Blockworlds clan, not from the client's own
	if(Bw.IsLoggedIn() && Bw.GetClanId() > 0)
	{
		const char *pClanName = "";
		CClansData Clan;
		if(Clans() && Clans()->GetClanSnapshotById(Bw.GetClanId(), Clan))
			pClanName = Clan.m_ClanName;
		StrToInts(pClientInfo->m_aClan, std::size(pClientInfo->m_aClan), pClanName);
	}
	else
	{
		StrToInts(pClientInfo->m_aClan, std::size(pClientInfo->m_aClan), "");
	}

	// an NPC wears the skin of whoever is looking at it
	if(Bw.m_IsNpc && SnappingClient != SERVER_DEMO_CLIENT && SnappingClient >= 0 && SnappingClient < MAX_CLIENTS)
	{
		CPlayer *pViewer = GameServer()->m_apPlayers[SnappingClient];
		if(pViewer && !pViewer->Bw().m_IsNpc)
		{
			StrToInts(pClientInfo->m_aSkin, std::size(pClientInfo->m_aSkin), pViewer->TeeInfos().m_aSkinName);
			pClientInfo->m_UseCustomColor = pViewer->TeeInfos().m_UseCustomColor;
			pClientInfo->m_ColorBody = pViewer->TeeInfos().m_ColorBody;
			pClientInfo->m_ColorFeet = pViewer->TeeInfos().m_ColorFeet;
		}
	}

	CCharacter *pChar = GameServer()->GetPlayerChar(ClientId);
	if(pChar && pChar->Bw().IsHookRainbowActive())
	{
		const int64_t TickDef = Server()->Tick() - pPlayer->m_DieTick;
		float Freq = 255.0f;
		const float Divider = pChar->Bw().GetHookRainbowDivider();
		if(Divider > 0.0f)
			Freq *= Divider;

		const float h = (std::sin(TickDef / Freq) + 1.0f) / 2.0f;
		const float s = 0.5f;
		const float L = 0.5f;
		const int Color = ((int)(h * 255) << 16) + ((int)(s * 255) << 8) + (int)((L - 0.5f) * 255 * 2);

		pClientInfo->m_ColorBody = Color;
		pClientInfo->m_ColorFeet = Color;
		pClientInfo->m_UseCustomColor = 1;
	}

	if(Bw.GetSkinMani() != -1)
	{
		// skip the skin manipulation for viewers who turned cosmetics off
		bool HideSkinMani = false;
		if(SnappingClient != SERVER_DEMO_CLIENT && SnappingClient != ClientId &&
			SnappingClient >= 0 && SnappingClient < MAX_CLIENTS)
		{
			CPlayer *pViewer = GameServer()->m_apPlayers[SnappingClient];
			if(pViewer && pViewer->Bw().m_HideCosmetics)
				HideSkinMani = true;
		}
		if(!HideSkinMani)
			Cosmetics()->SnapSkinmani(ClientId, pPlayer->m_DieTick, pClientInfo);
	}

	for(const auto &Component : g_ComponentRegistry.Active())
		Component->OnSnapClientInfo(ClientId, SnappingClient, pClientInfo);
}

void CBlockworlds::OnSnapPlayerInfo(int ClientId, int SnappingClient, CNetObj_PlayerInfo *pPlayerInfo)
{
	// TDM participants see each other in the vanilla red/blue teams
	auto Events = g_ComponentRegistry.Get<CEvents>();
	if(!Events)
		return;
	auto pActive = Events->GetActiveEvent();
	if(!pActive || pActive->GetState() != CEventComponent::EEventState::Active)
		return;
	if(str_comp(pActive->GetName(), "tdm") != 0)
		return;

	const auto &vParts = pActive->Participants();
	if(std::find(vParts.begin(), vParts.end(), SnappingClient) == vParts.end())
		return;

	if(auto TeamIdx = pActive->GetTeamIndexFor(ClientId); TeamIdx.has_value())
		pPlayerInfo->m_Team = TeamIdx.value() == 0 ? TEAM_BLUE : TEAM_RED;
	else
		// non-participants show as spectators so clients do not warn about balance
		pPlayerInfo->m_Team = TEAM_SPECTATORS;
}

void CBlockworlds::OnSnapDDNetPlayer(int ClientId, CNetObj_DDNetPlayer *pDDNetPlayer) const
{
	// only reveal the auth level when the server opted in, otherwise clients
	// highlight the names of moderators who want to stay unnoticed
	if(!g_Config.m_SvShowAuthedUsers)
		pDDNetPlayer->m_AuthLevel = AUTHED_NO;

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(pPlayer && pPlayer->Bw().m_IsNpc)
		pDDNetPlayer->m_Flags &= ~EXPLAYERFLAG_AFK;
}
