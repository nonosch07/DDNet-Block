#include "gamecontroller.h"

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/score.h>

#include <block/base.h>
#include <block/components/core/component_registry.h>
#include <block/components/events.h>
#include <block/components/oneonone_manager.h>
#include <block/context.h>
#include <block/player.h>
#include <block/util.h>
#include <block/version.h>

#include <algorithm>

#define BLOCK_GAME_TYPE_NAME "Block   BW"
#define BLOCK_TEST_TYPE_NAME "TestBlock   BW"

CGameControllerBlock::CGameControllerBlock(CGameContext *pGameServer) :
	CGameControllerDDNet(pGameServer)
{
	m_pGameType = g_Config.m_SvTestingCommands ? BLOCK_TEST_TYPE_NAME : BLOCK_GAME_TYPE_NAME;
}

void CGameControllerBlock::OnPlayerConnect(CPlayer *pPlayer)
{
	// Deliberately skips CGameControllerDDNet::OnPlayerConnect and repeats the
	// three lines of it that Block keeps: its join broadcast has to be withheld
	// until the entry checks clear the client, and the greeting is Block's own.
	IGameController::OnPlayerConnect(pPlayer);
	const int ClientId = pPlayer->GetCid();

	Score()->PlayerData(ClientId)->Reset();
	Score()->LoadPlayerData(ClientId);

	if(!Server()->ClientPrevIngame(ClientId))
	{
		// The broadcast is sent by CBlock once the entry checks (VPN detection)
		// cleared the client, so that banned clients are never announced.
		pPlayer->Block().m_JoinMsgPending = true;
		GameServer()->Block().SendChatTarget(ClientId, "Block modification made by Nouaa. Version: " BLOCK_VERSION);
	}
}

void CGameControllerBlock::SendJoinMessage(CPlayer *pPlayer, int VersionFlags)
{
	const int ClientId = pPlayer->GetCid();
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientId), GetTeamName(pPlayer->GetTeam()));
	GameServer()->SendChat(-1, TEAM_ALL, aBuf, -1, VersionFlags);
}

void CGameControllerBlock::OnPlayerDisconnect(CPlayer *pPlayer, const char *pReason)
{
	pPlayer->Block().OnPlayerSave(true);
	CGameControllerDDNet::OnPlayerDisconnect(pPlayer, pReason);
}

void CGameControllerBlock::SendLeaveMessage(CPlayer *pPlayer, const char *pReason)
{
	const int ClientId = pPlayer->GetCid();

	// NPC (bot) slots never appear in chat at all. A redirected client is no
	// longer "ingame" but still gets announced, which is why Block does this check
	// itself rather than leaving it to IGameController.
	if(pPlayer->Block().m_IsNpc)
		return;
	const bool Redirected = pReason && str_comp(pReason, "changed server") == 0;
	if(!Server()->ClientIngame(ClientId) && !Redirected)
		return;

	// Clients that are still waiting on their entry checks (VPN detection) were
	// never announced when they joined, so they leave silently as well.
	if(!pPlayer->Block().m_EntryChecksPending)
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
	BlockClientAddr(Server(), ClientId, aAddrStr, sizeof(aAddrStr));
	char aLeaveBuf[512];
	str_format(aLeaveBuf, sizeof(aLeaveBuf), "leave player='%d:%s' ip=<{%s}>", ClientId, Server()->ClientName(ClientId), aAddrStr);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aLeaveBuf);
}

void CGameControllerBlock::OnCharacterSpawn(CCharacter *pChr)
{
	CGameControllerDDNet::OnCharacterSpawn(pChr);
	GameServer()->Block().OnCharacterSpawn(pChr);
}

int CGameControllerBlock::SnapPlayerScore(int SnappingClient, CPlayer *pPlayer)
{
	return GameServer()->Block().SnapPlayerScore(SnappingClient, pPlayer);
}

void CGameControllerBlock::Tick()
{
	CGameControllerDDNet::Tick();
	// The block tracker resolves pending blocks and expires stale impacts on a
	// timer, so it has to be ticked or nothing is ever credited.
	GameServer()->Block().BlockTracker().Tick();
}

void CGameControllerBlock::OnSnapGameInfo(int SnappingClient, CNetObj_GameInfo *pGameInfo)
{
	GameServer()->Block().OnSnapGameInfo(SnappingClient, pGameInfo);
}

void CGameControllerBlock::OnSnapGameInfoEx(int SnappingClient, CNetObj_GameInfoEx *pGameInfoEx)
{
	GameServer()->Block().OnSnapGameInfoEx(SnappingClient, pGameInfoEx);
}
