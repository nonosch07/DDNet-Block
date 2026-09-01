#ifndef BLOCK_CHARACTER_H
#define BLOCK_CHARACTER_H

#include <base/vmath.h>

#include <cstdint>

class CCharacter;
struct CNetObj_PlayerInput;
struct CNetObj_DDNetCharacter;
class CGameContext;
class CShop;
class IServer;

// All Block per-character state.
//
// CCharacter carries one CBlockCharacter member plus a Block() accessor; everything
// Block used to add to upstream's character.h lives here instead.
class CBlockCharacter
{
public:
	void Init(CCharacter *pCharacter) { m_pCharacter = pCharacter; }

	CCharacter *Character() const { return m_pCharacter; }
	CGameContext *GameServer() const;
	IServer *Server() const;

	// Called from CCharacter::Reset(), so a respawning tee starts clean.
	void Reset();

	/// The weapon index that goes into the snapshot.
	///
	/// A tee that owns no weapon at all is shown holding nothing: DDNet clients
	/// skip drawing the gun when the index is negative. Clients older than
	/// November 2024 clamp it back into range and 0.7 has no such rule, so both
	/// keep seeing a hammer -- hence the plain index for sixup rather than a
	/// value it cannot read.
	int SnapWeapon(int Weapon, bool Sixup) const;

	// Upstream keeps the inputs private; Block reads them for the AI bot and the
	// telekinesis command.
	const CNetObj_PlayerInput &GetInput() const;
	const CNetObj_PlayerInput &GetLatestInput() const;

	// Writable core. Upstream only exposes a const Core() and a GetCore() that
	// returns a copy; CBlockCharacter is a friend of CCharacter, so Block code that
	// has to write the core goes through here.
	class CCharacterCore &Core() const;

	// Hook rainbow effect (cosmetic): recolours a hooked tee for a while.
	void StartHookRainbow(int DurationTicks, float RateDivider, int HookerId = -1);
	bool IsHookRainbowActive() const;
	float GetHookRainbowDivider() const { return m_HookRainbowDivider; }

	// Freeze that ignores the usual freeze protections.
	void FreezeForce(int Seconds);
	void FreezeForce(float Seconds);

	// The Block game tiles: VIP gate, wayblock protection, random
	// cosmetics. Called from CCharacter::HandleTiles with the tile indices it
	// has already looked up.
	void OnHandleTiles(int TileIndex, int TileFIndex);

	// --- weapons ---
	// True when this tick's fire press must be dropped: no instant hammer on the
	// tick you come out of freeze, and passive or protected players do not fire.
	bool BlocksFire(bool FrozenLastTick) const;
	// One hammer hit landing on pTarget. Returns false when the target is not a
	// legal one (passive or protected), in which case upstream skips it.
	bool OnHammerHit(class CCharacter *pTarget);
	// False while carrying the bomb in BombTag, where a hammer passes it on
	// instead of freeing the tee.
	bool HammerUnfreezes() const;

	// Hooking somebody counts as an impact, and a VIP hooker paints the tee they
	// caught. Called when the hook attaches to a player.
	void OnHookAttach(int HookedPlayer);
	// Per-tick upkeep: a pending shop purchase.
	void OnTick() const;
	// Regenerates grenade ammo up to sv_zcatch_grenade_max_ammo, the way the
	// zCatch-grenade event needs.
	void HandleGrenadeAmmoRegen();
	// Telekinesis: an admin holding a tee freezes its input and gravity, then
	// TickDeferred teleports it to where they are pointing.
	void ApplyTelekinesisInput();
	void ApplyTelekinesisMove();
	// Clears the hook rainbow on both ends when the hook lets go.
	void OnReleaseHook();
	// A pending shop purchase does not survive the character.
	void OnDestroy() const;
	// Extra snap flags: passive tees look solo, protected ones look untouchable.
	void OnSnapDDNetCharacter(CNetObj_DDNetCharacter *pDDNetCharacter) const;

	// --- block tracking / anti-farm bookkeeping ---
	int m_FrozenSinceTick = 0;
	int m_FrozenAndUnmovedSinceTick = 0;
	int m_FrozenAndUntouchedSinceTick = 0;
	int m_CurrentKillingSpree = 0;
	int64_t m_AliveSince = 0;
	int64_t m_FrozenSince = 0;
	int m_LastToucher = -1;
	int m_LastTouchingWeapon = -1;
	int m_KillStreak = 0;

	// --- cosmetics ---
	float m_HookRainbowDivider = 1.0f;
	int m_HookRainbowEndTick = 0;
	int m_HookedBy = -1;

	// --- tiles ---
	bool m_IsOnPassiveTile = false;
	bool m_IsOnRandomCosmeticTile = false;

	// --- shop ---
	CShop *m_PendingPurchase = nullptr;
	int64_t m_LastShopTick = 0;

	// --- telekinesis (admin) ---
	bool m_TelekinesisHeld = false;
	vec2 m_TelekinesisTargetPos = vec2(0, 0);

private:
	CCharacter *m_pCharacter = nullptr;
};

#endif // BLOCK_CHARACTER_H
