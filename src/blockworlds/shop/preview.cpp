#include "preview.h"

#include "npcmanager.h"

#include <engine/server.h>

#include <game/server/entities/character.h>
#include <game/server/entities/pickup.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/bw_context.h>

#include <cmath> // for fabs

CShopPreview::CShopPreview() :
	m_pGameContext(nullptr),
	m_pCosmeticsHandler(nullptr),
	m_pPlayer(nullptr),
	m_LastPriceToggle(0),
	m_LastSlowEffects(0),
	m_LastMediumEffects(0),
	m_LastFastEffects(0),
	m_ShowPrice(true)
{
	m_pNpcManager = new CNpcManager();
}

CShopPreview::CShopPreview(CGameContext *pGameContext) :
	CShopPreview() // delegate to the default constructor - dont understand ddnet code but its needed smh
{
	m_pGameContext = pGameContext;
	m_pNpcManager->Init(pGameContext);
}

CShopPreview::~CShopPreview()
{
	delete m_pNpcManager;
	m_pNpcManager = nullptr;
}

void CShopPreview::Init(CGameContext *pGameServer)
{
	m_pGameContext = pGameServer;
	m_LastPriceToggle = 0;
	m_LastSlowEffects = 0;
	m_LastMediumEffects = 0;
	m_LastFastEffects = 0;
	m_ShowPrice = true;
	m_vPickups.clear();
	if(m_pNpcManager)
		m_pNpcManager->Init(pGameServer);
}

void CShopPreview::Tick()
{
	int Tick = GameServer()->Server()->Tick();
	int Rate = GameServer()->Server()->TickSpeed();

	// === 3-second cycle: price/level text toggle, skin manipulations, persistent animations ===
	if(Tick - m_LastPriceToggle >= Rate * 3)
	{
		m_LastPriceToggle = Tick;
		m_ShowPrice = !m_ShowPrice;
		DisplayPricesAndLevels();
	}

	if(Tick - m_LastSlowEffects >= Rate * 3)
	{
		m_LastSlowEffects = Tick;
		DisplaySlowKnockouts();
		DisplayPickupGundesigns();
		DisplaySkinManipulations();
	}

	// === 1-second cycle: laserwrite knockouts, noisy instant knockouts ===
	if(Tick - m_LastMediumEffects >= Rate)
	{
		m_LastMediumEffects = Tick;
		DisplayMediumKnockouts();
	}

	// === 0.25-second cycle: silent damage-indicator knockouts, burst gundesigns ===
	if(Tick - m_LastFastEffects >= Rate / 4)
	{
		m_LastFastEffects = Tick;
		DisplayFastGundesigns();
		DisplayFastKnockouts();
	}

	// maintain NPC positions and properties every tick
	if(m_pNpcManager)
		m_pNpcManager->Tick();
}

// ─── Gun Designs ──────────────────────────────────────────────────────────────

void CShopPreview::DisplayPickupGundesigns()
{
	int NumGundesigns = CCosmeticsHandler::NUM_GUNDESIGNS;
	if((int)m_vPickups.size() < NumGundesigns)
		m_vPickups.resize(NumGundesigns, nullptr);

	for(int i = 0; i < NumGundesigns; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(!m_pGameContext->Bw().Cosmetics()->ShopInfoGundesign(i, Price, Level, PreviewPos))
			continue;

		if(m_vPickups[i] != nullptr)
			continue;

		if(i == CCosmeticsHandler::GUNDESIGN_ARMOR)
		{
			m_vPickups[i] = new CPickup(&GameServer()->m_World, 1, 1, 1, 0, 0);
			m_vPickups[i]->m_Pos = PreviewPos;
		}
		else if(i == CCosmeticsHandler::GUNDESIGN_HEART)
		{
			m_vPickups[i] = new CPickup(&GameServer()->m_World, 0, 0, 1, 0, 0);
			m_vPickups[i]->m_Pos = PreviewPos;
		}
		else if(i == CCosmeticsHandler::GUNDESIGN_BLINKING)
		{
			m_vPickups[i] = new CPickup(&GameServer()->m_World, CGameWorld::ENTTYPE_PICKUP, 1, 1, 0, 0);
			m_vPickups[i]->m_Pos = PreviewPos;
		}
	}
}

// Fast gun designs: star bursts, PEW, damage-indicator effects — all very short-lived
void CShopPreview::DisplayFastGundesigns()
{
	for(int i = 0; i < CCosmeticsHandler::NUM_GUNDESIGNS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(!m_pGameContext->Bw().Cosmetics()->ShopInfoGundesign(i, Price, Level, PreviewPos))
			continue;

		switch(i)
		{
		case CCosmeticsHandler::GUNDESIGN_CLOCKWISE:
		case CCosmeticsHandler::GUNDESIGN_COUNTERCLOCK:
		case CCosmeticsHandler::GUNDESIGN_TWOCLOCK:
		case CCosmeticsHandler::GUNDESIGN_REVERSE:
		case CCosmeticsHandler::GUNDESIGN_STARX:
		case CCosmeticsHandler::GUNDESIGN_VIP_STARGUN:
		case CCosmeticsHandler::GUNDESIGN_SHURIKEN:
		case CCosmeticsHandler::GUNDESIGN_SPARKLER:
		case CCosmeticsHandler::GUNDESIGN_PEW:
			GameServer()->Bw().Cosmetics()->DoGundesignRaw(PreviewPos, i, vec2(1, 0));
			break;
		default:
			break;
		}
	}
}

// ─── Knockout Effects ─────────────────────────────────────────────────────────

// Slow knockouts: Love (3s), Thunderstorm (3s), Splash (shrinks over ~1s)
void CShopPreview::DisplaySlowKnockouts()
{
	static const int SlowKnockouts[] = {
		CCosmeticsHandler::KNOCKOUT_LOVE,
		CCosmeticsHandler::KNOCKOUT_THUNDERSTORM,
		CCosmeticsHandler::KNOCKOUT_VIP_SPLASH,
	};

	for(int Ko : SlowKnockouts)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(m_pGameContext->Bw().Cosmetics()->ShopInfoKnockout(Ko, Price, Level, PreviewPos))
		{
			GameServer()->Bw().Cosmetics()->DoKnockoutEffectRaw(PreviewPos, Ko);
		}
	}
}

// medium knockouts: laserwrite effects + noisy instant effects (explosion, starexplosion, hammerhit)
void CShopPreview::DisplayMediumKnockouts()
{
	static const int MediumKnockouts[] = {
		CCosmeticsHandler::KNOCKOUT_KORIP,
		CCosmeticsHandler::KNOCKOUT_KOEZ,
		CCosmeticsHandler::KNOCKOUT_KONOOB,
		CCosmeticsHandler::KNOCKOUT_SORRY,
		CCosmeticsHandler::KNOCKOUT_PRO,
		CCosmeticsHandler::KNOCKOUT_GG,
		CCosmeticsHandler::KNOCKOUT_EXPLOSION,
		CCosmeticsHandler::KNOCKOUT_STAREXPLOSION,
		CCosmeticsHandler::KNOCKOUT_HAMMERHIT,
	};

	for(int Ko : MediumKnockouts)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(m_pGameContext->Bw().Cosmetics()->ShopInfoKnockout(Ko, Price, Level, PreviewPos))
		{
			GameServer()->Bw().Cosmetics()->DoKnockoutEffectRaw(PreviewPos, Ko);
		}
	}
}

// fast knockouts: silent single-frame damage-indicator effects that need frequent refresh
void CShopPreview::DisplayFastKnockouts()
{
	static const int FastKnockouts[] = {
		CCosmeticsHandler::KNOCKOUT_KOSTARS,
		CCosmeticsHandler::KNOCKOUT_STARRING,
	};

	for(int Ko : FastKnockouts)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(m_pGameContext->Bw().Cosmetics()->ShopInfoKnockout(Ko, Price, Level, PreviewPos))
		{
			GameServer()->Bw().Cosmetics()->DoKnockoutEffectRaw(PreviewPos, Ko);
		}
	}
}

// ─── Skin Manipulations ──────────────────────────────────────────────────────

void CShopPreview::DisplaySkinManipulations()
{
	int Num = CCosmeticsHandler::NUM_SKINMANIS;
	if(m_pNpcManager)
		m_pNpcManager->Resize(Num);

	for(int i = 0; i < Num; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;

		if(!m_pGameContext->Bw().Cosmetics()->ShopInfoSkinmani(i, Price, Level, PreviewPos))
			continue;

		if(m_pNpcManager)
			m_pNpcManager->EnsureNpcAndApplySkinmani(i, PreviewPos, CCosmeticsHandler::ms_SkinmaniNames[i]);
	}
}

// ─── Price / Level Labels ────────────────────────────────────────────────────

void CShopPreview::DisplayPriceLevel(const vec2 &PreviewPos, int Price, int Level)
{
	char aBuf[128];
	if(m_ShowPrice)
		str_format(aBuf, sizeof(aBuf), "BP: %d", Price);
	else
		str_format(aBuf, sizeof(aBuf), "LVL: %d", Level);

	vec2 TextPos = PreviewPos - vec2(0, -100.0f);
	GameServer()->Bw().Animations()->RemoveAnimationsNear(TextPos, 200.0f);

	const int Lifetime = GameServer()->Server()->TickSpeed() * 3;
	GameServer()->Bw().Animations()->Laserwrite(aBuf, TextPos, 5.0f, Lifetime, true);
}

void CShopPreview::DisplayPricesAndLevels()
{
	for(int i = 0; i < CCosmeticsHandler::NUM_GUNDESIGNS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(m_pGameContext->Bw().Cosmetics()->ShopInfoGundesign(i, Price, Level, PreviewPos))
			DisplayPriceLevel(PreviewPos, Price, Level);
	}

	for(int i = 0; i < CCosmeticsHandler::NUM_KNOCKOUTS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(m_pGameContext->Bw().Cosmetics()->ShopInfoKnockout(i, Price, Level, PreviewPos))
			DisplayPriceLevel(PreviewPos, Price, Level);
	}

	for(int i = 0; i < CCosmeticsHandler::NUM_SKINMANIS; i++)
	{
		int Price = 0, Level = 0;
		vec2 PreviewPos;
		if(m_pGameContext->Bw().Cosmetics()->ShopInfoSkinmani(i, Price, Level, PreviewPos))
			DisplayPriceLevel(PreviewPos, Price, Level);
	}
}
