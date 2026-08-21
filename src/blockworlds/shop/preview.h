#ifndef BLOCKWORLDS_SHOP_PREVIEW_H
#define BLOCKWORLDS_SHOP_PREVIEW_H

#include "base/vmath.h"
#include <vector>
class CGameContext;
class CCosmeticsHandler;
class CPlayer;
class CPickup;

class CShopPreview
{
private:
	CGameContext *m_pGameContext;
	CCosmeticsHandler *m_pCosmeticsHandler;
	CPlayer *m_pPlayer;
	class CNpcManager *m_pNpcManager;

	// timers for different refresh rates
	int m_LastPriceToggle; // 3s — toggles BP/LVL text
	int m_LastSlowEffects; // 3s — persistent/long animations (love, thunderstorm, splash, skin manis)
	int m_LastMediumEffects; // 1s — medium-duration effects (laserwrite knockouts, PEW)
	int m_LastFastEffects; // 0.25s — very short burst effects (star gundesigns, damage-ind gundesigns)
	bool m_ShowPrice;
	std::vector<CPickup *> m_vPickups;

	void DisplayPickupGundesigns();
	void DisplaySlowKnockouts();
	void DisplayMediumKnockouts();
	void DisplayFastKnockouts();
	void DisplayFastGundesigns();
	void DisplaySkinManipulations();

	void DisplayPricesAndLevels();
	void DisplayPriceLevel(const vec2 &PreviewPos, int Price, int Level);

public:
	CShopPreview();

	~CShopPreview();

	CShopPreview(CGameContext *pGameContext);
	void Init(CGameContext *pGameServer);
	void Tick();

	CGameContext *GameServer() { return m_pGameContext; }
	CCosmeticsHandler *CosmeticsHandler() { return m_pCosmeticsHandler; }
	CNpcManager *NpcManager() { return m_pNpcManager; }
};

#endif // BLOCKWORLDS_SHOP_PREVIEW_H
