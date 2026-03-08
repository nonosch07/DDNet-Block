#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_H

#include <engine/shared/protocol.h>

#include <game/gamecore.h>

#include <blockworlds/components/core/component.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>

class CEventComponent : public CComponent
{
public:
	explicit CEventComponent(CGameContext *pGameServer);
	virtual ~CEventComponent();

	enum class EEventState
	{
		Created, // transitional, just created, setting up registration
		Registration,
		Preparation, // transitional, registration closed, setting up arena and players
		Active,
		Ending, // transitional, event finished, announcing results, returning players, freeing up resources
		Finished
	};
	using FnOnStateChange = std::function<void(EEventState OldState, EEventState NewState)>;

	// Snapshot of a player's active cosmetics so they can be saved/restored around events
	struct SCosmeticsSnapshot
	{
		int m_Special = -1;
		int m_SkinMani = -1;
		int m_GunDesign = -1;
		int m_Knockout = -1;
		int m_FlagExpireTick = 0; // server tick when flag expires (0 = no flag)
	};

protected:
	EEventState m_State = EEventState::Created;
	FnOnStateChange m_pfnOnStateChange;
	void SetState(EEventState NewState);

	std::map<int, std::unique_ptr<class CSaveTee>> m_pSavedPlayers;
	std::map<int, std::array<CCharacterCore::WeaponStat, NUM_WEAPONS>> m_SavedWeapons;

	std::vector<int> m_DeferredLoadQueue;

	std::vector<int> m_DeferredWeaponsQueue;

	std::vector<int> m_Candidates;
	std::vector<int> m_Participants;

	// cosmetics snapshot map: ClientId -> snapshot taken when player entered the event
	std::map<int, SCosmeticsSnapshot> m_SavedCosmetics;

	bool m_EmergencyShutdown;
	char m_EmergencyMessage[256];


	void SaveAndClearCosmetics(int ClientId);
	void RestoreCosmetics(int ClientId);

public:
	[[nodiscard]] virtual const char *GetEventName() const = 0; // this is printable name for players, GetName() is internal name for logging

	virtual void OpenRegistration() = 0;
	virtual void CloseRegistration() = 0;
	virtual void StartEvent() = 0;
	virtual void FinishEvent() = 0;

	virtual void ForceNextStage() = 0;

	virtual bool CheckEndCondition() = 0;

	virtual bool Register(int ClientId) = 0;
	virtual bool DeRegister(int ClientId) = 0;

	virtual bool Join(int ClientId) = 0;
	virtual bool Leave(int ClientId) = 0;

	virtual void EmergencyShutdown(const char *pMsg)
	{
		str_copy(m_EmergencyMessage, pMsg);
		m_EmergencyShutdown = true;
	};
	const char *GetEmergencyMessage() const { return m_EmergencyMessage; };
	bool EmergencyShutdown() const { return m_EmergencyShutdown; };

	[[nodiscard]] const std::vector<int> &Participants() const { return m_Participants; }
	[[nodiscard]] const std::vector<int> &Candidates() const { return m_Candidates; }
	[[nodiscard]] EEventState GetState() const { return m_State; }
	[[nodiscard]] const char *GetStateName() const;
	[[nodiscard]] static const char *GetStateName(EEventState State);
	void SetStateChangeCallback(FnOnStateChange pfnCallback) { m_pfnOnStateChange = std::move(pfnCallback); }

	// Process deferred operations (default implementation processes deferred loads).
	virtual void OnTick() override;

	// Final override for OnPlayerDropping: restores cosmetics first, then calls OnEventPlayerDropping.
	// Derived classes MUST override OnEventPlayerDropping instead of OnPlayerDropping.
	void OnPlayerDropping(int ClientId) final override;

	[[nodiscard]] virtual std::optional<int> GetScoreOf(int /*ClientId*/) const { return std::nullopt; }

	// called when a confirmed block-kill happens while the event is active
	// override in events that need killbased mechanics like zcatch or tdm
	virtual void OnBlockedKill(int /*VictimID*/, int /*KillerID*/) {}

	// called when a participant physically impacts another (hammer hit or hook attach)
	// fired before the block tracker clears its state, so impactor data is still fresh
	virtual void OnPlayerImpacted(int /*VictimId*/, int /*InitiatorId*/) {}

	// returns true if this event allows the given participant to kill themselves
	[[nodiscard]] virtual bool AllowKillCommandFor(int /*ClientId*/) const { return false; }

	// optional per-event team index for presenting teams to clients (e.g., vanilla UI teams).
	// Return 0 for red, 1 for blue, or nullopt if not applicable/not a participant.
	[[nodiscard]] virtual std::optional<int> GetTeamIndexFor(int /*ClientId*/) const { return std::nullopt; }

	[[nodiscard]] const char *GetName() const override { return GetEventName(); }

	// Minimum candidates required to start this event (used for test-mode dummy population).
	[[nodiscard]] virtual int GetMinCandidates() const { return 2; }

protected:
	// Called by OnPlayerDropping after cosmetics are restored. Override this instead of OnPlayerDropping.
	virtual void OnEventPlayerDropping(int /*ClientId*/) {}

	void SavePosition(int ClientId);
	void LoadPosition(int ClientId);

	void SaveWeapons(int ClientId);
	void LoadWeapons(int ClientId);

	int PlayerHookedGroundFor(int ClientId) const;

	// Query whether the event considers the client protected (server-side protection
	// like post-death invulnerability). Default: no protection.
	[[nodiscard]] virtual bool IsClientProtected(int ClientId) const { return false; }
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_EVENT_H
