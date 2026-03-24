#include "movingeffectzone.h"

#include <base/math.h>
#include <engine/map.h>
#include <engine/shared/map.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>

// reuse point_in_polygon from zone.cpp
template<typename T>
static constexpr inline bool point_in_polygon_local(const vector2_base<T> *points, int num_points, vector2_base<T> target)
{
	bool inside = false;
	for(int i = 0, j = num_points - 1; i < num_points; j = i++)
	{
		if((points[i].y > target.y) != (points[j].y > target.y))
			if(target.x < (points[j].x - points[i].x) * (target.y - points[i].y) / (points[j].y - points[i].y) + points[i].x)
				inside = !inside;
	}
	return inside;
}

CMovingEffectZone::CMovingEffectZone(CGameContext *pGameServer, int Effect) :
	IZone(pGameServer, -1), m_Effect(Effect)
{
	mem_zero(m_aWasInZone, sizeof(m_aWasInZone));
	m_pEnvPoints = nullptr;
	m_NumEnvPoints = 0;
}

void CMovingEffectZone::InitMoving(CMapItemLayerQuads *pQuadsLayer)
{
	IMap *pMap = GameServer()->Layers()->Map();

	// load envelope metadata
	int EnvStart, EnvNum;
	pMap->GetType(MAPITEMTYPE_ENVELOPE, &EnvStart, &EnvNum);
	m_vEnvelopes.resize(EnvNum);
	for(int i = 0; i < EnvNum; i++)
	{
		auto *pEnv = static_cast<CMapItemEnvelope *>(pMap->GetItem(EnvStart + i));
		m_vEnvelopes[i].m_StartPoint = pEnv->m_StartPoint;
		m_vEnvelopes[i].m_NumPoints = pEnv->m_NumPoints;
		m_vEnvelopes[i].m_Channels = pEnv->m_Channels;
	}

	// load envelope points
	int PointsStart, PointsNum;
	pMap->GetType(MAPITEMTYPE_ENVPOINTS, &PointsStart, &PointsNum);
	if(PointsNum > 0)
	{
		int PointsSize = pMap->GetItemSize(PointsStart);
		m_NumEnvPoints = PointsSize / (int)sizeof(CEnvPoint);
		m_pEnvPoints = static_cast<CEnvPoint *>(pMap->GetItem(PointsStart));
	}

	// load quads with envelope info
	auto *pQuads = static_cast<CQuad *>(pMap->GetData(pQuadsLayer->m_Data));
	for(int q = 0; q < pQuadsLayer->m_NumQuads; q++)
	{
		CQuad &Q = pQuads[q];
		CMovingQuad mq;

		for(int i = 0; i < 4; i++)
			mq.m_aBasePoints[i] = vec2(fx2f(Q.m_aPoints[i].x), fx2f(Q.m_aPoints[i].y));

		// Z-shaped vertices to match polygon winding
		std::swap(mq.m_aBasePoints[2], mq.m_aBasePoints[3]);

		mq.m_Pivot = vec2(fx2f(Q.m_aPoints[4].x), fx2f(Q.m_aPoints[4].y));
		mq.m_PosEnv = Q.m_PosEnv;
		mq.m_PosEnvOffset = Q.m_PosEnvOffset;

		m_vQuads.push_back(mq);
	}

	Enable();
}

void CMovingEffectZone::EvalPositionEnvelope(int EnvIndex, int OffsetMs, float &OutX, float &OutY) const
{
	OutX = 0.0f;
	OutY = 0.0f;

	if(EnvIndex < 0 || EnvIndex >= (int)m_vEnvelopes.size())
		return;

	const CEnvelopeData &Env = m_vEnvelopes[EnvIndex];
	if(Env.m_NumPoints <= 0 || !m_pEnvPoints)
		return;

	const CEnvPoint *pPoints = m_pEnvPoints + Env.m_StartPoint;
	int NumPoints = Env.m_NumPoints;

	// clamp to available points
	if(Env.m_StartPoint + NumPoints > m_NumEnvPoints)
		NumPoints = m_NumEnvPoints - Env.m_StartPoint;
	if(NumPoints <= 0)
		return;

	// current time in ms
	int64_t TickMs = (int64_t)GameServer()->Server()->Tick() * 1000 / GameServer()->Server()->TickSpeed();
	int64_t TimeMs = TickMs + OffsetMs;

	// single point: static offset
	if(NumPoints == 1)
	{
		OutX = fx2f(pPoints[0].m_aValues[0]);
		OutY = fx2f(pPoints[0].m_aValues[1]);
		return;
	}

	// wrap time within envelope duration
	int64_t MaxTime = pPoints[NumPoints - 1].m_Time;
	if(MaxTime > 0)
	{
		TimeMs = TimeMs % MaxTime;
		if(TimeMs < 0)
			TimeMs += MaxTime;
	}
	else
	{
		TimeMs = 0;
	}

	// find the two envelope points to interpolate between
	for(int i = 0; i < NumPoints - 1; i++)
	{
		if(TimeMs >= pPoints[i].m_Time && TimeMs < pPoints[i + 1].m_Time)
		{
			float Delta = (float)(pPoints[i + 1].m_Time - pPoints[i].m_Time);
			float a = Delta > 0.0f ? (float)(TimeMs - pPoints[i].m_Time) / Delta : 0.0f;

			switch(pPoints[i].m_Curvetype)
			{
			case CURVETYPE_STEP:
				a = 0.0f;
				break;
			case CURVETYPE_SLOW:
				a = a * a * a;
				break;
			case CURVETYPE_FAST:
				a = 1.0f - a;
				a = 1.0f - a * a * a;
				break;
			case CURVETYPE_SMOOTH:
				a = -2.0f * a * a * a + 3.0f * a * a;
				break;
			case CURVETYPE_LINEAR:
			default:
				break;
			}

			OutX = fx2f(pPoints[i].m_aValues[0]) + (fx2f(pPoints[i + 1].m_aValues[0]) - fx2f(pPoints[i].m_aValues[0])) * a;
			OutY = fx2f(pPoints[i].m_aValues[1]) + (fx2f(pPoints[i + 1].m_aValues[1]) - fx2f(pPoints[i].m_aValues[1])) * a;
			return;
		}
	}

	// past last point
	OutX = fx2f(pPoints[NumPoints - 1].m_aValues[0]);
	OutY = fx2f(pPoints[NumPoints - 1].m_aValues[1]);
}

bool CMovingEffectZone::IsPlayerInMovingZone(vec2 PlayerPos) const
{
	for(const auto &mq : m_vQuads)
	{
		float OffX = 0.0f, OffY = 0.0f;
		if(mq.m_PosEnv >= 0)
			EvalPositionEnvelope(mq.m_PosEnv, mq.m_PosEnvOffset, OffX, OffY);

		// translate quad corners by envelope offset (relative to pivot)
		std::array<vec2, 4> Translated;
		for(int i = 0; i < 4; i++)
			Translated[i] = mq.m_aBasePoints[i] + vec2(OffX, OffY);

		if(point_in_polygon_local(Translated.data(), 4, PlayerPos))
			return true;
	}
	return false;
}

void CMovingEffectZone::Tick()
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer)
		{
			m_aWasInZone[i] = false;
			continue;
		}

		CCharacter *pChar = pPlayer->GetCharacter();
		if(!pChar)
		{
			m_aWasInZone[i] = false;
			continue;
		}

		bool InZone = IsPlayerInMovingZone(pChar->m_Pos);

		switch(m_Effect)
		{
		case MOVINGEFFECT_FREEZE:
			if(InZone && !pChar->Core()->m_Super && !pChar->Core()->m_Invincible)
				pChar->Freeze();
			break;

		case MOVINGEFFECT_HOOK:
			if(InZone)
			{
				// pull player toward the nearest quad center
				vec2 Target = vec2(0, 0);
				float BestDist = -1.0f;
				for(const auto &mq : m_vQuads)
				{
					float OffX = 0.0f, OffY = 0.0f;
					if(mq.m_PosEnv >= 0)
						EvalPositionEnvelope(mq.m_PosEnv, mq.m_PosEnvOffset, OffX, OffY);

					vec2 Center = mq.m_Pivot + vec2(OffX, OffY);
					float Dist = distance(pChar->m_Pos, Center);
					if(BestDist < 0.0f || Dist < BestDist)
					{
						BestDist = Dist;
						Target = Center;
					}
				}

				// apply pull force toward quad center
				vec2 Dir = Target - pChar->m_Pos;
				float Len = length(Dir);
				if(Len > 1.0f)
				{
					Dir = normalize(Dir);
					float PullStrength = clamp(Len * 0.1f, 1.0f, 12.0f);
					pChar->Core()->m_Vel += Dir * PullStrength;
				}
			}
			break;

		case MOVINGEFFECT_UNHOOK:
			if(InZone && !pChar->Core()->m_Super && !pChar->Core()->m_Invincible)
				pChar->UnFreeze();
			break;
		}

		m_aWasInZone[i] = InZone;
	}
}
