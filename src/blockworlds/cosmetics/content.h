#ifndef BLOCKWORLDS_COSMETICS_CONTENT_H
#define BLOCKWORLDS_COSMETICS_CONTENT_H

#include "animations.h"

#include <engine/server.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/bw_context.h>

class CAnimLove : public CMapAnimation
{
	static const int NUM = 4;
	int m_aIds[NUM];
	vec2 m_aPos[NUM];
	bool m_aSpawned[NUM];

public:
	CAnimLove(vec2 Pos, int64_t Tick, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(int i = 0; i < NUM; i++)
		{
			m_aIds[i] = Server()->SnapNewId().value_or(-1);
			m_aSpawned[i] = false;
		}

		m_aPos[0] = GetPos();
		m_aPos[1] = GetPos() + vec2(12.0f, 0);
		m_aPos[2] = GetPos() - vec2(12.0f, 0);
		m_aPos[3] = GetPos();
	}

	~CAnimLove() override
	{
		for(int Id : m_aIds)
			Server()->SnapFreeId(Id);
	}

	void Tick() override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 1.5f * (i / (float)NUM) + Server()->TickSpeed() <= Server()->Tick())
				m_aSpawned[i] = false;
			else if(GetTick() + Server()->TickSpeed() * 1.5f * (i / (float)NUM) <= Server()->Tick() && m_aSpawned[i] == false)
			{
				m_aSpawned[i] = true;
				GameServer()->CreateSound(m_aPos[i], SOUND_PICKUP_HEALTH);
			}
		}

		for(int i = 0; i < NUM; i++)
		{
			if(m_aSpawned[i] == false)
				continue;

			m_aPos[i] += vec2(0, -2.0f);
		}
	}

	void Snap(int SnappingClient) override
	{
		int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
		bool Sixup = Server()->IsSixup(SnappingClient);
		for(int i = 0; i < NUM; i++)
		{
			if(m_aSpawned[i] == false)
				continue;

			GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Sixup, SnappingClient), m_aIds[i], m_aPos[i], POWERUP_HEALTH, 0, 0, PICKUPFLAG_NO_PREDICT);
		}
	}

	bool Done() override
	{
		return GetTick() + Server()->TickSpeed() * 3.0f < Server()->Tick();
	}
};

class CSplash : public CMapAnimation
{
	static const int MAX_PARTICLES = 6;
	int m_aIds[MAX_PARTICLES];
	vec2 m_Pos;
	vec2 m_aRotatePos[MAX_PARTICLES];
	bool m_Done;
	int m_Rad;

public:
	CSplash(vec2 Pos, int64_t Tick, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		m_Pos = Pos;

		m_Rad = 50.0f;
		m_Done = false;

		for(int &Id : m_aIds)
			Id = Server()->SnapNewId().value_or(-1);
	}

	~CSplash() override
	{
		for(int Id : m_aIds)
		{
			Server()->SnapFreeId(Id);
		}
	}

	void Tick() override
	{
		m_Rad--;

		if(m_Rad <= 5)
			m_Done = true;

		for(int i = 0; i < MAX_PARTICLES; i++)
		{
			float TurnFac = 0.070f;
			m_aRotatePos[i].x = cosf(2 * pi * (i / (float)MAX_PARTICLES) + Server()->Tick() * TurnFac) * m_Rad;
			m_aRotatePos[i].y = sinf(2 * pi * (i / (float)MAX_PARTICLES) + Server()->Tick() * TurnFac) * m_Rad;
		}
	}

	void Snap(int SnappingClient) override
	{
		for(int i = 0; i < MAX_PARTICLES; i++)
		{
			const int SnapVer = Server()->GetClientVersion(SnappingClient);
			const bool SixUp = Server()->IsSixup(SnappingClient);
			vec2 Pos = m_Pos + m_aRotatePos[i];

			GameServer()->Bw().SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_aIds[i], Pos, Pos, Server()->Tick(), -1, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
		}
	}

	bool Done() override
	{
		return m_Done;
	}
};

class CAnimThunderstorm : public CMapAnimation
{
	static const int NUM = 25;
	int m_aIds[NUM];
	vec2 m_aPos[NUM];
	bool m_aSpawned[NUM];

public:
	CAnimThunderstorm(vec2 Pos, int64_t Tick, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(int i = 0; i < NUM; i++)
		{
			m_aIds[i] = Server()->SnapNewId().value_or(-1);
			m_aSpawned[i] = false;
		}
	}

	~CAnimThunderstorm() override
	{
		for(int Id : m_aIds)
			Server()->SnapFreeId(Id);
	}

	void Tick() override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 3.0f * (i / (float)NUM) <= Server()->Tick() && m_aSpawned[i] == false)
			{
				m_aSpawned[i] = true;
				float Dist = rand() % 128 - 64;
				vec2 Dir = normalize(vec2((rand() % 20 - 10) / 10.0f, (rand() % 20 - 10) / 10.0f));
				m_aPos[i] = GetPos() + Dir * Dist;

				GameServer()->CreateSound(m_aPos[i], SOUND_BODY_LAND);
			}
		}
	}

	void Snap(int SnappingClient) override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(m_aSpawned[i] == false)
				break;

			const int SnapVer = Server()->GetClientVersion(SnappingClient);
			const bool SixUp = Server()->IsSixup(SnappingClient);
			vec2 Pos = m_aPos[i];

			GameServer()->Bw().SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_aIds[i], Pos, Pos, Server()->Tick() - 1, -1, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
		}
	}

	bool Done() override
	{
		return GetTick() + Server()->TickSpeed() * 3.0f < Server()->Tick();
	}
};

class CStarsCW : public CMapAnimation
{
	static const int NUM = 10;
	bool m_aSpawned[NUM];
	vec2 m_Direction;

public:
	CStarsCW(vec2 Pos, int64_t Tick, vec2 Dir, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(bool &i : m_aSpawned)
			i = false;

		m_Direction = Dir;
	}

	void Tick() override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 0.2f * (i / (float)NUM) <= Server()->Tick() && m_aSpawned[i] == false)
			{
				float AngleFrom = angle(m_Direction) + 5.1f;
				float AngleTo = AngleFrom + ((i - 5) / 5.0f) * pi * 0.3f;
				GameServer()->CreateDamageInd(GetPos(), AngleTo, 1);
				m_aSpawned[i] = true;
			}
		}
	}

	void Snap(int SnappingClient) override {}

	bool Done() override
	{
		return GetTick() + Server()->TickSpeed() * 0.2f < Server()->Tick();
	}
};

class CStarsCCW : public CMapAnimation
{
	static const int NUM = 10;
	bool m_aSpawned[NUM];
	vec2 m_Direction;

public:
	CStarsCCW(vec2 Pos, int64_t Tick, vec2 Dir, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(bool &i : m_aSpawned)
			i = false;

		m_Direction = Dir;
	}

	void Tick() override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 0.2f * (i / (float)NUM) <= Server()->Tick() && m_aSpawned[i] == false)
			{
				float AngleFrom = angle(m_Direction) + 5.1f;
				float AngleTo = AngleFrom + ((5 - i) / 5.0f) * pi * 0.3f;
				GameServer()->CreateDamageInd(GetPos(), AngleTo, 1);
				m_aSpawned[i] = true;
			}
		}
	}

	void Snap(int SnappingClient) override {}

	bool Done() override
	{
		return GetTick() + Server()->TickSpeed() * 0.2f < Server()->Tick();
	}
};

class CStarsTOC : public CMapAnimation
{
	static const int NUM = 10;
	bool m_aSpawned[NUM];
	vec2 m_Direction;

public:
	CStarsTOC(vec2 Pos, int64_t Tick, vec2 Dir, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(bool &i : m_aSpawned)
			i = false;

		m_Direction = Dir;
	}

	void Tick() override
	{
		for(int i = 0; i < NUM / 2; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 0.2f * (i / (float)(NUM / 2.0f)) <= Server()->Tick() && m_aSpawned[i] == false)
			{
				float AngleFrom = angle(m_Direction) + 5.0f;
				float AngleTo = AngleFrom + ((i - 5) / 5.0f) * pi * 0.3f;
				GameServer()->CreateDamageInd(GetPos(), AngleTo, 1);
				m_aSpawned[i] = true;
			}
		}

		for(int i = NUM / 2; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 0.2f * ((i - NUM / 2) / (float)(NUM / 2.0f)) <= Server()->Tick() && m_aSpawned[i] == false)
			{
				float AngleFrom = angle(m_Direction) + 5.0f;
				float AngleTo = AngleFrom + ((5 - (i - NUM / 2)) / 5.0f) * pi * 0.3f;
				GameServer()->CreateDamageInd(GetPos(), AngleTo, 1);
				m_aSpawned[i] = true;
			}
		}
	}

	void Snap(int SnappingClient) override {}

	bool Done() override
	{
		return GetTick() + Server()->TickSpeed() * 0.2f < Server()->Tick();
	}
};

#endif // BLOCKWORLDS_COSMETICS_CONTENT_H
