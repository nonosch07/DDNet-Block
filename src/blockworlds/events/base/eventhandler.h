#ifndef GAME_SERVER_BLOCKWORLDS_EVENTS_EVENTHANDLER_H
#define GAME_SERVER_BLOCKWORLDS_EVENTS_EVENTHANDLER_H

#include "event_base.h"
#include "game/server/gamecontext.h"

class CPlayer;
class CGameContext;

class BW_CEventHandler : public CEvent
{
private:
	int m_pExpireTick;
	char *m_pEventString;

public:
	BW_CEventHandler(CGameContext *pGameContext, CPlayer *pInviteFrom, int pEvent, CPlayer *(*pInvited)[MAX_CLIENTS] = nullptr);
	int m_pEvent;
	CPlayer *m_pInviteFrom;
	CPlayer **m_pInvited[MAX_CLIENTS];
	std::vector<CPlayer *> m_pJoined = std::vector<CPlayer *>();
	CGameContext *GameServer() { return m_pGameContext; }
	int m_CurrentTick = GameServer()->Server()->Tick();
	void Expire();
	bool playersInclude(int pPlayerID) override;
	void Accept(CPlayer *pAccepter);
	void OnTick() override;
	void StartEvent();
	bool isPublic() override { return true; };
	const char *getEventString() override { return m_pEventString; };
	bool Leave(CPlayer *pPlayer) override;
	void OnPlayerDisconnect(CPlayer *pPlayer, bool disconnect) override;
};
#endif
