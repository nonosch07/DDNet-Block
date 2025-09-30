#ifndef BLOCKWORLDS_SHOP_NPCMANAGER_H
#define BLOCKWORLDS_SHOP_NPCMANAGER_H

#include "base/vmath.h"
#include <vector>
class CGameContext;

class CNpcManager
{
public:
	struct SNpc
	{
		int m_ClientID = -1;
		bool m_Toggled = false;
		bool m_Teleported = false;
		int m_ConnectionTick = -1;
	};

	CNpcManager();
	~CNpcManager();

	void Init(CGameContext *pGameServer);
	void Resize(size_t Num);

	int EnsureNpcAndApplySkinmani(int Index, const vec2 &PreviewPos, const char *pSkinName);

	void RemoveAll();

private:
	CGameContext *m_pGameServer;
	std::vector<SNpc> m_vNpcs;
};

#endif // BLOCKWORLDS_SHOP_NPCMANAGER_H
