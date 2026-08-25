#include "storemanager.h"

#include <base/time.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/accounts.h>
#include <blockworlds/bw_context.h>
#include <blockworlds/common.h>

static const char *PurchaseBlockedReason(CPlayer *pOwner, int Category, int Product)
{
	switch(Category)
	{
	case CShop::CATEGORY_GUNDESIGN:
		return pOwner->Bw().GetPlayerGundesign()[Product] == '1' ? "You already own this cosmetic." : nullptr;
	case CShop::CATEGORY_KNOCKOUT:
		return pOwner->Bw().GetPlayerKnockouts()[Product] == '1' ? "You already own this cosmetic." : nullptr;
	case CShop::CATEGORY_SKINMANI:
		return pOwner->Bw().GetPlayerSkinmani()[Product] == '1' ? "You already own this cosmetic." : nullptr;
	case CShop::CATEGORY_UTILITY:
		// consumables can be stacked; VIP cannot, whether it was bought or given by an admin.
		if(Product == CCosmeticsHandler::UTILITY_VIP_WEEK && pOwner->Bw().HasVip())
			return "You are already VIP.";
		return nullptr;
	default:
		return "Invalid cosmetic category.";
	}
}

// display name of a utility product.
const char *CShop::UtilityName(int Product)
{
	switch(Product)
	{
	case CCosmeticsHandler::UTILITY_WEAPONKIT: return "Weapon Kit";
	case CCosmeticsHandler::UTILITY_DEATHNOTE_PAGE: return "Deathnote Page";
	case CCosmeticsHandler::UTILITY_PASSIVE_REMOVER: return "Passive Remover";
	case CCosmeticsHandler::UTILITY_VIP_WEEK: return "VIP (1 week)";
	default: return "Utility Item";
	}
}

// hands over a utility product that has already been paid for
static void GrantUtility(CGameContext *pGameContext, CPlayer *pOwner, int Product)
{
	CBwPlayer &Bw = pOwner->Bw();
	switch(Product)
	{
	case CCosmeticsHandler::UTILITY_WEAPONKIT:
		Bw.SetPlayerWeaponkits(Bw.GetPlayerWeaponkits() + 1);
		break;
	case CCosmeticsHandler::UTILITY_DEATHNOTE_PAGE:
		Bw.SetPlayerPages(Bw.GetPlayerPages() + 1);
		break;
	case CCosmeticsHandler::UTILITY_PASSIVE_REMOVER:
		Bw.SetPlayerPassiveRemovers(Bw.GetPlayerPassiveRemovers() + 1);
		break;
	case CCosmeticsHandler::UTILITY_VIP_WEEK:
		Bw.GrantTimedVip(VIP_WEEK_SECONDS, time_timestamp());
		pGameContext->Bw().SendChatTarget(pOwner->GetCid(), "You are now VIP for 7 days. Thanks for the support!");
		break;
	default:
		break;
	}
}

CShop::CShop(CGameContext *pGameContext, CPlayer *pOwner, int pCategory, int pCosmetics, int ExpireInS) :
	m_pGameContext(pGameContext), m_pOwner(pOwner), m_pProduct(pCosmetics), m_pCategory(pCategory)
{
	// initialize cosmetics handler pointer and a safe default name
	m_pCosmeticsHandler = pGameContext ? pGameContext->Bw().Cosmetics() : nullptr;
	m_pCosmeticName = "<unknown>";

	if(!m_pGameContext || !m_pOwner)
	{
		dbg_msg("shop", "invalid arguments when creating shop (null game context or owner)");
		// cannot proceed, destroy this instance
		if(m_pGameContext)
			Destroy(true);
		else
			delete this;
		return;
	}
	m_pExpireTick = (m_pGameContext ? m_pGameContext->Server()->Tick() : 0) + ExpireInS * (m_pGameContext ? m_pGameContext->Server()->TickSpeed() : 50);

	if(!pOwner->Bw().IsLoggedIn())
	{
		if(m_pGameContext)
		{
			m_pGameContext->SendChatTarget(pOwner->GetCid(), "You need to be logged in to make purchases.");
			m_pGameContext->SendChatTarget(pOwner->GetCid(), "Use /accounts for information on the account system.");
		}
		Destroy(true);
		return;
	}

	bool ValidIndex = false;
	switch(pCategory)
	{
	case CATEGORY_GUNDESIGN:
		ValidIndex = (pCosmetics >= 0 && pCosmetics < CCosmeticsHandler::NUM_GUNDESIGNS);
		break;
	case CATEGORY_KNOCKOUT:
		ValidIndex = (pCosmetics >= 0 && pCosmetics < CCosmeticsHandler::NUM_KNOCKOUTS);
		break;
	case CATEGORY_SKINMANI:
		ValidIndex = (pCosmetics >= 0 && pCosmetics < CCosmeticsHandler::NUM_SKINMANIS);
		break;
	case CATEGORY_UTILITY:
		ValidIndex = (pCosmetics >= 0 && pCosmetics < CCosmeticsHandler::NUM_UTILITY_ITEMS);
		break;
	default:
		break;
	}
	if(!ValidIndex)
	{
		if(m_pGameContext)
			m_pGameContext->SendChatTarget(pOwner->GetCid(), "Invalid cosmetic selection.");
		Destroy(true);
		return;
	}

	if(const char *pBlocked = PurchaseBlockedReason(pOwner, pCategory, pCosmetics))
	{
		if(m_pGameContext)
			m_pGameContext->SendChatTarget(pOwner->GetCid(), pBlocked);
		Destroy(true);
		return;
	}

	if(!SetProductInfo(pCategory, pCosmetics) || m_pPrice <= 0)
	{
		if(m_pGameContext)
			m_pGameContext->SendChatTarget(pOwner->GetCid(), "Could not retrieve product info or invalid price.");
		Destroy(true);
		return;
	}

	if(CCharacter *pChar = pOwner->GetCharacter())
	{
		if(pChar->Bw().m_PendingPurchase)
		{
			pChar->Bw().m_PendingPurchase->Destroy(false);
		}
		pChar->Bw().m_PendingPurchase = this;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Do you want to buy '%s' for %d blockpoints? (/yes, /no)", m_pCosmeticName, m_pPrice);
		GameServer()->Bw().SendChatTarget(m_pOwner->GetCid(), aBuf);
	}
	else
	{
		Destroy(true);
	}
}

bool CShop::SetProductInfo(int Category, int Cosmetics)
{
	bool Success = false;
	vec2 PreviewPos;
	if(!m_pCosmeticsHandler)
	{
		dbg_msg("shop", "Cosmetics handler not available when setting product info");
		m_pCosmeticName = "<unknown>";
		return false;
	}
	switch(Category)
	{
	case CATEGORY_SKINMANI:
		Success = m_pCosmeticsHandler->ShopInfoSkinmani(Cosmetics, m_pPrice, m_pLevel, PreviewPos);
		if(Success && Cosmetics >= 0 && Cosmetics < CCosmeticsHandler::NUM_SKINMANIS)
			m_pCosmeticName = CCosmeticsHandler::ms_SkinmaniNames[Cosmetics];
		break;
	case CATEGORY_KNOCKOUT:
		Success = m_pCosmeticsHandler->ShopInfoKnockout(Cosmetics, m_pPrice, m_pLevel, PreviewPos);
		if(Success && Cosmetics >= 0 && Cosmetics < CCosmeticsHandler::NUM_KNOCKOUTS)
			m_pCosmeticName = CCosmeticsHandler::ms_KnockoutNames[Cosmetics];
		break;
	case CATEGORY_GUNDESIGN:
		Success = m_pCosmeticsHandler->ShopInfoGundesign(Cosmetics, m_pPrice, m_pLevel, PreviewPos);
		if(Success && Cosmetics >= 0 && Cosmetics < CCosmeticsHandler::NUM_GUNDESIGNS)
			m_pCosmeticName = CCosmeticsHandler::ms_GundesignNames[Cosmetics];
		break;
	case CATEGORY_UTILITY:
		Success = m_pCosmeticsHandler->ShopInfoUtility(Cosmetics, m_pPrice, m_pLevel, PreviewPos);
		if(Success)
			m_pCosmeticName = UtilityName(Cosmetics);
		break;
	default:
		return false;
	}
	return Success;
}

void CShop::OnTick()
{
	if(GameServer()->Server()->Tick() >= m_pExpireTick)
	{
		Expire();
	}
}

void CShop::Expire()
{
	if(!m_pOwner || !m_pGameContext)
	{
		dbg_msg("shop", "Expire called on invalid shop object");
		delete this;
		return;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Your buying time for '%s' has expired. Aborting purchase..", m_pCosmeticName);
	m_pGameContext->SendChatTarget(m_pOwner->GetCid(), aBuf);
	Destroy(true);
}

void CShop::Destroy(bool Silent)
{
	if(m_pGameContext && m_pOwner)
	{
		dbg_msg("shop", "destroying purchase from %s", m_pGameContext->Server()->ClientName(m_pOwner->GetCid()));

		if(!Silent)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Your purchase for '%s' has been aborted.", m_pCosmeticName);
			m_pGameContext->SendChatTarget(m_pOwner->GetCid(), aBuf);
		}

		if(m_pOwner->GetCharacter())
			m_pOwner->GetCharacter()->Bw().m_PendingPurchase = nullptr;
	}
	else
	{
		dbg_msg("shop", "destroying purchase: missing game context or owner");
	}

	delete this;
}

void CShop::Purchase()
{
	if(!m_pOwner || !m_pGameContext)
	{
		dbg_msg("shop", "Purchase called on invalid shop object");
		delete this;
		return;
	}

	if(!m_pOwner->Bw().IsLoggedIn())
	{
		m_pGameContext->SendChatTarget(m_pOwner->GetCid(), "You are not logged in yet.");
		Destroy(true);
		return;
	}

	if(m_pPrice > m_pOwner->Bw().GetPlayerBlockpoints())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You do not have enough blockpoints for '%s'. You need %d blockpoints, but you only have %d bp.", m_pCosmeticName, m_pPrice, m_pOwner->Bw().GetPlayerBlockpoints());
		GameServer()->Bw().SendChatTarget(m_pOwner->GetCid(), aBuf);
		Destroy(false);
		return;
	}
	else if(m_pLevel > m_pOwner->Bw().GetPlayerLevel())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You need to be at least level %d to purchase this item.", m_pLevel);
		GameServer()->Bw().SendChatTarget(m_pOwner->GetCid(), aBuf);
		Destroy(false);
		return;
	}

	m_pOwner->Bw().SetPlayerBlockpoints(m_pOwner->Bw().GetPlayerBlockpoints() - m_pPrice);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "You have successfully bought '%s'.", m_pCosmeticName);
	GameServer()->Bw().SendChatTarget(m_pOwner->GetCid(), aBuf);

	switch(m_pCategory)
	{
	case CATEGORY_KNOCKOUT:
		m_pOwner->Bw().SetPlayerKnockouts(m_pProduct, '1');
		break;

	case CATEGORY_GUNDESIGN:
		m_pOwner->Bw().SetPlayerGundesign(m_pProduct, '1');
		break;

	case CATEGORY_SKINMANI:
		m_pOwner->Bw().SetPlayerSkinmani(m_pProduct, '1');
		break;

	case CATEGORY_UTILITY:
		GrantUtility(m_pGameContext, m_pOwner, m_pProduct);
		break;

	default:
		GameServer()->Bw().SendChatTarget(m_pOwner->GetCid(), "Unknown category.");
		break;
	}
	GameServer()->Bw().ClearVotes(m_pOwner->GetCid());
	GameServer()->ProgressVoteOptions(m_pOwner->GetCid());
	Destroy(true);
}

void CShop::Decline()
{
	Destroy(false);
}

bool CShop::InstantPurchase(CGameContext *pGameContext, CPlayer *pOwner, int Category, int Cosmetics)
{
	if(!pGameContext || !pOwner)
		return false;

	CCosmeticsHandler *pCosmetics = pGameContext->Bw().Cosmetics();
	if(!pCosmetics)
		return false;

	int ClientId = pOwner->GetCid();

	if(!pOwner->Bw().IsLoggedIn())
	{
		pGameContext->SendChatTarget(ClientId, "You need to be logged in to make purchases.");
		return false;
	}

	// Validate index
	bool ValidIndex = false;
	switch(Category)
	{
	case CATEGORY_GUNDESIGN: ValidIndex = (Cosmetics >= 0 && Cosmetics < CCosmeticsHandler::NUM_GUNDESIGNS); break;
	case CATEGORY_KNOCKOUT: ValidIndex = (Cosmetics >= 0 && Cosmetics < CCosmeticsHandler::NUM_KNOCKOUTS); break;
	case CATEGORY_SKINMANI: ValidIndex = (Cosmetics >= 0 && Cosmetics < CCosmeticsHandler::NUM_SKINMANIS); break;
	case CATEGORY_UTILITY: ValidIndex = (Cosmetics >= 0 && Cosmetics < CCosmeticsHandler::NUM_UTILITY_ITEMS); break;
	default: break;
	}
	if(!ValidIndex)
	{
		pGameContext->SendChatTarget(ClientId, "Invalid cosmetic selection.");
		return false;
	}

	if(const char *pBlocked = PurchaseBlockedReason(pOwner, Category, Cosmetics))
	{
		pGameContext->SendChatTarget(ClientId, pBlocked);
		return false;
	}

	// Get price/level info
	int Price = 0, Level = 0;
	vec2 PreviewPos;
	const char *pName = "<unknown>";
	bool InfoOk = false;
	switch(Category)
	{
	case CATEGORY_SKINMANI:
		InfoOk = pCosmetics->ShopInfoSkinmani(Cosmetics, Price, Level, PreviewPos);
		if(InfoOk && Cosmetics < CCosmeticsHandler::NUM_SKINMANIS)
			pName = CCosmeticsHandler::ms_SkinmaniNames[Cosmetics];
		break;
	case CATEGORY_KNOCKOUT:
		InfoOk = pCosmetics->ShopInfoKnockout(Cosmetics, Price, Level, PreviewPos);
		if(InfoOk && Cosmetics < CCosmeticsHandler::NUM_KNOCKOUTS)
			pName = CCosmeticsHandler::ms_KnockoutNames[Cosmetics];
		break;
	case CATEGORY_GUNDESIGN:
		InfoOk = pCosmetics->ShopInfoGundesign(Cosmetics, Price, Level, PreviewPos);
		if(InfoOk && Cosmetics < CCosmeticsHandler::NUM_GUNDESIGNS)
			pName = CCosmeticsHandler::ms_GundesignNames[Cosmetics];
		break;
	case CATEGORY_UTILITY:
		InfoOk = pCosmetics->ShopInfoUtility(Cosmetics, Price, Level, PreviewPos);
		if(InfoOk)
			pName = UtilityName(Cosmetics);
		break;
	default: break;
	}
	if(!InfoOk || Price <= 0)
	{
		pGameContext->SendChatTarget(ClientId, "Could not retrieve product info or invalid price.");
		return false;
	}

	// Check balance
	if(Price > pOwner->Bw().GetPlayerBlockpoints())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You need %d blockpoints, but you only have %d bp.", Price, pOwner->Bw().GetPlayerBlockpoints());
		pGameContext->SendChatTarget(ClientId, aBuf);
		return false;
	}

	// Check level
	if(Level > pOwner->Bw().GetPlayerLevel())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You need to be at least level %d to purchase this item.", Level);
		pGameContext->SendChatTarget(ClientId, aBuf);
		return false;
	}

	// Deduct and grant
	pOwner->Bw().SetPlayerBlockpoints(pOwner->Bw().GetPlayerBlockpoints() - Price);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "You have successfully bought '%s'.", pName);
	pGameContext->SendChatTarget(ClientId, aBuf);

	switch(Category)
	{
	case CATEGORY_KNOCKOUT: pOwner->Bw().SetPlayerKnockouts(Cosmetics, '1'); break;
	case CATEGORY_GUNDESIGN: pOwner->Bw().SetPlayerGundesign(Cosmetics, '1'); break;
	case CATEGORY_SKINMANI: pOwner->Bw().SetPlayerSkinmani(Cosmetics, '1'); break;
	case CATEGORY_UTILITY: GrantUtility(pGameContext, pOwner, Cosmetics); break;
	default: break;
	}

	return true;
}
