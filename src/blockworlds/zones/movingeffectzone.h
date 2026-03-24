#ifndef BLOCKWORLDS_ZONES_MOVINGEFFECTZONE_H
#define BLOCKWORLDS_ZONES_MOVINGEFFECTZONE_H

#include "zone.h"
#include <engine/shared/protocol.h>
#include <game/mapitems.h>
#include <vector>

enum
{
	MOVINGEFFECT_FREEZE,
	MOVINGEFFECT_HOOK,
	MOVINGEFFECT_UNHOOK,
};

class CMovingEffectZone final : public IZone
{
	struct CMovingQuad
	{
		std::array<vec2, 4> m_aBasePoints; // static corner positions
		vec2 m_Pivot;                      // pivot point (aPoints[4])
		int m_PosEnv;                      // envelope index (-1 = none)
		int m_PosEnvOffset;                // envelope time offset in ms
	};

	int m_Effect;
	std::vector<CMovingQuad> m_vQuads;
	bool m_aWasInZone[MAX_CLIENTS];

	// envelope data loaded from map
	struct CEnvelopeData
	{
		int m_StartPoint;
		int m_NumPoints;
		int m_Channels;
	};
	std::vector<CEnvelopeData> m_vEnvelopes;
	CEnvPoint *m_pEnvPoints;
	int m_NumEnvPoints;

	void EvalPositionEnvelope(int EnvIndex, int OffsetMs, float &OutX, float &OutY) const;
	bool IsPlayerInMovingZone(vec2 PlayerPos) const;

public:
	CMovingEffectZone(class CGameContext *pGameServer, int Effect);

	void InitMoving(struct CMapItemLayerQuads *pQuadsLayer);

	void Tick() override;
	void Snap(int ClientID) override { (void)ClientID; }
};

#endif // BLOCKWORLDS_ZONES_MOVINGEFFECTZONE_H
