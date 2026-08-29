#include "zone.h"

#include <engine/map.h>

#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include <algorithm>

// can be moved to base/vmath.h
template<typename T>
static constexpr bool point_in_polygon(const vector2_base<T> *Points, int NumPoints, vector2_base<T> Target)
{
	// https://wfranklin.org/Research/Short_Notes/pnpoly.html

	bool Inside = false;
	for(int i = 0, j = NumPoints - 1; i < NumPoints; j = i++)
	{
		if((Points[i].y > Target.y) != (Points[j].y > Target.y))
			if(Target.x < (Points[j].x - Points[i].x) * (Target.y - Points[i].y) / (Points[j].y - Points[i].y) + Points[i].x)
				Inside = !Inside;
	}
	return Inside;
}

IZone::IZone(CGameContext *pGameServer, int Type)
{
	m_pGameServer = pGameServer;
	m_ZoneType = Type;
	m_IsEnabled = false;
}

void IZone::Init(CMapItemLayerQuads *pQuadsLayer)
{
	auto *pQuads = static_cast<CQuad *>(GameServer()->Layers()->Map()->GetData(pQuadsLayer->m_Data));

	for(CQuad *pQuad = pQuads; pQuad != pQuads + pQuadsLayer->m_NumQuads; pQuad++)
	{
		// fixed to float
		std::array<vec2, 4> Points;

		std::transform(pQuad->m_aPoints,
			pQuad->m_aPoints + 4, // only 4 (no envelopes support, this can be added though)
			Points.begin(),
			[](ivec2 Point) { return vec2{fx2f(Point.x), fx2f(Point.y)}; });

		// Z-shaped vertices
		std::swap(Points[2], Points[3]);

		m_apQuads.push_back(Points);
	}
	m_IsEnabled = true;
}

bool IZone::IsInZone(vec2 Target) const
{
	if(m_apQuads.empty())
		return false;
	for(auto pQuad : m_apQuads)
		if(point_in_polygon(pQuad.data(), 4, Target))
			return true;
	return false;
}

std::vector<vec2> IZone::GetCenters() const
{
	std::vector<vec2> Result;
	Result.reserve(m_apQuads.size());
	for(const auto &q : m_apQuads)
	{
		vec2 c{0.0f, 0.0f};
		for(int i = 0; i < 4; i++)
			c += q[i];
		c /= 4.0f;
		Result.push_back(c);
	}
	return Result;
}
