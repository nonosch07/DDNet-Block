#include "redirectzone.h"

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <blockworlds/bw_context.h>

CRedirectZone::CRedirectZone(CGameContext *pGameServer, int Port) :
	IZone(pGameServer, -1), m_Port(Port)
{
	mem_zero(m_aWasInZone, sizeof(m_aWasInZone));
	mem_zero(m_aLastRedirectTick, sizeof(m_aLastRedirectTick));
}

void CRedirectZone::Tick()
{
	IServer *pServer = GameServer()->Server();

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer)
		{
			m_aWasInZone[i] = false;
			continue;
		}

		CCharacter *pChar = pPlayer->GetCharacter();
		if(!pChar)
		{
			m_aWasInZone[i] = false;
			continue;
		}

		bool InZone = IsInZone(pChar->m_Pos);

		if(InZone && !m_aWasInZone[i])
		{
			int CurrentTick = pServer->Tick();

			// debounce: prevent repeated redirects within 3 seconds
			if(CurrentTick < m_aLastRedirectTick[i] + pServer->TickSpeed() * 3)
			{
				m_aWasInZone[i] = InZone;
				continue;
			}

			// don't redirect to our own port
			if(m_Port == pServer->Port())
			{
				m_aWasInZone[i] = InZone;
				continue;
			}

			m_aLastRedirectTick[i] = CurrentTick;
			GameServer()->Bw().RedirectClient(i, m_Port);
		}

		m_aWasInZone[i] = InZone;
	}
}
