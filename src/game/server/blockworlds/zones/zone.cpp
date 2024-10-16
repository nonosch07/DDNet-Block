#include <algorithm>

#include <engine/map.h>

#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include "zone.h"

// can be moved to base/vmath.h
template<typename T>
constexpr inline bool point_in_polygon(const vector2_base<T> *points, int num_points, vector2_base<T> target)
{
	// https://wfranklin.org/Research/Short_Notes/pnpoly.html

	bool inside = false;
	for(int i = 0, j = num_points - 1; i < num_points; j = i++)
	{
		if((points[i].y > target.y) != (points[j].y > target.y))
			if(target.x < (points[j].x - points[i].x) * (target.y - points[i].y) / (points[j].y - points[i].y) + points[i].x)
				inside = !inside;
	}
	return inside;
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
