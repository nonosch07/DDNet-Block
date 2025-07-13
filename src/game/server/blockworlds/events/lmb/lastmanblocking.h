#ifndef GAME_SERVER_BLOCKWORLDS_EVENTS_LASTMANBLOCKING_H
#define GAME_SERVER_BLOCKWORLDS_EVENTS_LASTMANBLOCKING_H
#include "game/server/blockworlds/events/base/event_base.h"
#include "game/server/entity.h"
#include "game/server/gamecontext.h"
#include "game/server/player.h"
class CPlayer;
class CCharacter;
class CSaveTee;
class CLastManBlocking : public CEvent
{
	// private:
	// CGameContext *m_pGameContext;
public:
	using CEvent::CEvent;
	CLastManBlocking(CGameContext *pGameContext);
	CGameContext *GameServer() { return m_pGameContext; }
	int m_Player1ID; // the ID of the first player
	int m_Player2ID; // the ID of the second player
	int m_TileID; // the ID of the tile where the 1v1 starts
	bool m_Started;
	int m_StartTimer; // in seconds
	int m_StartTick;
	std::vector<CPlayer *> pPlayers;

	int m_Team; // the score of the second player
	void Start();
	bool Leave(CPlayer *pPlayer, bool disqualify = false);
	bool Leave(CPlayer *pPlayer) override;
	void Join(CPlayer *pPlayer, bool Silent = false);
	void EndTournament(CPlayer *pWinner);

	void Teleport(CPlayer *pPlayer1, vec2 pPos);
	void OnTick() override;
	void OnCharacterSpawn(class CCharacter *pVictim) override;
	void OnPlayerDisconnect(CPlayer *pPlayer, bool disconnect) override;
	bool playersInclude(int pPlayerID) override;
	bool isPublic() override { return true; };
	const char *getEventString() override { return "LMB"; };
};

#endif
