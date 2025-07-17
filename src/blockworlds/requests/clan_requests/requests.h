#ifndef BLOCKWORLDS_REQUESTS_CLAN_REQUESTS_REQUESTS_H
#define BLOCKWORLDS_REQUESTS_CLAN_REQUESTS_REQUESTS_H

class CGameContext;
class CPlayer;

class CClanRequests
{
private:
	CGameContext *m_pGameContext;
	CPlayer *m_pClanSeeker;
	CPlayer *m_pClanOwner;
	int m_pExpireTick = 0;
	int m_pClanId = 0;

public:
	CClanRequests(CGameContext *pGameContext, CPlayer *pClanSeeker, CPlayer *pClanOwner, int pClanId, int ExpireInS);
	void OnTick();
	void Expire();
	void Destroy(bool Silent = true);
	void Accept();
	void Decline();

	CGameContext *GameServer() { return m_pGameContext; }
};

#endif // BLOCKWORLDS_REQUESTS_CLAN_REQUESTS_REQUESTS_H
