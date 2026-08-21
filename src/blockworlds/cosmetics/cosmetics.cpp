#include "cosmetics.h"

#include <engine/server.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/bw_base.h>
#include <blockworlds/bw_context.h>
#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>
#include <blockworlds/cosmetics/animations.h>

#include <algorithm>

// TODO: move it all to db instead, add lua support
const char *CCosmeticsHandler::ms_KnockoutNames[NUM_KNOCKOUTS] = {
	"Explosion",
	"Hammerhit",
	"KO Stars",
	"Star Ring",
	"Starexplosion",
	"Thunderstorm",
	"KO RIP",
	"Love",
	"KO EZ",
	"KO NOOB",
	"KO PRO",
	"GG",

	"Sorry c: (Teemo)",
	"Splash (VIP)",
};

const char *CCosmeticsHandler::ms_GundesignNames[NUM_GUNDESIGNS] = {
	"Clockwise",
	"Counterclock",
	"TwoOClock",
	"Blinking Bullet",
	"StarX",
	"Reverse",
	"Armorgun",
	"Heartgun",
	"Pew",
	"Shuriken",
	"Sparkler",

	"1337 gun",
	"Stargun (VIP)",
};

const char *CCosmeticsHandler::ms_SkinmaniNames[NUM_SKINMANIS] = {
	"Feet Fire",
	"Feet Water",
	"Feet Poison",
	"Feet Blackwhite",
	"Feet RGB",
	"Feet CMY",
	"Body Fire",
	"Body Water",
	"Body Poison",
	"Dual Fire",
	"Dual Water",
	"Dual Poison",
	"Dual BlackWhite",
	"Sunset Fade",
	"Ocean Drift",
	"Aurora",

	"Nightblue",
	"Rainbow (VIP)",
	"Epi Rainbow (VIP)",
	"Hook Rainbow (VIP)",
	"Electric (VIP)"

};

// specials
static const char *g_SpecialNames[CCosmeticsHandler::NUM_SPECIALS] = {
	"Ball",
	"Crown",
	"Epic Circle",
	"Halo"};

bool CCosmeticsHandler::ToggleSpecial(int ClientID, const char *pName)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	if(m_pGameServer)
	{
		if(auto EventsAccessor = g_ComponentRegistry.Get<CEvents>(); EventsAccessor)
		{
			auto pEv = EventsAccessor->GetActiveEvent();
			if(pEv)
			{
				const char *pEvName = pEv->GetEventName();
				bool IsBlockedEvent = (str_comp(pEvName, "LMB") == 0) || (str_comp(pEvName, "Team Deathmatch") == 0);
				if(IsBlockedEvent)
				{
					const auto &Parts = pEv->Participants();
					if(std::find(Parts.begin(), Parts.end(), ClientID) != Parts.end())
					{
						m_pGameServer->SendChatTarget(ClientID, "Cosmetics are disabled during LMB/TDM.");
						return false;
					}
				}
			}
		}
	}

	std::string SName = pName ? pName : "";
	for(int i = 0; i < CCosmeticsHandler::NUM_SPECIALS; ++i)
	{
		if(str_comp_nocase(g_SpecialNames[i], SName.c_str()) == 0)
		{
			CPlayer *pPlayer = GameServer()->Bw().GetPlayer(ClientID);
			if(!pPlayer)
				return false;

			// NOTE: all remaining specials in this list are toggleable by players

			if(pPlayer->Bw().ToggleSpecial(i))
				return true;
			return false;
		}
	}

	return false;
}

bool CCosmeticsHandler::HasSpecial(int ClientID, int Index)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	if(Index < 0 || Index >= NUM_SPECIALS)
		return false;

	// Admins / authed clients should always be allowed to see/use specials
	if(Server()->GetAuthedState(ClientID) != AUTHED_NO)
		return true;
	if(!GameServer()->m_apPlayers[ClientID]->Bw().IsLoggedIn())
		return false;
	if(GameServer()->m_apPlayers[ClientID]->Bw().GetPlayerVip())
		return true;

	return false;
}

const char *CCosmeticsHandler::GetPlayerSpecials()
{
	return ""; // placeholder not used
}

static inline int HslToCc(vec3 HSL)
{
	return ((int)(HSL.h * 255) << 16) + ((int)(HSL.s * 255) << 8) + (HSL.l - 0.5f) * 255 * 2;
}

static inline vec3 CcToHsl(int Cc)
{
	return vec3(((Cc >> 16) & 0xff) / 255.0f, ((Cc >> 8) & 0xff) / 255.0f, 0.5f + (Cc & 0xff) / 255.0f * 0.5f);
}

void CCosmeticsHandler::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = pGameServer->Server();
}

int CCosmeticsHandler::FindKnockoutEffect(const char *pName)
{
	int Effect = -1;
	for(int i = 0; i < NUM_KNOCKOUTS; i++)
	{
		if(str_comp_nocase(ms_KnockoutNames[i], pName) == 0)
		{
			Effect = i;
			break;
		}
	}

	return Effect;
}

bool CCosmeticsHandler::HasKnockoutEffect(int ClientID, int Index)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	if(GameServer()->m_apPlayers[ClientID]->Bw().m_IsNpc)
		return true;
	if(Index < 0 || Index >= NUM_KNOCKOUTS)
		return false;

	if(Server()->GetAuthedState(ClientID) != AUTHED_NO)
		return true;

	if(!GameServer()->m_apPlayers[ClientID]->Bw().IsLoggedIn())
		return false;

	if(Index == KNOCKOUT_VIP_SPLASH)
	{
		if(GameServer()->m_apPlayers[ClientID]->Bw().GetPlayerVip())
			return true;
	}

	return GameServer()->Bw().GetPlayer(ClientID)->Bw().GetPlayerKnockouts()[Index] == '1';
}

bool CCosmeticsHandler::DoKnockoutEffect(int ClientID, vec2 Pos)
{
	// if(ClientID < 0 || ClientID >= MAX_CLIENTS || GameServer()->m_apPlayers[ClientID] == 0x0)
	// 	return false;

	CPlayer *pPlayer = GameServer()->Bw().GetPlayer(ClientID);

	int Effect = pPlayer->Bw().GetKnockout();

	if(Effect == -1)
		return false;
	DoKnockoutEffectRaw(Pos, Effect);
	return true;
}

void CCosmeticsHandler::DoKnockoutEffectRaw(vec2 Pos, int Effect)
{
	/*if (g_Config.m_Debug)
		dbg_msg("cosmetics", "Knockouteffect %s", ms_KnockoutNames[Effect]);*/

	if(Effect == KNOCKOUT_EXPLOSION)
	{
		// Visual-only: grenade explosion appearance + sound without physics push
		GameServer()->Bw().CreateExplosionVisual(Pos);
	}
	else if(Effect == KNOCKOUT_HAMMERHIT)
		GameServer()->CreateHammerHit(Pos);
	else if(Effect == KNOCKOUT_KOSTARS)
	{
		// 8 evenly spaced indicators; an int counter keeps the count exact
		for(int Step = 0; Step < 8; Step++)
			GameServer()->CreateDamageInd(Pos, 0.1f + Step * (pi / 4.0f), 1);
	}
	else if(Effect == KNOCKOUT_STARRING)
	{
		// 40 evenly spaced indicators
		for(int Step = 0; Step < 40; Step++)
			GameServer()->CreateDamageInd(Pos, Step * (pi / 20.0f), 1);
	}
	else if(Effect == KNOCKOUT_STAREXPLOSION)
	{
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);

		GameServer()->CreateExplosion(Pos, -1, WEAPON_GRENADE, true, 0);

		// 8 evenly spaced indicators; an int counter keeps the count exact
		for(int Step = 0; Step < 8; Step++)
			GameServer()->CreateDamageInd(Pos, 0.1f + Step * (pi / 4.0f), 1);
	}
	else if(Effect == KNOCKOUT_LOVE)
		GameServer()->Bw().Animations()->DoAnimation(Pos, CAnimationHandler::ANIMATION_LOVE);
	else if(Effect == KNOCKOUT_THUNDERSTORM)
		GameServer()->Bw().Animations()->DoAnimation(Pos, CAnimationHandler::ANIMATION_THUNDERSTORM);
	else if(Effect == KNOCKOUT_KORIP)
		GameServer()->Bw().Animations()->Laserwrite("RIP", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_KOEZ)
		GameServer()->Bw().Animations()->Laserwrite("EZ", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_KONOOB)
		GameServer()->Bw().Animations()->Laserwrite("NOOB", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_SORRY)
		GameServer()->Bw().Animations()->Laserwrite("SORRY c:", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_PRO)
		GameServer()->Bw().Animations()->Laserwrite("PRO", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_VIP_SPLASH)
		GameServer()->Bw().Animations()->DoAnimation(Pos, CAnimationHandler::ANIMATION_SPLASH);
	else if(Effect == KNOCKOUT_GG)
		GameServer()->Bw().Animations()->Laserwrite("GG", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if((unsigned int)Effect < sizeof(ms_KnockoutNames) / sizeof(ms_KnockoutNames[0]))
	{
		dbg_msg("cosmetics", "ERROR: Knockouteffect '%s' not implemented!", ms_KnockoutNames[Effect]);
	}
	else
		dbg_msg("cosmetics", "ERROR: Invalid knockout effect ID!");
}

bool CCosmeticsHandler::ToggleKnockout(int ClientID, const char *pName)
{
	dbg_msg("knockout", "toggling %s", pName);
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	if(m_pGameServer)
	{
		if(auto EventsAccessor = g_ComponentRegistry.Get<CEvents>(); EventsAccessor)
		{
			auto pEv = EventsAccessor->GetActiveEvent();
			if(pEv)
			{
				const char *pEvName = pEv->GetEventName();
				bool IsBlockedEvent = (str_comp(pEvName, "LMB") == 0) || (str_comp(pEvName, "Team Deathmatch") == 0);
				if(IsBlockedEvent)
				{
					const auto &Parts = pEv->Participants();
					if(std::find(Parts.begin(), Parts.end(), ClientID) != Parts.end())
					{
						m_pGameServer->SendChatTarget(ClientID, "Cosmetics are disabled during LMB/TDM.");
						return false;
					}
				}
			}
		}
	}

	int Effect = FindKnockoutEffect(pName);
	if(Effect == -1)
		return false;

	if(HasKnockoutEffect(ClientID, Effect) == false)
		return false;

	GameServer()->Bw().GetPlayer(ClientID)->Bw().ToggleKnockout(Effect);
	dbg_msg("cosmetics", "Applied cosmetic effect %d to client %d", Effect, ClientID);
	return true;
}

int CCosmeticsHandler::FindGundesign(const char *pName)
{
	int Effect = -1;
	for(int i = 0; i < NUM_GUNDESIGNS; i++)
	{
		if(str_comp_nocase(ms_GundesignNames[i], pName) == 0)
		{
			Effect = i;
			break;
		}
	}

	return Effect;
}

bool CCosmeticsHandler::HasGundesign(int ClientID, int Index)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	if(Index < 0 || Index >= NUM_GUNDESIGNS)
		return false;

	if(Server()->GetAuthedState(ClientID) != AUTHED_NO)
		return true;

	if(!GameServer()->m_apPlayers[ClientID]->Bw().IsLoggedIn())
		return false;

	if(GameServer()->m_apPlayers[ClientID]->Bw().GetPlayerVip() &&
		(Index == GUNDESIGN_VIP_STARGUN))
		return true;

	return GameServer()->Bw().GetPlayer(ClientID)->Bw().GetPlayerGundesign()[Index] == '1';
}

bool CCosmeticsHandler::DoGundesign(int ClientID, vec2 Pos, vec2 Direction)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || GameServer()->m_apPlayers[ClientID] == nullptr)
		return false;

	int Effect = GameServer()->Bw().GetPlayer(ClientID)->Bw().GetGunDesign();

	if(Effect == -1)
		return false;

	return DoGundesignRaw(Pos, Effect, Direction);
}

bool CCosmeticsHandler::DoGundesignRaw(vec2 Pos, int Effect, vec2 Direction)
{
	/*if (g_Config.m_Debug)
		dbg_msg("cosmetics", "Gundesigneffect %s", ms_GundesignNames[Effect]);*/

	if(Effect == GUNDESIGN_PEW)
	{
		GameServer()->Bw().Animations()->Laserwrite("PEW", Pos, 7.0f, Server()->TickSpeed() * 0.2f);
	}
	else if(Effect == GUNDESIGN_REVERSE)
	{
		float AngleFrom = angle(Direction) + 5.1f;
		for(int i = 0; i < 10; i++)
		{
			float AngleTo = AngleFrom + ((i - 5) / 5.0f) * pi * 0.3f;
			GameServer()->CreateDamageInd(Pos + direction(AngleTo - 5.1f) * 85.0f, AngleTo + pi, 1);
		}
	}
	else if(Effect == GUNDESIGN_STARX)
	{
		GameServer()->CreateDamageInd(Pos + vec2(1, 1) * 28.0f, 2.7f, 1);
		GameServer()->CreateDamageInd(Pos + vec2(-1, 1) * 28.0f, 2.7f + pi * 0.5f, 1);
	}
	else if(Effect == GUNDESIGN_CLOCKWISE)
	{
		GameServer()->Bw().Animations()->DoAnimationGundesign(Pos, CAnimationHandler::ANIMATION_STARS_CW, Direction);
	}
	else if(Effect == GUNDESIGN_COUNTERCLOCK)
	{
		GameServer()->Bw().Animations()->DoAnimationGundesign(Pos, CAnimationHandler::ANIMATION_STARS_CCW, Direction);
	}
	else if(Effect == GUNDESIGN_TWOCLOCK)
	{
		GameServer()->Bw().Animations()->DoAnimationGundesign(Pos, CAnimationHandler::ANIMATION_STARS_TOC, Direction);
	}
	else if(Effect == GUNDESIGN_VIP_STARGUN)
	{
		GameServer()->CreateDamageInd(Pos, angle(Direction) + 5.1f, 1);
	}
	else if(Effect == GUNDESIGN_SHURIKEN)
	{
		// spinning cross of 4 damage indicators
		float BaseAngle = angle(Direction);
		for(int i = 0; i < 4; i++)
		{
			float a = BaseAngle + (pi / 2.0f) * i;
			GameServer()->CreateDamageInd(Pos + direction(a) * 24.0f, a + pi, 1);
		}
	}
	else if(Effect == GUNDESIGN_SPARKLER)
	{
		// burst of 6 random damage indicators
		for(int i = 0; i < 6; i++)
		{
			float a = random_float() * 2.0f * pi;
			float Dist = 10.0f + random_float() * 40.0f;
			GameServer()->CreateDamageInd(Pos + direction(a) * Dist, a, 1);
		}
	}
	else
		return false;

	return true;
}

bool CCosmeticsHandler::ToggleGundesign(int ClientID, const char *pName)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	if(m_pGameServer)
	{
		if(auto EventsAccessor = g_ComponentRegistry.Get<CEvents>(); EventsAccessor)
		{
			auto pEv = EventsAccessor->GetActiveEvent();
			if(pEv)
			{
				const char *pEvName = pEv->GetEventName();
				bool IsBlockedEvent = (str_comp(pEvName, "LMB") == 0) || (str_comp(pEvName, "Team Deathmatch") == 0);
				if(IsBlockedEvent)
				{
					const auto &Parts = pEv->Participants();
					if(std::find(Parts.begin(), Parts.end(), ClientID) != Parts.end())
					{
						m_pGameServer->SendChatTarget(ClientID, "Cosmetics are disabled during LMB/TDM.");
						return false;
					}
				}
			}
		}
	}

	int Effect = FindGundesign(pName);

	if(Effect == -1)
		return false;

	if(HasGundesign(ClientID, Effect) == false)
		return false;

	GameServer()->Bw().GetPlayer(ClientID)->Bw().ToggleGunDesign(Effect);

	return true;
}

bool CCosmeticsHandler::SnapGundesign(int ClientID, vec2 Pos, vec2 Dir, int EntityID, int SnappingClient)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	int Effect = GameServer()->Bw().GetPlayer(ClientID)->Bw().GetGunDesign();

	if(Effect == -1)
		return false;

	return SnapGundesignRaw(Pos, Dir, Effect, EntityID, SnappingClient);
}

bool CCosmeticsHandler::SnapGundesignRaw(vec2 Pos, vec2 Dir, int Effect, int EntityID, int SnappingClient)
{
	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	bool Sixup = Server()->IsSixup(SnappingClient);

	if(Effect == GUNDESIGN_HEART)
	{
		GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Sixup, SnappingClient), EntityID, Pos, POWERUP_HEALTH, 0, 0, PICKUPFLAG_NO_PREDICT);
		return true;
	}
	else if(Effect == GUNDESIGN_ARMOR)
	{
		GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Sixup, SnappingClient), EntityID, Pos, POWERUP_ARMOR, 0, 0, PICKUPFLAG_NO_PREDICT);
		return true;
	}
	else if(Effect == GUNDESIGN_1337)
	{
		GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Sixup, SnappingClient), EntityID, Pos, 16 /* 53 */, 0, 0, PICKUPFLAG_NO_PREDICT);
		return true;
	}
	else if(Effect == GUNDESIGN_BLINKING)
	{
		if(Server()->Tick() % 3 == 0)
			return true;
	}
	else if(Effect == GUNDESIGN_VIP_STARGUN)
	{
		GameServer()->CreateDamageInd(Pos, angle(Dir) + 5.1f, 1, CClientMask().set(SnappingClient));
		return true;
	}
	else if(Effect == GUNDESIGN_SHURIKEN)
	{
		float BaseAngle = (float)(Server()->Tick() % 20) / 20.0f * 2.0f * pi;
		for(int i = 0; i < 4; i++)
		{
			float a = BaseAngle + (pi / 2.0f) * i;
			GameServer()->CreateDamageInd(Pos + direction(a) * 18.0f, a + pi, 1, CClientMask().set(SnappingClient));
		}
		return false;
	}
	else if(Effect == GUNDESIGN_SPARKLER)
	{
		// random spark around the bullet each tick
		float a = ((float)(Server()->Tick() * 7 % 31)) / 31.0f * 2.0f * pi;
		float Dist = 12.0f + ((Server()->Tick() * 13 % 17) / 17.0f) * 20.0f;
		GameServer()->CreateDamageInd(Pos + direction(a) * Dist, a + pi, 1, CClientMask().set(SnappingClient));
		return false; // show normal bullet too
	}

	return false;
}

int CCosmeticsHandler::FindSkinmani(const char *pName)
{
	int Effect = -1;
	for(int i = 0; i < NUM_SKINMANIS; i++)
	{
		if(!str_comp_nocase(ms_SkinmaniNames[i], pName))
		{
			Effect = i;
			break;
		}
	}
	return Effect;
}

bool CCosmeticsHandler::HasSkinmani(int ClientID, int Index)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	if(Index < 0 || Index >= NUM_SKINMANIS)
		return false;

	if(Server()->GetAuthedState(ClientID) != AUTHED_NO)
		return true;

	if(!GameServer()->m_apPlayers[ClientID]->Bw().IsLoggedIn())
		return false;

	if(GameServer()->m_apPlayers[ClientID]->Bw().GetPlayerVip() &&
		(Index == SKINMANI_VIP_RAINBOW || Index == SKINMANI_VIP_RAINBOW_EPI || Index == SKINMANI_VIP_HOOK_RAINBOW || Index == SKINMANI_VIP_ELECTRIC))
		return true;

	return GameServer()->Bw().GetPlayer(ClientID)->Bw().GetPlayerSkinmani()[Index] == '1';
}

bool CCosmeticsHandler::ToggleSkinmani(int ClientID, const char *pName)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	// m_pGameServer is only set in Init(); the rest of this file guards it too
	if(!m_pGameServer)
		return false;

	if(m_pGameServer)
	{
		if(auto EventsAccessor = g_ComponentRegistry.Get<CEvents>(); EventsAccessor)
		{
			auto pEv = EventsAccessor->GetActiveEvent();
			if(pEv)
			{
				const char *pEvName = pEv->GetEventName();
				bool IsBlockedEvent = (str_comp(pEvName, "LMB") == 0) || (str_comp(pEvName, "Team Deathmatch") == 0);
				if(IsBlockedEvent)
				{
					const auto &Parts = pEv->Participants();
					if(std::find(Parts.begin(), Parts.end(), ClientID) != Parts.end())
					{
						m_pGameServer->SendChatTarget(ClientID, "Cosmetics are disabled during LMB/TDM.");
						return false;
					}
				}
			}
		}
	}

	int Effect = FindSkinmani(pName);
	if(Effect == -1)
		return false;

	CPlayer *pPlayer = m_pGameServer->Bw().GetPlayer(ClientID);
	if(!pPlayer)
		return false;
	else if(pPlayer->Bw().m_IsNpc)
	{
		pPlayer->Bw().ToggleSkinMani(Effect);
		return true;
	}
	if(HasSkinmani(ClientID, Effect) == false)
		return false;

	int Prev = pPlayer->Bw().GetSkinMani();
	int New = pPlayer->Bw().ToggleSkinMani(Effect);

	// If the player just disabled the Hook Rainbow skin mani, clear any
	// characters that recorded this player as their hooker for rainbow so the
	// effect is removed immediately.
	if(Prev == SKINMANI_VIP_HOOK_RAINBOW && New != SKINMANI_VIP_HOOK_RAINBOW)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			CCharacter *pChr = m_pGameServer->GetPlayerChar(i);
			if(pChr && pChr->Bw().m_HookedBy == ClientID)
			{
				pChr->Bw().m_HookedBy = -1;
				pChr->Bw().m_HookRainbowDivider = 1.0f;
			}
		}
	}

	return true;
}
void CCosmeticsHandler::SnapSkinmani(int ClientID, int64_t Tick, CNetObj_ClientInfo *pClientInfo)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	int Effect = GameServer()->Bw().GetPlayer(ClientID)->Bw().GetSkinMani();

	if(Effect == -1)
		return;

	SnapSkinmaniRaw(Tick, pClientInfo, Effect, ClientID);
}

void CCosmeticsHandler::SnapSkinmaniRaw(int64_t Tick, CNetObj_ClientInfo *pClientInfo, int Effect, int ClientID)
{
	int64_t TickDef = Server()->Tick() - Tick; // only work with Tickdef
	vec3 HSLBody = CcToHsl(pClientInfo->m_ColorBody);
	vec3 HSLFeet = CcToHsl(pClientInfo->m_ColorFeet);

	if(Effect == SKINMANI_FEET_FIRE)
	{
		HSLFeet.h = 0.0f;
		HSLFeet.s = (sinf(TickDef / 128.0f) + 1.0f) / 2.0f;
		HSLFeet.l = 0.5f;
		pClientInfo->m_ColorFeet = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_FEET_WATER)
	{
		HSLFeet.h = 0.6f;
		HSLFeet.s = (sinf(TickDef / 128.0f) + 1.0f) / 2.0f;
		HSLFeet.l = 0.5f;
		pClientInfo->m_ColorFeet = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_FEET_POISON)
	{
		HSLFeet.h = 0.3f;
		HSLFeet.s = (sinf(TickDef / 128.0f) + 1.0f) / 2.0f;
		HSLFeet.l = 0.5f;
		pClientInfo->m_ColorFeet = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_FEET_BLACKWHITE)
	{
		HSLFeet.h = 0.0f;
		HSLFeet.s = 0.0f;
		HSLFeet.l = (sinf(TickDef / 128.0f) + 3.0f) / 4.0f;
		pClientInfo->m_ColorFeet = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_FEET_RGB)
	{
		HSLFeet.h = 0.3f * ((Server()->Tick() / (Server()->TickSpeed() * 2)) % 3);
		HSLFeet.s = 1.0f;
		HSLFeet.l = 0.5f;
		pClientInfo->m_ColorFeet = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_FEET_CMY)
	{
		HSLFeet.h = 0.15f + 0.3f * ((Server()->Tick() / (Server()->TickSpeed() * 2)) % 3);
		HSLFeet.s = 1.0f;
		HSLFeet.l = 0.5f;
		pClientInfo->m_ColorFeet = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	if(Effect == SKINMANI_BODY_FIRE)
	{
		HSLFeet.h = 0.0f;
		HSLFeet.s = (sinf(TickDef / 255.0f) + 1.0f) / 2.0f;
		HSLFeet.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_BODY_WATER)
	{
		HSLFeet.h = 0.6f;
		HSLFeet.s = (sinf(TickDef / 255.0f) + 1.0f) / 2.0f;
		HSLFeet.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_BODY_POISON)
	{
		HSLFeet.h = 0.3f;
		HSLFeet.s = (sinf(TickDef / 255.0f) + 1.0f) / 2.0f;
		HSLFeet.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_NIGHTBLUE)
	{
		HSLBody.h = 0.71f;
		HSLBody.s = 0.7f * std::clamp((sinf(TickDef / 50.0f) + 1.2f), 0.2f, 0.7f);
		HSLBody.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(HSLBody);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_VIP_RAINBOW)
	{
		float Base = 300.0f;
		if(ClientID >= 0 && ClientID < MAX_CLIENTS && GameServer()->GetPlayerChar(ClientID) && GameServer()->GetPlayerChar(ClientID)->Bw().IsHookRainbowActive())
			Base *= GameServer()->GetPlayerChar(ClientID)->Bw().GetHookRainbowDivider();

		HSLBody.h = (sinf(TickDef / Base) + 1.0f) / 2.0f;
		HSLBody.s = 0.6f;
		HSLBody.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(HSLBody);
		pClientInfo->m_ColorFeet = HslToCc(HSLBody);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_VIP_RAINBOW_EPI)
	{
		float Freq = 2.0f;
		if(ClientID >= 0 && ClientID < MAX_CLIENTS && GameServer()->GetPlayerChar(ClientID) && GameServer()->GetPlayerChar(ClientID)->Bw().IsHookRainbowActive())
			Freq *= GameServer()->GetPlayerChar(ClientID)->Bw().GetHookRainbowDivider();

		HSLBody.h = (sinf(TickDef / Freq) + 1.0f) / 2.0f;
		HSLBody.s = 1.0f;
		HSLBody.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(HSLBody);
		pClientInfo->m_ColorFeet = HslToCc(HSLBody);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_VIP_ELECTRIC)
	{
		float t = (sinf(TickDef / 20.0f) + 1.0f) / 2.0f;
		HSLBody.h = 0.6f;
		HSLBody.s = 0.9f;
		HSLBody.l = 0.4f + 0.3f * t;
		pClientInfo->m_ColorBody = HslToCc(HSLBody);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_DUAL_BLACKWHITE)
	{
		float Speed = (float)g_Config.m_SvDualSkinmaniSpeed;
		float t = (sinf(TickDef / Speed) + 1.0f) / 2.0f;
		vec3 Body, Feet;
		Body.h = 0.0f;
		Body.s = 0.0f;
		Body.l = 0.5f + 0.5f * t;
		Feet.h = 0.0f;
		Feet.s = 0.0f;
		Feet.l = 0.5f + 0.5f * (1.0f - t);
		pClientInfo->m_ColorBody = HslToCc(Body);
		pClientInfo->m_ColorFeet = HslToCc(Feet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_DUAL_FIRE)
	{
		float Speed = (float)g_Config.m_SvDualSkinmaniSpeed;
		float t = (sinf(TickDef / Speed) + 1.0f) / 2.0f;
		vec3 Body, Feet;
		Body.h = 0.0f;
		Body.s = t;
		Body.l = 0.5f;
		Feet.h = 0.0f;
		Feet.s = 1.0f - t;
		Feet.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(Body);
		pClientInfo->m_ColorFeet = HslToCc(Feet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_DUAL_WATER)
	{
		float Speed = (float)g_Config.m_SvDualSkinmaniSpeed;
		float t = (sinf(TickDef / Speed) + 1.0f) / 2.0f;
		vec3 Body, Feet;
		Body.h = 0.6f;
		Body.s = t;
		Body.l = 0.5f;
		Feet.h = 0.6f;
		Feet.s = 1.0f - t;
		Feet.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(Body);
		pClientInfo->m_ColorFeet = HslToCc(Feet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_DUAL_POISON)
	{
		float Speed = (float)g_Config.m_SvDualSkinmaniSpeed;
		float t = (sinf(TickDef / Speed) + 1.0f) / 2.0f;
		vec3 Body, Feet;
		Body.h = 0.3f;
		Body.s = t;
		Body.l = 0.5f;
		Feet.h = 0.3f;
		Feet.s = 1.0f - t;
		Feet.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(Body);
		pClientInfo->m_ColorFeet = HslToCc(Feet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_SUNSET_FADE)
	{
		// Smooth warm hue: pink(0.93) -> red(0.0) -> orange(0.10), using circular hue wrap
		float t = (sinf(TickDef / 150.0f) + 1.0f) / 2.0f;
		float Hue = fmodf(0.93f + t * 0.17f, 1.0f);
		HSLBody.h = Hue;
		HSLBody.s = 0.85f;
		HSLBody.l = 0.55f;
		pClientInfo->m_ColorBody = HslToCc(HSLBody);
		pClientInfo->m_ColorFeet = HslToCc(HSLBody);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_OCEAN_DRIFT)
	{
		// Smooth cool hue cycling: teal(0.45) -> blue(0.66) -> purple(0.80)
		float t = (sinf(TickDef / 130.0f) + 1.0f) / 2.0f;
		float Hue = 0.45f + t * 0.35f;
		HSLBody.h = Hue;
		HSLBody.s = 0.7f;
		HSLBody.l = 0.55f;
		pClientInfo->m_ColorBody = HslToCc(HSLBody);
		pClientInfo->m_ColorFeet = HslToCc(HSLBody);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_AURORA)
	{
		// Smooth full-spectrum pastel rainbow using sin for seamless loop
		float t = (sinf(TickDef / 200.0f) + 1.0f) / 2.0f;
		HSLBody.h = t;
		HSLBody.s = 0.5f;
		HSLBody.l = 0.6f;
		vec3 Feet;
		Feet.h = (sinf(TickDef / 200.0f + 0.6f) + 1.0f) / 2.0f; // slight phase offset
		Feet.s = 0.5f;
		Feet.l = 0.6f;
		pClientInfo->m_ColorBody = HslToCc(HSLBody);
		pClientInfo->m_ColorFeet = HslToCc(Feet);
		pClientInfo->m_UseCustomColor = 1;
	}
}

// TODO: don't hardcode the PreviewPos

bool CCosmeticsHandler::ShopInfoSkinmani(int Index, int &Price, int &Level, vec2 &PreviewPos)
{
	if(Index == CCosmeticsHandler::SKINMANI_FEET_FIRE)
	{
		Price = 1250;
		Level = 50;
		PreviewPos = vec2(1872.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_WATER)
	{
		Price = 1500;
		Level = 60;
		PreviewPos = vec2(2257.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_POISON)
	{
		Price = 2000;
		Level = 70;
		PreviewPos = vec2(2638.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_BLACKWHITE)
	{
		Price = 2250;
		Level = 80;
		PreviewPos = vec2(3409.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_RGB)
	{
		Price = 2750;
		Level = 90;
		PreviewPos = vec2(3793.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_CMY)
	{
		Price = 3250;
		Level = 100;
		PreviewPos = vec2(4174.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_BODY_FIRE)
	{
		Price = 3500;
		Level = 110;
		PreviewPos = vec2(4942.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_BODY_WATER)
	{
		Price = 4000;
		Level = 120;
		PreviewPos = vec2(5326.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_BODY_POISON)
	{
		Price = 4500;
		Level = 130;
		PreviewPos = vec2(5710.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_DUAL_BLACKWHITE)
	{
		Price = 20000;
		Level = 350;
		PreviewPos = vec2(6478.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_DUAL_FIRE)
	{
		Price = 8500;
		Level = 200;
		PreviewPos = vec2(6862.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_DUAL_WATER)
	{
		Price = 9000;
		Level = 200;
		PreviewPos = vec2(7246.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_DUAL_POISON)
	{
		Price = 9500;
		Level = 200;
		PreviewPos = vec2(7630.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_SUNSET_FADE)
	{
		Price = 10000;
		Level = 200;
		PreviewPos = vec2(8398.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_OCEAN_DRIFT)
	{
		Price = 10500;
		Level = 220;
		PreviewPos = vec2(8782.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_AURORA)
	{
		Price = 12500;
		Level = 250;
		PreviewPos = vec2(9166.0f, 2193.0f);
		return true;
	}
	else
	{
		return false;
	}
}

bool CCosmeticsHandler::ShopInfoGundesign(int Index, int &Price, int &Level, vec2 &PreviewPos)
{
	if(Index == CCosmeticsHandler::GUNDESIGN_CLOCKWISE)
	{
		Price = 1250;
		Level = 50;
		PreviewPos = vec2(1872.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_COUNTERCLOCK)
	{
		Price = 1250;
		Level = 50;
		PreviewPos = vec2(2257.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_TWOCLOCK)
	{
		Price = 1500;
		Level = 60;
		PreviewPos = vec2(2638.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_BLINKING)
	{
		Price = 3750;
		Level = 115;
		PreviewPos = vec2(3409.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_STARX)
	{
		Price = 5250;
		Level = 145;
		PreviewPos = vec2(3793.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_REVERSE)
	{
		Price = 7000;
		Level = 175;
		PreviewPos = vec2(4174.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_ARMOR)
	{
		Price = 9000;
		Level = 205;
		PreviewPos = vec2(4942.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_HEART)
	{
		Price = 12000;
		Level = 245;
		PreviewPos = vec2(5326.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_PEW)
	{
		Price = 15000;
		Level = 285;
		PreviewPos = vec2(5710.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_SHURIKEN)
	{
		Price = 18500;
		Level = 320;
		PreviewPos = vec2(6478.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_SPARKLER)
	{
		Price = 25000;
		Level = 400;
		PreviewPos = vec2(6862.0f, 3729.0f);
		return true;
	}
	else
	{
		return false;
	}
}

bool CCosmeticsHandler::ShopInfoKnockout(int Index, int &Price, int &Level, vec2 &PreviewPos)
{
	if(Index == CCosmeticsHandler::KNOCKOUT_EXPLOSION)
	{
		Price = 1250;
		Level = 50;
		PreviewPos = vec2(1873.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_HAMMERHIT)
	{
		Price = 1250;
		Level = 50;
		PreviewPos = vec2(2257.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_KOSTARS)
	{
		Price = 1500;
		Level = 60;
		PreviewPos = vec2(2638.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_STARRING)
	{
		Price = 3750;
		Level = 115;
		PreviewPos = vec2(3409.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_STAREXPLOSION)
	{
		Price = 4750;
		Level = 135;
		PreviewPos = vec2(3793.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_THUNDERSTORM)
	{
		Price = 7000;
		Level = 175;
		PreviewPos = vec2(4174.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_KORIP)
	{
		Price = 10500;
		Level = 225;
		PreviewPos = vec2(4942.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_LOVE)
	{
		Price = 12500;
		Level = 255;
		PreviewPos = vec2(5326.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_KOEZ)
	{
		Price = 16000;
		Level = 295;
		PreviewPos = vec2(5710.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_KONOOB)
	{
		Price = 17000;
		Level = 305;
		PreviewPos = vec2(6478.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_GG)
	{
		Price = 19500;
		Level = 330;
		PreviewPos = vec2(7630.0f, 5265.0f);
		return true;
	}
	else
	{
		return false;
	}
}

bool CCosmeticsHandler::ShopInfoUtility(int Index, int &Price, int &Level, vec2 &PreviewPos)
{
	if(Index == CCosmeticsHandler::UTILITY_WEAPONKIT)
	{
		Price = 50;
		Level = 10;
		PreviewPos = vec2(0.0f, 0.0f); // placeholder for now
		return true;
	}
	else if(Index == CCosmeticsHandler::UTILITY_DEATHNOTE_PAGE)
	{
		Price = 75;
		Level = 10;
		PreviewPos = vec2(50.0f, 0.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::UTILITY_PASSIVE_REMOVER)
	{
		Price = 2000;
		Level = 30;
		PreviewPos = vec2(100.0f, 0.0f);
		return true;
	}
	return false;
}
