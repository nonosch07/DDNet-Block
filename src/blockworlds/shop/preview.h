#ifndef BLOCKWORLDS_SHOP_PREVIEW_H
#define BLOCKWORLDS_SHOP_PREVIEW_H

#include "base/vmath.h"
class CGameContext;
class CCosmeticsHandler;
class CPlayer;

class CShopPreview
{
private:
	CGameContext *m_pGameContext;
	CCosmeticsHandler *m_pCosmeticsHandler;
	CPlayer *m_pPlayer;

	int m_LastUpdateTime;
	void DisplayGundesign();
	void DisplayKnockouts();
	void DisplaySkinmanis();
	void DisplayCosmetics();

	void DisplaySkinManipulations();

	void DisplayPricesAndLevels();
	void DisplayPriceLevel(const vec2 &PreviewPos, int Price, int Level);

public:
	CShopPreview();

	CShopPreview(CGameContext *pGameContext);
	void Init(CGameContext *pGameServer);
	void Tick();

	CGameContext *GameServer() { return m_pGameContext; }
	CCosmeticsHandler *CosmeticsHandler() { return m_pCosmeticsHandler; }
};

#endif // BLOCKWORLDS_SHOP_PREVIEW_H
