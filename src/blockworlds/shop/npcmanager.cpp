#include "npcmanager.h"
#include "game/server/entities/character.h"
#include "game/server/gamecontext.h"
#include "game/server/player.h"
#include <engine/server.h>

#include <blockworlds/cosmetics/cosmetics.h>

#include <cmath>
#include <blockworlds/bw_context.h>

CNpcManager::CNpcManager()
{
	m_pGameServer = nullptr;
}

CNpcManager::~CNpcManager()
{
}

void CNpcManager::Init(CGameContext *pGameServer)
{
	RemoveAll();
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
	N.m_PreviewPos = PreviewPos;

	// create npc if needed
	if(N.m_ClientID == -1)
	{
		int DummyID = m_pGameServer->Bw().GetNextClientID();
		if(DummyID == -1)
			return -1;

		N.m_ClientID = DummyID;

		// OnClientConnected create the CPlayer and sets it in m_apPlayers
		m_pGameServer->OnClientConnected(DummyID, 0);
		m_pGameServer->Bw().BotJoin(DummyID, "");

		CPlayer *pFakePlayer = m_pGameServer->m_apPlayers[DummyID];
		if(!pFakePlayer)
			return -1;

		pFakePlayer->Bw().m_IsNpc = true;
		pFakePlayer->SetAfk(true);

		N.m_ConnectionTick = m_pGameServer->Server()->Tick();
	}

	CPlayer *pPlayer = m_pGameServer->Bw().GetPlayer(N.m_ClientID);
	if(!pPlayer)
		return -1;

	// toggle effect once
	if(!N.m_Toggled)
	{
		m_pGameServer->Bw().Cosmetics()->ToggleSkinmani(N.m_ClientID, pSkinName);
		N.m_Toggled = true;
	}

	// wait a small delay for character to spawn, then teleport
	CCharacter *pChr = pPlayer->GetCharacter();
	if(pChr && !N.m_Teleported)
	{
		if(m_pGameServer->Server()->Tick() - N.m_ConnectionTick >= m_pGameServer->Server()->TickSpeed() * 3)
		{
			m_pGameServer->Bw().Teleport(pChr, PreviewPos);
			N.m_Teleported = true;
		}
	}

	return N.m_ClientID;
}

void CNpcManager::Tick()
{
	if(!m_pGameServer)
		return;

	int TickSpeed = m_pGameServer->Server()->TickSpeed();

	for(auto &N : m_vNpcs)
	{
		if(N.m_ClientID == -1)
			continue;

		CPlayer *pPlayer = m_pGameServer->Bw().GetPlayer(N.m_ClientID);
		if(!pPlayer)
			continue;

		CCharacter *pChr = pPlayer->GetCharacter();
		if(!pChr)
			continue;

		if(distance(pChr->m_Pos, N.m_PreviewPos) > 2.0f)
		{
			m_pGameServer->Bw().Teleport(pChr, N.m_PreviewPos);
		}

		pChr->Bw().Core().m_Vel = vec2(0, 0);

		// pick a new target angle every 2-5 seconds
		N.m_AimChangeTimer--;
		if(N.m_AimChangeTimer <= 0)
		{
			N.m_AimTarget = (float)(rand() % 6283 - 3141) / 1000.0f;
			N.m_AimChangeTimer = TickSpeed * 2 + (rand() % (TickSpeed * 3));
		}

		// smootlhy interpolate toward target angle
		float diff = N.m_AimTarget - N.m_AimAngle;

		while(diff > 3.14159f)
			diff -= 6.28318f;
		while(diff < -3.14159f)
			diff += 6.28318f;
		N.m_AimAngle += diff * 0.03f;

		// set aim via input injection so CCharacterCore::Tick() computes the correct angle
		int tx = (int)(cosf(N.m_AimAngle) * 200.0f);
		int ty = (int)(sinf(N.m_AimAngle) * 200.0f);
		CNetObj_PlayerInput Input = {};
		Input.m_TargetX = tx;
		Input.m_TargetY = ty;
		pChr->OnPredictedInput(&Input);
		pChr->OnDirectInput(&Input);
	}
}

int CNpcManager::GetNpcAimAngle(int ClientID) const
{
	for(const auto &N : m_vNpcs)
	{
		if(N.m_ClientID == ClientID)
			return (int)(N.m_AimAngle * 256.0f);
	}
	return 0;
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
			if(p && p->Bw().m_IsNpc)
			{
				m_pGameServer->Bw().BotLeave(ClientID);
			}
		}

		N.m_ClientID = -1;
		N.m_Toggled = false;
		N.m_Teleported = false;
		N.m_ConnectionTick = -1;
	}
}
