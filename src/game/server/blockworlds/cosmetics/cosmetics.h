#ifndef GAME_SERVER_BLOCKWORLDS_COSMETICS_HANDLER_H
#define GAME_SERVER_BLOCKWORLDS_COSMETICS_HANDLER_H

#include "game/server/blockworlds/accounts.h"
#include <base/vmath.h>
#include <engine/server.h>

class CGameContext;
class CGameWorld;
class CAccounts;

class CCosmeticsHandler
{
	CGameContext *m_pGameServer;
	CAccounts *m_pAccounts;
	IServer *m_pServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const { return m_pServer; }

	CAccounts *Accounts() const { return m_pAccounts; }

public:
	enum
	{ // Maximum sizeof(m_aKnockouts)/sizeof(char) = 256
		KNOCKOUT_EXPLOSION = 0,
		KNOCKOUT_HAMMERHIT,
		KNOCKOUT_KOSTARS,
		KNOCKOUT_STARRING,
		KNOCKOUT_STAREXPLOSION,
		KNOCKOUT_THUNDERSTORM,
		KNOCKOUT_LOVE,
		KNOCKOUT_KORIP,
		KNOCKOUT_KOEZ,
		KNOCKOUT_KONOOB,
		KNOCKOUT_PRO,

		KNOCKOUT_SORRY,
		KNOCKOUT_VIP_SPLASH,
		NUM_KNOCKOUTS,

		GUNDESIGN_CLOCKWISE = 0,
		GUNDESIGN_COUNTERCLOCK,
		GUNDESIGN_TWOCLOCK,
		GUNDESIGN_BLINKING,
		GUNDESIGN_REVERSE,
		GUNDESIGN_STARX,
		GUNDESIGN_INVISBULLET,
		GUNDESIGN_ARMOR,
		GUNDESIGN_HEART,
		GUNDESIGN_PEW,
		GUNDESIGN_1337,

		GUNDESIGN_VIP_STARGUN,
		NUM_GUNDESIGNS,

		SKINMANI_FEET_FIRE = 0,
		SKINMANI_FEET_WATER,
		SKINMANI_FEET_POISON,
		SKINMANI_FEET_BLACKWHITE,
		SKINMANI_FEET_RGB,
		SKINMANI_FEET_CMY,
		SKINMANI_BODY_FIRE,
		SKINMANI_BODY_WATER,
		SKINMANI_BODY_POISON,

		SKINMANI_NIGHTBLUE,
		SKINMANI_VIP_RAINBOW,
		SKINMANI_VIP_RAINBOW_EPI,
		SKINMANI_VIP_HOOK_RAINBOW,
		NUM_SKINMANIS,
	};

	static const char *ms_KnockoutNames[NUM_KNOCKOUTS];
	static const char *ms_GundesignNames[NUM_GUNDESIGNS];
	static const char *ms_SkinmaniNames[NUM_SKINMANIS];

	void Init(CGameContext *pGameServer);

	int FindKnockoutEffect(const char *pName);
	bool HasKnockoutEffect(int ClientID, int Index);
	bool DoKnockoutEffect(int ClientID, vec2 Pos);
	void DoKnockoutEffectRaw(vec2 Pos, int Effect);
	bool ToggleKnockout(int ClientID, const char *pName);

	int FindGundesign(const char *pName);
	bool HasGundesign(int ClientID, int Index);
	bool DoGundesign(int ClientID, vec2 Pos, vec2 Direction);
	bool DoGundesignRaw(vec2 Pos, int Effect, vec2 Direction);
	bool ToggleGundesign(int ClientID, const char *pName);

	bool SnapGundesign(int ClientID, vec2 Pos, vec2 Dir, int EntityID, int SnappingClient);
	bool SnapGundesignRaw(vec2 Pos, vec2 Dir, int Effect, int EntityID, int SnappingClient);

	int FindSkinmani(const char *pName);
	bool HasSkinmani(int ClientID, int Index);
	bool ToggleSkinmani(int ClientID, const char *pName);
	void SnapSkinmani(int ClientID, int64_t Tick, CNetObj_ClientInfo *pClientInfo);
	void SnapSkinmaniRaw(int64_t Tick, CNetObj_ClientInfo *pClientInfo, int Effect, int ClientID = -1);

	bool ShopInfoSkinmani(int Index, int &Price, int &Level, vec2 &Position);
	bool ShopInfoKnockout(int Index, int &Price, int &Level, vec2 &Position);
	bool ShopInfoGundesign(int Index, int &Price, int &Level, vec2 &Position);
};

#endif
