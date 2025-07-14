#include "event.h"
#include "game/teamscore.h"

#include <engine/shared/config.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CEventComponent::CEventComponent(CGameContext *pGameServer) :
	CComponent(pGameServer)
{
	m_State = EEventState::Created;
	m_StartTick = 0;

	m_EmergencyShutdown = false;
	m_EmergencyMessage[0] = '\0';
}

void CEventComponent::SavePosition(int ClientId)
{
	auto *pSavedTee = new CSaveTee();
	auto *pChar = GameServer()->GetPlayerChar(ClientId);
	if(pChar)
		pSavedTee->Save(pChar, false);

	m_pSavedPlayers.insert_or_assign(ClientId, pSavedTee);
}
void CEventComponent::LoadPosition(int ClientId)
{
	auto *pChar = GameServer()->GetPlayerChar(ClientId);
	if(!pChar)
		return;

	auto it = m_pSavedPlayers.find(ClientId);
	if(it != m_pSavedPlayers.end())
	{
		it->second->Load(pChar, TEAM_FLOCK, false);
		m_pSavedPlayers.erase(it);
	}
	else if(pChar->IsAlive())
	{
		pChar->Die(-1, WEAPON_WORLD);
	}
}
