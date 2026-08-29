#ifndef BLOCK_COSMETICS_COSMETICS_H
#define BLOCK_COSMETICS_COSMETICS_H

#include <base/vmath.h>

#include <engine/server.h>

class CGameContext;

class CCosmeticsHandler
{
	CGameContext *m_pGameServer;
	IServer *m_pServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const { return m_pServer; }

public:
	enum
	{
		KNOCKOUT_EXPLOSION = 0,
		KNOCKOUT_HAMMERHIT = 1,
		KNOCKOUT_KOSTARS = 2,
		KNOCKOUT_STARRING = 3,
		KNOCKOUT_STAREXPLOSION = 4,
		KNOCKOUT_THUNDERSTORM = 5,
		KNOCKOUT_KORIP = 6,
		KNOCKOUT_LOVE = 7,
		KNOCKOUT_KOEZ = 8,
		KNOCKOUT_KONOOB = 9,
		KNOCKOUT_PRO = 10,
		KNOCKOUT_GG = 11,
		// ---
		KNOCKOUT_SORRY = 12,
		KNOCKOUT_VIP_SPLASH = 13,
		NUM_KNOCKOUTS = 14,

		GUNDESIGN_CLOCKWISE = 0,
		GUNDESIGN_COUNTERCLOCK = 1,
		GUNDESIGN_TWOCLOCK = 2,
		GUNDESIGN_BLINKING = 3,
		GUNDESIGN_STARX = 4,
		GUNDESIGN_REVERSE = 5,
		GUNDESIGN_ARMOR = 6,
		GUNDESIGN_HEART = 7,
		GUNDESIGN_PEW = 8,
		GUNDESIGN_SHURIKEN = 9,
		GUNDESIGN_SPARKLER = 10,
		// ---
		GUNDESIGN_1337 = 11,
		GUNDESIGN_VIP_STARGUN = 12,
		NUM_GUNDESIGNS = 13,

		SKINMANI_FEET_FIRE = 0,
		SKINMANI_FEET_WATER = 1,
		SKINMANI_FEET_POISON = 2,
		SKINMANI_FEET_BLACKWHITE = 3,
		SKINMANI_FEET_RGB = 4,
		SKINMANI_FEET_CMY = 5,
		SKINMANI_BODY_FIRE = 6,
		SKINMANI_BODY_WATER = 7,
		SKINMANI_BODY_POISON = 8,
		SKINMANI_DUAL_FIRE = 9,
		SKINMANI_DUAL_WATER = 10,
		SKINMANI_DUAL_POISON = 11,
		SKINMANI_DUAL_BLACKWHITE = 12,
		SKINMANI_SUNSET_FADE = 13,
		SKINMANI_OCEAN_DRIFT = 14,
		SKINMANI_AURORA = 15,
		// ---
		SKINMANI_NIGHTBLUE = 16,
		SKINMANI_VIP_RAINBOW = 17,
		SKINMANI_VIP_RAINBOW_EPI = 18,
		SKINMANI_VIP_HOOK_RAINBOW = 19,
		SKINMANI_VIP_ELECTRIC = 20,
		NUM_SKINMANIS = 21,
	};

	// utility shop items (not cosmetics but exposed here for shop info)
	enum
	{
		UTILITY_WEAPONKIT = 0,
		UTILITY_DEATHNOTE_PAGE,
		UTILITY_PASSIVE_REMOVER,
		UTILITY_VIP_WEEK,
		NUM_UTILITY_ITEMS,
	};

	static const char *ms_KnockoutNames[NUM_KNOCKOUTS];
	static const char *ms_GundesignNames[NUM_GUNDESIGNS];
	static const char *ms_SkinmaniNames[NUM_SKINMANIS];

	// special items
	enum
	{
		SPECIAL_BALL = 0,
		SPECIAL_CROWN,
		SPECIAL_EPICCIRCLE,
		SPECIAL_HALO,
		NUM_SPECIALS,
	};

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

	bool ShopInfoSkinmani(int Index, int &Price, int &Level, vec2 &PreviewPos);
	bool ShopInfoKnockout(int Index, int &Price, int &Level, vec2 &PreviewPos);
	bool ShopInfoGundesign(int Index, int &Price, int &Level, vec2 &PreviewPos);

	// utility shop info (weaponkits, deathnote pages)
	bool ShopInfoUtility(int Index, int &Price, int &Level, vec2 &PreviewPos);

	// specials
	bool ToggleSpecial(int ClientID, const char *pName);
	bool HasSpecial(int ClientID, int Index);
	const char *GetPlayerSpecials();
};

#endif // BLOCK_COSMETICS_COSMETICS_H
