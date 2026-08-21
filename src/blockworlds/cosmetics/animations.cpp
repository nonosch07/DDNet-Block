
#include "animations.h"

#include "content.h"
#include "letters.h"

#include <engine/server.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

static bool gs_LetterBits[256][5 * 7] = {};

IServer *CMapAnimation::Server() { return m_pGameWorld->Server(); }
CGameContext *CMapAnimation::GameServer() { return m_pGameWorld->GameServer(); }
CGameWorld *CMapAnimation::GameWorld() { return m_pGameWorld; }

CAnimationHandler::CAnimationHandler()
{
	const char lc_letters[] = "abcdefghijklmnopqrstuvwxyz";
	const char uc_letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char num_letters[] = "0123456789";
	const char spl_letters[] = "+-!?%$().,:<>=|";
	const char space[] = " ";

	void *num_destinations[] = {
		gs_Letter0,
		gs_Letter1,
		gs_Letter2,
		gs_Letter3,
		gs_Letter4,
		gs_Letter5,
		gs_Letter6,
		gs_Letter7,
		gs_Letter8,
		gs_Letter9};
	void *spl_destinations[] = {
		gs_LetterPL,
		gs_LetterMN,
		gs_LetterEM,
		gs_LetterQM,
		gs_LetterPC,
		gs_LetterDL,
		gs_LetterBO,
		gs_LetterBC,
		gs_LetterDT,
		gs_LetterCM,
		gs_LetterDD,
		gs_LetterSM,
		gs_LetterBI,
		gs_LetterEQ,
		gs_LetterLV,
		gs_LetterSP};
	void *lc_destinations[] = {
		gs_LetterA,
		gs_LetterB,
		gs_LetterC,
		gs_LetterD,
		gs_LetterE,
		gs_LetterF,
		gs_LetterG,
		gs_LetterH,
		gs_LetterI,
		gs_LetterJ,
		gs_LetterK,
		gs_LetterL,
		gs_LetterM,
		gs_LetterN,
		gs_LetterO,
		gs_LetterP,
		gs_LetterQ,
		gs_LetterR,
		gs_LetterS,
		gs_LetterT,
		gs_LetterU,
		gs_LetterV,
		gs_LetterW,
		gs_LetterX,
		gs_LetterY,
		gs_LetterZ};

	void *uc_destinations[] = {
		gs_LetterA,
		gs_LetterB,
		gs_LetterC,
		gs_LetterD,
		gs_LetterE,
		gs_LetterF,
		gs_LetterG,
		gs_LetterH,
		gs_LetterI,
		gs_LetterJ,
		gs_LetterK,
		gs_LetterL,
		gs_LetterM,
		gs_LetterN,
		gs_LetterO,
		gs_LetterP,
		gs_LetterQ,
		gs_LetterR,
		gs_LetterS,
		gs_LetterT,
		gs_LetterU,
		gs_LetterV,
		gs_LetterW,
		gs_LetterX,
		gs_LetterY,
		gs_LetterZ};

	for(size_t i = 0; i < strlen(lc_letters); i++)
	{
		unsigned char idx = static_cast<unsigned char>(lc_letters[i]);
		mem_copy(gs_LetterBits[idx], lc_destinations[i], sizeof(gs_LetterBits[0]));
	}

	for(size_t i = 0; i < strlen(uc_letters); i++)
	{
		unsigned char idx = static_cast<unsigned char>(uc_letters[i]);
		mem_copy(gs_LetterBits[idx], uc_destinations[i], sizeof(gs_LetterBits[0]));
	}

	for(size_t i = 0; i < strlen(num_letters); i++)
	{
		unsigned char idx = static_cast<unsigned char>(num_letters[i]);
		mem_copy(gs_LetterBits[idx], num_destinations[i], sizeof(gs_LetterBits[0]));
	}

	for(size_t i = 0; i < strlen(spl_letters); i++)
	{
		unsigned char idx = static_cast<unsigned char>(spl_letters[i]);
		mem_copy(gs_LetterBits[idx], spl_destinations[i], sizeof(gs_LetterBits[0]));
	}

	unsigned char space_idx = static_cast<unsigned char>(space[0]); // because space is a bxxxx.
	mem_copy(gs_LetterBits[space_idx], gs_LetterSP, sizeof(gs_LetterBits[0]));
}

void CAnimationHandler::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = pGameServer->Server();
}

void CAnimationHandler::Laserwrite(const char *pText, vec2 StartPos, float Size, int Ticks, bool Shotgun)
{
	int Length = str_length(pText);

	vec2 Pos = StartPos - vec2(Size * Length * 5.5f * 0.5f + 2.0f * Length, 5 * Size);
	for(int i = 0; i < Length; i++)
	{
		CAnimLetter *pChar = new CAnimLetter(Pos, Server()->Tick(), &GameServer()->m_World, Ticks, gs_LetterBits[(unsigned char)pText[i]], Size, Shotgun);
		m_lpAnimations.push_back(pChar);
		Pos.x += pChar->Width() + Size + 4.0f;
	}
}

void CAnimationHandler::RemoveAnimationsNear(vec2 Pos, float Radius)
{
	for(size_t i = 0; i < m_lpAnimations.size(); i++)
	{
		if(distance(m_lpAnimations[i]->GetPos(), Pos) <= Radius)
		{
			delete m_lpAnimations[i];
			m_lpAnimations.erase(m_lpAnimations.begin() + i);
			i--;
		}
	}
}

void CAnimationHandler::DoAnimation(vec2 Pos, int Index)
{
	switch(Index)
	{
	case ANIMATION_LOVE: m_lpAnimations.push_back(new CAnimLove(Pos, Server()->Tick(), &GameServer()->m_World)); break;
	case ANIMATION_THUNDERSTORM: m_lpAnimations.push_back(new CAnimThunderstorm(Pos, Server()->Tick(), &GameServer()->m_World)); break;
	case ANIMATION_SPLASH: m_lpAnimations.push_back(new CSplash(Pos, Server()->Tick(), &GameServer()->m_World)); break;
	}
}

void CAnimationHandler::DoAnimationGundesign(vec2 Pos, int Index, vec2 Direction)
{
	switch(Index)
	{
	case ANIMATION_STARS_CW: m_lpAnimations.push_back(new CStarsCW(Pos, Server()->Tick(), Direction, &GameServer()->m_World)); break;
	case ANIMATION_STARS_CCW: m_lpAnimations.push_back(new CStarsCCW(Pos, Server()->Tick(), Direction, &GameServer()->m_World)); break;
	case ANIMATION_STARS_TOC: m_lpAnimations.push_back(new CStarsTOC(Pos, Server()->Tick(), Direction, &GameServer()->m_World)); break;
	default:
		dbg_assert(false, "out of bound animation index");
	}
}

void CAnimationHandler::Tick()
{
	for(size_t i = 0; i < m_lpAnimations.size(); i++)
	{
		if(m_lpAnimations[i]->Done() == false)
		{
			m_lpAnimations[i]->Tick();
		}
		else
		{
			delete m_lpAnimations[i];
			m_lpAnimations.erase(m_lpAnimations.begin() + i);
			i--;
		}
	}
}

//make this as efficient as possible
template<typename T>
inline bool within_reach(const vector2_base<T> a, const vector2_base<T> &b, float Dist, bool Equals = false)
{
	vector2_base<T> d = b - a;
	//out of outer quad
	if(fabs(d.x) > Dist || fabs(d.y) > Dist)
		return false;
	if(Equals)
		return length(d) <= Dist;
	else
		return length(d) < Dist;
}

void CAnimationHandler::Snap(int SnappingClient)
{
	for(auto *Animation : m_lpAnimations)
	{
		float dx = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.x - Animation->GetPos().x;
		float dy = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.y - Animation->GetPos().y;

		if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
			continue;

		if(!within_reach(GameServer()->m_apPlayers[SnappingClient]->m_ViewPos, Animation->GetPos(), 1100.0f))
			continue;

		Animation->Snap(SnappingClient);
	}
}
