#include "npcmanager.h"
#include "game/server/entities/character.h"
#include "game/server/gamecontext.h"
#include "game/server/player.h"
#include <engine/server.h>

#include <blockworlds/cosmetics/cosmetics.h>

CNpcManager::CNpcManager()
{
	m_pGameServer = nullptr;
}

CNpcManager::~CNpcManager()
{
	RemoveAll();
}

void CNpcManager::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
}

void CNpcManager::Resize(size_t Num)
{
	m_vNpcs.resize(Num);
}

int CNpcManager::EnsureNpcAndApplySkinmani(int Index, const vec2 &PreviewPos, const char *pSkinName)
{
	if(!m_pGameServer)
		return -1;

	if((size_t)Index >= m_vNpcs.size())
		Resize(Index + 1);

	SNpc &N = m_vNpcs[Index];

	// create npc if needed
	if(N.m_ClientID == -1)
	{
		int DummyID = m_pGameServer->GetNextClientID();
		if(DummyID == -1)
			return -1;

		N.m_ClientID = DummyID;
		CPlayer *pFakePlayer = new(DummyID) CPlayer(m_pGameServer, m_pGameServer->m_NextUniqueClientId, DummyID, TEAM_RED);
		m_pGameServer->m_NextUniqueClientId++;
		m_pGameServer->m_apPlayers[DummyID] = pFakePlayer;

		// simulate connection steps
		m_pGameServer->OnClientConnected(DummyID, 0);
		m_pGameServer->Server()->BotJoin(DummyID, "");
		pFakePlayer->m_IsNpc = true;
		pFakePlayer->SetAfk(true);

		N.m_ConnectionTick = m_pGameServer->Server()->Tick();
	}

	CPlayer *pPlayer = m_pGameServer->GetPlayer(N.m_ClientID);
	if(!pPlayer)
		return -1;

	// toggle effect once
	if(!N.m_Toggled)
	{
		m_pGameServer->Cosmetics()->ToggleSkinmani(N.m_ClientID, pSkinName);
		N.m_Toggled = true;
	}

	// wait a small delay for character to spawn, then teleport once
	CCharacter *pChr = pPlayer->GetCharacter();
	if(pChr && !N.m_Teleported)
	{
		if(m_pGameServer->Server()->Tick() - N.m_ConnectionTick >= m_pGameServer->Server()->TickSpeed() * 3)
		{
			m_pGameServer->Teleport(pChr, PreviewPos);
			N.m_Teleported = true;
		}
	}

	return N.m_ClientID;
}

void CNpcManager::RemoveAll()
{
	if(!m_pGameServer)
		return;

	for(auto &N : m_vNpcs)
	{
		if(N.m_ClientID == -1)
			continue;

		int ClientID = N.m_ClientID;

		if(ClientID >= 0 && ClientID < (int)MAX_CLIENTS)
		{
			CPlayer *p = m_pGameServer->m_apPlayers[ClientID];
			if(p)
			{
				if(p->m_IsNpc)
				{
					delete p;
					m_pGameServer->m_apPlayers[ClientID] = nullptr;
				}
			}
		}

		N.m_ClientID = -1;
		N.m_Toggled = false;
		N.m_Teleported = false;
		N.m_ConnectionTick = -1;
	}
}
