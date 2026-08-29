// AI bot component for Block game mode - Experimental by Nouaa

#include "ai_bot.h"

#include <base/vmath.h>

#include <engine/console.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/storage.h>

#include <game/collision.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <block/base.h>
#include <block/context.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

void CAiBotComponent::SActionStats::Decay(float f)
{
	m_MoveLeft = (uint32_t)(m_MoveLeft * f);
	m_MoveRight = (uint32_t)(m_MoveRight * f);
	m_MoveIdle = (uint32_t)(m_MoveIdle * f);
	m_Jump = (uint32_t)(m_Jump * f);
	m_NoJump = (uint32_t)(m_NoJump * f);
	m_Fire = (uint32_t)(m_Fire * f);
	m_NoFire = (uint32_t)(m_NoFire * f);
	m_Hook = (uint32_t)(m_Hook * f);
	m_NoHook = (uint32_t)(m_NoHook * f);
	m_BlockReward = (uint32_t)(m_BlockReward * f);
}

CAiBotComponent::CAiBotComponent(CGameContext *pGameServer) :
	CComponent(pGameServer)
{
	str_copy(m_aAccountName, "mabite", sizeof(m_aAccountName));
	str_copy(m_aAccountPassword, "magrossebite123", sizeof(m_aAccountPassword));
	const char *pMap = g_Config.m_SvMap;
	if(pMap && str_comp_nocase(pMap, "blmapV3ROYAL") == 0)
		m_MapAllowed = true;
	LoadModel();

	CONSOLE_COMMAND("ai_enable", "i[0|1]", ConAiEnable, "")
	CONSOLE_COMMAND("ai_stats", "", ConAiStats, "")
	CONSOLE_COMMAND("ai_spawn", "", ConAiSpawn, "")
	CONSOLE_COMMAND("ai_despawn", "", ConAiDespawn, "")
	CONSOLE_COMMAND("ai_reset", "", ConAiReset, "")
	CONSOLE_COMMAND("ai_set_min_samples", "i[value]", ConAiSetMinSamples, "")
	CONSOLE_COMMAND("ai_save", "", ConAiSave, "")
	CONSOLE_COMMAND("ai_learn_all", "i[0|1]", ConAiLearnAll, "")
}

void CAiBotComponent::OnShutdown()
{
	SaveModel();
}

bool CAiBotComponent::IsBotActive() const
{
	return m_BotCid >= 0 && m_BotCid < MAX_CLIENTS && GameServer()->m_apPlayers[m_BotCid];
}

void CAiBotComponent::OnTick()
{
	if(!m_Enabled || !m_MapAllowed)
		return;

	if(!m_MapAnalyzed)
		AnalyzeMap();
	TickCollect();
	TickControlBot();
	MaybeAutoSave();
}

void CAiBotComponent::TickCollect()
{
	if(m_LearnAllPlayers)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pPl = GameServer()->m_apPlayers[i];
			if(!pPl)
				continue;
			if(pPl->Block().m_IsNpc)
				continue;
			if(!pPl->Block().IsLoggedIn())
				continue;
			CCharacter *pChr = pPl->GetCharacter();
			if(!pChr)
				continue;

			CCharacter *pEnemy = nullptr;
			float Dist2 = 0.f;
			for(int j = 0; j < MAX_CLIENTS; j++)
			{
				if(j == i)
					continue;
				CPlayer *pOther = GameServer()->m_apPlayers[j];
				if(!pOther || pOther->Block().m_IsNpc)
					continue;
				CCharacter *pOChr = pOther->GetCharacter();
				if(!pOChr)
					continue;
				float D2 = length(pChr->m_Pos - pOChr->m_Pos);
				if(!pEnemy || D2 < Dist2)
				{
					pEnemy = pOChr;
					Dist2 = D2;
				}
			}
			uint64_t Key = BuildFeatureKey(pChr, pEnemy);
			UpdateStats(Key, pChr);
			++m_SampleCount;
		}
		m_BestPlayerCid = -1;
	}
	else
	{
		int Best = -1;
		int BestLevel = -1;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pPl = GameServer()->m_apPlayers[i];
			if(!pPl)
				continue;
			if(pPl->Block().m_IsNpc)
				continue;
			if(!pPl->Block().IsLoggedIn())
				continue;
			CCharacter *pChr = pPl->GetCharacter();
			if(!pChr)
				continue;
			int Lvl = pPl->Block().GetPlayerLevel();
			if(Lvl > BestLevel)
			{
				BestLevel = Lvl;
				Best = i;
			}
		}
		m_BestPlayerCid = Best;
		if(Best >= 0)
		{
			CPlayer *pBest = GameServer()->m_apPlayers[Best];
			CCharacter *pBestChr = pBest->GetCharacter();
			if(pBestChr)
			{
				CCharacter *pEnemy = nullptr;
				float Dist2 = 0.f;
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(i == Best)
						continue;
					CPlayer *pOther = GameServer()->m_apPlayers[i];
					if(!pOther || pOther->Block().m_IsNpc)
						continue;
					CCharacter *pOChr = pOther->GetCharacter();
					if(!pOChr)
						continue;
					float D2 = length(pBestChr->m_Pos - pOChr->m_Pos);
					if(!pEnemy || D2 < Dist2)
					{
						pEnemy = pOChr;
						Dist2 = D2;
					}
				}
				uint64_t Key = BuildFeatureKey(pBestChr, pEnemy);
				UpdateStats(Key, pBestChr);
				++m_SampleCount;
			}
		}
	}
	EvaluateBlockingReward();

	if(Server()->Tick() - m_LastDecayTick > m_DecayIntervalTicks)
	{
		m_LastDecayTick = Server()->Tick();
		for(auto &Kv : m_Table)
			Kv.second.Decay(m_DecayFactor);
		m_Dirty = true;
	}
}

void CAiBotComponent::TickControlBot()
{
	if(!IsBotActive())
	{
		if(m_SampleCount >= m_MinSamplesToSpawn)
			EnsureBotSpawned();
		return;
	}

	CPlayer *pBot = GameServer()->m_apPlayers[m_BotCid];
	if(!pBot->GetCharacter())
		pBot->Respawn();
	InferAndApplyInput();
}

void CAiBotComponent::EnsureBotSpawned()
{
	if(IsBotActive())
		return;

	int Slot = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameServer()->m_apPlayers[i])
		{
			Slot = i;
			break;
		}
	}
	if(Slot < 0)
		return;
	GameServer()->Block().BotJoin(Slot, "mabite");

	if(GameServer()->m_apPlayers[Slot])
	{
		delete GameServer()->m_apPlayers[Slot];
		GameServer()->m_apPlayers[Slot] = nullptr;
	}

	GameServer()->m_apPlayers[Slot] = new(Slot) CPlayer(GameServer(), (uint32_t)Slot, Slot, TEAM_RED);
	GameServer()->m_apPlayers[Slot]->Block().m_IsNpc = true;
	m_BotCid = Slot;
	LoginBotAccount();
	GameServer()->m_apPlayers[Slot]->Respawn();
	Log("Spawned AI bot at cid=%d", Slot);
}

void CAiBotComponent::DespawnBot()
{
	if(!IsBotActive())
		return;
	int Cid = m_BotCid;
	GameServer()->Block().BotLeave(Cid, true);
	if(GameServer()->m_apPlayers[Cid])
	{
		delete GameServer()->m_apPlayers[Cid];
		GameServer()->m_apPlayers[Cid] = nullptr;
	}
	m_BotCid = -1;
	Log("AI bot despawned cid=%d", Cid);
}

uint64_t CAiBotComponent::BuildFeatureKey(const CCharacter *pChr, const CCharacter *pEnemy) const
{
	int px = (int)std::round(pChr->m_Pos.x / 64.0f);
	int py = (int)std::round(pChr->m_Pos.y / 64.0f);

	CCharacter *pMutable = const_cast<CCharacter *>(pChr);
	int Vx = pMutable->Core()->m_Vel.x > 1.0f ? 1 : (pMutable->Core()->m_Vel.x < -1.0f ? 2 : 0);
	int Vy = pMutable->Core()->m_Vel.y > 1.0f ? 1 : (pMutable->Core()->m_Vel.y < -1.0f ? 2 : 0);
	int Grounded = pMutable->IsGrounded() ? 1 : 0;
	int Weapon = pMutable->Core()->m_ActiveWeapon & 7;
	int Relq = 0;
	int DistBucket = 0;
	if(pEnemy)
	{
		vec2 d = pEnemy->m_Pos - pChr->m_Pos;
		Relq = (d.x >= 0 ? 1 : 0) | (d.y >= 0 ? 2 : 0); // 2 bits
		float D2 = length(d);
		DistBucket = D2 < 200 ? 0 : D2 < 400 ? 1 :
				    D2 < 800         ? 2 :
						       3;
	}
	uint64_t Key = 0;
	auto Pack = [&](uint64_t v, int Bits) { Key = (Key << Bits) | (v & ((1ULL << Bits) - 1)); };
	Pack((uint64_t)(px & 0x7F), 7);
	Pack((uint64_t)(py & 0x7F), 7);
	Pack(Vx, 2);
	Pack(Vy, 2);
	Pack(Grounded, 1);
	Pack(Weapon, 3);
	Pack(Relq, 2);
	Pack(DistBucket, 2);
	return Key;
}

void CAiBotComponent::UpdateStats(uint64_t Key, const CCharacter *pChr)
{
	auto &Row = m_Table[Key];
	CCharacter *pMutable = const_cast<CCharacter *>(pChr);
	const auto &In = pMutable->Core()->m_Input;
	int Dir = In.m_Direction;
	if(Dir < -1)
		Dir = -1;
	if(Dir > 1)
		Dir = 1;
	if(Dir == -1)
		Row.m_MoveLeft++;
	else if(Dir == 1)
		Row.m_MoveRight++;
	else
		Row.m_MoveIdle++;
	if(In.m_Jump & 1)
		Row.m_Jump++;
	else
		Row.m_NoJump++;
	if(In.m_Fire & 1)
		Row.m_Fire++;
	else
		Row.m_NoFire++;
	if(In.m_Hook & 1)
		Row.m_Hook++;
	else
		Row.m_NoHook++;
	int Weapon = std::clamp(pMutable->Core()->m_ActiveWeapon, 0, 7);
	Row.m_WeaponUsed[Weapon]++;
	static int s_LastWeapon[MAX_CLIENTS] = {0};
	int Cid = pMutable->GetPlayer()->GetCid();
	if(s_LastWeapon[Cid] != Weapon)
	{
		Row.m_SwitchedWeapon++;
		s_LastWeapon[Cid] = Weapon;
	}
	vec2 Tgt = vec2(In.m_TargetX, In.m_TargetY);
	if(length(Tgt) > 0.1f)
	{
		Row.m_AimSector[ComputeAimSector(Tgt)]++;
	}
	m_Dirty = true;
}

void CAiBotComponent::InferAndApplyInput()
{
	if(!IsBotActive())
		return;
	CPlayer *pBot = GameServer()->m_apPlayers[m_BotCid];
	CCharacter *pChr = pBot->GetCharacter();
	if(!pChr)
		return;

	CCharacter *pEnemy = nullptr;
	float Dist2 = 0.f;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == m_BotCid)
			continue;
		CPlayer *pO = GameServer()->m_apPlayers[i];
		if(!pO)
			continue;
		if(pO->Block().m_IsNpc)
			continue;
		CCharacter *Pc = pO->GetCharacter();
		if(!Pc)
			continue;
		float D2 = length(pChr->m_Pos - Pc->m_Pos);
		if(!pEnemy || D2 < Dist2)
		{
			pEnemy = Pc;
			Dist2 = D2;
		}
	}
	uint64_t Key = BuildFeatureKey(pChr, pEnemy);
	m_LastBotKey = Key;
	m_HaveLastBotKey = true;
	auto It = m_Table.find(Key);
	CNetObj_PlayerInput Input{};

	if(pEnemy)
	{
		vec2 d = pEnemy->m_Pos - pChr->m_Pos;
		if(fabs(d.x) < 1.f)
			d.x = (d.x >= 0) ? 1.f : -1.f;
		Input.m_TargetX = (int)d.x;
		Input.m_TargetY = (int)d.y;
	}
	else
	{
		Input.m_TargetX = 0;
		Input.m_TargetY = -1;
	}

	if(It != m_Table.end())
	{
		const auto &Row = It->second;

		uint64_t WLeft = Row.m_MoveLeft * (1 + Row.m_BlockReward);
		uint64_t WRight = Row.m_MoveRight * (1 + Row.m_BlockReward);
		uint64_t WIdle = Row.m_MoveIdle * (1 + Row.m_BlockReward);
		int Move = 0;
		uint64_t Best = WIdle;
		if(WLeft > Best)
		{
			Move = -1;
			Best = WLeft;
		}
		if(WRight > Best)
		{
			Move = 1;
		}
		Input.m_Direction = Move;
		Input.m_Jump = Row.m_Jump >= Row.m_NoJump ? 1 : 0;
		Input.m_Fire = Row.m_Fire >= Row.m_NoFire ? 1 : 0;
		Input.m_Hook = Row.m_Hook >= Row.m_NoHook ? 1 : 0;

		if(m_BlockingMode)
		{
			float dx = m_BlockTarget.x - pChr->m_Pos.x;
			if(std::fabs(dx) > 8.0f)
				Input.m_Direction = dx > 0 ? 1 : -1;
			else
				Input.m_Direction = 0; // hold position
		}
		int Sector = 0;
		uint32_t Sbest = 0;
		for(int s = 0; s < 8; s++)
		{
			if(Row.m_AimSector[s] > Sbest)
			{
				Sbest = Row.m_AimSector[s];
				Sector = s;
			}
		}
		float Angle = (float)Sector * (pi / 4.0f) + (pi / 8.0f);
		Input.m_TargetX = (int)(cosf(Angle) * 1000.0f);
		Input.m_TargetY = (int)(sinf(Angle) * 1000.0f);
	}
	else
	{
		if(pEnemy)
			Input.m_Direction = (pEnemy->m_Pos.x > pChr->m_Pos.x) ? 1 : -1;
		Input.m_Jump = (Server()->Tick() % 50 == 0) ? 1 : 0;
		Input.m_Fire = (Server()->Tick() % 30 == 0) ? 1 : 0;
		Input.m_Hook = 0;
	}

	Input.m_PlayerFlags = 0;
	ApplyInput(pBot, Input);
}

void CAiBotComponent::EvaluateBlockingReward()
{
	if(!IsBotActive())
		return;
	if(Server()->Tick() - m_LastRewardTick < m_RewardCooldownTicks)
		return;
	CPlayer *pBot = GameServer()->m_apPlayers[m_BotCid];
	CCharacter *pBotChr = pBot ? pBot->GetCharacter() : nullptr;
	if(!pBotChr || !m_HaveLastBotKey)
		return;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == m_BotCid)
			continue;
		CPlayer *pO = GameServer()->m_apPlayers[i];
		if(!pO || pO->Block().m_IsNpc)
			continue;
		CCharacter *pOC = pO->GetCharacter();
		if(!pOC)
			continue;
		float Dist = distance(pOC->m_Pos, pBotChr->m_Pos);
		float CurSpeed = length(pOC->Core()->m_Vel);
		float Prev = m_LastEnemySpeed[i];
		if(Dist < m_BlockDetectRadius && Prev > 0.1f && CurSpeed < Prev * m_BlockSpeedDropFactor)
		{
			auto It = m_Table.find(m_LastBotKey);
			if(It != m_Table.end())
			{
				It->second.m_BlockReward += (uint32_t)ceilf(m_RewardPerEvent);
				m_Dirty = true;
			}
			m_LastRewardTick = Server()->Tick();
			break;
		}
		m_LastEnemySpeed[i] = CurSpeed;
	}

	m_BlockingMode = false;
	if(pBotChr && !m_Chokepoints.empty())
	{
		CCharacter *pTargetEnemy = nullptr;
		float BestScore = 0.f;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i == m_BotCid)
				continue;
			CPlayer *pO = GameServer()->m_apPlayers[i];
			if(!pO || pO->Block().m_IsNpc)
				continue;
			CCharacter *pOC = pO->GetCharacter();
			if(!pOC)
				continue;
			float Spd = length(pOC->Core()->m_Vel);
			if(Spd > BestScore)
			{
				BestScore = Spd;
				pTargetEnemy = pOC;
			}
		}
		if(pTargetEnemy)
		{
			vec2 Spot = FindBlockingSpot(pTargetEnemy, pBotChr);
			if(length(Spot) > 0.1f)
			{
				m_BlockTarget = Spot;
				m_BlockingMode = true;
			}
		}
	}
}

void CAiBotComponent::AnalyzeMap()
{
	if(m_MapAnalyzed)
		return;
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
	{
		m_MapAnalyzed = true;
		return;
	}
	const int W = pCol->GetWidth();
	const int H = pCol->GetHeight();
	for(int y = 2; y < H - 2; y++)
	{
		for(int x = 2; x < W - 2; x++)
		{
			int LeftSolid = pCol->GetTile(x - 1, y) == TILE_SOLID;
			int RightSolid = pCol->GetTile(x + 1, y) == TILE_SOLID;
			int CenterEmpty = pCol->GetTile(x, y) != TILE_SOLID;
			if(LeftSolid && RightSolid && CenterEmpty)
			{
				vec2 Pos = vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
				m_Chokepoints.push_back({Pos});
			}
		}
	}
	m_MapAnalyzed = true;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Map analyzed: %lu chokepoints", (unsigned long)m_Chokepoints.size());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, GetName(), aBuf);
}

vec2 CAiBotComponent::FindBlockingSpot(const CCharacter *pEnemy, const CCharacter *pBot) const
{
	if(m_Chokepoints.empty() || !pEnemy || !pBot)
		return vec2(0, 0);
	CCharacter *pEnemyMut = const_cast<CCharacter *>(pEnemy);
	vec2 Future = pEnemy->m_Pos + pEnemyMut->Core()->m_Vel * 10.0f; // simple projection
	float Best = 1e9f;
	vec2 BestPos(0, 0);
	for(const auto &c : m_Chokepoints)
	{
		float DEnemy = distance(Future, c.m_Pos);
		float DBot = distance(pBot->m_Pos, c.m_Pos);
		// choose chokepoint near enemy trajectory but not too far for bot (weighted sum)
		float Score = DEnemy + DBot * 0.5f;
		if(Score < Best && PredictEnemyThroughChoke(pEnemy, c.m_Pos))
		{
			Best = Score;
			BestPos = c.m_Pos;
		}
	}
	return BestPos;
}

bool CAiBotComponent::PredictEnemyThroughChoke(const CCharacter *pEnemy, const vec2 &ChokePos) const
{
	if(!pEnemy)
		return false;
	vec2 Rel = ChokePos - pEnemy->m_Pos;
	CCharacter *pEnemyMut = const_cast<CCharacter *>(pEnemy);
	vec2 v = pEnemyMut->Core()->m_Vel;
	if(length(v) < 1.0f)
		return false;
	float t = dot(Rel, v) / (length(v) * length(v));
	if(t < 0 || t > 2.0f)
		return false; // only near-term
	vec2 Proj = pEnemy->m_Pos + v * t;
	return distance(Proj, ChokePos) < 96.0f;
}

void CAiBotComponent::ApplyInput(CPlayer *pBot, const CNetObj_PlayerInput &In)
{
	CNetObj_PlayerInput Tmp = In;
	pBot->OnPredictedInput(&Tmp);
	pBot->OnDirectInput(&Tmp);
}

void CAiBotComponent::LoginBotAccount()
{
	CPlayer *pBot = GameServer()->m_apPlayers[m_BotCid];
	if(!pBot)
		return;

	if(pBot->Block().IsLoggedIn())
		return;
	pBot->Block().SetPlayerId(0);
	pBot->Block().SetPlayerName(m_aAccountName);
	pBot->Block().SetPlayerPassword(m_aAccountPassword);
	// real registration/login path would create DB entries; here we just mark logged-in state mimic by giving Id != 0.
	pBot->Block().SetPlayerId(13371337);
	pBot->Block().m_IsNpc = true;
}

void CAiBotComponent::LoadModel()
{
	IOHANDLE f = GameServer()->Storage()->OpenFile("ai_model.dat", IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!f)
		return;
	char Magic[9] = {0};
	if(io_read(f, Magic, 8) != 8)
	{
		io_close(f);
		return;
	}
	if(str_comp(Magic, "AIBOTV2") != 0 && str_comp(Magic, "AIBOTV3") != 0)
	{
		io_close(f);
		return;
	}
	int Version = 0;
	io_read(f, &Version, sizeof(Version));
	if(Version != 2 && Version != 3)
	{
		io_close(f);
		return;
	}
	uint64_t Count = 0;
	if(io_read(f, &Count, sizeof(Count)) != sizeof(Count))
	{
		io_close(f);
		return;
	}
	for(uint64_t i = 0; i < Count; i++)
	{
		uint64_t Key;
		SActionStats Stats{};
		if(io_read(f, &Key, sizeof(Key)) != sizeof(Key))
			break;
		if(io_read(f, &Stats, sizeof(Stats)) != sizeof(Stats))
			break;
		m_Table[Key] = Stats;
	}
	io_close(f);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Loaded AI model entries=%lu", (unsigned long)m_Table.size());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, GetName(), aBuf);
}

void CAiBotComponent::SaveModel()
{
	if(!m_Dirty)
		return;
	IOHANDLE f = GameServer()->Storage()->OpenFile("ai_model.dat.tmp", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!f)
		return;
	const char *Magic = "AIBOTV2";
	io_write(f, Magic, 8);
	io_write(f, &m_ModelVersion, sizeof(m_ModelVersion));
	uint64_t Count = m_Table.size();
	io_write(f, &Count, sizeof(Count));
	for(const auto &Kv : m_Table)
	{
		io_write(f, &Kv.first, sizeof(Kv.first));
		io_write(f, &Kv.second, sizeof(Kv.second));
	}
	io_close(f);

	GameServer()->Storage()->RenameFile("ai_model.dat.tmp", "ai_model.dat", IStorage::TYPE_SAVE);
	m_Dirty = false;
	m_LastSaveTick = Server()->Tick();
}

int CAiBotComponent::ComputeAimSector(const vec2 &Delta) const
{
	float Ang = atan2f(Delta.y, Delta.x);
	if(Ang < 0)
		Ang += 2.0f * pi;

	int Sector = (int)floorf(Ang / (pi / 4.0f));
	if(Sector < 0)
		Sector = 0;
	if(Sector > 7)
		Sector = 7;
	return Sector;
}

void CAiBotComponent::MaybeAutoSave()
{
	if(!m_Dirty)
		return;
	if(Server()->Tick() - m_LastSaveTick < m_SaveIntervalTicks)
		return;
	SaveModel();
}

void CAiBotComponent::ConAiEnable(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CAiBotComponent *>(pUser);
	pSelf->m_Enabled = pResult->GetInteger(0) != 0;
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), pSelf->m_Enabled ? "enabled" : "disabled");
}
void CAiBotComponent::ConAiStats(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CAiBotComponent *>(pUser);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "samples=%d keys=%lu botCid=%d best=%d", pSelf->m_SampleCount, (unsigned long)pSelf->m_Table.size(), pSelf->m_BotCid, pSelf->m_BestPlayerCid);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), aBuf);
}
void CAiBotComponent::ConAiSpawn(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CAiBotComponent *>(pUser);
	pSelf->EnsureBotSpawned();
}
void CAiBotComponent::ConAiDespawn(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CAiBotComponent *>(pUser);
	pSelf->DespawnBot();
}
void CAiBotComponent::ConAiReset(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CAiBotComponent *>(pUser);
	pSelf->DespawnBot();
	pSelf->m_Table.clear();
	pSelf->m_SampleCount = 0;
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), "model reset");
}
void CAiBotComponent::ConAiSetMinSamples(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CAiBotComponent *>(pUser);
	int v = pResult->GetInteger(0);
	if(v < 0)
		v = 0;
	if(v > 10000000)
		v = 10000000;
	pSelf->m_MinSamplesToSpawn = v;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "min_samples=%d", v);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), aBuf);
}
void CAiBotComponent::ConAiSave(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CAiBotComponent *>(pUser);
	pSelf->SaveModel();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), "model saved");
}
void CAiBotComponent::ConAiLearnAll(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CAiBotComponent *>(pUser);
	pSelf->m_LearnAllPlayers = pResult->GetInteger(0) != 0;
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), pSelf->m_LearnAllPlayers ? "learning from ALL players" : "learning from BEST player only");
}
