#ifndef BLOCKWORLDS_COMPONENTS_JSON_HELPERS_H
#define BLOCKWORLDS_COMPONENTS_JSON_HELPERS_H

/**
 * JSON parsing helper functions for VPN service implementations
 * 
 * Simple JSON parsing utilities for extracting values from JSON responses.
 * These functions use basic string operations and are suitable for parsing
 * simple API responses without requiring a full JSON library.
 */
namespace JsonHelpers {
/**
	 * Parses a string value from JSON
	 * @param pJson JSON string to parse
	 * @param pKey Key to search for
	 * @param pOut Output buffer
	 * @param OutSize Size of output buffer
	 * @return true if key was found and value extracted, false otherwise
	 */
bool ParseString(const char *pJson, const char *pKey, char *pOut, int OutSize);

/**
	 * Parses a boolean value from JSON
	 * @param pJson JSON string to parse
	 * @param pKey Key to search for
	 * @param Out Output boolean value
	 * @return true if key was found and value extracted, false otherwise
	 */
bool ParseBool(const char *pJson, const char *pKey, bool &Out);

/**
	 * Parses an integer value from JSON
	 * @param pJson JSON string to parse
	 * @param pKey Key to search for
	 * @param Out Output integer value
	 * @return true if key was found and value extracted, false otherwise
	 */
bool ParseInt(const char *pJson, const char *pKey, int &Out);

/**
	 * Parses a floating point value from JSON
	 * @param pJson JSON string to parse
	 * @param pKey Key to search for
	 * @param Out Output float value
	 * @return true if key was found and value extracted, false otherwise
	 */
bool ParseFloat(const char *pJson, const char *pKey, float &Out);
} // namespace JsonHelpers

#endif // BLOCKWORLDS_COMPONENTS_JSON_HELPERS_H
