#ifndef BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_JSON_HELPERS_H
#define BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_JSON_HELPERS_H

/**
 * JSON parsing helper functions for VPN service implementations
 *
 * Two calling conventions are provided for every Parse* function:
 *
 *   (A) Root-based  — caller owns a json_value* already parsed via json_parse().
 *                     Use this when extracting multiple fields from the same
 *                     document to avoid re-parsing the JSON string each time.
 *
 *   (B) String-based — convenience wrappers that call json_parse() internally.
 *                      Fine for one-off lookups; avoid when extracting several
 *                      fields from the same response (O(n) parses otherwise).
 *
 * Typical usage (multi-field):
 *   json_value *pRoot = json_parse(pJson, str_length(pJson));
 *   JsonHelpers::ParseString(pRoot, "key", buf, sizeof(buf));
 *   JsonHelpers::ParseBool(pRoot, "flag", b);
 *   json_value_free(pRoot);
 */
struct _json_value;
typedef struct _json_value json_value;

namespace JsonHelpers
{
	const json_value *GetObjectItem(const json_value *pObject, const char *pKey);
	const json_value *GetPath(const json_value *pRoot, const char *pPath);

	/**
	 * Extracts a string value from a pre-parsed JSON tree.
	 * @param pRoot  Pre-parsed root (from json_parse); must not be null.
	 * @param pKey   Dot-separated path (e.g. "isp.asn").
	 * @param pOut   Output buffer.
	 * @param OutSize Size of output buffer.
	 * @return true if the key was found and the value extracted.
	 */
	bool ParseString(const json_value *pRoot, const char *pKey, char *pOut, int OutSize);

	/**
	 * Extracts a boolean value from a pre-parsed JSON tree.
	 */
	bool ParseBool(const json_value *pRoot, const char *pKey, bool &Out);

	/**
	 * Extracts an integer value from a pre-parsed JSON tree.
	 */
	bool ParseInt(const json_value *pRoot, const char *pKey, int &Out);

	/**
	 * Extracts a float value from a pre-parsed JSON tree.
	 */
	bool ParseFloat(const json_value *pRoot, const char *pKey, float &Out);

	// String-based convenience overloads (parse-on-every-call)
	// Prefer the root-based overloads when reading multiple fields.

	bool ParseString(const char *pJson, const char *pKey, char *pOut, int OutSize);
	bool ParseBool(const char *pJson, const char *pKey, bool &Out);
	bool ParseInt(const char *pJson, const char *pKey, int &Out);
	bool ParseFloat(const char *pJson, const char *pKey, float &Out);
} // namespace JsonHelpers

#endif // BLOCKWORLDS_COMPONENTS_VPNDETECTION_SERVICES_JSON_HELPERS_H
