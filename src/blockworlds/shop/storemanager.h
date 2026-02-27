#ifndef BLOCKWORLDS_SHOP_STOREMANAGER_H
#define BLOCKWORLDS_SHOP_STOREMANAGER_H

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
	int m_pLevel = 0;
	int m_pProduct = 0;
	int m_pCategory = 0;
	const char *m_pCosmeticName;

	bool SetProductInfo(int Category, int Cosmetics);

public:
	enum
	{
		CATEGORY_SKINMANI = 0,
		CATEGORY_GUNDESIGN,
		CATEGORY_KNOCKOUT,
		CATEGORY_UTILITY
	};

	CShop(CGameContext *pGameContext, CPlayer *pOwner, int pCategory, int pCosmetics, int ExpireInS = 15);
	void OnTick();
	void Expire();
	void Destroy(bool Silent = true);
	void Purchase();
	void Decline();

	// Instant purchase: validates and buys in one call, no /yes /no.
	// Returns true if the purchase succeeded.
	static bool InstantPurchase(CGameContext *pGameContext, CPlayer *pOwner, int Category, int Cosmetics);

	CGameContext *GameServer() { return m_pGameContext; }
	CCosmeticsHandler *CosmeticsHandler() { return m_pCosmeticsHandler; }
};

#endif // BLOCKWORLDS_SHOP_STOREMANAGER_H
