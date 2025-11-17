#include "password_hash.h"

#include <base/hash_ctxt.h>
#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#if defined(CONF_FAMILY_WINDOWS)
	#include <io.h>
	#include <Windows.h>
	#include <wincrypt.h>
#else
	#include <unistd.h>
#endif

#if defined(CONF_FAMILY_WINDOWS)
	#define strtok_r strtok_s
#endif

// Internal constants
static const int SALT_BYTES = 16;
static const int DK_BYTES = 32;

struct PHmacCtx
{
	SHA256_CTX Inner;
	SHA256_CTX Outer;
};
static void hmac_init(PHmacCtx &ctx, const unsigned char *key, size_t key_len)
{
	unsigned char K0[64];
	mem_zero(K0, sizeof(K0));
	if(key_len > 64)
	{
		SHA256_CTX t;
		sha256_init(&t);
		sha256_update(&t, key, key_len);
		auto h = sha256_finish(&t);
		mem_copy(K0, h.data, 32);
	}
	else
	{
		mem_copy(K0, key, key_len);
	}
	unsigned char ipad[64];
	unsigned char opad[64];
	for(int i = 0; i < 64; i++)
	{
		ipad[i] = K0[i] ^ 0x36;
		opad[i] = K0[i] ^ 0x5c;
	}
	sha256_init(&ctx.Inner);
	sha256_update(&ctx.Inner, ipad, 64);
	sha256_init(&ctx.Outer);
	sha256_update(&ctx.Outer, opad, 64);
}
static void hmac_update(PHmacCtx &ctx, const unsigned char *data, size_t len) { sha256_update(&ctx.Inner, data, len); }
static void hmac_final(PHmacCtx &ctx, unsigned char *mac)
{
	auto inner = sha256_finish(&ctx.Inner);
	sha256_update(&ctx.Outer, inner.data, 32);
	auto outer = sha256_finish(&ctx.Outer);
	mem_copy(mac, outer.data, 32);
}

static void pbkdf2_hmac_sha256(const char *password, const unsigned char *salt, int salt_len, int iter, unsigned char *out, int out_len)
{
	int blocks = (out_len + 31) / 32;
	for(int b = 1; b <= blocks; b++)
	{
		unsigned char U[32];
		unsigned char T[32];
		unsigned char salt_block[SALT_BYTES + 4];
		mem_copy(salt_block, salt, salt_len);
		salt_block[salt_len + 0] = (b >> 24) & 0xff;
		salt_block[salt_len + 1] = (b >> 16) & 0xff;
		salt_block[salt_len + 2] = (b >> 8) & 0xff;
		salt_block[salt_len + 3] = (b)&0xff;
		PHmacCtx h;
		hmac_init(h, (const unsigned char *)password, str_length(password));
		hmac_update(h, salt_block, salt_len + 4);
		hmac_final(h, U);
		mem_copy(T, U, 32);
		for(int i = 1; i < iter; i++)
		{
			PHmacCtx h2;
			hmac_init(h2, (const unsigned char *)password, str_length(password));
			hmac_update(h2, U, 32);
			hmac_final(h2, U);
			for(int k = 0; k < 32; k++)
				T[k] ^= U[k];
		}
		int copy = minimum(32, out_len - (b - 1) * 32);
		mem_copy(out + (b - 1) * 32, T, copy);
	}
}

static void bin_to_hex(const unsigned char *in, int len, char *out, int out_size)
{
	static const char *H = "0123456789abcdef";
	int need = len * 2 + 1;
	if(out_size < need)
	{
		if(out_size > 0)
			out[0] = '\0';
		return;
	}
	for(int i = 0; i < len; i++)
	{
		out[i * 2] = H[in[i] >> 4];
		out[i * 2 + 1] = H[in[i] & 0xf];
	}
	out[len * 2] = '\0';
}
static int hex_to_bin(const char *hex, unsigned char *out, int out_len)
{
	auto hv = [](char c) -> int { if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return 10+c-'a'; if(c>='A'&&c<='F') return 10+c-'A'; return -1; };
	int L = str_length(hex);
	if(L != out_len * 2)
		return 0;
	for(int i = 0; i < out_len; i++)
	{
		int hi = hv(hex[i * 2]);
		int lo = hv(hex[i * 2 + 1]);
		if(hi < 0 || lo < 0)
			return 0;
		out[i] = (hi << 4) | lo;
	}
	return 1;
}

int pw_hash_is_pbkdf2(const char *stored)
{
	return stored && str_find(stored, "PBKDF2$") == stored;
}

static int effective_iterations(int Iter)
{
	if(Iter <= 0)
	{
		int Conf = 0;
		Conf = g_Config.m_SvPasswordPbkdf2Iter;
		if(Conf <= 0)
			Conf = 120000;
		return Conf;
	}
	return Iter;
}

int pw_hash_generate(const char *pPassword, char *pOut, int OutSize, int Iterations)
{
	if(!pPassword || !pOut)
		return -1;
	Iterations = effective_iterations(Iterations);
	unsigned char salt[SALT_BYTES];
	
#if defined(CONF_FAMILY_WINDOWS)
	HCRYPTPROV hProv = 0;
	if(CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		if(!CryptGenRandom(hProv, SALT_BYTES, salt))
		{
			for(size_t i = 0; i < sizeof(salt); ++i)
				salt[i] = (unsigned char)(rand() % 256);
		}
		CryptReleaseContext(hProv, 0);
	}
	else
	{
		for(size_t i = 0; i < sizeof(salt); ++i)
			salt[i] = (unsigned char)(rand() % 256);
	}
#else
	int fd = open("/dev/urandom", O_RDONLY);
	if(fd >= 0)
	{
		size_t off = 0;
		while(off < sizeof(salt))
		{
			ssize_t r = read(fd, salt + off, sizeof(salt) - off);
			if(r <= 0)
				break;
			off += (size_t)r;
		}
		close(fd);
	}
	if(fd < 0)
	{
		for(size_t i = 0; i < sizeof(salt); ++i)
			salt[i] = (unsigned char)(rand() % 256);
	}
#endif
	unsigned char dk[DK_BYTES];
	pbkdf2_hmac_sha256(pPassword, salt, SALT_BYTES, Iterations, dk, DK_BYTES);
	char salt_hex[SALT_BYTES * 2 + 1];
	char dk_hex[DK_BYTES * 2 + 1];
	bin_to_hex(salt, SALT_BYTES, salt_hex, sizeof(salt_hex));
	bin_to_hex(dk, DK_BYTES, dk_hex, sizeof(dk_hex));
	str_format(pOut, OutSize, "PBKDF2$%d$%s$%s", Iterations, salt_hex, dk_hex);
	mem_zero(salt, sizeof(salt));
	mem_zero(dk, sizeof(dk));
	return 0;
}

int pw_hash_verify(const char *pPassword, const char *pStored)
{
	if(!pw_hash_is_pbkdf2(pStored))
		return 0;
	char copy[512];
	str_copy(copy, pStored, sizeof(copy));
	char *save = nullptr;
	char *tok = strtok_r(copy, "$", &save); // PBKDF2
	tok = strtok_r(nullptr, "$", &save);
	if(!tok)
		return 0;
	int iter = str_toint(tok);
	tok = strtok_r(nullptr, "$", &save);
	if(!tok)
		return 0;
	char *salt_hex = tok;
	tok = strtok_r(nullptr, "$", &save);
	if(!tok)
		return 0;
	char *dk_hex = tok;
	unsigned char salt[SALT_BYTES];
	unsigned char dk[DK_BYTES];
	unsigned char test[DK_BYTES];
	if(!hex_to_bin(salt_hex, salt, SALT_BYTES) || !hex_to_bin(dk_hex, dk, DK_BYTES))
		return 0;
	pbkdf2_hmac_sha256(pPassword, salt, SALT_BYTES, iter, test, DK_BYTES);
	unsigned char diff = 0;
	for(int i = 0; i < DK_BYTES; i++)
		diff |= (unsigned char)(test[i] ^ dk[i]);
	// wipe sensitive
	mem_zero(salt, sizeof(salt));
	mem_zero(dk, sizeof(dk));
	mem_zero(test, sizeof(test));
	return diff == 0;
}
