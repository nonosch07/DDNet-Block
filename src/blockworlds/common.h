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

#endif // BLOCKWORLDS_COMMON_H
