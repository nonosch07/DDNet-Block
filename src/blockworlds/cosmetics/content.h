#ifndef GAME_SERVER_BLOCKWORLDS_ANIMATIONS_CONTENT_H
#define GAME_SERVER_BLOCKWORLDS_ANIMATIONS_CONTENT_H

#include <engine/server.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "animations.h"

class CAnimLove : public CMapAnimation
{
	static const int NUM = 4;
	int m_IDs[NUM];
	vec2 m_Pos[NUM];
	bool m_Spawned[NUM];

public:
	CAnimLove(vec2 Pos, int64_t Tick, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(int i = 0; i < NUM; i++)
		{
			m_IDs[i] = Server()->SnapNewId();
			m_Spawned[i] = false;
		}

		m_Pos[0] = GetPos();
		m_Pos[1] = GetPos() + vec2(12.0f, 0);
		m_Pos[2] = GetPos() - vec2(12.0f, 0);
		m_Pos[3] = GetPos();
	}

	~CAnimLove()
	{
		for(int i = 0; i < NUM; i++)
			Server()->SnapFreeId(m_IDs[i]);
	}

	void Tick() override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 1.5f * (i / (float)NUM) + Server()->TickSpeed() <= Server()->Tick())
				m_Spawned[i] = false;
			else if(GetTick() + Server()->TickSpeed() * 1.5f * (i / (float)NUM) <= Server()->Tick() && m_Spawned[i] == false)
			{
				m_Spawned[i] = true;
				GameServer()->CreateSound(m_Pos[i], SOUND_PICKUP_HEALTH);
			}
		}

		for(int i = 0; i < NUM; i++)
		{
			if(m_Spawned[i] == false)
				continue;

			m_Pos[i] += vec2(0, -2.0f);
		}
	}

	void Snap(int SnappingClient) override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(m_Spawned[i] == false)
				continue;

			CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_IDs[i], sizeof(CNetObj_Pickup)));
			if(pP)
			{
				pP->m_X = (int)m_Pos[i].x;
				pP->m_Y = (int)m_Pos[i].y;
				pP->m_Type = POWERUP_HEALTH;
				pP->m_Subtype = 0;
			}
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
	int m_aIDs[MAX_PARTICLES];
	vec2 m_Pos;
	vec2 m_RotatePos[MAX_PARTICLES];
	bool m_Done;
	int m_Rad;

public:
	CSplash(vec2 Pos, int64_t Tick, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		m_Pos = Pos;

		m_Rad = 50.0f;
		m_Done = false;

		for(int i = 0; i < MAX_PARTICLES; i++)
			m_aIDs[i] = Server()->SnapNewId();
	}

	~CSplash()
	{
		for(int i = 0; i < MAX_PARTICLES; i++)
		{
			Server()->SnapFreeId(m_aIDs[i]);
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
			m_RotatePos[i].x = cosf(2 * pi * (i / (float)MAX_PARTICLES) + Server()->Tick() * TurnFac) * m_Rad;
			m_RotatePos[i].y = sinf(2 * pi * (i / (float)MAX_PARTICLES) + Server()->Tick() * TurnFac) * m_Rad;
		}
	}

	void Snap(int SnappingClient) override
	{
		CNetObj_Laser *pParticle[MAX_PARTICLES];
		for(int i = 0; i < MAX_PARTICLES; i++)
		{
			pParticle[i] = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_aIDs[i], sizeof(CNetObj_Laser)));
			if(pParticle[i])
			{
				pParticle[i]->m_X = m_Pos.x + m_RotatePos[i].x;
				pParticle[i]->m_Y = m_Pos.y + m_RotatePos[i].y;
				pParticle[i]->m_FromX = m_Pos.x + m_RotatePos[i].x;
				pParticle[i]->m_FromY = m_Pos.y + m_RotatePos[i].y;
				pParticle[i]->m_StartTick = Server()->Tick();
			}
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
	int m_IDs[NUM];
	vec2 m_Pos[NUM];
	bool m_Spawned[NUM];

public:
	CAnimThunderstorm(vec2 Pos, int64_t Tick, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(int i = 0; i < NUM; i++)
		{
			m_IDs[i] = Server()->SnapNewId();
			m_Spawned[i] = false;
		}
	}

	~CAnimThunderstorm()
	{
		for(int i = 0; i < NUM; i++)
			Server()->SnapFreeId(m_IDs[i]);
	}

	void Tick() override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 3.0f * (i / (float)NUM) <= Server()->Tick() && m_Spawned[i] == false)
			{
				m_Spawned[i] = true;
				float Dist = rand() % 128 - 64;
				vec2 Dir = normalize(vec2((rand() % 20 - 10) / 10.0f, (rand() % 20 - 10) / 10.0f));
				m_Pos[i] = GetPos() + Dir * Dist;

				GameServer()->CreateSound(m_Pos[i], SOUND_BODY_LAND);
			}
		}
	}

	void Snap(int SnappingClient) override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(m_Spawned[i] == false)
				break;

			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[i], sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			pObj->m_X = (int)m_Pos[i].x;
			pObj->m_Y = (int)m_Pos[i].y;
			pObj->m_FromX = (int)m_Pos[i].x;
			pObj->m_FromY = (int)m_Pos[i].y;
			pObj->m_StartTick = Server()->Tick() - 1;
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
	bool m_Spawned[NUM];
	vec2 m_Direction;

public:
	CStarsCW(vec2 Pos, int64_t Tick, vec2 Dir, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(int i = 0; i < NUM; i++)
			m_Spawned[i] = false;

		m_Direction = Dir;
	}

	void Tick() override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 0.2f * (i / (float)NUM) <= Server()->Tick() && m_Spawned[i] == false)
			{
				float AngleFrom = angle(m_Direction) + 5.1f;
				float AngleTo = AngleFrom + ((i - 5) / 5.0f) * pi * 0.3f;
				GameServer()->CreateDamageInd(GetPos(), AngleTo, 1);
				m_Spawned[i] = true;
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
	bool m_Spawned[NUM];
	vec2 m_Direction;

public:
	CStarsCCW(vec2 Pos, int64_t Tick, vec2 Dir, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(int i = 0; i < NUM; i++)
			m_Spawned[i] = false;

		m_Direction = Dir;
	}

	void Tick() override
	{
		for(int i = 0; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 0.2f * (i / (float)NUM) <= Server()->Tick() && m_Spawned[i] == false)
			{
				float AngleFrom = angle(m_Direction) + 5.1f;
				float AngleTo = AngleFrom + ((5 - i) / 5.0f) * pi * 0.3f;
				GameServer()->CreateDamageInd(GetPos(), AngleTo, 1);
				m_Spawned[i] = true;
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
	bool m_Spawned[NUM];
	vec2 m_Direction;

public:
	CStarsTOC(vec2 Pos, int64_t Tick, vec2 Dir, CGameWorld *pGameWorld) :
		CMapAnimation(Pos, Tick, pGameWorld)
	{
		for(int i = 0; i < NUM; i++)
			m_Spawned[i] = false;

		m_Direction = Dir;
	}

	void Tick() override
	{
		for(int i = 0; i < NUM / 2; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 0.2f * (i / (float)(NUM / 2.0f)) <= Server()->Tick() && m_Spawned[i] == false)
			{
				float AngleFrom = angle(m_Direction) + 5.0f;
				float AngleTo = AngleFrom + ((i - 5) / 5.0f) * pi * 0.3f;
				GameServer()->CreateDamageInd(GetPos(), AngleTo, 1);
				m_Spawned[i] = true;
			}
		}

		for(int i = NUM / 2; i < NUM; i++)
		{
			if(GetTick() + Server()->TickSpeed() * 0.2f * ((i - NUM / 2) / (float)(NUM / 2.0f)) <= Server()->Tick() && m_Spawned[i] == false)
			{
				float AngleFrom = angle(m_Direction) + 5.0f;
				float AngleTo = AngleFrom + ((5 - (i - NUM / 2)) / 5.0f) * pi * 0.3f;
				GameServer()->CreateDamageInd(GetPos(), AngleTo, 1);
				m_Spawned[i] = true;
			}
		}
	}

	void Snap(int SnappingClient) override {}

	bool Done() override
	{
		return GetTick() + Server()->TickSpeed() * 0.2f < Server()->Tick();
	}
};

#endif
