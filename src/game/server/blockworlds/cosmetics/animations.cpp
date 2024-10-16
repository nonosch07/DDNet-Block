
#include <engine/server.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "content.h"
#include "letters.h"

#include "animations.h"

static bool gs_LetterBits[256][5 * 7] = {};

IServer *CMapAnimation::Server() { return m_pGameWorld->Server(); }
CGameContext *CMapAnimation::GameServer() { return m_pGameWorld->GameServer(); }
CGameWorld *CMapAnimation::GameWorld() { return m_pGameWorld; }

CAnimationHandler::CAnimationHandler()
{
	mem_copy(gs_LetterBits['a'], gs_LetterA, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['b'], gs_LetterB, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['c'], gs_LetterC, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['d'], gs_LetterD, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['e'], gs_LetterE, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['f'], gs_LetterF, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['g'], gs_LetterG, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['h'], gs_LetterH, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['i'], gs_LetterI, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['j'], gs_LetterJ, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['k'], gs_LetterK, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['l'], gs_LetterL, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['m'], gs_LetterM, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['n'], gs_LetterN, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['o'], gs_LetterO, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['p'], gs_LetterP, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['q'], gs_LetterQ, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['r'], gs_LetterR, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['s'], gs_LetterS, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['t'], gs_LetterT, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['u'], gs_LetterU, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['v'], gs_LetterV, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['w'], gs_LetterW, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['x'], gs_LetterX, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['y'], gs_LetterY, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['z'], gs_LetterZ, sizeof(gs_LetterBits[0]));

	mem_copy(gs_LetterBits['A'], gs_LetterA, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['B'], gs_LetterB, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['C'], gs_LetterC, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['D'], gs_LetterD, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['E'], gs_LetterE, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['F'], gs_LetterF, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['G'], gs_LetterG, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['H'], gs_LetterH, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['I'], gs_LetterI, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['J'], gs_LetterJ, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['K'], gs_LetterK, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['L'], gs_LetterL, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['M'], gs_LetterM, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['N'], gs_LetterN, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['O'], gs_LetterO, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['P'], gs_LetterP, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['Q'], gs_LetterQ, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['R'], gs_LetterR, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['S'], gs_LetterS, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['T'], gs_LetterT, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['U'], gs_LetterU, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['V'], gs_LetterV, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['W'], gs_LetterW, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['X'], gs_LetterX, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['Y'], gs_LetterY, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['Z'], gs_LetterZ, sizeof(gs_LetterBits[0]));

	mem_copy(gs_LetterBits['0'], gs_Letter0, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['1'], gs_Letter1, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['2'], gs_Letter2, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['3'], gs_Letter3, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['4'], gs_Letter4, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['5'], gs_Letter5, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['6'], gs_Letter6, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['7'], gs_Letter7, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['8'], gs_Letter8, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['9'], gs_Letter9, sizeof(gs_LetterBits[0]));

	mem_copy(gs_LetterBits['+'], gs_LetterPL, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['-'], gs_LetterMN, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['!'], gs_LetterEM, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['?'], gs_LetterQM, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['%'], gs_LetterPC, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['$'], gs_LetterDL, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['('], gs_LetterBO, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits[')'], gs_LetterBC, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['.'], gs_LetterDT, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits[','], gs_LetterCM, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits[':'], gs_LetterDD, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['<'], gs_LetterSM, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['>'], gs_LetterBI, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['='], gs_LetterEQ, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits['|'], gs_LetterLV, sizeof(gs_LetterBits[0]));
	mem_copy(gs_LetterBits[' '], gs_LetterSP, sizeof(gs_LetterBits[0]));
}

void CAnimationHandler::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = pGameServer->Server();
}

void CAnimationHandler::Laserwrite(const char *pText, vec2 StartPos, float Size, int Ticks, bool Shotgun)
{
	int Length = str_length(pText);

	vec2 Pos = StartPos - vec2(Size * Length * 5.5f * 0.5f + 2.0f * Length, 7 * Size);
	for(int i = 0; i < Length; i++)
	{
		CAnimLetter *pChar = new CAnimLetter(Pos, Server()->Tick(), &GameServer()->m_World, Ticks, gs_LetterBits[(unsigned char)pText[i]], Size, Shotgun);
		m_lpAnimations.push_back(pChar);
		Pos.x += pChar->Width() + Size + 4.0f;
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
