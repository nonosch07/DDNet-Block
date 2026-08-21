#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_UTILS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_UTILS_H

#include <base/vmath.h>

#include <game/server/gamecontext.h>

#include <vector>

struct S1on1SpawnReservation
{
	int m_Pos1Idx = -1;
	int m_Pos2Idx = -1;
};

int GetTilePositions(int TileID, CGameContext *pSelf, std::vector<vec2> &Result);

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_UTILS_H
