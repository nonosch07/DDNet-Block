#ifndef BLOCK_PASSWORD_HASH_H
#define BLOCK_PASSWORD_HASH_H

#include <cstddef>

// Public API for password hashing (PBKDF2-HMAC-SHA256)
// Format: PBKDF2$<iterations>$<salt_hex>$<dk_hex>
// All buffers are assumed non-null.

int pw_hash_generate(const char *pPassword, char *pOut, int OutSize, int Iterations);
int pw_hash_verify(const char *pPassword, const char *pStored);
int pw_hash_is_pbkdf2(const char *pStored);

#endif // BLOCK_PASSWORD_HASH_H
