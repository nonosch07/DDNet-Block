#ifndef GAME_SERVER_BLOCKWORLDS_PURCHASE_H
#define GAME_SERVER_BLOCKWORLDS_PURCHASE_H

class CGameContext;
class CPlayer;
class CCosmeticsHandler;

class CShop
{
private:
	CGameContext *m_pGameContext;
	CPlayer *m_pOwner;
	CCosmeticsHandler *m_pCosmeticsHandler;
	int m_pExpireTick = 0;
	int m_pPrice = 0;
	int m_pProduct = 0;
	int m_pCategory = 0;
	const char *m_pCosmeticName;

	bool SetProductInfo(int Category, int Cosmetics);

public:
	enum
	{
		CATEGORY_SKINMANI = 0,
		CATEGORY_GUNDESIGN,
		CATEGORY_KNOCKOUT
	};

	CShop(CGameContext *pGameContext, CPlayer *pOwner, int pCategory, int pCosmetics, int ExpireInS = 15);
	void OnTick();
	void Expire();
	void Destroy(bool Silent = true);
	void Purchase();
	void Decline();

	CGameContext *GameServer() { return m_pGameContext; }
	CCosmeticsHandler *CosmeticsHandler() { return m_pCosmeticsHandler; }
};

#endif
