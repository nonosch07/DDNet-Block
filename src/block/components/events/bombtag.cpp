#include "bombtag.h"

#include "event_helpers.h"

#include <engine/server/server.h>
#include <engine/shared/config.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <block/components/core/component_registry.h>
#include <block/config.h>
#include <block/context.h>
#include <block/discord/webhook.h>
#include <block/util.h>

#include <algorithm>

CBombTagEvent::CBombTagEvent(CGameContext *pGameServer) :
	CEventComponent(pGameServer)
{
	m_SpawnPositions.clear();

	m_SpawnPositions = GameServer()->Block().ZoneManager()->GetNamedQuadCenters("bbt_spawn");
	if(m_SpawnPositions.empty())
	{
		EmergencyShutdown("Map has no bombtag spawns!");
		return;
	}
}

void CBombTagEvent::OpenRegistration()
{
	m_Candidates.clear();
	m_RegistrationEndTick = Server()->Tick() + Config()->m_SvBombTagRegistrationTime * Server()->TickSpeed();
	SetState(EEventState::Registration);
}

void CBombTagEvent::CloseRegistration()
{
	SetState(EEventState::Preparation);
	GameServer()->Block().SendBroadcast(-1, " ", false);
	if((int)m_Candidates.size() < Config()->m_SvBombTagMinimumCandidates)
	{
		FinishEvent(NOT_ENOUGH_CANDIDATES);
		return;
	}
	m_Participants = m_Candidates;
	m_Candidates.clear();
	StartEvent();
}

void CBombTagEvent::StartEvent()
{
	// drop participants who no longer have a live character
	{
		auto ParticipantsCopy = m_Participants;
		for(int ClientId : ParticipantsCopy)
		{
			if(!GameServer()->GetPlayerChar(ClientId))
				Leave(ClientId);
		}
	}

	// find a free DDRace team
	auto &Teams = GameServer()->m_pController->Teams();
	int ChosenTeam = -1;
	for(int t = 1; t < NUM_DDRACE_TEAMS; ++t)
	{
		if(Teams.GetTeamState(t) == ETeamState::EMPTY && !Teams.IsTeamEvent(t))
		{
			ChosenTeam = t;
			break;
		}
	}
	m_DDRaceTeam = ChosenTeam;
	if(m_DDRaceTeam == -1)
	{
		EmergencyShutdown("No free team was found");
		return;
	}
	Teams.SetTeamEvent(m_DDRaceTeam, true);
	Teams.SetTeamLock(m_DDRaceTeam, false);
	Teams.SetTeamInvitesOpen(m_DDRaceTeam, false);

	// clear state
	m_UsedSpawnIndices.clear();
	m_pSavedPlayers.clear();
	m_SavedWeapons.clear();
	m_PrevSoloState.clear();
	m_Winner = -1;
	m_Bomb = -1;

	// join all participants — RandomSpawnPos ensures each player gets a unique
	// random spawn position (no two land on the same quad).
	for(int ClientId : m_Participants)
		Join(ClientId);

	GameServer()->Block().SendChatTarget(-1, "BombTag started ! Last man standing wins !");

	m_ActiveStartTick = Server()->Tick();

	SetState(EEventState::Active);
}

void CBombTagEvent::FinishEvent()
{
	SetState(EEventState::Ending);

	if(m_FinishReason == NATURAL)
	{
		if(m_Winner != -1)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "'%s' has won BombTag!", Server()->ClientName(m_Winner));
			GameServer()->Block().SendChatTarget(-1, aBuf);
			GameServer()->Block().SendBroadcast(-1, aBuf, false);

			// give winner exp multiplier and flag
			if(auto *pWinner = GameServer()->Block().GetPlayer(m_Winner))
			{
				pWinner->Block().AddExpMultiplier(Config()->m_SvBombTagWinnerExpMultiplier, Config()->m_SvBombTagWinnerExpDuration);
				pWinner->Block().GiveFlag(Config()->m_SvBombTagWinnerExpDuration);
				char aBonusBuf[256];
				str_format(aBonusBuf, sizeof(aBonusBuf), "%d%% experience bonus enabled for %d minutes!", Config()->m_SvBombTagWinnerExpMultiplier, Config()->m_SvBombTagWinnerExpDuration);
				GameServer()->Block().SendChatTarget(m_Winner, aBonusBuf);
			}

			// webhook
			{
				CDiscordWebhook Discord(GameServer()->Engine(), GameServer()->Block().Http());
				const char *pBombTagUrl = g_Config.m_SvDiscordWebhookUrlBombTag[0] ? g_Config.m_SvDiscordWebhookUrlBombTag : nullptr;
				if(Discord.IsConfigured(pBombTagUrl))
				{
					const char *pWinnerName = Server()->ClientName(m_Winner);

					char aDiscord[1024];

					str_format(aDiscord, sizeof(aDiscord),
						"**BombTag Result**\n"
						"**Winner: %s**",
						pWinnerName ? pWinnerName : "?");

					CDiscordWebhook::SSendOptions Opt;
					Opt.m_pWebhookUrl = pBombTagUrl;
					Discord.Send(aDiscord, Opt);
				}
			}
		}
		else
		{
			const char *pMsg = "BombTag ended with no winner.";
			GameServer()->Block().SendChatTarget(-1, pMsg);
			GameServer()->Block().SendBroadcast(-1, pMsg, false);
		}
	}
	else if(m_FinishReason == NOT_ENOUGH_CANDIDATES)
	{
		GameServer()->Block().SendChatTarget(-1, "Not enough players joined BombTag.");
	}
	else if(m_FinishReason == EMERGENCY)
	{
		GameServer()->Block().SendChatTarget(-1, "BombTag ended prematurely.");
	}

	auto RemainingParticipants = m_Participants;
	for(int ClientId : RemainingParticipants)
		Leave(ClientId);

	CEventComponent::OnTick(); // flush deferred position/weapon queues

	// restore solo/collision state
	for(const auto &Entry : m_PrevSoloState)
	{
		if(auto *pChar = GameServer()->GetPlayerChar(Entry.first))
		{
			if(Entry.second.m_Solo)
				pChar->SetSolo(true);
			pChar->Block().Core().m_CollisionDisabled = Entry.second.m_Collision;
		}
	}
	m_PrevSoloState.clear();

	if(m_DDRaceTeam != -1)
	{
		GameServer()->m_pController->Teams().ResetRoundState(m_DDRaceTeam);
		GameServer()->m_pController->Teams().SetTeamEvent(m_DDRaceTeam, false);
		m_DDRaceTeam = -1;
	}

	SetState(EEventState::Finished);
}

void CBombTagEvent::ForceNextStage()
{
	if(GetState() == EEventState::Registration)
		CloseRegistration();
	else if(GetState() == EEventState::Active)
		FinishEvent(NATURAL);
}

bool CBombTagEvent::CheckEndCondition()
{
	if(m_Participants.size() == 1)
	{
		m_Winner = m_Participants[0];
		return true;
	}
	if(m_Participants.empty())
	{
		m_Winner = -1;
		return true;
	}
	return false;
}

bool CBombTagEvent::Register(int ClientId)
{
	if(!CheckClientId(ClientId))
		return false;
	if(GetState() != EEventState::Registration)
	{
		GameServer()->Block().SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	if(!GameServer()->Block().GetPlayer(ClientId))
		return false;
	if(IsCandidate(ClientId))
	{
		GameServer()->Block().SendChatTarget(ClientId, "You are already registered for BombTag.");
		return false;
	}
	if(g_Config.m_SvMaxClientsPerIp > 1 && g_Config.m_SvEventsTestMode == 0)
	{
		for(int CandId : m_Candidates)
		{
			if(BlockIsClientsSameAddr(GameServer()->Server(), ClientId, CandId))
			{
				GameServer()->Block().SendChatTarget(ClientId, "You cannot register for this event (Already registered).");
				return false;
			}
		}
	}

	m_Candidates.push_back(ClientId);
	GameServer()->Block().SendChatTarget(ClientId, "You joined BombTag registration!");
	return true;
}

bool CBombTagEvent::DeRegister(int ClientId)
{
	if(GetState() != EEventState::Registration)
	{
		GameServer()->Block().SendChatTarget(ClientId, "Registration phase is over!");
		return false;
	}
	auto It = std::find(m_Candidates.begin(), m_Candidates.end(), ClientId);
	if(It == m_Candidates.end())
	{
		GameServer()->Block().SendChatTarget(ClientId, "You are not registered for BombTag.");
		return false;
	}
	m_Candidates.erase(It);
	GameServer()->Block().SendChatTarget(ClientId, "You left BombTag registration.");
	return true;
}

bool CBombTagEvent::Join(int ClientId)
{
	SaveWeapons(ClientId); // saves current weapons, clears all, gives hammer + gun
	SavePosition(ClientId);

	auto *pChar = GameServer()->GetPlayerChar(ClientId);
	if(pChar)
	{
		bool WasSolo = pChar->Core()->m_Solo;
		bool WasCollision = pChar->Core()->m_CollisionDisabled;
		m_PrevSoloState[ClientId] = {WasSolo, WasCollision};
		if(WasSolo)
			pChar->SetSolo(false);
		if(WasCollision)
			pChar->Block().Core().m_CollisionDisabled = false;
		pChar->GetPlayer()->Pause(CPlayer::PAUSE_NONE, false);
		pChar->SetDeepFrozen(false);

		GameServer()->m_pController->Teams().SetForceCharacterTeam(ClientId, m_DDRaceTeam);
		pChar->ResetVelocity();
		pChar->Block().FreezeForce(Config()->m_SvBombTagInitialFreezeTime);
		GameServer()->Block().Teleport(pChar, NextSpawnPos());

		// Replace hammer+gun (given by SaveWeapons) with grenade only
		ArmWithHammer(pChar);
	}

	if(auto *pPlayer = GameServer()->Block().GetPlayer(ClientId))
	{
		SaveAndClearCosmetics(ClientId);
		pPlayer->Block().ClearCosmetics();
	}

	return true;
}

bool CBombTagEvent::Leave(int ClientId)
{
	auto It = std::find(m_Participants.begin(), m_Participants.end(), ClientId);
	if(It == m_Participants.end())
		return false;
	m_Participants.erase(It);

	if(m_Bomb == ClientId)
		m_Bomb = -1;

	LoadPositionHelper(GameServer(), m_pSavedPlayers, ClientId);
	LoadWeaponsHelper(GameServer(), m_SavedWeapons, ClientId);

	m_DeferredLoadQueue.erase(std::remove(m_DeferredLoadQueue.begin(), m_DeferredLoadQueue.end(), ClientId), m_DeferredLoadQueue.end());
	m_DeferredWeaponsQueue.erase(std::remove(m_DeferredWeaponsQueue.begin(), m_DeferredWeaponsQueue.end(), ClientId), m_DeferredWeaponsQueue.end());

	// restore solo/collision state
	if(auto *pChar = GameServer()->GetPlayerChar(ClientId))
	{
		auto SoloIt = m_PrevSoloState.find(ClientId);
		if(SoloIt != m_PrevSoloState.end())
		{
			if(SoloIt->second.m_Solo)
				pChar->SetSolo(true);
			pChar->Block().Core().m_CollisionDisabled = SoloIt->second.m_Collision;
			m_PrevSoloState.erase(SoloIt);
		}
	}

	RestoreCosmetics(ClientId);
	return true;
}

void CBombTagEvent::EmergencyShutdown(const char *pMsg)
{
	CEventComponent::EmergencyShutdown(pMsg);
	if(GetState() != EEventState::Finished)
		FinishEvent(EMERGENCY);
}

void CBombTagEvent::OnCharacterTakeDamage(vec2 Force, int Dmg, int From, int ClientId, int Weapon)
{
	if(GetState() != EEventState::Active)
		return;
	if(Weapon != WEAPON_HAMMER)
		return;

	// Both must be free (uncaught) participants
	if(!IsParticipant(ClientId))
		return;

	// Guard IsAlive().
	auto *pChar = GameServer()->GetPlayerChar(ClientId);
	if(!pChar || !pChar->IsAlive())
		return;

	if(From == m_Bomb)
	{
		SetBomb(ClientId);
		pChar->Block().FreezeForce(0.5f);
	}
	else
	{
		if(ClientId == m_Bomb)
			m_BombExplodeTick = m_BombExplodeTick - Server()->TickSpeed();
		else if(pChar->m_FreezeTime == 0) // Player can stun the bomb and other players
			pChar->Block().FreezeForce(g_BwConfig.m_SvBombTagStunDuration);
	}
}

void CBombTagEvent::OnCharacterSpawn(int ClientId, vec2 /*SpawnPos*/)
{
	if(GetState() == CEventComponent::EEventState::Active)
	{
		if(IsParticipant(ClientId))
		{
			Leave(ClientId);
			GameServer()->Block().SendChatTarget(ClientId, "You were eliminated!");
			GameServer()->Block().SendBroadcast("You were eliminated!", ClientId);
		}
	}
}

void CBombTagEvent::OnCharacterDeath(int /*KillerId*/, int ClientId, int /*Weapon*/)
{
	if(GetState() != EEventState::Active)
		return;
	if(!IsParticipant(ClientId))
		return;

	if(ClientId == m_Bomb)
	{
		m_Bomb = -1;
	}
}

void CBombTagEvent::OnEventPlayerDropping(int ClientId)
{
	if(GetState() == EEventState::Active)
	{
		if(IsParticipant(ClientId))
			Leave(ClientId);
	}
	else if(GetState() == EEventState::Registration)
	{
		if(IsCandidate(ClientId))
			DeRegister(ClientId);
	}
}

void CBombTagEvent::OnSnapClientInfo(int ClientId, int SnappingClient, struct CNetObj_ClientInfo *pClientInfo)
{
	if(GetState() != CEventComponent::EEventState::Active || !IsParticipant(ClientId))
		return;

	StrToInts(pClientInfo->m_aSkin, std::size(pClientInfo->m_aSkin), ClientId == m_Bomb ? "bomb" : "default");
	pClientInfo->m_UseCustomColor = false;
	pClientInfo->m_ColorBody = 0;
	pClientInfo->m_ColorFeet = 0;
}

void CBombTagEvent::OnSnapPlayerInfo(int ClientId, int SnappingClient, CNetObj_PlayerInfo *pPlayerInfo)
{
	if(GetState() != CEventComponent::EEventState::Active)
		return;

	if(Server()->GetAuthedState(SnappingClient) == AUTHED_NO && IsParticipant(ClientId))
	{
		pPlayerInfo->m_Score = 0;
	}
}

void CBombTagEvent::OnTick()
{
	if(CEventComponent::EmergencyShutdown())
	{
		FinishEvent(EMERGENCY);
		return;
	}

	if(GetState() == EEventState::Registration)
	{
		if(Server()->Tick() >= m_RegistrationEndTick)
		{
			CloseRegistration();
			return;
		}
		CEventComponent::OnTick(); // test-mode dummies
		char aTimeLeft[32];
		FormatTimeLeft(aTimeLeft, sizeof(aTimeLeft), ((m_RegistrationEndTick - Server()->Tick()) / Server()->TickSpeed()));

		for(int i = 0; i < Server()->MaxClients(); ++i)
		{
			if(!Server()->ClientIngame(i))
				continue;

			CPlayer *pPlayer = GameServer()->Block().GetPlayer(i);
			if(!pPlayer)
				continue;

			pPlayer->Block().SendBroadcastAlignedLeft("BombTag is about to start!\n"
								  "Register with /join\n"
								  "Time left: %s\n\n"
								  "Participants: %" PRIzu "\n"
								  "%s",
				aTimeLeft,
				m_Candidates.size(),
				(int)m_Candidates.size() < Config()->m_SvBombTagMinimumCandidates ? "Not enough participants!\n" : "");
		}
	}
	else if(GetState() == EEventState::Active)
	{
		CEventComponent::OnTick(); // process deferred position/weapon restores

		// Bomb's out, kill the player
		if(Server()->Tick() >= m_BombExplodeTick)
		{
			CPlayer *pPlayer = GameServer()->Block().GetPlayer(m_Bomb);
			if(pPlayer && pPlayer->GetCharacter())
			{
				GameServer()->CreateExplosion(pPlayer->GetCharacter()->GetPos(), pPlayer->GetCid(), WEAPON_GRENADE, true, m_DDRaceTeam, pPlayer->GetCharacter()->TeamMask());
				pPlayer->GetCharacter()->Die(-1, WEAPON_GRENADE);
			}
		}

		if(m_Bomb == -1 && m_Participants.size() > 1)
		{
			if(Server()->Tick() > m_ActiveStartTick + Config()->m_SvBombTagInitialFreezeTime * Server()->TickSpeed())
				ElectBomb();
		}

		if(Server()->Tick() % Server()->TickSpeed() == 0)
		{
			CPlayer *pPlayer = GameServer()->Block().GetPlayer(m_Bomb);
			if(pPlayer && pPlayer->GetCharacter())
			{
				// Only generates the last 10 seconds of stars
				if((m_BombExplodeTick - Server()->Tick()) / Server()->TickSpeed() <= 10)
				{
					int Amount = (m_BombExplodeTick - Server()->Tick()) / Server()->TickSpeed();
					GameServer()->CreateDamageInd(pPlayer->GetCharacter()->GetPos(), 0, Amount, pPlayer->GetCharacter()->TeamMask());
				}
			}
		}

		if(CheckEndCondition())
			FinishEvent(NATURAL);
	}
}

int CBombTagEvent::GetMinCandidates() const
{
	return Config()->m_SvBombTagMinimumCandidates;
}

bool CBombTagEvent::IsCandidate(int ClientId) const
{
	return std::find(m_Candidates.begin(), m_Candidates.end(), ClientId) != m_Candidates.end();
}

bool CBombTagEvent::IsParticipant(int ClientId) const
{
	return std::find(m_Participants.begin(), m_Participants.end(), ClientId) != m_Participants.end();
}

void CBombTagEvent::SetBomb(int ClientId)
{
	m_Bomb = ClientId;
}

// Elects a new bomb when none is active
void CBombTagEvent::ElectBomb()
{
	int Elected = m_Participants[rand() % m_Participants.size()];

	SetBomb(Elected);
	m_BombExplodeTick = Server()->Tick() + Config()->m_SvBombTagBombDuration * Server()->TickSpeed();

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s is the new bomb !", Server()->ClientName(Elected));
	GameServer()->SendChat(-1, m_DDRaceTeam, aBuf);
}

vec2 CBombTagEvent::NextSpawnPos()
{
	return RandomSpawnPos(m_SpawnPositions, m_UsedSpawnIndices);
}

void CBombTagEvent::ArmWithHammer(CCharacter *pChar)
{
	if(!pChar)
		return;
	// Remove gun, give hammer only
	pChar->GiveWeapon(WEAPON_GUN, true);
	pChar->SetActiveWeapon(WEAPON_HAMMER);
}
