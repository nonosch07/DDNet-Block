#include "component.h"

#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>

#include <game/server/gamecontext.h>

#include <blockworlds/bw_context.h>
#include <blockworlds/components/core/component_registry.h>

CGameContext *CComponent::GameServer() const { return m_pGameServer; }
CConfig *CComponent::Config() const { return m_pGameServer->Config(); }
CServer *CComponent::Server() const { return (CServer *)m_pGameServer->Server(); }
CConsole *CComponent::Console() const { return (CConsole *)m_pGameServer->Console(); }
CComponentRegistry *CComponent::Registry() const { return &g_ComponentRegistry; }

CComponent::CComponent(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
}

bool CComponent::IsDebug() const
{
	return Config()->m_Debug;
}

void CComponent::OnConsoleInit()
{
	CServer *pServer = static_cast<CServer *>(Server());
	pServer->SendRconCmdGroupStart(-1);
	GameServer()->Bw().SendChatCmdGroupStart(-1);
	for(auto &Cmd : m_ConsoleCommands)
	{
		Console()->Register(Cmd.m_pName, Cmd.m_pParams, Cmd.m_Flags,
			Cmd.m_pfnCallback, Cmd.m_pUserData, Cmd.m_pHelp);
		const auto *Command = Console()->GetCommandInfo(Cmd.m_pName, Cmd.m_Flags, false);

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(Server()->ClientSlotEmpty(i))
				continue;

			if(pServer->CanClientUseCommand(i, Command))
			{
				pServer->SendRconCmdAdd(Command, i);
				if(Command->Flags() & CFGFLAG_CHAT)
					GameServer()->Bw().SendChatCmdAdd(Command, i);
			}
		}
	}
	GameServer()->Bw().SendChatCmdGroupEnd(-1);
	pServer->SendRconCmdGroupEnd(-1);

	for(auto ChainCmd : m_ChainCommands)
	{
		Console()->Chain(ChainCmd.m_pName, ChainCmd.m_pfnCallback, ChainCmd.m_pUserData);
		Log("Successfully chained '%s'", ChainCmd.m_pName);
	}
}

void CComponent::OnConsoleTerminate()
{
	for(auto &Cmd : m_ConsoleCommands)
	{
		const auto *Command = Console()->GetCommandInfo(Cmd.m_pName, Cmd.m_Flags, false);
		if(Command)
		{
			static_cast<CServer *>(Server())->SendRconCmdRem(Command, -1);
			if(Command->Flags() & CFGFLAG_CHAT)
				GameServer()->Bw().SendChatCmdRem(Command, -1);
		}
		Console()->Deregister(Cmd.m_pName);
	}

	for(auto ChainCmd : m_ChainCommands)
	{
		Console()->UnChain(ChainCmd.m_pName, ChainCmd.m_pfnCallback);
		Log("Successfully unchained '%s'", ChainCmd.m_pName);
	}
}
