#include "bw_gamecontroller.h"

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/score.h>

#include <blockworlds/bw_base.h>
#include <blockworlds/bw_context.h>
#include <blockworlds/bw_player.h>
#include <blockworlds/bw_util.h>
#include <blockworlds/bw_version.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/components/oneonone_manager.h>

#include <algorithm>

#define BW_GAME_TYPE_NAME "BW"
#define BW_TEST_TYPE_NAME "TestBW"

CGameControllerBW::CGameControllerBW(CGameContext *pGameServer) :
	CGameControllerDDNet(pGameServer)
{
	m_pGameType = g_Config.m_SvTestingCommands ? BW_TEST_TYPE_NAME : BW_GAME_TYPE_NAME;
}

void CGameControllerBW::OnPlayerConnect(CPlayer *pPlayer)
{
	// Deliberately skips CGameControllerDDNet::OnPlayerConnect and repeats the
	// three lines of it that BW keeps: its join broadcast has to be withheld
	// until the entry checks clear the client, and the greeting is BW's own.
	IGameController::OnPlayerConnect(pPlayer);
	const int ClientId = pPlayer->GetCid();

	Score()->PlayerData(ClientId)->Reset();
	Score()->LoadPlayerData(ClientId);

	if(!Server()->ClientPrevIngame(ClientId))
	{
		// The broadcast is sent by CBlockworlds once the entry checks (VPN detection)
		// cleared the client, so that banned clients are never announced.
		pPlayer->Bw().m_JoinMsgPending = true;
		GameServer()->Bw().SendChatTarget(ClientId, "Blockworlds src2 by Nouaa. Version: " BLOCKWORLDS_VERSION);
	}
}

void CGameControllerBW::SendJoinMessage(CPlayer *pPlayer, int VersionFlags)
{
	const int ClientId = pPlayer->GetCid();
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientId), GetTeamName(pPlayer->GetTeam()));
	GameServer()->SendChat(-1, TEAM_ALL, aBuf, -1, VersionFlags);
}

void CGameControllerBW::OnPlayerDisconnect(CPlayer *pPlayer, const char *pReason)
{
	pPlayer->Bw().OnPlayerSave(true);
	CGameControllerDDNet::OnPlayerDisconnect(pPlayer, pReason);
}

void CGameControllerBW::SendLeaveMessage(CPlayer *pPlayer, const char *pReason)
{
	const int ClientId = pPlayer->GetCid();

	// NPC (bot) slots never appear in chat at all. A redirected client is no
	// longer "ingame" but still gets announced, which is why BW does this check
	// itself rather than leaving it to IGameController.
	if(pPlayer->Bw().m_IsNpc)
		return;
	const bool Redirected = pReason && str_comp(pReason, "changed server") == 0;
	if(!Server()->ClientIngame(ClientId) && !Redirected)
		return;

	// Clients that are still waiting on their entry checks (VPN detection) were
	// never announced when they joined, so they leave silently as well.
	if(!pPlayer->Bw().m_EntryChecksPending)
	{
		char aBuf[512];
		if(Redirected)
			str_format(aBuf, sizeof(aBuf), "\xE2\x9C\x88 '%s' has joined another server!", Server()->ClientName(ClientId));
		else if(pReason && *pReason)
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game (%s)", Server()->ClientName(ClientId), pReason);
		else
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game", Server()->ClientName(ClientId));
		GameServer()->SendChat(-1, TEAM_ALL, aBuf, -1, CGameContext::FLAG_SIX);
	}

	char aAddrStr[NETADDR_MAXSTRSIZE];
	BwClientAddr(Server(), ClientId, aAddrStr, sizeof(aAddrStr));
	char aLeaveBuf[512];
	str_format(aLeaveBuf, sizeof(aLeaveBuf), "leave player='%d:%s' ip=<{%s}>", ClientId, Server()->ClientName(ClientId), aAddrStr);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aLeaveBuf);
}

void CGameControllerBW::OnCharacterSpawn(CCharacter *pChr)
{
	CGameControllerDDNet::OnCharacterSpawn(pChr);
	GameServer()->Bw().OnCharacterSpawn(pChr);
}

int CGameControllerBW::SnapPlayerScore(int SnappingClient, CPlayer *pPlayer)
{
	return GameServer()->Bw().SnapPlayerScore(SnappingClient, pPlayer);
}

void CGameControllerBW::Tick()
{
	CGameControllerDDNet::Tick();
	// The block tracker resolves pending blocks and expires stale impacts on a
	// timer, so it has to be ticked or nothing is ever credited.
	GameServer()->Bw().BlockTracker().Tick();
}

void CGameControllerBW::OnSnapGameInfo(int SnappingClient, CNetObj_GameInfo *pGameInfo)
{
	GameServer()->Bw().OnSnapGameInfo(SnappingClient, pGameInfo);
}

void CGameControllerBW::OnSnapGameInfoEx(int SnappingClient, CNetObj_GameInfoEx *pGameInfoEx)
{
	GameServer()->Bw().OnSnapGameInfoEx(SnappingClient, pGameInfoEx);
}
