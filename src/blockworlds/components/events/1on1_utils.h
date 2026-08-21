#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_UTILS_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_UTILS_H

#include <base/vmath.h>

#include <game/server/gamecontext.h>

#include <vector>

struct S1on1SpawnReservation
{
	int pos1Idx = -1;
	int pos2Idx = -1;
};

int GetTilePositions(int TileID, CGameContext *pSelf, std::vector<vec2> &result);

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_UTILS_H
