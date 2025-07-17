#ifndef BLOCKWORLDS_EVENTS_1ON1_1ON1__H
#define BLOCKWORLDS_EVENTS_1ON1_1ON1__H
#include "blockworlds/events/base/event_base.h"
#include "game/server/entity.h"
#include "game/server/gamecontext.h"
#include "game/server/player.h"
#include "game/server/teams.h"
class CPlayer;
class CCharacter;
class CSaveTee;
class C1on1 : public CEvent
{
public:
	using CEvent::CEvent;

	C1on1(CGameContext *pGameContext, int Player1ID, int Player2ID, int Wager = 0);

	CGameTeams *m_pTeams;
	CGameContext *GameServer() { return m_pGameContext; }

	void Start1v1(int Player1ID, int Player2ID);
	void End1vs1(int PlayerID, bool aborted = false);
	bool Leave(CPlayer *pPlayer) override;

	void ResetPlayer(CPlayer *pPlayer, C1on1 *pCurrent);
	bool playersInclude(int pPlayerID) override;

	void Teleport(CPlayer *pPlayer1, CPlayer *pPlayer2);
	void Teleport(CCharacter *pChr, vec2 Pos);

	void OnTick() override;
	void OnCharacterSpawn(class CCharacter *pVictim) override;

private:
	// player and stats
	int m_Player1ID;
	int m_Player2ID;
	int m_Score1;
	int m_Score2;
	int m_Wager;
	int m_Team;
	// timers
	int64_t m_FrozenSince1, m_FrozenSince2;
	int m_TileFreezeSince1, m_TileFreezeSince2;
	int64_t m_StartTimer;

	// states
	CSaveTee *m_oldChar1;
	CSaveTee *m_oldChar2;

	bool m_Unfrozen;

	int m_CurrentTick = GameServer()->Server()->Tick();
};

#endif // BLOCKWORLDS_EVENTS_1ON1_1ON1__H
