#ifndef BLOCK_SHOP_NPCMANAGER_H
#define BLOCK_SHOP_NPCMANAGER_H

#include <base/vmath.h>

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
		vec2 m_PreviewPos = vec2(0, 0);
		float m_AimAngle = 0.0f;
		float m_AimTarget = 0.0f;
		int m_AimChangeTimer = 0;
	};

	CNpcManager();
	~CNpcManager();

	void Init(CGameContext *pGameServer);
	void Resize(size_t Num);
	void Tick();

	int EnsureNpcAndApplySkinmani(int Index, const vec2 &PreviewPos, const char *pSkinName);

	void RemoveAll();

	int GetNpcAimAngle(int ClientID) const;

private:
	CGameContext *m_pGameServer;
	std::vector<SNpc> m_vNpcs;
};

#endif // BLOCK_SHOP_NPCMANAGER_H
