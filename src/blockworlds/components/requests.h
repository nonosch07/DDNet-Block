#ifndef BLOCKWORLDS_COMPONENTS_REQUESTS_H
#define BLOCKWORLDS_COMPONENTS_REQUESTS_H

#include <memory>
#include <string>
#include <vector>

#include "core/component.h"

class CRequests : public CComponent
{
public:
	static constexpr const char *GetNameStatic() { return "Requests"; }
	[[nodiscard]] const char *GetName() const override { return GetNameStatic(); };

	struct SRequest
	{
		enum EType
		{
			OneOnOne = 0,
			Shop = 1,
		};
		int m_Id;
		EType m_Type;
		int m_From; // issuer
		int m_To; // target (for 1on1) or owner (for shop)
		int m_Wager;
		int m_Category; // shop category
		int m_Item; // shop item id
		int m_ExpireTick;
	};

	explicit CRequests(class CGameContext *pGameServer);

	// create requests
	int Create1on1Invite(int FromClient, int ToClient, int Wager, int ExpireSeconds = 30);
	int CreateShopRequest(int OwnerClient, int Category, int ItemId, int Price, int ExpireSeconds = 15);

	// accept/decline
	bool AcceptRequest(int RequestId);
	bool DeclineRequest(int RequestId);

	std::vector<int> GetRequestsFor(int ClientId, int TypeFilter = -1) const;

	// convenience lookups
	std::vector<int> GetRequestIdsTo(int ToClient, int TypeFilter = -1) const;
	std::vector<int> GetRequestIdsFromTo(int FromClient, int ToClient, int TypeFilter = -1) const;

	bool GetRequestInfo(int RequestId, SRequest &pOut) const;

	void OnTick() override;

private:
	int NextId();
	std::vector<SRequest> m_Requests;
	int m_NextId = 1;
};

#endif // BLOCKWORLDS_COMPONENTS_REQUESTS_H
