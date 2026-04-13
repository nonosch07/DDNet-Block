// AI bot component for Blockworlds game mode - Experimental by Nouaa

#include "ai_bot.h"

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

#include <base/system.h>
#include <base/vmath.h>
#include <cmath>
#include <cstdio>


void CAiBotComponent::SActionStats::Decay(float f)
{
	moveLeft = (uint32_t)(moveLeft * f);
	moveRight = (uint32_t)(moveRight * f);
	moveIdle = (uint32_t)(moveIdle * f);
	jump = (uint32_t)(jump * f);
	noJump = (uint32_t)(noJump * f);
	fire = (uint32_t)(fire * f);
	noFire = (uint32_t)(noFire * f);
	hook = (uint32_t)(hook * f);
	noHook = (uint32_t)(noHook * f);
	blockReward = (uint32_t)(blockReward * f);
}

CAiBotComponent::CAiBotComponent(CGameContext *pGameServer) :
	CComponent(pGameServer)
{
	str_copy(m_aAccountName, "mabite", sizeof(m_aAccountName));
	str_copy(m_aAccountPassword, "magrossebite123", sizeof(m_aAccountPassword));
	const char *pMap = Server()->GetMapName();
	if(pMap && str_comp_nocase(pMap, "blmapV3ROYAL") == 0)
		m_MapAllowed = true;
	LoadModel();

	CONSOLE_COMMAND("ai_enable", "i[0|1]", ConAiEnable, "") \
	CONSOLE_COMMAND("ai_stats", "", ConAiStats, "") \
	CONSOLE_COMMAND("ai_spawn", "", ConAiSpawn, "") \
	CONSOLE_COMMAND("ai_despawn", "", ConAiDespawn, "") \
	CONSOLE_COMMAND("ai_reset", "", ConAiReset, "") \
	CONSOLE_COMMAND("ai_set_min_samples", "i[value]", ConAiSetMinSamples, "") \
	CONSOLE_COMMAND("ai_save", "", ConAiSave, "") \
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
			if(pPl->m_IsNpc)
				continue;
			if(!pPl->IsLoggedIn())
				continue;
			CCharacter *pChr = pPl->GetCharacter();
			if(!pChr)
				continue;

			CCharacter *pEnemy = nullptr;
			float dist2 = 0.f;
			for(int j = 0; j < MAX_CLIENTS; j++)
			{
				if(j == i)
					continue;
				CPlayer *pOther = GameServer()->m_apPlayers[j];
				if(!pOther || pOther->m_IsNpc)
					continue;
				CCharacter *pOChr = pOther->GetCharacter();
				if(!pOChr)
					continue;
				float d2 = length(pChr->m_Pos - pOChr->m_Pos);
				if(!pEnemy || d2 < dist2)
				{
					pEnemy = pOChr;
					dist2 = d2;
				}
			}
			uint64_t key = BuildFeatureKey(pChr, pEnemy);
			UpdateStats(key, pChr);
			++m_SampleCount;
		}
		m_BestPlayerCid = -1;
	}
	else
	{
		int best = -1;
		int bestLevel = -1;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pPl = GameServer()->m_apPlayers[i];
			if(!pPl)
				continue;
			if(pPl->m_IsNpc)
				continue;
			if(!pPl->IsLoggedIn())
				continue;
			CCharacter *pChr = pPl->GetCharacter();
			if(!pChr)
				continue;
			int lvl = pPl->GetPlayerLevel();
			if(lvl > bestLevel)
			{
				bestLevel = lvl;
				best = i;
			}
		}
		m_BestPlayerCid = best;
		if(best >= 0)
		{
			CPlayer *pBest = GameServer()->m_apPlayers[best];
			CCharacter *pBestChr = pBest->GetCharacter();
			if(pBestChr)
			{
				CCharacter *pEnemy = nullptr;
				float dist2 = 0.f;
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(i == best)
						continue;
					CPlayer *pOther = GameServer()->m_apPlayers[i];
					if(!pOther || pOther->m_IsNpc)
						continue;
					CCharacter *pOChr = pOther->GetCharacter();
					if(!pOChr)
						continue;
					float d2 = length(pBestChr->m_Pos - pOChr->m_Pos);
					if(!pEnemy || d2 < dist2)
					{
						pEnemy = pOChr;
						dist2 = d2;
					}
				}
				uint64_t key = BuildFeatureKey(pBestChr, pEnemy);
				UpdateStats(key, pBestChr);
				++m_SampleCount;
			}
		}
	}
	EvaluateBlockingReward();

	if(Server()->Tick() - m_LastDecayTick > m_DecayIntervalTicks)
	{
		m_LastDecayTick = Server()->Tick();
		for(auto &kv : m_Table)
			kv.second.Decay(m_DecayFactor);
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

	int slot = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameServer()->m_apPlayers[i])
		{
			slot = i;
			break;
		}
	}
	if(slot < 0)
		return;
	Server()->BotJoin(slot, "mabite");

	if(GameServer()->m_apPlayers[slot])
	{
		delete GameServer()->m_apPlayers[slot];
		GameServer()->m_apPlayers[slot] = nullptr;
	}

	GameServer()->m_apPlayers[slot] = new(slot) CPlayer(GameServer(), (uint32_t)slot, slot, TEAM_RED);
	GameServer()->m_apPlayers[slot]->m_IsNpc = true;
	m_BotCid = slot;
	LoginBotAccount();
	GameServer()->m_apPlayers[slot]->Respawn();
	Log("Spawned AI bot at cid=%d", slot);
}

void CAiBotComponent::DespawnBot()
{
	if(!IsBotActive())
		return;
	int cid = m_BotCid;
	Server()->BotLeave(cid, true);
	if(GameServer()->m_apPlayers[cid])
	{
		delete GameServer()->m_apPlayers[cid];
		GameServer()->m_apPlayers[cid] = nullptr;
	}
	m_BotCid = -1;
	Log("AI bot despawned cid=%d", cid);
}

uint64_t CAiBotComponent::BuildFeatureKey(const CCharacter *pChr, const CCharacter *pEnemy) const
{
	int px = (int)round(pChr->m_Pos.x / 64.0f);
	int py = (int)round(pChr->m_Pos.y / 64.0f);

	CCharacter *pMutable = const_cast<CCharacter *>(pChr);
	int vx = pMutable->Core()->m_Vel.x > 1.0f ? 1 : (pMutable->Core()->m_Vel.x < -1.0f ? 2 : 0);
	int vy = pMutable->Core()->m_Vel.y > 1.0f ? 1 : (pMutable->Core()->m_Vel.y < -1.0f ? 2 : 0);
	int grounded = pMutable->IsGrounded() ? 1 : 0;
	int weapon = pMutable->Core()->m_ActiveWeapon & 7;
	int relq = 0;
	int distBucket = 0;
	if(pEnemy)
	{
		vec2 d = pEnemy->m_Pos - pChr->m_Pos;
		relq = (d.x >= 0 ? 1 : 0) | (d.y >= 0 ? 2 : 0); // 2 bits
		float d2 = length(d);
		distBucket = d2 < 200 ? 0 : d2 < 400 ? 1 : d2 < 800 ? 2 : 3;
	}
	uint64_t key = 0;
	auto pack = [&](uint64_t v, int bits) { key = (key << bits) | (v & ((1ULL << bits) - 1)); };
	pack((uint64_t)(px & 0x7F), 7);
	pack((uint64_t)(py & 0x7F), 7);
	pack(vx, 2);
	pack(vy, 2);
	pack(grounded, 1);
	pack(weapon, 3);
	pack(relq, 2);
	pack(distBucket, 2);
	return key;
}

void CAiBotComponent::UpdateStats(uint64_t key, const CCharacter *pChr)
{
	auto &row = m_Table[key];
	CCharacter *pMutable = const_cast<CCharacter *>(pChr);
	const auto &in = pMutable->Core()->m_Input;
	int dir = in.m_Direction;
	if(dir < -1)
		dir = -1;
	if(dir > 1)
		dir = 1;
	if(dir == -1)
		row.moveLeft++;
	else if(dir == 1)
		row.moveRight++;
	else
		row.moveIdle++;
	if(in.m_Jump & 1)
		row.jump++;
	else
		row.noJump++;
	if(in.m_Fire & 1)
		row.fire++;
	else
		row.noFire++;
	if(in.m_Hook & 1)
		row.hook++;
	else
		row.noHook++;
	int weapon = clamp(pMutable->Core()->m_ActiveWeapon, 0, 7);
	row.weaponUsed[weapon]++;
	static int s_LastWeapon[MAX_CLIENTS] = {0};
	int cid = pMutable->GetPlayer()->GetCid();
	if(s_LastWeapon[cid] != weapon)
	{
		row.switchedWeapon++;
		s_LastWeapon[cid] = weapon;
	}
	vec2 tgt = vec2(in.m_TargetX, in.m_TargetY);
	if(length(tgt) > 0.1f)
	{
		row.aimSector[ComputeAimSector(tgt)]++;
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
	float dist2 = 0.f;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == m_BotCid)
			continue;
		CPlayer *pO = GameServer()->m_apPlayers[i];
		if(!pO)
			continue;
		if(pO->m_IsNpc)
			continue;
		CCharacter *pc = pO->GetCharacter();
		if(!pc)
			continue;
		float d2 = length(pChr->m_Pos - pc->m_Pos);
		if(!pEnemy || d2 < dist2)
		{
			pEnemy = pc;
			dist2 = d2;
		}
	}
	uint64_t key = BuildFeatureKey(pChr, pEnemy);
	m_LastBotKey = key;
	m_HaveLastBotKey = true;
	auto it = m_Table.find(key);
	CNetObj_PlayerInput input{};

	if(pEnemy)
	{
		vec2 d = pEnemy->m_Pos - pChr->m_Pos;
		if(fabs(d.x) < 1.f)
			d.x = (d.x >= 0) ? 1.f : -1.f;
		input.m_TargetX = (int)d.x;
		input.m_TargetY = (int)d.y;
	}
	else
	{
		input.m_TargetX = 0;
		input.m_TargetY = -1;
	}

	if(it != m_Table.end())
	{
		const auto &row = it->second;

		uint64_t wLeft = row.moveLeft * (1 + row.blockReward);
		uint64_t wRight = row.moveRight * (1 + row.blockReward);
		uint64_t wIdle = row.moveIdle * (1 + row.blockReward);
		int move = 0;
		uint64_t best = wIdle;
		if(wLeft > best)
		{
			move = -1;
			best = wLeft;
		}
		if(wRight > best)
		{
			move = 1;
		}
		input.m_Direction = move;
		input.m_Jump = row.jump >= row.noJump ? 1 : 0;
		input.m_Fire = row.fire >= row.noFire ? 1 : 0;
		input.m_Hook = row.hook >= row.noHook ? 1 : 0;

		if(m_BlockingMode)
		{
			float dx = m_BlockTarget.x - pChr->m_Pos.x;
			if(fabs(dx) > 8.0f)
				input.m_Direction = dx > 0 ? 1 : -1;
			else
				input.m_Direction = 0; // hold position
		}
		int sector = 0;
		uint32_t sbest = 0;
		for(int s = 0; s < 8; s++)
		{
			if(row.aimSector[s] > sbest)
			{
				sbest = row.aimSector[s];
				sector = s;
			}
		}
		float angle = (float)sector * (pi / 4.0f) + (pi / 8.0f);
		input.m_TargetX = (int)(cosf(angle) * 1000.0f);
		input.m_TargetY = (int)(sinf(angle) * 1000.0f);
	}
	else
	{
		if(pEnemy)
			input.m_Direction = (pEnemy->m_Pos.x > pChr->m_Pos.x) ? 1 : -1;
		input.m_Jump = (Server()->Tick() % 50 == 0) ? 1 : 0;
		input.m_Fire = (Server()->Tick() % 30 == 0) ? 1 : 0;
		input.m_Hook = 0;
	}

	input.m_PlayerFlags = 0;
	ApplyInput(pBot, input);
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
		if(!pO || pO->m_IsNpc)
			continue;
		CCharacter *pOC = pO->GetCharacter();
		if(!pOC)
			continue;
		float dist = distance(pOC->m_Pos, pBotChr->m_Pos);
		float curSpeed = length(pOC->Core()->m_Vel);
		float prev = m_LastEnemySpeed[i];
		if(dist < m_BlockDetectRadius && prev > 0.1f && curSpeed < prev * m_BlockSpeedDropFactor)
		{
			auto it = m_Table.find(m_LastBotKey);
			if(it != m_Table.end())
			{
				it->second.blockReward += (uint32_t)ceilf(m_RewardPerEvent);
				m_Dirty = true;
			}
			m_LastRewardTick = Server()->Tick();
			break;
		}
		m_LastEnemySpeed[i] = curSpeed;
	}

	m_BlockingMode = false;
	if(pBotChr && !m_Chokepoints.empty())
	{
		CCharacter *pTargetEnemy = nullptr;
		float bestScore = 0.f;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i == m_BotCid)
				continue;
			CPlayer *pO = GameServer()->m_apPlayers[i];
			if(!pO || pO->m_IsNpc)
				continue;
			CCharacter *pOC = pO->GetCharacter();
			if(!pOC)
				continue;
			float spd = length(pOC->Core()->m_Vel);
			if(spd > bestScore)
			{
				bestScore = spd;
				pTargetEnemy = pOC;
			}
		}
		if(pTargetEnemy)
		{
			vec2 spot = FindBlockingSpot(pTargetEnemy, pBotChr);
			if(length(spot) > 0.1f)
			{
				m_BlockTarget = spot;
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
			int leftSolid = pCol->GetTile(x - 1, y) == TILE_SOLID;
			int rightSolid = pCol->GetTile(x + 1, y) == TILE_SOLID;
			int centerEmpty = pCol->GetTile(x, y) != TILE_SOLID;
			if(leftSolid && rightSolid && centerEmpty)
			{
				vec2 pos = vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
				m_Chokepoints.push_back({pos});
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
	vec2 future = pEnemy->m_Pos + pEnemyMut->Core()->m_Vel * 10.0f; // simple projection
	float best = 1e9f;
	vec2 bestPos(0, 0);
	for(const auto &c : m_Chokepoints)
	{
		float dEnemy = distance(future, c.Pos);
		float dBot = distance(pBot->m_Pos, c.Pos);
		// choose chokepoint near enemy trajectory but not too far for bot (weighted sum)
		float score = dEnemy + dBot * 0.5f;
		if(score < best && PredictEnemyThroughChoke(pEnemy, c.Pos))
		{
			best = score;
			bestPos = c.Pos;
		}
	}
	return bestPos;
}

bool CAiBotComponent::PredictEnemyThroughChoke(const CCharacter *pEnemy, const vec2 &ChokePos) const
{
	if(!pEnemy)
		return false;
	vec2 rel = ChokePos - pEnemy->m_Pos;
	CCharacter *pEnemyMut = const_cast<CCharacter *>(pEnemy);
	vec2 v = pEnemyMut->Core()->m_Vel;
	if(length(v) < 1.0f)
		return false;
	float t = dot(rel, v) / (length(v) * length(v));
	if(t < 0 || t > 2.0f)
		return false; // only near-term
	vec2 proj = pEnemy->m_Pos + v * t;
	return distance(proj, ChokePos) < 96.0f;
}

void CAiBotComponent::ApplyInput(CPlayer *pBot, const CNetObj_PlayerInput &In)
{
	CNetObj_PlayerInput tmp = In;
	pBot->OnPredictedInput(&tmp);
	pBot->OnDirectInput(&tmp);
}

void CAiBotComponent::LoginBotAccount()
{
	CPlayer *pBot = GameServer()->m_apPlayers[m_BotCid];
	if(!pBot)
		return;

	if(pBot->IsLoggedIn())
		return;
	pBot->SetPlayerId(0);
	pBot->SetPlayerName(m_aAccountName);
	pBot->SetPlayerPassword(m_aAccountPassword);
	// real registration/login path would create DB entries; here we just mark logged-in state mimic by giving Id != 0.
	pBot->SetPlayerId(13371337);
	pBot->m_IsNpc = true;
}

void CAiBotComponent::LoadModel()
{
	IOHANDLE f = GameServer()->Storage()->OpenFile("ai_model.dat", IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!f)
		return;
	char magic[9] = {0};
	if(io_read(f, magic, 8) != 8)
	{
		io_close(f);
		return;
	}
	if(str_comp(magic, "AIBOTV2") != 0 && str_comp(magic, "AIBOTV3") != 0)
	{
		io_close(f);
		return;
	}
	int version = 0;
	io_read(f, &version, sizeof(version));
	if(version != 2 && version != 3)
	{
		io_close(f);
		return;
	}
	uint64_t count = 0;
	if(io_read(f, &count, sizeof(count)) != sizeof(count))
	{
		io_close(f);
		return;
	}
	for(uint64_t i = 0; i < count; i++)
	{
		uint64_t key;
		SActionStats stats{};
		if(io_read(f, &key, sizeof(key)) != sizeof(key))
			break;
		if(io_read(f, &stats, sizeof(stats)) != sizeof(stats))
			break;
		m_Table[key] = stats;
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
	const char *magic = "AIBOTV2";
	io_write(f, magic, 8);
	io_write(f, &m_ModelVersion, sizeof(m_ModelVersion));
	uint64_t count = m_Table.size();
	io_write(f, &count, sizeof(count));
	for(const auto &kv : m_Table)
	{
		io_write(f, &kv.first, sizeof(kv.first));
		io_write(f, &kv.second, sizeof(kv.second));
	}
	io_close(f);

	GameServer()->Storage()->RenameFile("ai_model.dat.tmp", "ai_model.dat", IStorage::TYPE_SAVE);
	m_Dirty = false;
	m_LastSaveTick = Server()->Tick();
}

int CAiBotComponent::ComputeAimSector(const vec2 &Delta) const
{
	float ang = atan2f(Delta.y, Delta.x);
	if(ang < 0)
		ang += 2.0f * pi;

	int sector = (int)floorf(ang / (pi / 4.0f));
	if(sector < 0)
		sector = 0;
	if(sector > 7)
		sector = 7;
	return sector;
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
