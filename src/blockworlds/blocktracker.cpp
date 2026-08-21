#include "blocktracker.h"

#include <engine/shared/config.h>

#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/accounts.h>
#include <blockworlds/bw_context.h>
#include <blockworlds/bw_util.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/components/events/event.h>
#include <blockworlds/components/oneonone_manager.h>
#include <blockworlds/entities/experience.h>

#include <algorithm>
#include <ctime>
#include <deque>
#include <unordered_map>

CBlockTracker::CBlockTracker(CGameContext *pGameServer) :
	m_pGameContext(pGameServer)
{
	for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
	{
		m_aTrackedPlayers[ClientID].m_Tracked = false;
		m_aHourlyStats[ClientID] = {};
	}
}

float CBlockTracker::SecondsPassed(int SinceTick) const
{
	int Tick = m_pGameContext->Server()->Tick();
	int TickSpeed = m_pGameContext->Server()->TickSpeed();
	return static_cast<float>(Tick - SinceTick) / TickSpeed;
}

bool CBlockTracker::Blocked(int ClientID, int BlockerID)
{
	if(!m_pGameContext->PlayerExists(ClientID) || !m_pGameContext->PlayerExists(BlockerID))
		return false;

	int64_t NowTick = m_pGameContext->Server()->Tick();

	int activePlayers = GetActiveNonAfkPlayers();
	if(g_Config.m_SvMinActivePlayersForExp > 0 && activePlayers < g_Config.m_SvMinActivePlayersForExp)
	{
		DebugMsg(BlockerID, "No EXP: not enough active players");
		return false;
	}
	if(!IsPlayerActive(BlockerID))
	{
		DebugMsg(BlockerID, "No EXP: you are not considered active yet (move/play longer)\n");
		return false;
	}
	if(!IsPlayerActive(ClientID))
	{
		DebugMsg(BlockerID, "No EXP: victim not active");
		return false;
	}
	// recent action checks (inline to know reason)
	int Tick = m_pGameContext->Server()->Tick();
	int TickSpeed = m_pGameContext->Server()->TickSpeed();
	CPlayer *pVictimForAction = m_pGameContext->m_apPlayers[ClientID];
	CPlayer *pKillerForAction = m_pGameContext->m_apPlayers[BlockerID];
	if(g_Config.m_SvExpVictimRecentActionSec > 0 && pVictimForAction && (Tick - pVictimForAction->m_LastActionTick) > g_Config.m_SvExpVictimRecentActionSec * TickSpeed)
	{
		DebugMsg(BlockerID, "No EXP: victim inactive (no recent input)");
		return false;
	}
	if(g_Config.m_SvExpKillerRecentActionSec > 0 && pKillerForAction && (Tick - pKillerForAction->m_LastActionTick) > g_Config.m_SvExpKillerRecentActionSec * TickSpeed)
	{
		DebugMsg(BlockerID, "No EXP: you were inactive (no recent input)");
		return false;
	}
	if(!PassedSameVictimLimit(ClientID, BlockerID, NowTick))
	{
		DebugMsg(BlockerID, "No EXP: repeated kills on same victim (limit)");
		return false;
	}
	if(DetectLoopPattern(BlockerID, ClientID, NowTick))
	{
		// DetectLoopPattern already sends a message
		return false;
	}
	// prevent abuse
	CPlayer *pVictimPlayer = m_pGameContext->m_apPlayers[ClientID];
	if(g_Config.m_SvIgnoreAfkKills && pVictimPlayer)
	{
		if(pVictimPlayer->IsAfk() || pVictimPlayer->IsPaused())
		{
			DebugMsg(BlockerID, "No EXP: victim AFK/paused");
			return false;
		}
	}
	CPlayer *pBlockerPlayer = m_pGameContext->m_apPlayers[BlockerID];
	if(g_Config.m_SvIgnoreClanmateKills && pVictimPlayer && pBlockerPlayer)
	{
		int VictimClan = pVictimPlayer->Bw().GetClanId();
		int BlockerClan = pBlockerPlayer->Bw().GetClanId();
		if(VictimClan != 0 && VictimClan == BlockerClan)
		{
			DebugMsg(BlockerID, "No EXP: same clan kill");
			return false;
		}
	}
	if(BwIsClientsSameAddr(m_pGameContext->Server(), ClientID, BlockerID) && !g_Config.m_SvAllowExpFromSameIp)
	{
		DebugMsg(BlockerID, "No EXP: same IP restricted");
		return false;
	}
	if(auto events = g_ComponentRegistry.Get<CEvents>())
	{
		for(auto &sub : events->GetSubComponents())
		{
			CEventComponent *pEv = dynamic_cast<CEventComponent *>(sub.operator->());
			if(!pEv)
				continue;
			if(pEv->GetName() && str_comp(pEv->GetName(), "tdm") == 0)
				continue;
			const auto &parts = pEv->Participants();
			if(std::find(parts.begin(), parts.end(), ClientID) != parts.end() || std::find(parts.begin(), parts.end(), BlockerID) != parts.end())
			{
				DebugMsg(BlockerID, "No EXP: event participant exclusion");
				return false;
			}
		}

		if(auto p1on1 = g_ComponentRegistry.Get<COneOnOneManager>())
		{
			if(p1on1->GetMatchForPlayer(ClientID) || p1on1->GetMatchForPlayer(BlockerID))
			{
				DebugMsg(BlockerID, "No EXP: 1on1 participant exclusion");
				return false;
			}
		}

		auto active = events->GetActiveEvent();
		if(active)
		{
			const char *pEvName = active->GetName();
			if(pEvName && (str_comp(pEvName, "LMB") == 0 || str_comp(pEvName, "tdm") == 0) &&
				(std::find(active->Participants().begin(), active->Participants().end(), ClientID) != active->Participants().end() ||
					std::find(active->Participants().begin(), active->Participants().end(), BlockerID) != active->Participants().end()))
			{
				return false;
			}
		}
	}

	CCharacter *pChr = m_pGameContext->GetPlayerChar(ClientID);
	if(!pChr)
	{
		DebugMsg(BlockerID, "No EXP: victim character invalid");
		return false;
	}

	int64_t CurrentTick = m_pGameContext->Server()->Tick();
	int64_t AliveTickSpeed = m_pGameContext->Server()->TickSpeed();
	int64_t AliveTime = CurrentTick - pChr->Bw().m_AliveSince;

	if(AliveTime < g_Config.m_SvBlockMinAliveTime * AliveTickSpeed)
	{
		DebugMsg(BlockerID, "No EXP: victim alive too short");
		return false;
	}

	auto &LastBlockedTime = m_aTrackedPlayers[ClientID].m_LastBlockedTime;
	if(LastBlockedTime.find(ClientID) != LastBlockedTime.end())
	{
		int64_t LastBlockTick = LastBlockedTime[ClientID];
		if((CurrentTick - LastBlockTick) < g_Config.m_SvBlockInterval * TickSpeed)
		{
			DebugMsg(BlockerID, "No EXP: block cooldown active");
			return false;
		}
	}

	auto &BlockerExpCount = m_aTrackedPlayers[ClientID].m_BlockerExpCount;
	if(BlockerExpCount.find(BlockerID) != BlockerExpCount.end() && BlockerExpCount[BlockerID] >= 2)
	{
		DebugMsg(BlockerID, "No EXP: per-victim limit reached");
		return false;
	}

	BlockerExpCount[BlockerID]++;
	LastBlockedTime[ClientID] = CurrentTick;

	// --- dynamic EXP calculation ---
	float ExpBase = g_Config.m_SvBlockExperience;
	float PopScale = PopulationScale();
	float UniqueScale = 1.0f;
	float UniqueRatio = UniqueVictimRatio(BlockerID);
	float MinUniqueRatio = g_Config.m_SvExpMinUniqueRatioPercent / 100.0f;
	if(MinUniqueRatio > 0.0f && UniqueRatio >= 0.0f && UniqueRatio < MinUniqueRatio)
		UniqueScale = 0.25f; // penalize low diversity (if I kill a noob 10 times, I should not get full EXP)
	float LevelScale = LevelDiffScale(BlockerID, ClientID);
	float DailyScale = DailySoftCapScale(BlockerID);
	float AfkScale = 1.0f;
	float AfkRatio = AfkVictimRatio(BlockerID);
	if(g_Config.m_SvExpMaxAfkVictimRatioPercent > 0 && AfkRatio * 100.0f > g_Config.m_SvExpMaxAfkVictimRatioPercent)
		AfkScale = 0.0f; // fully suppress when abusing AFK victims

	float FinalExp = ExpBase * PopScale * UniqueScale * LevelScale * DailyScale * AfkScale;
	int AwardExp = FinalExp <= 0.0f ? 0 : std::max(1, (int)FinalExp);

	// report accurate, outcome-based scaling info without changing gameplay
	const int BaseExpInt = (int)ExpBase;
	if(AfkScale == 0.0f)
	{
		DebugMsg(BlockerID, "EXP suppressed: AFK victim ratio too high");
	}
	else if(AwardExp > 0)
	{
		// only claim a scale-down if the awarded EXP is actually smaller than base
		if(AwardExp < BaseExpInt)
		{
			char aDbg[128];
			str_format(aDbg, sizeof(aDbg), "EXP scaled: base %d -> %d", BaseExpInt, AwardExp);
			DebugMsg(BlockerID, aDbg);
		}
		else if((PopScale < 1.0f || UniqueScale < 1.0f || LevelScale < 1.0f || DailyScale < 1.0f) && AwardExp == BaseExpInt)
		{
			// factors reduced but rounding/min clamp kept award at base; keep message honest
			char aDbg[128];
			str_format(aDbg, sizeof(aDbg), "EXP factors reduced (rounded to %d)", AwardExp);
			DebugMsg(BlockerID, aDbg);
		}
	}

	if(AwardExp > 0)
	{
		new CExperience(&m_pGameContext->m_World, pChr->m_Pos, AwardExp, BlockerID);
		m_aKillerStats[BlockerID].m_TodayExp += AwardExp;
		m_aKillerStats[BlockerID].m_LastDebugMsg.clear(); // allow next denial reason to be sent
	}
	else
	{
		DebugMsg(BlockerID, "No EXP: final scaled amount <= 0");
	}

	KillStreaks(ClientID, BlockerID);

	m_pGameContext->Bw().Cosmetics()->DoKnockoutEffect(m_aTrackedPlayers[ClientID].m_ImpactedClientID, pChr->m_Pos);

	{ // Send kill msg
		CNetMsg_Sv_KillMsg Msg;
		Msg.m_Killer = BlockerID;
		Msg.m_Victim = ClientID;
		Msg.m_Weapon = WEAPON_GAME;
		Msg.m_ModeSpecial = 0;
		m_pGameContext->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}

	return true;
}

int CBlockTracker::GetActiveNonAfkPlayers() const
{
	int Count = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *p = m_pGameContext->m_apPlayers[i];
		if(!p)
			continue;
		if(!m_pGameContext->Server()->ClientIngame(i))
			continue;
		if(p->GetTeam() == TEAM_SPECTATORS)
			continue;
		if(p->IsAfk() || p->IsPaused())
			continue;
		if(!p->IsPlaying())
			continue;
		Count++;
	}
	return Count;
}

bool CBlockTracker::IsPlayerActive(int ClientID) const
{
	CPlayer *p = m_pGameContext->m_apPlayers[ClientID];
	if(!p)
		return false;
	if(!m_pGameContext->Server()->ClientIngame(ClientID))
		return false;
	if(p->GetTeam() == TEAM_SPECTATORS)
		return false;
	if(p->IsAfk() || p->IsPaused())
		return false;
	if(!p->IsPlaying())
		return false;
	// session length
	int64_t ConnectedTicks = m_pGameContext->Server()->Tick() - p->m_JoinTick;
	if(g_Config.m_SvExpMinSessionMinutes > 0 && ConnectedTicks < (int64_t)g_Config.m_SvExpMinSessionMinutes * m_pGameContext->Server()->TickSpeed() * 60)
		return false;
	return true;
}

bool CBlockTracker::PassedRecentActionChecks(int VictimID, int KillerID) const
{
	CPlayer *pVictim = m_pGameContext->m_apPlayers[VictimID];
	CPlayer *pKiller = m_pGameContext->m_apPlayers[KillerID];
	if(!pVictim || !pKiller)
		return false;
	int Tick = m_pGameContext->Server()->Tick();
	int TickSpeed = m_pGameContext->Server()->TickSpeed();
	if(g_Config.m_SvExpVictimRecentActionSec > 0 && (Tick - pVictim->m_LastActionTick) > g_Config.m_SvExpVictimRecentActionSec * TickSpeed)
		return false;
	if(g_Config.m_SvExpKillerRecentActionSec > 0 && (Tick - pKiller->m_LastActionTick) > g_Config.m_SvExpKillerRecentActionSec * TickSpeed)
		return false;
	return true;
}

bool CBlockTracker::PassedSameVictimLimit(int VictimID, int KillerID, int64_t NowTick)
{
	SKillerRecent &KS = m_aKillerStats[KillerID];
	int TickSpeed = m_pGameContext->Server()->TickSpeed();
	int Window = g_Config.m_SvExpSameVictimWindowSec * TickSpeed;
	auto &map = KS.m_Victims;
	auto it = map.find(VictimID);
	if(it == map.end())
		return true;
	// prune window
	if(NowTick - it->second.FirstTick > Window)
	{
		map.erase(it);
		return true;
	}
	if(it->second.Count >= g_Config.m_SvExpMaxSameVictim)
		return false;
	return true;
}

float CBlockTracker::PopulationScale() const
{
	if(g_Config.m_SvExpTargetFullPlayers <= 0)
		return 1.0f;
	int active = GetActiveNonAfkPlayers();
	int minReq = g_Config.m_SvMinActivePlayersForExp;
	if(active <= minReq)
		return 0.0f;
	float span = (float)(g_Config.m_SvExpTargetFullPlayers - minReq);
	if(span <= 0.0f)
		return 1.0f;
	float scale = (active - minReq) / span;
	if(scale > 1.0f)
		scale = 1.0f;
	if(scale < 0.0f)
		scale = 0.0f;
	return scale;
}

float CBlockTracker::UniqueVictimRatio(int KillerID) const
{
	const SKillerRecent &KS = m_aKillerStats[KillerID];
	int total = 0;
	int unique = 0;
	int64_t NowTick = m_pGameContext->Server()->Tick();
	int TickSpeed = m_pGameContext->Server()->TickSpeed();
	int Window = g_Config.m_SvExpSameVictimWindowSec * TickSpeed; // reuse window
	for(const auto &pr : KS.m_Victims)
	{
		if(NowTick - pr.second.LastTick > Window)
			continue; // ignore stale
		unique++;
		total += pr.second.Count;
	}
	if(total == 0)
		return 1.0f; // neutral when no data
	return (float)unique / (float)total;
}

bool CBlockTracker::DetectLoopPattern(int KillerID, int VictimID, int64_t NowTick)
{
	SKillerRecent &KS = m_aKillerStats[KillerID];
	if(KS.m_LoopSuppressedUntilTick > NowTick)
		return true; // still suppressed
	int TickSpeed = m_pGameContext->Server()->TickSpeed();
	int Window = g_Config.m_SvExpLoopDetectionWindowSec * TickSpeed;
	// examine global buffer for pattern A-B-A-B
	int alternations = 0;
	int lastK = -1, lastV = -1;
	for(auto it = m_GlobalKillBuffer.rbegin(); it != m_GlobalKillBuffer.rend(); ++it)
	{
		if(NowTick - it->m_Tick > Window)
			break;
		if(lastK == -1)
		{
			lastK = it->m_Killer;
			lastV = it->m_Victim;
			continue;
		}
		// check alternating pair with current attempted pair
		if((it->m_Killer == KillerID && it->m_Victim == VictimID) || (it->m_Killer == VictimID && it->m_Victim == KillerID))
		{
			if(it->m_Killer != lastK && it->m_Victim != lastV)
			{
				alternations++;
				lastK = it->m_Killer;
				lastV = it->m_Victim;
			}
		}
	}
	if(alternations >= g_Config.m_SvExpLoopMinAlternations)
	{
		KS.m_LoopSuppressedUntilTick = NowTick + Window; // suppress for one windows
		DebugMsg(KillerID, "EXP suppressed: loop pattern detected");
		return true;
	}
	return false;
}

float CBlockTracker::LevelDiffScale(int KillerID, int VictimID) const
{
	if(g_Config.m_SvExpLevelDiffSoftCap <= 0)
		return 1.0f;
	CPlayer *pKiller = m_pGameContext->m_apPlayers[KillerID];
	CPlayer *pVictim = m_pGameContext->m_apPlayers[VictimID];
	if(!pKiller || !pVictim)
		return 1.0f;
	if(!pKiller->Bw().IsLoggedIn() || !pVictim->Bw().IsLoggedIn())
		return 1.0f;
	int kLevel = pKiller->Bw().GetPlayerLevel();
	int vLevel = pVictim->Bw().GetPlayerLevel();
	int diff = kLevel - vLevel;
	if(diff <= g_Config.m_SvExpLevelDiffSoftCap)
		return 1.0f;
	float K = g_Config.m_SvExpLevelDiffDecayKPercent / 100.0f;
	if(K <= 0.0f)
		K = 4.0f;
	float scale = expf(-(diff - g_Config.m_SvExpLevelDiffSoftCap) / K);
	if(scale < 0.05f)
		scale = 0.05f; // keep a small reward
	return scale;
}

float CBlockTracker::DailySoftCapScale(int KillerID)
{
	SKillerRecent &KS = m_aKillerStats[KillerID];
	time_t t = time(nullptr);
	struct tm tmres;
	time_localtime_safe(&t, &tmres);
	int yyyymmdd = (tmres.tm_year + 1900) * 10000 + (tmres.tm_mon + 1) * 100 + tmres.tm_mday;
	if(KS.m_TodayDate != yyyymmdd)
	{
		KS.m_TodayDate = yyyymmdd;
		KS.m_TodayExp = 0;
	}
	if(g_Config.m_SvExpDailySoftCap <= 0)
		return 1.0f;
	if(KS.m_TodayExp < g_Config.m_SvExpDailySoftCap)
		return 1.0f;
	// beyond cap scale down to 50% then 25% then 10%
	int Over = KS.m_TodayExp - g_Config.m_SvExpDailySoftCap;
	if(Over < g_Config.m_SvExpDailySoftCap)
		return 0.5f;
	if(Over < 2 * g_Config.m_SvExpDailySoftCap)
		return 0.25f;
	return 0.1f;
}

float CBlockTracker::AfkVictimRatio(int KillerID) const
{
	const SKillerRecent &KS = m_aKillerStats[KillerID];
	int afk = 0;
	int total = 0;
	int64_t NowTick = m_pGameContext->Server()->Tick();
	int TickSpeed = m_pGameContext->Server()->TickSpeed();
	int Window = g_Config.m_SvExpSameVictimWindowSec * TickSpeed;
	for(const auto &kv : KS.m_Victims)
	{
		if(NowTick - kv.second.LastTick > Window)
			continue;
		total += kv.second.Count;
		// approximate: if last was afk treat proportionally
		if(kv.second.LastWasAfk)
			afk += 1; // coarse but cheap
	}
	if(total == 0)
		return 0.0f;
	return (float)afk / (float)total;
}

void CBlockTracker::RecordKill(int KillerID, int VictimID, bool VictimWasAfk, int64_t NowTick)
{
	if(KillerID < 0 || KillerID >= MAX_CLIENTS)
		return;

	SKillerRecent &KS = m_aKillerStats[KillerID];
	int TickSpeed = m_pGameContext->Server()->TickSpeed();
	int Window = g_Config.m_SvExpSameVictimWindowSec * TickSpeed;
	// prune stale victim stats
	for(auto it = KS.m_Victims.begin(); it != KS.m_Victims.end();)
	{
		if(NowTick - it->second.LastTick > Window)
			it = KS.m_Victims.erase(it);
		else
			++it;
	}
	auto &vs = KS.m_Victims[VictimID];
	if(vs.Count == 0)
	{
		vs.FirstTick = NowTick;
	}
	vs.Count++;
	vs.LastTick = NowTick;
	vs.LastWasAfk = VictimWasAfk;

	// global buffer
	m_GlobalKillBuffer.push_back({KillerID, VictimID, NowTick});
	while(m_GlobalKillBuffer.size() > 128)
		m_GlobalKillBuffer.pop_front();
}

void CBlockTracker::DebugMsg(int KillerID, const char *pMsg) const
{
	if(!g_Config.m_SvDebugAntifarm)
		return;
	if(KillerID < 0 || KillerID >= MAX_CLIENTS)
		return;
	// suppress duplicated msgs ffs
	SKillerRecent &KS = const_cast<CBlockTracker *>(this)->m_aKillerStats[KillerID];
	if(KS.m_LastDebugMsg == pMsg)
		return;
	KS.m_LastDebugMsg = pMsg;
	m_pGameContext->SendChatTarget(KillerID, pMsg);
}

void CBlockTracker::KillStreaks(int ClientID, int BlockerID)
{
	CCharacter *pBlockerChr = m_pGameContext->GetPlayerChar(BlockerID);
	CCharacter *pClientChr = m_pGameContext->GetPlayerChar(ClientID);

	if(!pBlockerChr || !pClientChr)
		return;

	pBlockerChr->Bw().m_KillStreak++;

	// update hourly best streak (EXP-eligible kills only, i.e. real blocks)
	if(pBlockerChr->Bw().m_KillStreak > m_aHourlyStats[BlockerID].m_BestStreak)
		m_aHourlyStats[BlockerID].m_BestStreak = pBlockerChr->Bw().m_KillStreak;

	if(CPlayer *pBlockerPlayer = pBlockerChr->GetPlayer())
	{
		if(pBlockerPlayer->Bw().IsLoggedIn())
		{
			// only update stored account killstreak if we reached a new maximum
			if(pBlockerChr->Bw().m_KillStreak > pBlockerPlayer->Bw().GetPlayerKillstreak())
			{
				pBlockerPlayer->Bw().SetPlayerKillstreak(pBlockerChr->Bw().m_KillStreak);
			}
		}
	}
	if(pBlockerChr->Bw().m_KillStreak % g_Config.m_SvKillStreakCount == 0)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "'%s' has a killstreak of %i!", m_pGameContext->Server()->ClientName(BlockerID), pBlockerChr->Bw().m_KillStreak);
		m_pGameContext->SendChat(-1, -2, aBuf);
	}

	if(pClientChr->Bw().m_KillStreak >= g_Config.m_SvKillStreakCount)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "'%s's killing spree of %d ended by '%s'!", m_pGameContext->Server()->ClientName(ClientID), pClientChr->Bw().m_KillStreak, m_pGameContext->Server()->ClientName(BlockerID));
		m_pGameContext->SendChat(-1, -2, aBuf);
	}

	pClientChr->Bw().m_KillStreak = 0;
}

void CBlockTracker::Tick()
{
	for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
	{
		auto &Player = m_aTrackedPlayers[ClientID];

		if(!Player.m_Tracked)
			continue;

		if(Player.m_FreezedTick >= 0 && SecondsPassed(Player.m_FreezedTick) >= g_Config.m_SvBlockFreezedInterval && Player.m_ImpactedClientID >= 0)
		{
			if(Blocked(ClientID, Player.m_ImpactedClientID))
			{
				Player.m_IsResisted = false;
				Player.m_ImpactedClientID = -1;
				Player.m_LastImpactedTick = -1;
			}
		}

		if(Player.m_UnfreezedTick >= 0 && SecondsPassed(Player.m_UnfreezedTick) > g_Config.m_SvBlockResetUnfreezedInterval && SecondsPassed(Player.m_LastImpactedTick) > g_Config.m_SvBlockResetNoImpactInterval)
		{
			Player.m_ImpactedClientID = -1;
			Player.m_LastImpactedTick = -1;
		}
	}
}

void CBlockTracker::StartTrackPlayer(int ClientID)
{
	STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
	Player.m_Tracked = true;
	Player.m_IsResisted = false;
	Player.m_LastActionTick = -1;
	Player.m_ImpactedClientID = -1;
	Player.m_LastImpactedTick = -1;
	Player.m_FreezedTick = -1;
	Player.m_UnfreezedTick = m_pGameContext->Server()->Tick();
	Player.m_KilledTick = m_pGameContext->Server()->Tick();

	Player.m_BlockerExpCount.clear();
	// spawn position
	CCharacter *pChr = m_pGameContext->GetPlayerChar(ClientID);
	if(pChr)
		Player.m_SpawnPos = pChr->m_Pos;
	else
		Player.m_SpawnPos = vec2(0, 0);

	// reset killer stats
	SKillerRecent &KS = m_aKillerStats[ClientID];
	KS.m_Victims.clear();

	// initialise hourly stats for this session window
	m_aHourlyStats[ClientID] = {};
}

void CBlockTracker::StopTrackPlayer(int ClientID)
{
	STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
	Player.m_Tracked = false;
	Player.m_LastBlockedTime.clear();
}

void CBlockTracker::OnPlayerFreeze(int ClientID)
{
	STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
	if(!Player.m_Tracked || Player.m_FreezedTick != -1)
		return;

	Player.m_FreezedTick = m_pGameContext->Server()->Tick();
	Player.m_UnfreezedTick = -1;

	if(SecondsPassed(Player.m_LastActionTick) < g_Config.m_SvBlockImpactIntervalToResist)
		Player.m_IsResisted = true;
}

void CBlockTracker::OnPlayerUnfreeze(int ClientID)
{
	STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
	if(!Player.m_Tracked)
		return;

	Player.m_FreezedTick = -1;
	Player.m_UnfreezedTick = m_pGameContext->Server()->Tick();
}

void CBlockTracker::OnPlayerImpacted(int ClientID, int InitiatorID)
{
	if(ClientID == InitiatorID)
		return;

	if(auto events = g_ComponentRegistry.Get<CEvents>())
	{
		auto subs = events->GetSubComponents();
		for(auto &sub : subs)
		{
			CEventComponent *pEv = dynamic_cast<CEventComponent *>(sub.operator->());
			if(!pEv)
				continue;
			// TDM and zCatch handle their own kill/catch detection; don't suppress impact tracking for them
			if(pEv->GetName() && (str_comp(pEv->GetName(), "tdm") == 0 || str_comp(pEv->GetName(), "zcatch") == 0))
				continue;
			const auto &parts = pEv->Participants();
			if(std::find(parts.begin(), parts.end(), ClientID) != parts.end() || std::find(parts.begin(), parts.end(), InitiatorID) != parts.end())
				return;
		}

		auto active = events->GetActiveEvent();
		if(active)
		{
			const char *pEvName = active->GetName();
			if(pEvName && (str_comp(pEvName, "LMB") == 0 || str_comp(pEvName, "tdm") == 0) &&
				(std::find(active->Participants().begin(), active->Participants().end(), ClientID) != active->Participants().end() ||
					std::find(active->Participants().begin(), active->Participants().end(), InitiatorID) != active->Participants().end()))
			{
				return;
			}
		}
	}

	// exclude 1on1 participants
	if(auto p1on1 = g_ComponentRegistry.Get<COneOnOneManager>())
	{
		if(p1on1->GetMatchForPlayer(ClientID) || p1on1->GetMatchForPlayer(InitiatorID))
			return;
	}

	STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
	if(!Player.m_Tracked || Player.m_FreezedTick >= 0 || SecondsPassed(Player.m_UnfreezedTick) < g_Config.m_SvBlockUnfreezeNoImpactInterval)
		return;

	Player.m_ImpactedClientID = InitiatorID;
	Player.m_LastImpactedTick = m_pGameContext->Server()->Tick();
}

bool CBlockTracker::OnPlayerKill(int ClientID)
{
	STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
	if(Player.m_ImpactedClientID < 0 || !Player.m_Tracked)
		return false;
	bool VictimWasAfk = false;
	CPlayer *pVictimPlayer = m_pGameContext->m_apPlayers[ClientID];
	if(pVictimPlayer)
		VictimWasAfk = pVictimPlayer->IsAfk() || pVictimPlayer->IsPaused();

	// hourly stats: count this kill/death regardless of EXP eligibility
	int KillerID = Player.m_ImpactedClientID;
	if(KillerID >= 0 && KillerID < MAX_CLIENTS)
	{
		m_aHourlyStats[KillerID].m_Kills++;
		m_aHourlyStats[KillerID].m_Active = true;
	}
	m_aHourlyStats[ClientID].m_Deaths++;
	m_aHourlyStats[ClientID].m_Active = true;

	// notify active event of the block kill (e.g. used by zCatch)
	if(auto events = g_ComponentRegistry.Get<CEvents>())
	{
		if(auto active = events->GetActiveEvent())
			active->OnBlockedKill(ClientID, KillerID);
	}

	const bool ExpKillMsgSent = Blocked(ClientID, Player.m_ImpactedClientID);

	// record kill stats regardless of EXP (for detection)
	RecordKill(Player.m_ImpactedClientID, ClientID, VictimWasAfk, m_pGameContext->Server()->Tick());

	OnPlayerDeath(ClientID);

	CCharacter *pChr = m_pGameContext->GetPlayerChar(ClientID);
	if(CPlayer *pPlayer = pChr->GetPlayer(); pPlayer && pPlayer->Bw().IsLoggedIn())
	{
		// don't count account-level deaths during active server events
		bool InEvent = false;
		if(auto pEvents = g_ComponentRegistry.Get<CEvents>(); pEvents && pEvents->GetActiveEvent())
		{
			auto pActiveEvent = pEvents->GetActiveEvent();
			if(pActiveEvent->GetState() == CEventComponent::EEventState::Active || pActiveEvent->GetState() == CEventComponent::EEventState::Preparation)
			{
				const auto &Participants = pActiveEvent->Participants();
				if(std::find(Participants.begin(), Participants.end(), ClientID) != Participants.end())
					InEvent = true;
			}
		}
		if(!InEvent)
			pPlayer->Bw().SetPlayerDeaths(pPlayer->Bw().GetPlayerDeaths() + 1);
	}

	return ExpKillMsgSent;
}

void CBlockTracker::ResetHourlyStats(int ClientID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		m_aHourlyStats[ClientID] = {};
}

void CBlockTracker::ResetAllHourlyStats()
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
		m_aHourlyStats[i] = {};
}

void CBlockTracker::OnPlayerDeath(int ClientID)
{
	STrackedPlayer &Player = m_aTrackedPlayers[ClientID];
	Player.m_BlockerExpCount.clear();

	Player.m_IsResisted = false;
	Player.m_ImpactedClientID = -1;
	Player.m_LastImpactedTick = -1;
	Player.m_FreezedTick = -1;
	Player.m_UnfreezedTick = -1;
	Player.m_KilledTick = m_pGameContext->Server()->Tick();
}
