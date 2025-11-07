#include <base/system.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <blockworlds/components/core/component_registry.h>
#include <blockworlds/components/events.h>

#include <blockworlds/cosmetics/animations.h>

#include "cosmetics.h"

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

	"Nightblue",
	"Rainbow (VIP)",
	"Epi Rainbow (VIP)",
	"Hook Rainbow (VIP)",
	"Electric (VIP)",
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
		if(auto eventsAccessor = g_ComponentRegistry.Get<CEvents>(); eventsAccessor)
		{
			auto pEv = eventsAccessor->GetActiveEvent();
			if(pEv)
			{
				const char *pEvName = pEv->GetEventName();
				bool IsBlockedEvent = (str_comp(pEvName, "LMB") == 0) || (str_comp(pEvName, "Team Deathmatch") == 0);
				if(IsBlockedEvent)
				{
					const auto &parts = pEv->Participants();
					if(std::find(parts.begin(), parts.end(), ClientID) != parts.end())
					{
						m_pGameServer->SendChatTarget(ClientID, "Cosmetics are disabled during LMB/TDM.");
						return false;
					}
				}
			}
		}
	}

	std::string sName = pName ? pName : "";
	for(int i = 0; i < CCosmeticsHandler::NUM_SPECIALS; ++i)
	{
		if(str_comp_nocase(g_SpecialNames[i], sName.c_str()) == 0)
		{
			CPlayer *pPlayer = GameServer()->GetPlayer(ClientID);
			if(!pPlayer)
				return false;

			// NOTE: all remaining specials in this list are toggleable by players

			if(pPlayer->ToggleSpecial(i))
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
	if(Server()->ClientAuthed(ClientID))
		return true;
	if(!GameServer()->m_apPlayers[ClientID]->IsLoggedIn())
		return false;
	if(GameServer()->m_apPlayers[ClientID]->GetPlayerVip())
		return true;

	return false;
}

const char *CCosmeticsHandler::GetPlayerSpecials()
{
	return ""; // placeholder not used
}

inline int HslToCc(vec3 HSL)
{
	return ((int)(HSL.h * 255) << 16) + ((int)(HSL.s * 255) << 8) + (HSL.l - 0.5f) * 255 * 2;
}

inline vec3 CcToHsl(int Cc)
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
	if(GameServer()->m_apPlayers[ClientID]->m_IsNpc)
		return true;
	if(Index < 0 || Index >= NUM_KNOCKOUTS)
		return false;

	if(Server()->ClientAuthed(ClientID))
		return true;

	if(!GameServer()->m_apPlayers[ClientID]->IsLoggedIn())
		return false;

	if(Index == KNOCKOUT_VIP_SPLASH)
	{
		if(GameServer()->m_apPlayers[ClientID]->GetPlayerVip())
			return true;
	}

	return GameServer()->GetPlayer(ClientID)->GetPlayerKnockouts()[Index] == '1';
}

bool CCosmeticsHandler::DoKnockoutEffect(int ClientID, vec2 Pos)
{
	// if(ClientID < 0 || ClientID >= MAX_CLIENTS || GameServer()->m_apPlayers[ClientID] == 0x0)
	// 	return false;

	CPlayer *pPlayer = GameServer()->GetPlayer(ClientID);

	int Effect = pPlayer->GetKnockout();

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
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateExplosion(Pos, -1, WEAPON_GRENADE, true, 0);
	}
	else if(Effect == KNOCKOUT_HAMMERHIT)
		GameServer()->CreateHammerHit(Pos);
	else if(Effect == KNOCKOUT_KOSTARS)
	{
		for(float i = 0.1f; i < 2 * pi; i += pi / 4.0f)
			GameServer()->CreateDamageInd(Pos, i, 1);
	}
	else if(Effect == KNOCKOUT_STARRING)
	{
		for(float i = 0.0f; i < 2 * pi; i += pi / 20.0f)
			GameServer()->CreateDamageInd(Pos, i, 1);
	}
	else if(Effect == KNOCKOUT_STAREXPLOSION)
	{
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);

		GameServer()->CreateExplosion(Pos, -1, WEAPON_GRENADE, true, 0);

		for(float i = 0.1f; i < 2 * pi; i += pi / 4.0f)
			GameServer()->CreateDamageInd(Pos, i, 1);
	}
	else if(Effect == KNOCKOUT_LOVE)
		GameServer()->Animations()->DoAnimation(Pos, CAnimationHandler::ANIMATION_LOVE);
	else if(Effect == KNOCKOUT_THUNDERSTORM)
		GameServer()->Animations()->DoAnimation(Pos, CAnimationHandler::ANIMATION_THUNDERSTORM);
	else if(Effect == KNOCKOUT_KORIP)
		GameServer()->Animations()->Laserwrite("RIP", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_KOEZ)
		GameServer()->Animations()->Laserwrite("EZ", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_KONOOB)
		GameServer()->Animations()->Laserwrite("NOOB", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_SORRY)
		GameServer()->Animations()->Laserwrite("SORRY c:", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_PRO)
		GameServer()->Animations()->Laserwrite("PRO", Pos + vec2(0, 10.0f), 10.0f, Server()->TickSpeed() * 0.7f);
	else if(Effect == KNOCKOUT_VIP_SPLASH)
		GameServer()->Animations()->DoAnimation(Pos, CAnimationHandler::ANIMATION_SPLASH);
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
		if(auto eventsAccessor = g_ComponentRegistry.Get<CEvents>(); eventsAccessor)
		{
			auto pEv = eventsAccessor->GetActiveEvent();
			if(pEv)
			{
				const char *pEvName = pEv->GetEventName();
				bool IsBlockedEvent = (str_comp(pEvName, "LMB") == 0) || (str_comp(pEvName, "Team Deathmatch") == 0);
				if(IsBlockedEvent)
				{
					const auto &parts = pEv->Participants();
					if(std::find(parts.begin(), parts.end(), ClientID) != parts.end())
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

	GameServer()->GetPlayer(ClientID)->ToggleKnockout(Effect);
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

	if(Server()->ClientAuthed(ClientID))
		return true;

	if(!GameServer()->m_apPlayers[ClientID]->IsLoggedIn())
		return false;

	if(GameServer()->m_apPlayers[ClientID]->GetPlayerVip() &&
		(Index == GUNDESIGN_VIP_STARGUN))
		return true;

	return GameServer()->GetPlayer(ClientID)->GetPlayerGundesign()[Index] == '1';
}

bool CCosmeticsHandler::DoGundesign(int ClientID, vec2 Pos, vec2 Direction)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || GameServer()->m_apPlayers[ClientID] == 0x0)
		return false;

	int Effect = GameServer()->GetPlayer(ClientID)->GetGunDesign();

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
		GameServer()->Animations()->Laserwrite("PEW", Pos, 7.0f, Server()->TickSpeed() * 0.2f);
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
		GameServer()->Animations()->DoAnimationGundesign(Pos, CAnimationHandler::ANIMATION_STARS_CW, Direction);
	}
	else if(Effect == GUNDESIGN_COUNTERCLOCK)
	{
		GameServer()->Animations()->DoAnimationGundesign(Pos, CAnimationHandler::ANIMATION_STARS_CCW, Direction);
	}
	else if(Effect == GUNDESIGN_TWOCLOCK)
	{
		GameServer()->Animations()->DoAnimationGundesign(Pos, CAnimationHandler::ANIMATION_STARS_TOC, Direction);
	}
	else if(Effect == GUNDESIGN_VIP_STARGUN)
	{
		GameServer()->CreateDamageInd(Pos, angle(Direction) + 5.1f, 1);
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
		if(auto eventsAccessor = g_ComponentRegistry.Get<CEvents>(); eventsAccessor)
		{
			auto pEv = eventsAccessor->GetActiveEvent();
			if(pEv)
			{
				const char *pEvName = pEv->GetEventName();
				bool IsBlockedEvent = (str_comp(pEvName, "LMB") == 0) || (str_comp(pEvName, "Team Deathmatch") == 0);
				if(IsBlockedEvent)
				{
					const auto &parts = pEv->Participants();
					if(std::find(parts.begin(), parts.end(), ClientID) != parts.end())
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

	GameServer()->GetPlayer(ClientID)->ToggleGunDesign(Effect);

	return true;
}

bool CCosmeticsHandler::SnapGundesign(int ClientID, vec2 Pos, vec2 Dir, int EntityID, int SnappingClient)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	int Effect = GameServer()->GetPlayer(ClientID)->GetGunDesign();

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
		GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Sixup), EntityID, Pos, POWERUP_HEALTH, 0, 0);
		return true;
	}
	else if(Effect == GUNDESIGN_ARMOR)
	{
		GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Sixup), EntityID, Pos, POWERUP_ARMOR, 0, 0);
		return true;
	}
	else if(Effect == GUNDESIGN_1337)
	{
		GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Sixup), EntityID, Pos, 16 /* 53 */, 0, 0);
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

	if(Server()->ClientAuthed(ClientID))
		return true;

	if(!GameServer()->m_apPlayers[ClientID]->IsLoggedIn())
		return false;

	if(GameServer()->m_apPlayers[ClientID]->GetPlayerVip() &&
		(Index == SKINMANI_VIP_RAINBOW || Index == SKINMANI_VIP_RAINBOW_EPI || Index == SKINMANI_VIP_HOOK_RAINBOW || Index == SKINMANI_VIP_ELECTRIC))
		return true;

	return GameServer()->GetPlayer(ClientID)->GetPlayerSkinmani()[Index] == '1';
}

bool CCosmeticsHandler::ToggleSkinmani(int ClientID, const char *pName)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	if(m_pGameServer)
	{
		if(auto eventsAccessor = g_ComponentRegistry.Get<CEvents>(); eventsAccessor)
		{
			auto pEv = eventsAccessor->GetActiveEvent();
			if(pEv)
			{
				const char *pEvName = pEv->GetEventName();
				bool IsBlockedEvent = (str_comp(pEvName, "LMB") == 0) || (str_comp(pEvName, "Team Deathmatch") == 0);
				if(IsBlockedEvent)
				{
					const auto &parts = pEv->Participants();
					if(std::find(parts.begin(), parts.end(), ClientID) != parts.end())
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

	CPlayer *pPlayer = m_pGameServer->GetPlayer(ClientID);
	if(!pPlayer)
		return false;
	else if(pPlayer->m_IsNpc)
	{
		pPlayer->ToggleSkinMani(Effect);
		return true;
	}
	if(HasSkinmani(ClientID, Effect) == false)
		return false;

	int Prev = pPlayer->GetSkinMani();
	int New = pPlayer->ToggleSkinMani(Effect);

	// If the player just disabled the Hook Rainbow skin mani, clear any
	// characters that recorded this player as their hooker for rainbow so the
	// effect is removed immediately.
	if(Prev == SKINMANI_VIP_HOOK_RAINBOW && New != SKINMANI_VIP_HOOK_RAINBOW)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			CCharacter *pChr = m_pGameServer->GetPlayerChar(i);
			if(pChr && pChr->m_HookedBy == ClientID)
			{
				pChr->m_HookedBy = -1;
				pChr->m_HookRainbowDivider = 1.0f;
			}
		}
	}

	return true;
}
void CCosmeticsHandler::SnapSkinmani(int ClientID, int64_t Tick, CNetObj_ClientInfo *pClientInfo)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	int Effect = GameServer()->GetPlayer(ClientID)->GetSkinMani();

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
		HSLFeet.h = 0.3f * ((int)(Server()->Tick() / (Server()->TickSpeed() * 2)) % 3);
		HSLFeet.s = 1.0f;
		HSLFeet.l = 0.5f;
		pClientInfo->m_ColorFeet = HslToCc(HSLFeet);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_FEET_CMY)
	{
		HSLFeet.h = 0.15f + 0.3f * ((int)(Server()->Tick() / (Server()->TickSpeed() * 2)) % 3);
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
		HSLBody.s = 0.7f * clamp((sinf(TickDef / 50.0f) + 1.2f), 0.2f, 0.7f);
		HSLBody.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(HSLBody);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_VIP_RAINBOW)
	{
		float base = 300.0f;
		if(ClientID >= 0 && ClientID < MAX_CLIENTS && GameServer()->GetPlayerChar(ClientID) && GameServer()->GetPlayerChar(ClientID)->IsHookRainbowActive())
			base *= GameServer()->GetPlayerChar(ClientID)->GetHookRainbowDivider();

		HSLBody.h = (sinf(TickDef / base) + 1.0f) / 2.0f;
		HSLBody.s = 0.6f;
		HSLBody.l = 0.5f;
		pClientInfo->m_ColorBody = HslToCc(HSLBody);
		pClientInfo->m_ColorFeet = HslToCc(HSLBody);
		pClientInfo->m_UseCustomColor = 1;
	}
	else if(Effect == SKINMANI_VIP_RAINBOW_EPI)
	{
		float freq = 2.0f;
		if(ClientID >= 0 && ClientID < MAX_CLIENTS && GameServer()->GetPlayerChar(ClientID) && GameServer()->GetPlayerChar(ClientID)->IsHookRainbowActive())
			freq *= GameServer()->GetPlayerChar(ClientID)->GetHookRainbowDivider();

		HSLBody.h = (sinf(TickDef / freq) + 1.0f) / 2.0f;
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
}

// TODO: don't hardcode the PreviewPos

bool CCosmeticsHandler::ShopInfoSkinmani(int Index, int &Price, int &Level, vec2 &PreviewPos)
{
	if(Index == CCosmeticsHandler::SKINMANI_FEET_FIRE)
	{
		Price = 600;
		Level = 50;
		PreviewPos = vec2(1872.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_WATER)
	{
		Price = 750;
		Level = 60;
		PreviewPos = vec2(2257.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_POISON)
	{
		Price = 900;
		Level = 70;
		PreviewPos = vec2(2638.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_BLACKWHITE)
	{
		Price = 1000;
		Level = 80;
		PreviewPos = vec2(3409.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_RGB)
	{
		Price = 1100;
		Level = 90;
		PreviewPos = vec2(3793.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_FEET_CMY)
	{
		Price = 1200;
		Level = 100;
		PreviewPos = vec2(4174.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_BODY_FIRE)
	{
		Price = 1300;
		Level = 110;
		PreviewPos = vec2(4942.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_BODY_WATER)
	{
		Price = 1500;
		Level = 120;
		PreviewPos = vec2(5326.0f, 2193.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::SKINMANI_BODY_POISON)
	{
		Price = 1700;
		Level = 130;
		PreviewPos = vec2(5710.0f, 2193.0f);
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
		Price = 600;
		Level = 50;
		PreviewPos = vec2(1872.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_COUNTERCLOCK)
	{
		Price = 600;
		Level = 50;
		PreviewPos = vec2(2257.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_TWOCLOCK)
	{
		Price = 800;
		Level = 60;
		PreviewPos = vec2(2638.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_BLINKING)
	{
		Price = 1200;
		Level = 115;
		PreviewPos = vec2(3409.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_STARX)
	{
		Price = 1500;
		Level = 145;
		PreviewPos = vec2(3793.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_REVERSE)
	{
		Price = 1800;
		Level = 175;
		PreviewPos = vec2(4174.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_ARMOR)
	{
		Price = 2200;
		Level = 205;
		PreviewPos = vec2(4942.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_HEART)
	{
		Price = 3000;
		Level = 245;
		PreviewPos = vec2(5326.0f, 3729.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::GUNDESIGN_PEW)
	{
		Price = 4000;
		Level = 285;
		PreviewPos = vec2(5710.0f, 3729.0f);
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
		Price = 600;
		Level = 50;
		PreviewPos = vec2(1873.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_HAMMERHIT)
	{
		Price = 600;
		Level = 50;
		PreviewPos = vec2(2257.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_KOSTARS)
	{
		Price = 800;
		Level = 60;
		PreviewPos = vec2(2638.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_STARRING)
	{
		Price = 1200;
		Level = 115;
		PreviewPos = vec2(3409.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_STAREXPLOSION)
	{
		Price = 1400;
		Level = 135;
		PreviewPos = vec2(3793.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_THUNDERSTORM)
	{
		Price = 1800;
		Level = 175;
		PreviewPos = vec2(4174.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_KORIP)
	{
		Price = 2600;
		Level = 225;
		PreviewPos = vec2(4942.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_LOVE)
	{
		Price = 3200;
		Level = 255;
		PreviewPos = vec2(5326.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_KOEZ)
	{
		Price = 4000;
		Level = 295;
		PreviewPos = vec2(5710.0f, 5265.0f);
		return true;
	}
	else if(Index == CCosmeticsHandler::KNOCKOUT_KONOOB)
	{
		Price = 4400;
		Level = 305;
		PreviewPos = vec2(6478.0f, 5265.0f);
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
		Price = 15;
		Level = 10;
		PreviewPos = vec2(0.0f, 0.0f); // placeholder for now
		return true;
	}
	else if(Index == CCosmeticsHandler::UTILITY_DEATHNOTE_PAGE)
	{
		Price = 30;
		Level = 10;
		PreviewPos = vec2(50.0f, 0.0f);
		return true;
	}
	return false;
}
