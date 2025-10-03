#ifndef BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_H
#define BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_H

#include "event.h"

class COneOnOneEvent : public CEventComponent
{
public:
	explicit COneOnOneEvent(CGameContext *pGameServer);

	constexpr const char *GetEventName() const override { return "1on1"; }
	void OpenRegistration() override {}
	void CloseRegistration() override {}
	void StartEvent() override;
	void FinishEvent() override;
	void ForceNextStage() override { FinishEvent(); }
	bool CheckEndCondition() override;

	bool Register(int ClientId) override { return false; }
	bool DeRegister(int ClientId) override { return false; }
	bool Join(int ClientId) override { return false; }
	bool Leave(int ClientId) override;

	void Initialize(int Player1ID, int Player2ID, int Wager = 0);

	void OnTick() override;
	void OnCharacterSpawn(int ClientId, vec2 SpawnPos) override;
	void OnCharacterDeath(int KillerId, int ClientId, int Weapon) override;
	void OnPlayerDropping(int ClientId) override;

	[[nodiscard]] std::optional<int> GetScoreOf(int ClientId) const;

private:
	int m_Player1ID;
	int m_Player2ID;
	int m_Score1;
	int m_Score2;
	int m_Wager;
	int m_Team;
	int64_t m_StartTimer;
	int64_t m_CurrentTick;
};

#endif // BLOCKWORLDS_COMPONENTS_EVENTS_1ON1_H
