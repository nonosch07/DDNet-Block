#include "storemanager.h"
#include <game/server/blockworlds/accounts.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CShop::CShop(CGameContext *pGameContext, CPlayer *pOwner, int pCategory, int pCosmetics, int ExpireInS) :
	m_pGameContext(pGameContext), m_pOwner(pOwner), m_pProduct(pCosmetics), m_pCategory(pCategory)
{
	m_pExpireTick = GameServer()->Server()->Tick() + ExpireInS * GameServer()->Server()->TickSpeed();

	// don't think that's needed but meh
	if(!pOwner->IsLoggedIn())
	{
		GameServer()->SendChatTarget(pOwner->GetCid(), "You need to be logged in to make purchases.");
		GameServer()->SendChatTarget(pOwner->GetCid(), "Use /accounts for information on the account system.");
		Destroy(true);
		return;
	}

	// check if the player already has the cosmetic
	bool HasCosmetic = false;
	int ClientID = pOwner->GetCid();
	switch(pCategory)
	{
	case CATEGORY_GUNDESIGN:
		HasCosmetic = m_pGameContext->Cosmetics()->HasGundesign(ClientID, pCosmetics);
		break;
	case CATEGORY_KNOCKOUT:
		HasCosmetic = m_pGameContext->Cosmetics()->HasKnockoutEffect(ClientID, pCosmetics);
		break;
	case CATEGORY_SKINMANI:
		HasCosmetic = m_pGameContext->Cosmetics()->HasSkinmani(ClientID, pCosmetics);
		break;
	default:
		GameServer()->SendChatTarget(pOwner->GetCid(), "Invalid cosmetic category.");
		Destroy(true);
		return;
	}

	if(HasCosmetic)
	{
		GameServer()->SendChatTarget(pOwner->GetCid(), "You already own this cosmetic.");
		Destroy(true);
		return;
	}

	if(!SetProductInfo(pCategory, pCosmetics))
	{
		Destroy(true);
		return;
	}

	if(m_pPrice == 0)
	{
		Destroy(true);
		return;
	}

	// check if there's an active purchase for the character and replace it if necessary
	if(pOwner->GetCharacter())
	{
		if(pOwner->GetCharacter()->m_PendingPurchase)
		{
			pOwner->GetCharacter()->m_PendingPurchase->Destroy(false);
		}
		pOwner->GetCharacter()->m_PendingPurchase = this;

		// send purchase confirmation message
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
	int Level;
	bool Success = false;

	switch(Category)
	{
	case CATEGORY_SKINMANI:
		Success = m_pCosmeticsHandler->ShopInfoSkinmani(Cosmetics, m_pPrice, Level);
		m_pCosmeticName = CCosmeticsHandler::ms_SkinmaniNames[Cosmetics];
		break;
	case CATEGORY_KNOCKOUT:
		Success = m_pCosmeticsHandler->ShopInfoKnockout(Cosmetics, m_pPrice, Level);
		m_pCosmeticName = CCosmeticsHandler::ms_KnockoutNames[Cosmetics];
		break;
	case CATEGORY_GUNDESIGN:
		Success = m_pCosmeticsHandler->ShopInfoGundesign(Cosmetics, m_pPrice, Level);
		m_pCosmeticName = CCosmeticsHandler::ms_GundesignNames[Cosmetics];
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
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Your buying time for '%s' has expired. Aborting purchase..", m_pCosmeticName);
	GameServer()->SendChatTarget(m_pOwner->GetCid(), aBuf);
	Destroy(true);
}

void CShop::Destroy(bool Silent)
{
	dbg_msg("shop", "destroying purchase from %s", GameServer()->Server()->ClientName(m_pOwner->GetCid()));

	if(!Silent)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Your purchase for '%s' has been aborted.", m_pCosmeticName);
		GameServer()->SendChatTarget(m_pOwner->GetCid(), aBuf);
	}

	if(m_pOwner && m_pOwner->GetCharacter())
		m_pOwner->GetCharacter()->m_PendingPurchase = nullptr;

	delete this;
}

void CShop::Purchase()
{
	if(!m_pOwner->IsLoggedIn())
	{
		GameServer()->SendChatTarget(m_pOwner->GetCid(), "You are not logged in yet.");
		Destroy(true);
		return;
	}

	if(m_pPrice > m_pOwner->GetPlayerBlockpoints())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You do not have enough blockpoints for '%s'. You need %d blockpoints, but you only have %d bp.", m_pCosmeticName, m_pPrice, m_pOwner->GetPlayerBlockpoints());
		GameServer()->SendChatTarget(m_pOwner->GetCid(), aBuf);
		Destroy(false);
	}
	else
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "You have successfully bought '%s'.", m_pCosmeticName);
		GameServer()->SendChatTarget(m_pOwner->GetCid(), aBuf);

		m_pOwner->SetPlayerBlockpoints(m_pOwner->GetPlayerBlockpoints() - m_pPrice);

		// // Grant cosmetics to player
		// m_pCosmeticsHandler->GrantCosmetic(m_pOwner->GetCID(), m_pCategory, m_pProduct);

		Destroy(true);
	}
}

void CShop::Decline()
{
	Destroy(false);
}
