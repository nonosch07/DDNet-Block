#ifndef BLOCKWORLDS_EVENTS_1ON1_1ON1_INVITE_H
#define BLOCKWORLDS_EVENTS_1ON1_1ON1_INVITE_H

class CPlayer;
class CGameContext;
class CInvite
{
private:
	CGameContext *m_pGameContext;
	int m_ExpireTick;
	int m_Wager = 0;

public:
	CInvite(CGameContext *pGameContext, CPlayer *pInviteTo, CPlayer *pInviteFrom, int pEvents, int pExpireInS = 30, int pWager = 0);
	int m_Event;
	CPlayer *m_pInviteTo;
	CPlayer *m_pInviteFrom;
	CGameContext *GameServer() { return m_pGameContext; }
	void Accept();
	void Decline();
	void Expire();
	void Destroy();
	void OnTick();
};
#endif // BLOCKWORLDS_EVENTS_1ON1_1ON1_INVITE_H
