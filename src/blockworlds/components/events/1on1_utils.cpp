#include "1on1_utils.h"

int GetTilePositions(int TileID, CGameContext *pSelf, std::vector<vec2> &result)
{
	if(TileID < 0 || TileID > 255)
		return 0;
	int Length = pSelf->Collision()->GetWidth() * pSelf->Collision()->GetHeight();
	int foundIndex = 0;
	for(int i = 0; i < Length; i++)
	{
		if(pSelf->Collision()->GetTileIndex(i) == TileID)
		{
			int X = pSelf->Collision()->GetPos(i).x;
			int Y = pSelf->Collision()->GetPos(i).y;
			result.push_back(vec2(X, Y));
			foundIndex++;
		}
	}
	return foundIndex;
}
