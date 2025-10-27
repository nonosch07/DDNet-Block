#ifndef BLOCKWORLDS_COMPONENTS_ONEONONE_MANAGER_H
#define BLOCKWORLDS_COMPONENTS_ONEONONE_MANAGER_H

#include <blockworlds/components/core/component.h>
#include <blockworlds/components/events/1on1.h>
#include <memory>
#include <vector>
#include <mutex>

class COneOnOneManager : public CComponent
{
public:
    explicit COneOnOneManager(CGameContext *pGameServer);
    ~COneOnOneManager() override = default;

    [[nodiscard]] static const char *GetNameStatic() { return "oneonone_manager"; }
    [[nodiscard]] const char *GetName() const override { return GetNameStatic(); }

    std::shared_ptr<COneOnOneEvent> CreateMatch(int Player1ID, int Player2ID, int Wager = 0);
    std::shared_ptr<COneOnOneEvent> GetMatchForPlayer(int ClientId) const;

    // forwarding hooks
    void OnTick() override;
    void OnPlayerDropping(int ClientId) override;
    void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
    void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;

private:
    mutable std::mutex m_Mutex; // guard access to m_Matches
    std::vector<std::shared_ptr<COneOnOneEvent>> m_Matches;
};

#endif // BLOCKWORLDS_COMPONENTS_ONEONONE_MANAGER_H
