#include "movingeffectzone.h"

#include <base/math.h>

#include <engine/map.h>
#include <engine/shared/map.h>

#include <game/server/entities/character.h>
#include <game/server/entities/projectile.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/bw_context.h>

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
	mem_zero(m_aHookableTrack, sizeof(m_aHookableTrack));
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

	// initialize prev offsets for platform delta computation
	for(auto &q : m_vQuads)
	{
		float ox = 0.0f, oy = 0.0f;
		if(q.m_PosEnv >= 0)
			EvalPositionEnvelope(q.m_PosEnv, q.m_PosEnvOffset, ox, oy);
		q.m_PrevOffset = vec2(ox, oy);
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
	// CEnvPoint::m_Time is a CFixedTime now; work in its internal milliseconds
	int64_t MaxTime = pPoints[NumPoints - 1].m_Time.GetInternal();
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
		if(TimeMs >= pPoints[i].m_Time.GetInternal() && TimeMs < pPoints[i + 1].m_Time.GetInternal())
		{
			float Delta = (float)(pPoints[i + 1].m_Time.GetInternal() - pPoints[i].m_Time.GetInternal());
			float a = Delta > 0.0f ? (float)(TimeMs - pPoints[i].m_Time.GetInternal()) / Delta : 0.0f;

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

// Push a point out of a convex polygon. Returns zero vector if point is outside.
static vec2 PushOutConvex(vec2 Point, const vec2 *pVertices, int NumVertices)
{
	// determine winding direction from cross product of first two edges
	vec2 E0 = pVertices[1] - pVertices[0];
	vec2 E1 = pVertices[2] - pVertices[1];
	float Cross = E0.x * E1.y - E0.y * E1.x;
	float WindSign = (Cross > 0.0f) ? 1.0f : -1.0f;

	float MinDepth = 1e18f;
	vec2 BestNormal(0, 0);

	for(int i = 0; i < NumVertices; i++)
	{
		vec2 A = pVertices[i];
		vec2 B = pVertices[(i + 1) % NumVertices];
		vec2 Edge = B - A;
		float EdgeLen = length(Edge);
		if(EdgeLen < 0.001f)
			continue;

		// outward normal (direction depends on winding)
		vec2 Normal = vec2(Edge.y, -Edge.x) * WindSign / EdgeLen;
		float Dist = dot(Point - A, Normal);

		if(Dist > 0.0f)
			return vec2(0, 0); // already outside

		float Depth = -Dist;
		if(Depth < MinDepth)
		{
			MinDepth = Depth;
			BestNormal = Normal;
		}
	}

	return BestNormal * (MinDepth + 1.0f); // +1 margin to prevent re-entry
}

bool CMovingEffectZone::IsPointInMovingQuad(vec2 Point, int QuadIndex) const
{
	const CMovingQuad &mq = m_vQuads[QuadIndex];
	float OffX = 0.0f, OffY = 0.0f;
	if(mq.m_PosEnv >= 0)
		EvalPositionEnvelope(mq.m_PosEnv, mq.m_PosEnvOffset, OffX, OffY);

	std::array<vec2, 4> Translated;
	for(int i = 0; i < 4; i++)
		Translated[i] = mq.m_aBasePoints[i] + vec2(OffX, OffY);

	return point_in_polygon_local(Translated.data(), 4, Point);
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
			m_aHookableTrack[i].m_Active = false;
			continue;
		}

		CCharacter *pChar = pPlayer->GetCharacter();
		if(!pChar)
		{
			m_aWasInZone[i] = false;
			m_aHookableTrack[i].m_Active = false;
			continue;
		}

		bool InZone = IsPlayerInMovingZone(pChar->m_Pos);

		switch(m_Effect)
		{
		case MOVINGEFFECT_FREEZE:
			if(InZone && !pChar->Core()->m_Super && !pChar->Core()->m_Invincible)
				pChar->Freeze();
			break;

		case MOVINGEFFECT_GRABME:
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
					float PullStrength = std::clamp(Len * 0.1f, 1.0f, 12.0f);
					pChar->Bw().Core().m_Vel += Dir * PullStrength;
				}
			}
			break;

		case MOVINGEFFECT_HOOKABLE:
		{
			CCharacterCore *pCore = &pChar->Bw().Core();

			// --- Solid collision & platform logic ---
			for(int qi = 0; qi < (int)m_vQuads.size(); qi++)
			{
				CMovingQuad &mq = m_vQuads[qi];
				float OffX = 0.0f, OffY = 0.0f;
				if(mq.m_PosEnv >= 0)
					EvalPositionEnvelope(mq.m_PosEnv, mq.m_PosEnvOffset, OffX, OffY);
				vec2 CurOffset(OffX, OffY);

				std::array<vec2, 4> Trans;
				for(int c = 0; c < 4; c++)
					Trans[c] = mq.m_aBasePoints[c] + CurOffset;

				bool CenterInside = point_in_polygon_local(Trans.data(), 4, pChar->m_Pos);

				if(CenterInside)
				{
					// Stop player at boundary like MoveBox: find nearest edge,
					// snap to it, and zero velocity on the blocked axis.
					vec2 Push = PushOutConvex(pChar->m_Pos, Trans.data(), 4);
					if(length(Push) > 0.01f)
					{
						pCore->m_Pos += Push;
						pChar->m_Pos = pCore->m_Pos;

						// Zero velocity on the blocked axis (like MoveBox with elasticity 0)
						if(std::abs(Push.x) > std::abs(Push.y))
						{
							// hit vertical wall → zero X velocity
							pCore->m_Vel.x = 0;
						}
						else
						{
							// hit horizontal surface → zero Y velocity
							pCore->m_Vel.y = 0;

							// pushed upward = landed on top
							if(Push.y < 0)
							{
								// ground friction (same as GroundFriction tuning = 0.5)
								if(pCore->m_Direction == 0)
									pCore->m_Vel.x *= pCore->m_Tuning.m_GroundFriction;

								// reset jumps like standing on solid ground
								pCore->m_Jumped &= ~2;
								pCore->m_JumpedTotal = 0;

								// ride with moving platform
								vec2 Delta = CurOffset - mq.m_PrevOffset;
								pCore->m_Pos += Delta;
								pChar->m_Pos = pCore->m_Pos;
							}
						}
					}
				}
				else
				{
					// feet check: standing on top without center penetration
					vec2 FeetCheck = pChar->m_Pos + vec2(0, 28.0f);
					if(point_in_polygon_local(Trans.data(), 4, FeetCheck))
					{
						// ground friction
						pCore->m_Vel.y = 0;
						if(pCore->m_Direction == 0)
							pCore->m_Vel.x *= pCore->m_Tuning.m_GroundFriction;

						// reset jumps
						pCore->m_Jumped &= ~2;
						pCore->m_JumpedTotal = 0;

						// ride with moving platform
						vec2 Delta = CurOffset - mq.m_PrevOffset;
						pCore->m_Pos += Delta;
						pChar->m_Pos = pCore->m_Pos;
					}
				}
			}

			// hook attachment: track hooks onto moving quads
			if(m_aHookableTrack[i].m_Active)
			{
				if(pCore->m_HookState != HOOK_GRABBED)
				{
					m_aHookableTrack[i].m_Active = false;
				}
				else
				{
					int qi = m_aHookableTrack[i].m_QuadIndex;
					if(qi >= 0 && qi < (int)m_vQuads.size())
					{
						const CMovingQuad &mq = m_vQuads[qi];
						float OffX = 0.0f, OffY = 0.0f;
						if(mq.m_PosEnv >= 0)
							EvalPositionEnvelope(mq.m_PosEnv, mq.m_PosEnvOffset, OffX, OffY);

						pCore->m_HookPos = m_aHookableTrack[i].m_HookBasePos + vec2(OffX, OffY);
					}
				}
			}

			if(!m_aHookableTrack[i].m_Active && pCore->m_HookState == HOOK_FLYING)
			{
				for(int qi = 0; qi < (int)m_vQuads.size(); qi++)
				{
					if(IsPointInMovingQuad(pCore->m_HookPos, qi))
					{
						const CMovingQuad &mq = m_vQuads[qi];
						float OffX = 0.0f, OffY = 0.0f;
						if(mq.m_PosEnv >= 0)
							EvalPositionEnvelope(mq.m_PosEnv, mq.m_PosEnvOffset, OffX, OffY);

						pCore->m_HookState = HOOK_GRABBED;
						pCore->m_TriggeredEvents |= COREEVENT_HOOK_ATTACH_GROUND;
						pCore->SetHookedPlayer(-1);

						m_aHookableTrack[i].m_Active = true;
						m_aHookableTrack[i].m_QuadIndex = qi;
						m_aHookableTrack[i].m_HookBasePos = pCore->m_HookPos - vec2(OffX, OffY);
						break;
					}
				}
			}
			break;
		}
		}

		m_aWasInZone[i] = InZone;
	}

	// projectile blocking
	if(m_Effect == MOVINGEFFECT_HOOKABLE)
	{
		for(CProjectile *pProj = (CProjectile *)GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE);
			pProj;
			pProj = (CProjectile *)pProj->TypeNext())
		{
			float Ct = (GameServer()->Server()->Tick() - pProj->StartTick()) / (float)GameServer()->Server()->TickSpeed();
			vec2 ProjPos = pProj->GetPos(Ct);

			for(int qi = 0; qi < (int)m_vQuads.size(); qi++)
			{
				if(IsPointInMovingQuad(ProjPos, qi))
				{
					GameServer()->Bw().CreateExplosionVisual(ProjPos);
					pProj->Reset();
					break;
				}
			}
		}

		// update prev offsets for next tick's delta computation
		for(auto &mq : m_vQuads)
		{
			float OffX = 0.0f, OffY = 0.0f;
			if(mq.m_PosEnv >= 0)
				EvalPositionEnvelope(mq.m_PosEnv, mq.m_PosEnvOffset, OffX, OffY);
			mq.m_PrevOffset = vec2(OffX, OffY);
		}
	}
}
