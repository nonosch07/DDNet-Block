#ifndef BLOCKWORLDS_COMPONENTS_REQUESTS_H
#define BLOCKWORLDS_COMPONENTS_REQUESTS_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/component.h"

class CRequests : public CComponent
{
public:
	static constexpr const char *GetNameStatic() { return "Requests"; }
	[[nodiscard]] const char *GetName() const override { return GetNameStatic(); };

	struct SRequest
	{
		enum class EType : int
		{
			OneOnOne = 0,
			Shop = 1,
			Clan = 2,
			BlockpointTransfer = 3,
		};
		int m_Id{};
		EType m_Type{EType::OneOnOne};
		int m_From{}; // issuer
		int m_To{}; // target (for 1on1) or owner (for shop)
		int m_Wager{};
		int m_ClanId{}; // clan id for clan invites
		int m_Category{}; // shop category
		int m_Item{}; // shop item id
		int m_ExpireTick{};
		// NOTE: for BlockpointTransfer we reuse m_Wager to store transfer amount to avoiding expanding struct size.
	};

	// helper conversion
	inline int ToInt(SRequest::EType t) { return static_cast<int>(t); }

	explicit CRequests(class CGameContext *pGameServer);

	// create requests
	int Create1on1Invite(int FromClient, int ToClient, int Wager, int ExpireSeconds = 15);
	int CreateClanInvite(int FromClient, int ToClient, int ClanId, int ExpireSeconds = 15);
	int CreateShopRequest(int OwnerClient, int Category, int ItemId, int Price, int ExpireSeconds = 15);
	int CreateBlockpointTransfer(int FromClient, int ToClient, int Amount, int ExpireSeconds = 15);

	// cancel all requests involving a client (either as sender or receiver). Optional type filter.
	// returns number of cancelled requests.
	int CancelRequestsInvolving(int ClientId, std::optional<SRequest::EType> typeFilter = std::nullopt, const char *pReason = nullptr);

	// accept/decline
	bool AcceptRequest(int RequestId);
	bool DeclineRequest(int RequestId);

	// If typeFilter is std::nullopt, return all
	std::vector<int> GetRequestsFor(int ClientId, std::optional<SRequest::EType> typeFilter = std::nullopt) const;

	// convenience lookups
	std::vector<int> GetRequestIdsTo(int ToClient, std::optional<SRequest::EType> typeFilter = std::nullopt) const;
	std::vector<int> GetRequestIdsFromTo(int FromClient, int ToClient, std::optional<SRequest::EType> typeFilter = std::nullopt) const;

	bool GetRequestInfo(int RequestId, SRequest &pOut) const;

	void OnTick() override;

private:
	int NextId();
	std::vector<SRequest> m_Requests;
	int m_NextId = 1;

	// daily BP transfer counters per sender account (UTC day)
	struct SDailyBpCounters
	{
		int m_TodayAmount = 0; // total amount sent today
		int m_TodayCount = 0; // number of completed transfers today
		int m_TodayDate = 0; // yyyymmdd (UTC)
	};
	std::unordered_map<int /*AccountId*/, SDailyBpCounters> m_BpDailyCounters;
};

#endif // BLOCKWORLDS_COMPONENTS_REQUESTS_H
