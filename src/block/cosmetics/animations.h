#ifndef BLOCK_COSMETICS_ANIMATIONS_H
#define BLOCK_COSMETICS_ANIMATIONS_H

#include <base/vmath.h>

#include <block/base.h>

#include <vector>

class CGameContext;
class IServer;
class CGameWorld;

class CMapAnimation
{
private:
	vec2 m_Pos;
	int64_t m_Tick;
	CGameWorld *m_pGameWorld;

public:
	CMapAnimation(vec2 Pos, int64_t Tick, CGameWorld *pGameWorld) :
		m_Pos(Pos), m_Tick(Tick), m_pGameWorld(pGameWorld) {}
	virtual ~CMapAnimation() = default;

	virtual void Tick() = 0;
	virtual void Snap(int SnappingClient) = 0;
	virtual bool Done() = 0;
	vec2 GetPos() const { return m_Pos; }
	int64_t GetTick() const { return m_Tick; }
	IServer *Server();
	CGameContext *GameServer();
	CGameWorld *GameWorld();
};

class CAnimationHandler
{
private:
	std::vector<CMapAnimation *> m_LpAnimations;
	CGameContext *m_pGameServer;
	IServer *m_pServer;

public:
	virtual ~CAnimationHandler() = default;
	CAnimationHandler();

	void Laserwrite(const char *pText, vec2 StartPos, float Size, int Ticks, bool Shotgun = false);
	void RemoveAnimationsNear(vec2 Pos, float Radius);
	void DoAnimation(vec2 Pos, int Index);
	void DoAnimationGundesign(vec2 Pos, int Index, vec2 Direction);

	void Init(CGameContext *pGameServer);

	virtual void Tick();
	virtual void Snap(int SnappingClient);

	enum
	{
		ANIMATION_LOVE = 0,
		ANIMATION_THUNDERSTORM,
		ANIMATION_SPLASH,
		ANIMATION_STARS_CW,
		ANIMATION_STARS_CCW,
		ANIMATION_STARS_TOC,
	};

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const { return m_pServer; }
};

#endif // BLOCK_COSMETICS_ANIMATIONS_H
