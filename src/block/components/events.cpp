#include "events.h"

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>

#include <block/components/core/component_registry.h>
#include <block/components/events/bombtag.h>
#include <block/components/events/clanwar.h>
#include <block/components/events/colorsoldiers.h>
#include <block/components/events/event.h>
#include <block/components/events/lmb.h>
#include <block/components/events/priv_tdm.h>
#include <block/components/events/tdm.h>
#include <block/components/events/zcatch.h>
#include <block/components/events/zcatch_grenade.h>
#include <block/components/oneonone_manager.h>
#include <block/context.h>
#include <block/utils/memory.h>
#include <block/votes/votemanager.h>

#include <algorithm>

CEvents::CEvents(CGameContext *pGameServer) :
	CComponent(pGameServer), m_pActiveEvent(nullptr), m_pEventToDelete(nullptr)
{
	// Public events
	RegisterEvent<CLastManBlockingEvent>("lmb", EEventCategory::Public);
	RegisterEvent<CTeamDeathmatchEvent>("tdm", EEventCategory::Public);
	RegisterEvent<CZCatchEvent>("zcatch", EEventCategory::Public);
	RegisterEvent<CZCatchGrenadeEvent>("zcatch_grenade", EEventCategory::Public);
	RegisterEvent<CBombTagEvent>("bombtag", EEventCategory::Public);
	RegisterEvent<CColorSoldiersEvent>("colorsoldiers", EEventCategory::Public);

	// Private events
	// Note: 1on1 is intentionally NOT registered here. 1on1 matches are managed by COneOnOneManager
	RegisterEvent<CPrivateTdmEvent>("priv_tdm", EEventCategory::Private);
	RegisterEvent<CClanwarEvent>("clanwar", EEventCategory::Private);

	CONSOLE_COMMAND("events_status", "", ConEventsStatus, "Status of current event")
	CONSOLE_COMMAND("events_list", "", ConEventsList, "List all events (public and private)")
	CONSOLE_COMMAND("events_list_public", "", ConEventsListPublic, "List public events")
	CONSOLE_COMMAND("events_list_private", "", ConEventsListPrivate, "List private events")
	CONSOLE_COMMAND("events_start", "r[name]", ConEventsStart, "Start specified event")
	CONSOLE_COMMAND("events_next_stage", "", ConEventsForceNextState, "Force current event to next stage")
	CONSOLE_COMMAND("events_end", "", ConEventsForceEnd, "Forcefully end current event")

	CHAT_COMMAND("join", "", ConJoin, "Chat command, try to register to current event")
	CHAT_COMMAND("sub", "", ConJoin, "Chat command, try to register to current event")
	CHAT_COMMAND("leave", "", ConLeave, "Chat command, try to deregister from current event")
}

CEvents::~CEvents()
{
	if(m_pEventToDelete)
	{
		m_pEventToDelete.reset();
		m_pEventToDelete = nullptr;
	}
}

std::vector<CComponentAccessor<CComponent>> CEvents::GetSubComponents() const
{
	std::vector<CComponentAccessor<CComponent>> vSubComponents;
	if(m_pActiveEvent)
		vSubComponents.emplace_back(m_pActiveEvent);
	return vSubComponents;
}

void CEvents::OnDisable()
{
	if(m_pActiveEvent)
	{
		m_pActiveEvent->EmergencyShutdown("Events Shutdown");
		m_pEventToDelete = m_pActiveEvent;
		m_pActiveEvent.reset();
	}
}

std::shared_ptr<CEventComponent> CEvents::CreateEventByName(const char *pName)
{
	if(!pName)
		return nullptr;
	char aClearName[64];
	str_copy(aClearName, pName);
	str_clean_whitespaces(aClearName);
	auto It = m_EventsFactory.find(aClearName);
	if(It == m_EventsFactory.end())
		return nullptr;
	return It->second.m_Factory(GameServer());
}

void CEvents::SetActiveEvent(std::shared_ptr<CEventComponent> pEvent)
{
	m_pActiveEvent = std::move(pEvent);
}

std::shared_ptr<CEventComponent> CEvents::GetActiveEvent() const
{
	return m_pActiveEvent;
}

void CEvents::OnTick()
{
	if(m_pEventToDelete)
		return;

	if(m_pActiveEvent)
	{
		if(m_pActiveEvent->GetState() == CEventComponent::EEventState::Finished)
		{
			if(m_pActiveEvent->EmergencyShutdown())
				Log("'%s' did emergency shutdown. Message: %s", m_pActiveEvent->GetEventName(), m_pActiveEvent->GetEmergencyMessage());
			Log("'%s' finished. Marking for clean up", m_pActiveEvent->GetEventName());
			m_pEventToDelete = m_pActiveEvent;
			m_pActiveEvent.reset();
		}
	}
}

void CEvents::OnPostTick()
{
	if(m_pEventToDelete)
	{
		m_pEventToDelete.reset();
		Log("Cleanup finished");
	}
}

void CEvents::OnCharacterDeath(int KillerId, int ClientId, int Weapon)
{
	if(m_pActiveEvent)
		m_pActiveEvent->OnCharacterDeath(KillerId, ClientId, Weapon);
}

void CEvents::ConEventsStatus(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	CEvents::Log("Current event: %s", pThis->m_pActiveEvent ? pThis->m_pActiveEvent->GetEventName() : "none");
	CEvents::Log("State: %s", pThis->m_pActiveEvent ? pThis->m_pActiveEvent->GetStateName() : "none");
	CEvents::Log("Candidates: %" PRIzu, pThis->m_pActiveEvent ? pThis->m_pActiveEvent->Candidates().size() : 0);
	CEvents::Log("Participants: %" PRIzu, pThis->m_pActiveEvent ? pThis->m_pActiveEvent->Participants().size() : 0);
}
void CEvents::ConEventsList(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	CEvents::Log("Available Events (Public):");
	for(const auto &[Name, Rec] : pThis->m_EventsFactory)
		if(Rec.m_Category == EEventCategory::Public)
			CEvents::Log(" - %s", Name.c_str());
	CEvents::Log("Available Events (Private):");
	for(const auto &[Name, Rec] : pThis->m_EventsFactory)
		if(Rec.m_Category == EEventCategory::Private)
			CEvents::Log(" - %s", Name.c_str());
}

void CEvents::ConEventsListPublic(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	CEvents::Log("Public Events:");
	for(const auto &[Name, Rec] : pThis->m_EventsFactory)
		if(Rec.m_Category == EEventCategory::Public)
			CEvents::Log(" - %s", Name.c_str());
}
void CEvents::ConEventsListPrivate(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	CEvents::Log("Private Events:");
	for(const auto &[Name, Rec] : pThis->m_EventsFactory)
		if(Rec.m_Category == EEventCategory::Private)
			CEvents::Log(" - %s", Name.c_str());
}
void CEvents::ConEventsStart(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	if(pThis->m_pActiveEvent)
	{
		CEvents::Log("Event is already running");
		return;
	}

	const char *pName = pResult->GetString(0);
	char aClearName[64];
	str_copy(aClearName, pName);
	str_clean_whitespaces(aClearName);

	auto It = pThis->m_EventsFactory.find(aClearName);
	if(It == pThis->m_EventsFactory.end())
	{
		CEvents::Log("Event with this name wasn't found");
		return;
	}

	// Don't ask, just believe
	auto pThisShared = ((CEvents *)pUserData)->Registry()->Get<CEvents>();
	pThis->m_pActiveEvent = It->second.m_Factory(pThis->GameServer());
	pThis->m_pActiveEvent->SetStateChangeCallback(MakeSafeCallback(&CEvents::OnEventStateChange, pThisShared.Store()));
	pThis->m_pActiveEvent->SetStateChangeCallback([pThis](auto OldState, auto NewState) { pThis->OnEventStateChange(OldState, NewState); });
	if(!pThis->m_pActiveEvent->EmergencyShutdown())
		pThis->m_pActiveEvent->OpenRegistration();
}
void CEvents::ConEventsForceNextState(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	if(!pThis->m_pActiveEvent)
	{
		CEvents::Log("No active event at this time");
		return;
	}

	pThis->m_pActiveEvent->ForceNextStage();
}
void CEvents::ConEventsForceEnd(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	if(!pThis->m_pActiveEvent)
	{
		CEvents::Log("No active event at this time");
		return;
	}

	pThis->m_pActiveEvent->EmergencyShutdown("Forced");
}

void CEvents::ConJoin(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;

	if(auto OneOnOneMgr = g_ComponentRegistry.Get<COneOnOneManager>(); OneOnOneMgr)
	{
		// any match that has not finished still owns this player's position, team and
		// cosmetics, so let it restore them before the player queues for an event
		auto Match = OneOnOneMgr->GetMatchForPlayer(pResult->m_ClientId);
		if(Match && Match->GetState() != COneOnOneEvent::EEventState::Finished)
		{
			pThis->GameServer()->Block().SendChatTarget(pResult->m_ClientId, "You are currently in a 1on1, finish it before joining an event.");
			return;
		}
	}

	if(pThis->m_pActiveEvent)
	{
		pThis->m_pActiveEvent->Register(pResult->m_ClientId);
		return;
	}

	pThis->GameServer()->Block().SendChatTarget(pResult->m_ClientId, "No active event at this time");
}
void CEvents::ConLeave(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = (CEvents *)pUserData;
	const int ClientId = pResult->m_ClientId;

	// handle 1on1 leave directly here (ConLeaveEvent in gamecontext is shadowed by this registration)
	if(auto Mgr = g_ComponentRegistry.Get<COneOnOneManager>())
	{
		if(auto Match = Mgr->GetMatchForPlayer(ClientId))
		{
			auto State = Match->GetState();
			if(State == COneOnOneEvent::EEventState::Preparation)
			{
				int OtherCid = (ClientId == Match->m_Player1ID) ? Match->m_Player2ID : Match->m_Player1ID;
				pThis->GameServer()->Block().SendChatTarget(ClientId, "[1on1] You left the warmup. Match cancelled.");
				pThis->GameServer()->Block().SendChatTarget(OtherCid, "[1on1] Your opponent left during warmup. Match cancelled.");
				Match->ClearDuelVoteUi();
				for(int Cid : {Match->m_Player1ID, Match->m_Player2ID})
				{
					g_VoteManager.NavigateToRoot(Cid);
					pThis->GameServer()->Block().ClearVotes(Cid);
				}
				Match->AbortAndRefund(nullptr);
				return;
			}
			else if(State == COneOnOneEvent::EEventState::Active)
			{
				Match->Leave(ClientId);
				return;
			}
		}
	}

	if(!pThis->m_pActiveEvent)
	{
		pThis->GameServer()->Block().SendChatTarget(pResult->m_ClientId, "No active event at this time");
		return;
	}

	if(pThis->m_pActiveEvent->GetState() == CEventComponent::EEventState::Registration)
	{
		if(!pThis->m_pActiveEvent->DeRegister(pResult->m_ClientId))
			pThis->GameServer()->Block().SendChatTarget(pResult->m_ClientId, "You aren't registered to participate.");
		return;
	}

	if(pThis->m_pActiveEvent->Leave(pResult->m_ClientId))
	{
		pThis->GameServer()->Block().SendChatTarget(pResult->m_ClientId, "You have left the event and have been disqualified.");
		pThis->GameServer()->Block().SendBroadcast(" ", pResult->m_ClientId, false);
	}
	else
	{
		pThis->GameServer()->Block().SendChatTarget(pResult->m_ClientId, "You are not participating in the current event.");
	}
}

void CEvents::OnEventStateChange(CEventComponent::EEventState OldState, CEventComponent::EEventState NewState)
{
	LogDebug("Event state changed: from %s to %s", CEventComponent::GetStateName(OldState), CEventComponent::GetStateName(NewState));

	if(NewState == CEventComponent::EEventState::Finished && m_pActiveEvent)
	{
		const auto &Parts = m_pActiveEvent->Participants();
		for(int ClientId : Parts)
		{
			if(ClientId < 0)
				continue;
			GameServer()->Block().SendBroadcast(" ", ClientId, false);
		}
	}
}

std::vector<std::string> CEvents::GetEventsByCategory(EEventCategory Category) const
{
	std::vector<std::string> Out;
	Out.reserve(m_EventsFactory.size());
	for(const auto &Kv : m_EventsFactory)
		if(Kv.second.m_Category == Category)
			Out.push_back(Kv.first);
	return Out;
}

bool CEvents::DropRegistration(int ClientId)
{
	if(!m_pActiveEvent || m_pActiveEvent->GetState() != CEventComponent::EEventState::Registration)
		return false;

	const auto &Candidates = m_pActiveEvent->Candidates();
	if(std::find(Candidates.begin(), Candidates.end(), ClientId) == Candidates.end())
		return false;

	return m_pActiveEvent->DeRegister(ClientId);
}

std::optional<CEvents::EEventCategory> CEvents::GetCategoryOf(const char *pName) const
{
	if(!pName)
		return std::nullopt;
	char aName[64];
	str_copy(aName, pName);
	str_clean_whitespaces(aName);
	auto It = m_EventsFactory.find(aName);
	if(It == m_EventsFactory.end())
		return std::nullopt;
	return It->second.m_Category;
}
