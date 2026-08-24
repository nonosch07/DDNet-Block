#ifndef BLOCKWORLDS_BW_GAMECONTROLLER_H
#define BLOCKWORLDS_BW_GAMECONTROLLER_H

#include <game/server/gamemodes/ddnet.h>

// The Blockworlds gamemode.
//
// BW used to get here by editing CGameControllerDDRace and IGameController in
// place. It is a subclass now, so upstream's controllers stay untouched and
// CGameContext only has to pick this one instead of the DDNet controller.
class CGameControllerBW : public CGameControllerDDNet
{
public:
	explicit CGameControllerBW(class CGameContext *pGameServer);

	void OnPlayerConnect(class CPlayer *pPlayer) override;
	void OnPlayerDisconnect(class CPlayer *pPlayer, const char *pReason) override;
	void SendLeaveMessage(class CPlayer *pPlayer, const char *pReason) override;
	void OnCharacterSpawn(class CCharacter *pChr) override;
	void Tick() override;
	int SnapPlayerScore(int SnappingClient, class CPlayer *pPlayer) override;
	// ddnet sux
	// read IGameController::SnapPlayerTime
	CFinishTime SnapPlayerTime(int SnappingClient, class CPlayer *pPlayer) override { return CFinishTime::Unset(); }
	CFinishTime SnapMapBestTime(int SnappingClient) override { return CFinishTime::Unset(); }
	void OnSnapGameInfo(int SnappingClient, CNetObj_GameInfo *pGameInfo) override;
	void OnSnapGameInfoEx(int SnappingClient, CNetObj_GameInfoEx *pGameInfoEx) override;

	// Announces a join. Called by CBlockworlds once the entry checks (VPN
	// detection) cleared the client, which can be seconds after connecting.
	void SendJoinMessage(class CPlayer *pPlayer, int VersionFlags);
};

#endif // BLOCKWORLDS_BW_GAMECONTROLLER_H
