#ifndef BLOCKWORLDS_COMPONENTS_ONEONONE_MANAGER_H
#define BLOCKWORLDS_COMPONENTS_ONEONONE_MANAGER_H

#include <blockworlds/components/core/component.h>
#include <blockworlds/components/events/1on1.h>
#include <memory>
#include <mutex>
#include <vector>

class COneOnOneManager : public CComponent
{
	DECLARE_COMPONENT(COneOnOneManager, "oneonone_manager")
	~COneOnOneManager() override = default;

	std::shared_ptr<COneOnOneEvent> CreateMatch(int Player1ID, int Player2ID, int Wager = 0);
	std::shared_ptr<COneOnOneEvent> CreateMatchWithConfig(int Player1ID, int Player2ID, int Wager = 0);
	std::shared_ptr<COneOnOneEvent> GetMatchForPlayer(int ClientId) const;

	// forwarding hooks
	void OnTick() override;
	void OnSnap(int SnappingClient) override;
	void OnPlayerDropping(int ClientId) override;
	void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;

private:
	// A player cannot be in a match and queued for an event at the same time, so a
	// starting match takes both players out of the pending event registration.
	void LeaveEventRegistration(int ClientId);

	mutable std::mutex m_Mutex; // guard access to m_Matches
	std::vector<std::shared_ptr<COneOnOneEvent>> m_Matches;
};

#endif // BLOCKWORLDS_COMPONENTS_ONEONONE_MANAGER_H
