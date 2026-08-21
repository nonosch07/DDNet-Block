#ifndef BLOCKWORLDS_COMMON_H
#define BLOCKWORLDS_COMMON_H

enum ExpCalculationMethod
{
	HIGHEST = 0,
	ADDITIVE,
	LOGARITHMIC,
	MULTIPLICATIVE,
	NUM_EXP_CALC_METHODS
};

// clannames are limited by the DDNet network protocol: 3 ints = 12 bytes (11 usable + null)
static constexpr int BW_CLAN_NAME_MAX_LENGTH = 11; // max bytes of content (matches MAX_CLAN_LENGTH - 1)
static constexpr int BW_CLAN_NAME_BUFFER_SIZE = 12; // equals MAX_CLAN_LENGTH

// Blockworlds game tiles. These sit above upstream's tile indices, so they can
// live here instead of in game/mapitems.h and never collide with a new DDNet
// tile. Keep in sync with the map editor's tileset.
enum
{
	TILE_BW_PASSIVE = 176,
	TILE_BW_VIP = 177,
	TILE_BW_RANDOM_COSMETIC = 178,
};

// seconds granted by TILE_BW_PASSIVE and TILE_BW_RANDOM_COSMETIC
static constexpr int PASSIVE_TILE_DURATION = 2 * 60 * 60;
static constexpr int RANDOM_COSMETIC_DURATION = 10 * 60;

#endif // BLOCKWORLDS_COMMON_H
