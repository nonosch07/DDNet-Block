#ifndef GAME_SERVER_BLOCKWORLDS_EVENTS_BASE_BASE_H
#define GAME_SERVER_BLOCKWORLDS_EVENTS_BASE_BASE_H
#include "game/server/save.h"

class CPlayer;
class CCharacter;
class CSaveTee;
class CGameContext;

// inspired by chillerdragon

class CEvent
{
private:
	int pGameType;

protected:
	CGameContext *m_pGameContext;
	CSaveTee *m_apSavedPositions[MAX_CLIENTS];
	bool m_aRestorePos[MAX_CLIENTS];
	bool destroy = false;
	;

public:
	CEvent(CGameContext *pGameContext, int pEventType);
	virtual ~CEvent() {}

	enum
	{
		EVENT_1on1 = 1,
		EVENT_LMB,
		EVENT_TDM,
		EVENT_INVITE
	};
	void CleanupEvent(); // stolen from chilerino
	int pGetGametype() { return pGameType; }
	virtual const char *getEventString() { return "event base"; };
	virtual bool isPublic() { return false; };

	// C1on1(CGameContext *pGameContext, int Player1ID, int Player2ID, int TileID);
	CGameContext *GameServer() { return m_pGameContext; }

	void Teleport(CPlayer *pPlayer1, CPlayer *pPlayer2, int TileID = 194);

	virtual bool Leave(CPlayer *pPlayer) { return false; };
	virtual void Destroy();
	virtual bool playersInclude(int pPlayerID) { return false; };
	virtual void OnTick(){};
	virtual void OnCharacterSpawn(class CCharacter *pVictim){};
	virtual void OnPlayerDisconnect(CPlayer *pPlayer, bool disconnect){};
	virtual void OnPlayerInit(CPlayer *pPlayer){};

	/*
			SavePosition

			Presist player position when joining an event
			to be later able to load it again
		*/
	virtual void SavePosition(CPlayer *pPlayer);
	/*
			LoadPosition

			Make sure SavePosition is called first.
			Use this to restore position after leaving an event.

			m_aRestorePos[ClientID] has to be set to true
		*/
	virtual void LoadPosition(CPlayer *pChr);
};

#endif
