#ifndef BLOCKWORLDS_COMPONENTS_AI_AI_BOT_H
#define BLOCKWORLDS_COMPONENTS_AI_AI_BOT_H

#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <blockworlds/components/core/component.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class CPlayer;
class CCharacter;

class CAiBotComponent : public CComponent
{
	DECLARE_COMPONENT(CAiBotComponent, "ai_bot")
	~CAiBotComponent() override = default;

	void OnTick() override;
	void OnShutdown() override;

private:
	struct SFeatureKey
	{
		uint64_t m_K{};
	};
	struct SActionStats
	{
		uint32_t m_MoveLeft{};
		uint32_t m_MoveRight{};
		uint32_t m_MoveIdle{};
		uint32_t m_Jump{};
		uint32_t m_NoJump{};
		uint32_t m_Fire{};
		uint32_t m_NoFire{};
		uint32_t m_Hook{};
		uint32_t m_NoHook{};
		uint32_t m_AimSector[8]{};
		uint32_t m_WeaponUsed[8]{};
		uint32_t m_SwitchedWeapon{};
		uint32_t m_BlockReward{};
		void Decay(float f);
	};
	std::unordered_map<uint64_t, SActionStats> m_Table;

	bool m_Enabled = true;
	bool m_MapAllowed = false;
	int m_BotCid = -1;
	int m_BestPlayerCid = -1;
	int m_SampleCount = 0;
	int m_MinSamplesToSpawn = 1500;
	bool m_LearnAllPlayers = false;
	int m_LastDecayTick = 0;
	float m_DecayFactor = 0.995f;
	int m_DecayIntervalTicks = 300;
	char m_aAccountName[32];
	char m_aAccountPassword[64];
	int m_ModelVersion = 3;
	bool m_Dirty = false;
	int m_LastSaveTick = 0;
	int m_SaveIntervalTicks = 1500;

	// blocking + reward system
	uint64_t m_LastBotKey = 0;
	bool m_HaveLastBotKey = false;
	float m_LastEnemySpeed[MAX_CLIENTS] = {0};
	int m_LastRewardTick = 0;
	int m_RewardCooldownTicks = 25;
	float m_BlockSpeedDropFactor = 0.5f;
	float m_BlockDetectRadius = 120.0f;
	float m_RewardPerEvent = 1.0f;

	bool m_MapAnalyzed = false;
	struct SChokepoint
	{
		vec2 m_Pos;
	};
	std::vector<SChokepoint> m_Chokepoints;
	void AnalyzeMap();
	vec2 FindBlockingSpot(const CCharacter *pEnemy, const CCharacter *pBot) const;
	bool PredictEnemyThroughChoke(const CCharacter *pEnemy, const vec2 &ChokePos) const;
	bool m_BlockingMode = false;
	vec2 m_BlockTarget{};

	void TickCollect();
	void TickControlBot();
	void EnsureBotSpawned();
	void DespawnBot();
	void LoadModel();
	void SaveModel();
	bool IsBotActive() const;
	CPlayer *BestPlayer();
	uint64_t BuildFeatureKey(const CCharacter *pChr, const CCharacter *pEnemy) const;
	void UpdateStats(uint64_t Key, const CCharacter *pChr);
	void InferAndApplyInput();
	void EvaluateBlockingReward();
	void ApplyInput(CPlayer *pBot, const struct CNetObj_PlayerInput &Input);
	void LoginBotAccount();
	int ComputeAimSector(const vec2 &Delta) const;
	void MaybeAutoSave();

	static void ConAiEnable(IConsole::IResult *pResult, void *pUser);
	static void ConAiStats(IConsole::IResult *pResult, void *pUser);
	static void ConAiSpawn(IConsole::IResult *pResult, void *pUser);
	static void ConAiDespawn(IConsole::IResult *pResult, void *pUser);
	static void ConAiReset(IConsole::IResult *pResult, void *pUser);
	static void ConAiSetMinSamples(IConsole::IResult *pResult, void *pUser);
	static void ConAiSave(IConsole::IResult *pResult, void *pUser);
	static void ConAiLearnAll(IConsole::IResult *pResult, void *pUser);
};

#endif
