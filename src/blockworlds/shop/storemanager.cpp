#include "storemanager.h"
#include <blockworlds/accounts.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CShop::CShop(CGameContext *pGameContext, CPlayer *pOwner, int pCategory, int pCosmetics, int ExpireInS) :
	m_pGameContext(pGameContext), m_pOwner(pOwner), m_pProduct(pCosmetics), m_pCategory(pCategory)
{
	// initialize cosmetics handler pointer and a safe default name
	m_pCosmeticsHandler = pGameContext ? pGameContext->Cosmetics() : nullptr;
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

	if(!pOwner->IsLoggedIn())
	{
		if(m_pGameContext)
		{
			m_pGameContext->SendChatTarget(pOwner->GetCid(), "You need to be logged in to make purchases.");
			m_pGameContext->SendChatTarget(pOwner->GetCid(), "Use /accounts for information on the account system.");
		}
		Destroy(true);
		return;
	}

	bool validIndex = false;
	switch(pCategory)
	{
	case CATEGORY_GUNDESIGN:
		validIndex = (pCosmetics >= 0 && pCosmetics < CCosmeticsHandler::NUM_GUNDESIGNS);
		break;
	case CATEGORY_KNOCKOUT:
		validIndex = (pCosmetics >= 0 && pCosmetics < CCosmeticsHandler::NUM_KNOCKOUTS);
		break;
	case CATEGORY_SKINMANI:
		validIndex = (pCosmetics >= 0 && pCosmetics < CCosmeticsHandler::NUM_SKINMANIS);
		break;
	case CATEGORY_UTILITY:
		validIndex = (pCosmetics >= 0 && pCosmetics < CCosmeticsHandler::NUM_UTILITY_ITEMS);
		break;
	default:
		break;
	}
	if(!validIndex)
	{
		if(m_pGameContext)
			m_pGameContext->SendChatTarget(pOwner->GetCid(), "Invalid cosmetic selection.");
		Destroy(true);
		return;
	}

	bool HasCosmetic = false;
	switch(pCategory)
	{
	case CATEGORY_GUNDESIGN:
		HasCosmetic = (pOwner->GetPlayerGundesign()[pCosmetics] == '1');
		break;
	case CATEGORY_KNOCKOUT:
		HasCosmetic = (pOwner->GetPlayerKnockouts()[pCosmetics] == '1');
		break;
	case CATEGORY_SKINMANI:
		HasCosmetic = (pOwner->GetPlayerSkinmani()[pCosmetics] == '1');
		break;
	case CATEGORY_UTILITY:
		break;
	default:
		if(m_pGameContext)
			m_pGameContext->SendChatTarget(pOwner->GetCid(), "Invalid cosmetic category.");
		Destroy(true);
		return;
	}
	if(HasCosmetic)
	{
		if(m_pGameContext)
			m_pGameContext->SendChatTarget(pOwner->GetCid(), "You already own this cosmetic.");
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
		if(pChar->m_PendingPurchase)
		{
			pChar->m_PendingPurchase->Destroy(false);
		}
		pChar->m_PendingPurchase = this;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Do you want to buy '%s' for %d blockpoints? (/yes, /no)", m_pCosmeticName, m_pPrice);
		GameServer()->SendChatTarget(m_pOwner->GetCid(), aBuf);
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
		{
			if(Cosmetics == 0)
				m_pCosmeticName = "Weapon Kit";
			else if(Cosmetics == 1)
				m_pCosmeticName = "Deathnote Page";
			else
				m_pCosmeticName = "Utility Item";
		}
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
			m_pOwner->GetCharacter()->m_PendingPurchase = nullptr;
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

	if(!m_pOwner->IsLoggedIn())
	{
		m_pGameContext->SendChatTarget(m_pOwner->GetCid(), "You are not logged in yet.");
		Destroy(true);
		return;
	}

	if(m_pPrice > m_pOwner->GetPlayerBlockpoints())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You do not have enough blockpoints for '%s'. You need %d blockpoints, but you only have %d bp.", m_pCosmeticName, m_pPrice, m_pOwner->GetPlayerBlockpoints());
		GameServer()->SendChatTarget(m_pOwner->GetCid(), aBuf);
		Destroy(false);
		return;
	}
	else if(m_pLevel > m_pOwner->GetPlayerLevel())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You need to be at least level %d to purchase this item.", m_pLevel);
		GameServer()->SendChatTarget(m_pOwner->GetCid(), aBuf);
		Destroy(false);
		return;
	}

	m_pOwner->SetPlayerBlockpoints(m_pOwner->GetPlayerBlockpoints() - m_pPrice);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "You have successfully bought '%s'.", m_pCosmeticName);
	GameServer()->SendChatTarget(m_pOwner->GetCid(), aBuf);

	switch(m_pCategory)
	{
	case CATEGORY_KNOCKOUT:
		m_pOwner->SetPlayerKnockouts(m_pProduct, '1');
		break;

	case CATEGORY_GUNDESIGN:
		m_pOwner->SetPlayerGundesign(m_pProduct, '1');
		break;

	case CATEGORY_SKINMANI:
		m_pOwner->SetPlayerSkinmani(m_pProduct, '1');
		break;

	case CATEGORY_UTILITY:
		// product ids: 0 = weaponkit, 1 = deathnote page
		if(m_pProduct == 0)
		{
			m_pOwner->SetPlayerWeaponkits(m_pOwner->GetPlayerWeaponkits() + 1);
		}
		else if(m_pProduct == 1)
		{
			// grant one deathnote page as an account page
			m_pOwner->SetPlayerPages(m_pOwner->GetPlayerPages() + 1);
		}
		break;

	default:
		GameServer()->SendChatTarget(m_pOwner->GetCid(), "Unknown category.");
		break;
	}
	GameServer()->ClearVotes(m_pOwner->GetCid());
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

	CCosmeticsHandler *pCosmetics = pGameContext->Cosmetics();
	if(!pCosmetics)
		return false;

	int ClientId = pOwner->GetCid();

	if(!pOwner->IsLoggedIn())
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

	// Check already owned
	bool HasCosmetic = false;
	switch(Category)
	{
	case CATEGORY_GUNDESIGN: HasCosmetic = (pOwner->GetPlayerGundesign()[Cosmetics] == '1'); break;
	case CATEGORY_KNOCKOUT: HasCosmetic = (pOwner->GetPlayerKnockouts()[Cosmetics] == '1'); break;
	case CATEGORY_SKINMANI: HasCosmetic = (pOwner->GetPlayerSkinmani()[Cosmetics] == '1'); break;
	case CATEGORY_UTILITY: break;
	default: break;
	}
	if(HasCosmetic)
	{
		pGameContext->SendChatTarget(ClientId, "You already own this cosmetic.");
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
		{
			if(Cosmetics == 0)
				pName = "Weapon Kit";
			else if(Cosmetics == 1)
				pName = "Deathnote Page";
			else
				pName = "Utility Item";
		}
		break;
	default: break;
	}
	if(!InfoOk || Price <= 0)
	{
		pGameContext->SendChatTarget(ClientId, "Could not retrieve product info or invalid price.");
		return false;
	}

	// Check balance
	if(Price > pOwner->GetPlayerBlockpoints())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You need %d blockpoints, but you only have %d bp.", Price, pOwner->GetPlayerBlockpoints());
		pGameContext->SendChatTarget(ClientId, aBuf);
		return false;
	}

	// Check level
	if(Level > pOwner->GetPlayerLevel())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You need to be at least level %d to purchase this item.", Level);
		pGameContext->SendChatTarget(ClientId, aBuf);
		return false;
	}

	// Deduct and grant
	pOwner->SetPlayerBlockpoints(pOwner->GetPlayerBlockpoints() - Price);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "You have successfully bought '%s'.", pName);
	pGameContext->SendChatTarget(ClientId, aBuf);

	switch(Category)
	{
	case CATEGORY_KNOCKOUT: pOwner->SetPlayerKnockouts(Cosmetics, '1'); break;
	case CATEGORY_GUNDESIGN: pOwner->SetPlayerGundesign(Cosmetics, '1'); break;
	case CATEGORY_SKINMANI: pOwner->SetPlayerSkinmani(Cosmetics, '1'); break;
	case CATEGORY_UTILITY:
		if(Cosmetics == 0)
			pOwner->SetPlayerWeaponkits(pOwner->GetPlayerWeaponkits() + 1);
		else if(Cosmetics == 1)
			pOwner->SetPlayerPages(pOwner->GetPlayerPages() + 1);
		break;
	default: break;
	}

	return true;
}
