#ifndef GAME_SERVER_BLOCKWORLDS_STORE_PREVIEW_H
#define GAME_SERVER_BLOCKWORLDS_STORE_PREVIEW_H

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

	void DisplayPricesAndLevels();

public:
	CShopPreview();

	CShopPreview(CGameContext *pGameContext);
	void Init(CGameContext *pGameServer);
	void Tick();

	CGameContext *GameServer() { return m_pGameContext; }
	CCosmeticsHandler *CosmeticsHandler() { return m_pCosmeticsHandler; }
};

#endif
