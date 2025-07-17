#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_H

#include <engine/shared/protocol.h>

#include <blockworlds/components/core/component.h>
#include <map>

class CEventComponent : public CComponent
{
public:
	explicit CEventComponent(CGameContext *pGameServer);

	enum class EEventState {
		Created, // transitional, just created, setting up registration
		Registration,
		Preparation, // transitional, registration closed, setting up arena and players
		Active,
		Finished // transitional, event finished, announcing results, returning players, freeing up resources
	};

protected:
	EEventState m_State = EEventState::Created;

	std::map<int, class CSaveTee*> m_pSavedPlayers;

	std::vector<int> m_Candidates;
	std::vector<int> m_Participants;
	int m_StartTick;

	bool m_EmergencyShutdown;
	char m_EmergencyMessage[256];

public:
	[[nodiscard]] virtual constexpr const char *GetEventName() const = 0; // this is printable name for players, GetName() is internal name for logging

	virtual void OpenRegistration() = 0;
	virtual void CloseRegistration() = 0;
	virtual void StartEvent() = 0;
	virtual void FinishEvent() = 0;

	virtual bool CheckEndCondition() = 0;

	[[nodiscard]] virtual bool CanPlayerRegister(int ClientId) const = 0;
	virtual bool Register(int ClientId) = 0;
	virtual bool DeRegister(int ClientId) = 0;

	virtual bool Join(int ClientId) = 0;
	virtual bool Leave(int ClientId) = 0;

	virtual void EmergencyShutdown(const char *pMsg) { str_copy(m_EmergencyMessage, pMsg); m_EmergencyShutdown = true; };
	const char *GetEmergencyMessage() const { return m_EmergencyMessage; };
	bool EmergencyShutdown() const { return m_EmergencyShutdown; };

	[[nodiscard]] const std::vector<int>& Participants() const { return m_Participants; }
	[[nodiscard]] const std::vector<int>& Candidates() const { return m_Candidates; }
	[[nodiscard]] EEventState GetState() const { return m_State; }

protected:
	void SavePosition(int ClientId);
	void LoadPosition(int ClientId);
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_H
