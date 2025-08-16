// half of it stolen from chilerino
#include "event_base.h"
#include "game/server/entities/character.h"
#include "game/server/gamecontext.h"
#include "game/server/gamemodes/DDRace.h"
#include "game/server/player.h"

CEvent::CEvent(CGameContext *pGameContext, int EventType)
{
	m_pGameContext = pGameContext;
	pGameType = EventType;
	for(auto &SavePos : m_apSavedPositions)
		SavePos = nullptr;
	for(auto &RestorePos : m_aRestorePos)
		RestorePos = false;

	GameServer()->m_vEvents.push_back(this);
}

void CEvent::CleanupEvent()
{
	for(auto &SavePos : m_apSavedPositions)
		delete SavePos;
	destroy = true;
}

void CEvent::Destroy()
{
	for(auto it = GameServer()->m_vEvents.begin(); it != GameServer()->m_vEvents.end(); ++it)
	{
		if(*it == this)
		{
			GameServer()->m_vEvents.erase(it);
			CleanupEvent();
			delete this;
			return;
		}
	}
}

void CEvent::SavePosition(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;

	if(m_apSavedPositions[pPlayer->GetCid()])
		delete m_apSavedPositions[pPlayer->GetCid()];
	m_apSavedPositions[pPlayer->GetCid()] = nullptr;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	// save ddnet state
	m_apSavedPositions[pPlayer->GetCid()] = new CSaveTee();
	m_apSavedPositions[pPlayer->GetCid()]->Save(pChr);
	pChr->m_CurrentKillingSpree = 0;
}

void CEvent::LoadPosition(CPlayer *pPlayer)
{
	// if(!pChr)
	// return;
	if(!pPlayer)
		return;
	if(!m_aRestorePos[pPlayer->GetCid()])
		return;
	if(pPlayer->GetCharacter())
		pPlayer->KillCharacter(-3, false);

	m_aRestorePos[pPlayer->GetCid()] = false;

	CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
	pController->Teams().SetForceCharacterTeam(pPlayer->GetCid(), 0);
	// pController->m_Teams.SetTeamLock(pCurrent->m_Team, false);
	if(!m_apSavedPositions[pPlayer->GetCid()])
		return;
	CCharacter *pChr;
	if(pPlayer->GetCharacter())
		pChr = pPlayer->GetCharacter();
	else
		pChr = pPlayer->ForceSpawn(m_apSavedPositions[pPlayer->GetCid()]->GetPos(), false);
	pChr->SetTeams(&pController->Teams());
	// pCurrent->m_oldChar1->Load(pChr, 0, false);
	// pCurrent->_Teleport(pPlayer->GetCharacter(), pCurrent->m_oldChar1->GetPos());

	// restore ddnet state
	m_apSavedPositions[pPlayer->GetCid()]->Load(pChr, 0);
	delete m_apSavedPositions[pPlayer->GetCid()];
	m_apSavedPositions[pPlayer->GetCid()] = nullptr;
	pChr->GetCore().m_Vel = vec2(0, 0);
}
