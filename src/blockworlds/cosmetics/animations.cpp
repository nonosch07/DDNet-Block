
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
	const char LcLetters[] = "abcdefghijklmnopqrstuvwxyz";
	const char UcLetters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char NumLetters[] = "0123456789";
	const char SplLetters[] = "+-!?%$().,:<>=|";
	const char Space[] = " ";

	void *NumDestinations[] = {
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
	void *SplDestinations[] = {
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
	void *LcDestinations[] = {
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

	void *UcDestinations[] = {
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

	for(size_t i = 0; i < strlen(LcLetters); i++)
	{
		unsigned char Idx = static_cast<unsigned char>(LcLetters[i]);
		mem_copy(gs_LetterBits[Idx], LcDestinations[i], sizeof(gs_LetterBits[0]));
	}

	for(size_t i = 0; i < strlen(UcLetters); i++)
	{
		unsigned char Idx = static_cast<unsigned char>(UcLetters[i]);
		mem_copy(gs_LetterBits[Idx], UcDestinations[i], sizeof(gs_LetterBits[0]));
	}

	for(size_t i = 0; i < strlen(NumLetters); i++)
	{
		unsigned char Idx = static_cast<unsigned char>(NumLetters[i]);
		mem_copy(gs_LetterBits[Idx], NumDestinations[i], sizeof(gs_LetterBits[0]));
	}

	for(size_t i = 0; i < strlen(SplLetters); i++)
	{
		unsigned char Idx = static_cast<unsigned char>(SplLetters[i]);
		mem_copy(gs_LetterBits[Idx], SplDestinations[i], sizeof(gs_LetterBits[0]));
	}

	unsigned char SpaceIdx = static_cast<unsigned char>(Space[0]); // because space is a bxxxx.
	mem_copy(gs_LetterBits[SpaceIdx], gs_LetterSP, sizeof(gs_LetterBits[0]));
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
		m_LpAnimations.push_back(pChar);
		Pos.x += pChar->Width() + Size + 4.0f;
	}
}

void CAnimationHandler::RemoveAnimationsNear(vec2 Pos, float Radius)
{
	for(size_t i = 0; i < m_LpAnimations.size(); i++)
	{
		if(distance(m_LpAnimations[i]->GetPos(), Pos) <= Radius)
		{
			delete m_LpAnimations[i];
			m_LpAnimations.erase(m_LpAnimations.begin() + i);
			i--;
		}
	}
}

void CAnimationHandler::DoAnimation(vec2 Pos, int Index)
{
	switch(Index)
	{
	case ANIMATION_LOVE: m_LpAnimations.push_back(new CAnimLove(Pos, Server()->Tick(), &GameServer()->m_World)); break;
	case ANIMATION_THUNDERSTORM: m_LpAnimations.push_back(new CAnimThunderstorm(Pos, Server()->Tick(), &GameServer()->m_World)); break;
	case ANIMATION_SPLASH: m_LpAnimations.push_back(new CSplash(Pos, Server()->Tick(), &GameServer()->m_World)); break;
	}
}

void CAnimationHandler::DoAnimationGundesign(vec2 Pos, int Index, vec2 Direction)
{
	switch(Index)
	{
	case ANIMATION_STARS_CW: m_LpAnimations.push_back(new CStarsCW(Pos, Server()->Tick(), Direction, &GameServer()->m_World)); break;
	case ANIMATION_STARS_CCW: m_LpAnimations.push_back(new CStarsCCW(Pos, Server()->Tick(), Direction, &GameServer()->m_World)); break;
	case ANIMATION_STARS_TOC: m_LpAnimations.push_back(new CStarsTOC(Pos, Server()->Tick(), Direction, &GameServer()->m_World)); break;
	default:
		dbg_assert(false, "out of bound animation index");
	}
}

void CAnimationHandler::Tick()
{
	for(size_t i = 0; i < m_LpAnimations.size(); i++)
	{
		if(m_LpAnimations[i]->Done() == false)
		{
			m_LpAnimations[i]->Tick();
		}
		else
		{
			delete m_LpAnimations[i];
			m_LpAnimations.erase(m_LpAnimations.begin() + i);
			i--;
		}
	}
}

//make this as efficient as possible
template<typename T>
static inline bool within_reach(const vector2_base<T> a, const vector2_base<T> &b, float Dist, bool Equals = false)
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
	for(auto *Animation : m_LpAnimations)
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
