#ifndef GAME_SERVER_BLOCKWORLDS_EVENTS_TDM_TEAMDEATHMATCH_H
#define GAME_SERVER_BLOCKWORLDS_EVENTS_TDM_TEAMDEATHMATCH_H

#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include "../base/event_base.h"
class CPlayer;
class CCharacter;
class CTeamDeathmatch : public CEvent
{
private:
	CGameContext *m_pGameContext;

public:
	CTeamDeathmatch(CGameContext *pGameContext, int TileID1 = 193, int TileID2 = 194);

	CGameContext *GameServer() { return m_pGameContext; }
	std::vector<int> m_Clan1IDs; // the IDs of the first clan
	std::vector<int> m_Clan2IDs; // the IDs of the second clan
	std::vector<int> *GetTeamVector(int i);
	std::vector<int> *GetTeamVectorByPlayer(int PlayerID);
	std::vector<CPlayer *> vIDtovPlayer(std::vector<int> pIDs);
	void doScore(int pTeam);
	int m_TileID1; // the ID of the tile where the 1v1 starts
	int m_TileID2; // the ID of the tile where the 1v1 starts
	int m_Score1; // the score of the first team
	int m_Score2; // the score of the second team
	int m_MaxRounds;
	char *m_TeamName1;
	char *m_TeamName2;
	std::vector<int64_t> m_FrozenSince1;
	std::vector<int64_t> m_FrozenSince2;
	int64_t m_StartTimer;
	bool m_Started;
	int m_Team; // the score of the second player
	bool m_Unfrozen;
	int64_t leftFrozenSince;
	int64_t rightFrozenSince;
	bool m_isPublic = false;
	int m_CurrentTick = GameServer()->Server()->Tick();

	void AddLeftTeam(int PlayerID, bool Silent = false);
	void AddRightTeam(int PlayerID, bool Silent = false);
	void Start(std::vector<int> Player1ID, std::vector<int> Player2ID, bool firstStart, int TileID1 = 193, int TileID2 = 194);
	void Teleport(std::vector<CPlayer *> pPlayer1, std::vector<CPlayer *> pPlayer2, bool savePos, int TileIDLeft = 193, int TileIDRight = 194);
	void teleportTeamLoop(std::vector<CPlayer *> pPlayer1, std::vector<vec2> tilePositions, int foundIndex, int item, int savePos);
	void ChatBroadcastRightTeam(const char *pText);
	void ChatBroadcastLeftTeam(const char *pText);
	void ChatBroadcast(const char *pText);
	void BroadcastLeftTeam(const char *pText);
	void BroadcastRightTeam(const char *pText);
	void Broadcast(const char *pText);
	void allowDeath(bool allow);
	void StopTDM();
	void Restart();
	int GetTeam(int pID);
	int handleSpectateTDM(CPlayer *pPlayer);
	void updateScores();
	void updateScore(int pTeamID);
	void ResetPlayer(int pCurrent);

	bool Leave(CPlayer *pPlayer) override;
	void OnTick() override;
	void OnCharacterSpawn(class CCharacter *pVictim) override;
	void OnPlayerDisconnect(CPlayer *pPlayer, bool disconnect) override;
	bool playersInclude(int pPlayerID) override;
	bool isPublic() override { return m_isPublic; };
	const char *getMinigameString() { return "TDM"; };
};

#endif
