#include "bw_character.h"

#include <blockworlds/bw_base.h>
#include <blockworlds/bw_context.h>

#include <engine/server.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/bw_player.h>
#include <blockworlds/common.h>
#include <blockworlds/cosmetics/cosmetics.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/components/events/bombtag.h>
#include <blockworlds/shop/storemanager.h>

#include <engine/shared/config.h>

#include <algorithm>

#include <base/secure.h>

#include <cmath>

CGameContext *CBwCharacter::GameServer() const { return m_pCharacter->GameServer(); }
IServer *CBwCharacter::Server() const { return m_pCharacter->Server(); }
CCharacterCore &CBwCharacter::Core() const { return m_pCharacter->m_Core; }
const CNetObj_PlayerInput &CBwCharacter::GetInput() const { return m_pCharacter->m_Input; }
const CNetObj_PlayerInput &CBwCharacter::GetLatestInput() const { return m_pCharacter->m_LatestInput; }

void CBwCharacter::Reset()
{
	m_FrozenSinceTick = 0;
	m_FrozenAndUnmovedSinceTick = 0;
	m_FrozenAndUntouchedSinceTick = 0;
	m_CurrentKillingSpree = 0;
	m_AliveSince = 0;
	m_FrozenSince = 0;
	m_LastToucher = -1;
	m_LastTouchingWeapon = -1;
	m_KillStreak = 0;
	m_HookRainbowDivider = 1.0f;
	m_HookedBy = -1;
	m_IsOnPassiveTile = false;
	m_IsOnRandomCosmeticTile = false;
	m_PendingPurchase = nullptr;
	m_LastShopTick = 0;
	m_TelekinesisHeld = false;
	m_TelekinesisTargetPos = vec2(0, 0);
}

void CBwCharacter::StartHookRainbow(int DurationTicks, float RateDivider, int HookerId)
{
	m_HookRainbowDivider = RateDivider > 0.0f ? RateDivider : 1.0f;
	m_HookedBy = HookerId;
}

bool CBwCharacter::IsHookRainbowActive() const
{
	if(m_HookedBy < 0 || m_HookedBy >= MAX_CLIENTS)
		return false;
	CCharacter *pHookerChr = GameServer()->GetPlayerChar(m_HookedBy);
	if(!pHookerChr)
		return false;
	return pHookerChr->Core()->HookedPlayer() == m_pCharacter->GetPlayer()->GetCid();
}

void CBwCharacter::FreezeForce(int Seconds)
{
	if(Seconds <= 0)
		return;
	FreezeForce(static_cast<float>(Seconds));
}

void CBwCharacter::FreezeForce(float Seconds)
{
	if(Seconds <= 0.f || m_pCharacter->m_Core.m_Super || m_pCharacter->m_Core.m_Invincible)
		return;

	m_pCharacter->m_Armor = 0;
	m_pCharacter->m_FreezeTime = static_cast<int>(std::ceil(Seconds * static_cast<float>(Server()->TickSpeed())));
	m_pCharacter->m_Core.m_FreezeStart = Server()->Tick();
	GameServer()->Bw().BlockTracker().OnPlayerFreeze(m_pCharacter->GetPlayer()->GetCid());
}

void CBwCharacter::OnHandleTiles(int TileIndex, int TileFIndex)
{
	CPlayer *pPlayer = m_pCharacter->GetPlayer();
	const int ClientId = pPlayer->GetCid();
	const auto OnTile = [&](int Tile) { return TileIndex == Tile || TileFIndex == Tile; };

	if(OnTile(TILE_BW_VIP))
	{
		if(!pPlayer->Bw().IsLoggedIn())
		{
			GameServer()->Bw().SendChatTarget(ClientId, "You need to be logged in to access this zone!");
			m_pCharacter->Die(ClientId, WEAPON_WORLD);
			return;
		}
		if(!pPlayer->Bw().GetPlayerVip())
		{
			GameServer()->Bw().SendChatTarget(ClientId, "You need to be a VIP to access this zone!");
			m_pCharacter->Die(ClientId, WEAPON_WORLD);
			return;
		}
	}

	// the tile fires once on entry, not every tick you stand on it
	const bool OnPassiveTile = OnTile(TILE_BW_PASSIVE);
	if(OnPassiveTile && !m_IsOnPassiveTile)
	{
		if(pPlayer->Bw().m_PassiveRaceCooldown > 0)
		{
			// still on cooldown: promise it instead of granting it
			char aTime[64];
			CBlockworlds::FormatDuration(pPlayer->Bw().m_PassiveRaceCooldown, aTime, sizeof(aTime));
			GameServer()->Bw().SendChatTarget(ClientId, "Wayblock Protection unlocked (in %s) for 2 hours!", aTime);
			pPlayer->Bw().m_PassivePendingGrant = true;
		}
		else
		{
			GameServer()->Bw().SendChatTarget(ClientId, "Wayblock Protection unlocked for 2 hours!");
			// a guest keeps it for the session only, since there is no account to store it on
			if(!pPlayer->Bw().IsLoggedIn())
				pPlayer->Bw().m_LocalPassiveDuration = PASSIVE_TILE_DURATION;
			else
				pPlayer->Bw().SetPlayerPassive(PASSIVE_TILE_DURATION);
		}
	}
	m_IsOnPassiveTile = OnPassiveTile;

	const bool OnRandomCosmeticTile = OnTile(TILE_BW_RANDOM_COSMETIC);
	if(OnRandomCosmeticTile && !m_IsOnRandomCosmeticTile)
	{
		const int SkinMani = secure_rand_below(CCosmeticsHandler::NUM_SKINMANIS);
		const int Knockout = secure_rand_below(CCosmeticsHandler::NUM_KNOCKOUTS);
		const int GunDesign = secure_rand_below(CCosmeticsHandler::NUM_GUNDESIGNS);

		// remembered separately so OnPlayerTick can take them back when they run out
		pPlayer->Bw().m_RandomCosmeticDuration = RANDOM_COSMETIC_DURATION;
		pPlayer->Bw().m_RandomCosmeticSkinmani = SkinMani;
		pPlayer->Bw().m_RandomCosmeticKnockout = Knockout;
		pPlayer->Bw().m_RandomCosmeticGundesign = GunDesign;

		pPlayer->Bw().SetSkinMani(SkinMani);
		pPlayer->Bw().SetKnockout(Knockout);
		pPlayer->Bw().SetGunDesign(GunDesign);

		GameServer()->Bw().SendChatTarget(ClientId,
			"\xE2\x9C\xA8 Random cosmetics activated for 10 minutes! Skinmani: %s, Knockout: %s, Gundesign: %s",
			CCosmeticsHandler::ms_SkinmaniNames[SkinMani],
			CCosmeticsHandler::ms_KnockoutNames[Knockout],
			CCosmeticsHandler::ms_GundesignNames[GunDesign]);
	}
	m_IsOnRandomCosmeticTile = OnRandomCosmeticTile;
}

bool CBwCharacter::BlocksFire(bool FrozenLastTick) const
{
	// a passive or protected tee is out of the fight in both directions
	if(Core().BwNoContact())
		return true;
	// hammering on the very tick you unfreeze is how you spam somebody who just
	// got out; the delay costs a legitimate player nothing
	return g_Config.m_SvNoHammerOnUnfreeze && FrozenLastTick &&
	       m_pCharacter->Core()->m_ActiveWeapon == WEAPON_HAMMER;
}

bool CBwCharacter::OnHammerHit(CCharacter *pTarget)
{
	if(pTarget->Core()->BwNoContact())
		return false;

	CPlayer *pPlayer = m_pCharacter->GetPlayer();
	const int ClientId = pPlayer->GetCid();
	const int TargetId = pTarget->GetPlayer()->GetCid();

	if(pPlayer->Bw().m_BanhammerActive && !pTarget->GetPlayer()->Bw().m_IsDummy)
	{
		// one strike per activation
		pPlayer->Bw().m_BanhammerActive = false;
		const int BanSeconds = g_Config.m_SvBanhammerDuration;
		const vec2 TargetPos = pTarget->m_Pos;
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "\xF0\x9F\x94\xA8 %s has been struck by the BANHAMMER! Banned for %d seconds.",
			Server()->ClientName(TargetId), BanSeconds);
		GameServer()->SendChat(-1, TEAM_ALL, aBuf);
		GameServer()->CreateExplosion(TargetPos, -1, WEAPON_GRENADE, true, -1);
		GameServer()->CreateSoundGlobal(SOUND_GRENADE_EXPLODE);
		Server()->Ban(TargetId, BanSeconds, "Struck by the Banhammer!", false);
		return false; // the target is gone: no unfreeze, no block credit
	}

	GameServer()->Bw().BlockTracker().OnPlayerImpacted(TargetId, ClientId);
	if(auto pEvents = g_ComponentRegistry.Get<CEvents>())
	{
		if(auto pActive = pEvents->GetActiveEvent())
			pActive->OnPlayerImpacted(TargetId, ClientId);
	}
	return true;
}

bool CBwCharacter::HammerUnfreezes() const
{
	// in BombTag a hammer passes the bomb rather than freeing the tee
	if(auto pEvents = g_ComponentRegistry.Get<CEvents>())
	{
		for(const auto &Sub : pEvents->GetSubComponents())
		{
			if(auto *pBombTag = dynamic_cast<CBombTagEvent *>(Sub.operator->()))
			{
				const auto &Participants = pBombTag->Participants();
				return std::find(Participants.begin(), Participants.end(),
					       m_pCharacter->GetPlayer()->GetCid()) == Participants.end();
			}
		}
	}
	return true;
}

void CBwCharacter::OnHookAttach(int HookedPlayer)
{
	const int ClientId = m_pCharacter->GetPlayer()->GetCid();
	GameServer()->Bw().BlockTracker().OnPlayerImpacted(HookedPlayer, ClientId);
	if(auto pEvents = g_ComponentRegistry.Get<CEvents>())
	{
		if(auto pActive = pEvents->GetActiveEvent())
			pActive->OnPlayerImpacted(HookedPlayer, ClientId);
	}

	// the VIP hook cosmetic colours whoever you catch, for as long as you hold on
	CPlayer *pHooker = GameServer()->Bw().GetPlayer(ClientId);
	CCharacter *pHookedChar = GameServer()->GetPlayerChar(HookedPlayer);
	if(pHooker && pHookedChar && pHooker->Bw().GetSkinMani() == CCosmeticsHandler::SKINMANI_VIP_HOOK_RAINBOW)
		pHookedChar->Bw().StartHookRainbow(Server()->TickSpeed() * 5, 0.5f, ClientId);
}

void CBwCharacter::OnTick()
{
	if(m_PendingPurchase)
		m_PendingPurchase->OnTick();
}

void CBwCharacter::HandleGrenadeAmmoRegen()
{
	CCharacterCore &TheCore = Core();
	// unlimited ammo needs no regen
	if(TheCore.m_aWeapons[WEAPON_GRENADE].m_Ammo < 0)
		return;

	if(m_pCharacter->m_ReloadTimer > 0)
	{
		TheCore.m_aWeapons[WEAPON_GRENADE].m_AmmoRegenStart = -1;
		return;
	}

	if(TheCore.m_aWeapons[WEAPON_GRENADE].m_AmmoRegenStart < 0)
		TheCore.m_aWeapons[WEAPON_GRENADE].m_AmmoRegenStart = Server()->Tick();

	// one grenade per second
	const int AmmoRegenTime = 1000;
	if(Server()->Tick() - TheCore.m_aWeapons[WEAPON_GRENADE].m_AmmoRegenStart >= AmmoRegenTime * Server()->TickSpeed() / 1000)
	{
		TheCore.m_aWeapons[WEAPON_GRENADE].m_Ammo =
			std::min(TheCore.m_aWeapons[WEAPON_GRENADE].m_Ammo + 1, g_Config.m_SvZCatchGrenadeMaxAmmo);
		TheCore.m_aWeapons[WEAPON_GRENADE].m_AmmoRegenStart = -1;
	}
}

void CBwCharacter::ApplyTelekinesisInput()
{
	if(!m_TelekinesisHeld)
		return;
	Core().m_Tuning.m_Gravity = 0.0f;
	CNetObj_PlayerInput &Input = const_cast<CNetObj_PlayerInput &>(GetInput());
	Input.m_Direction = 0;
	Input.m_Jump = 0;
	Input.m_Hook = 0;
}

void CBwCharacter::ApplyTelekinesisMove()
{
	if(!m_TelekinesisHeld)
		return;
	Core().m_Pos = m_TelekinesisTargetPos;
	Core().m_Vel = vec2(0, 0);
	m_pCharacter->m_Pos = m_TelekinesisTargetPos;
	// one move per command; the admin re-issues it to keep dragging
	m_TelekinesisHeld = false;
}

void CBwCharacter::OnReleaseHook()
{
	const int HookedPlayer = Core().HookedPlayer();

	m_HookRainbowDivider = 1.0f;
	m_HookedBy = -1;

	// the tee we were holding stops being painted by us
	if(HookedPlayer >= 0 && HookedPlayer < MAX_CLIENTS)
	{
		CCharacter *pHooked = GameServer()->GetPlayerChar(HookedPlayer);
		if(pHooked && pHooked->Bw().m_HookedBy == m_pCharacter->GetPlayer()->GetCid())
		{
			pHooked->Bw().m_HookRainbowDivider = 1.0f;
			pHooked->Bw().m_HookedBy = -1;
		}
	}
}

void CBwCharacter::OnDestroy()
{
	if(m_PendingPurchase)
		m_PendingPurchase->Destroy(false);
}

void CBwCharacter::OnSnapDDNetCharacter(CNetObj_DDNetCharacter *pDDNetCharacter) const
{
	// passive tees pass through everybody, which is what solo looks like
	if(Core().m_Passive)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_SOLO;
	if(Core().m_Protected)
	{
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_COLLISION_DISABLED;
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_HOOK_HIT_DISABLED;
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_HAMMER_HIT_DISABLED;
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_SHOTGUN_HIT_DISABLED;
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_GRENADE_HIT_DISABLED;
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_LASER_HIT_DISABLED;
	}
}
