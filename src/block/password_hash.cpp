#include "password_hash.h"

#include <base/hash_ctxt.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <block/base.h>
#include <fcntl.h>

#include <cstdlib>
#include <cstring>
#if defined(CONF_FAMILY_WINDOWS)
#include <Windows.h>
#include <io.h>
#include <wincrypt.h>
#else
#include <unistd.h>

#include <algorithm>
#endif

#if defined(CONF_FAMILY_WINDOWS)
#define strtok_r strtok_s
#endif

// Internal constants
static const int SALT_BYTES = 16;
static const int DK_BYTES = 32;

struct SPHmacCtx
{
	SHA256_CTX m_Inner;
	SHA256_CTX m_Outer;
};
static void hmac_init(SPHmacCtx &Ctx, const unsigned char *Key, size_t KeyLen)
{
	unsigned char K0[64];
	mem_zero(K0, sizeof(K0));
	if(KeyLen > 64)
	{
		SHA256_CTX t;
		sha256_init(&t);
		sha256_update(&t, Key, KeyLen);
		auto h = sha256_finish(&t);
		mem_copy(K0, h.data, 32);
	}
	else
	{
		mem_copy(K0, Key, KeyLen);
	}
	unsigned char Ipad[64];
	unsigned char Opad[64];
	for(int i = 0; i < 64; i++)
	{
		Ipad[i] = K0[i] ^ 0x36;
		Opad[i] = K0[i] ^ 0x5c;
	}
	sha256_init(&Ctx.m_Inner);
	sha256_update(&Ctx.m_Inner, Ipad, 64);
	sha256_init(&Ctx.m_Outer);
	sha256_update(&Ctx.m_Outer, Opad, 64);
}
static void hmac_update(SPHmacCtx &Ctx, const unsigned char *Data, size_t Len) { sha256_update(&Ctx.m_Inner, Data, Len); }
static void hmac_final(SPHmacCtx &Ctx, unsigned char *Mac)
{
	auto Inner = sha256_finish(&Ctx.m_Inner);
	sha256_update(&Ctx.m_Outer, Inner.data, 32);
	auto Outer = sha256_finish(&Ctx.m_Outer);
	mem_copy(Mac, Outer.data, 32);
}

static void pbkdf2_hmac_sha256(const char *Password, const unsigned char *Salt, int SaltLen, int Iter, unsigned char *Out, int OutLen)
{
	int Blocks = (OutLen + 31) / 32;
	for(int b = 1; b <= Blocks; b++)
	{
		unsigned char U[32];
		unsigned char T[32];
		unsigned char SaltBlock[SALT_BYTES + 4];
		mem_copy(SaltBlock, Salt, SaltLen);
		SaltBlock[SaltLen + 0] = (b >> 24) & 0xff;
		SaltBlock[SaltLen + 1] = (b >> 16) & 0xff;
		SaltBlock[SaltLen + 2] = (b >> 8) & 0xff;
		SaltBlock[SaltLen + 3] = (b) & 0xff;
		SPHmacCtx h;
		hmac_init(h, (const unsigned char *)Password, str_length(Password));
		hmac_update(h, SaltBlock, SaltLen + 4);
		hmac_final(h, U);
		mem_copy(T, U, 32);
		for(int i = 1; i < Iter; i++)
		{
			SPHmacCtx H2;
			hmac_init(H2, (const unsigned char *)Password, str_length(Password));
			hmac_update(H2, U, 32);
			hmac_final(H2, U);
			for(int k = 0; k < 32; k++)
				T[k] ^= U[k];
		}
		int Copy = std::min(32, OutLen - (b - 1) * 32);
		mem_copy(Out + (b - 1) * 32, T, Copy);
	}
}

static void bin_to_hex(const unsigned char *In, int Len, char *Out, int OutSize)
{
	static const char *s_H = "0123456789abcdef";
	int Need = Len * 2 + 1;
	if(OutSize < Need)
	{
		if(OutSize > 0)
			Out[0] = '\0';
		return;
	}
	for(int i = 0; i < Len; i++)
	{
		Out[i * 2] = s_H[In[i] >> 4];
		Out[i * 2 + 1] = s_H[In[i] & 0xf];
	}
	Out[Len * 2] = '\0';
}
static int hex_to_bin(const char *Hex, unsigned char *Out, int OutLen)
{
	auto Hv = [](char c) -> int { if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return 10+c-'a'; if(c>='A'&&c<='F') return 10+c-'A'; return -1; };
	int L = str_length(Hex);
	if(L != OutLen * 2)
		return 0;
	for(int i = 0; i < OutLen; i++)
	{
		int Hi = Hv(Hex[i * 2]);
		int Lo = Hv(Hex[i * 2 + 1]);
		if(Hi < 0 || Lo < 0)
			return 0;
		Out[i] = (Hi << 4) | Lo;
	}
	return 1;
}

int pw_hash_is_pbkdf2(const char *Stored)
{
	return Stored && str_find(Stored, "PBKDF2$") == Stored;
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
	unsigned char aSalt[SALT_BYTES];

#if defined(CONF_FAMILY_WINDOWS)
	HCRYPTPROV hProv = 0;
	if(CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		if(!CryptGenRandom(hProv, SALT_BYTES, aSalt))
		{
			for(size_t i = 0; i < sizeof(aSalt); ++i)
				aSalt[i] = (unsigned char)(rand() % 256);
		}
		CryptReleaseContext(hProv, 0);
	}
	else
	{
		for(size_t i = 0; i < sizeof(aSalt); ++i)
			aSalt[i] = (unsigned char)(rand() % 256);
	}
#else
	int Fd = open("/dev/urandom", O_RDONLY);
	if(Fd >= 0)
	{
		size_t Off = 0;
		while(Off < sizeof(aSalt))
		{
			ssize_t r = read(Fd, aSalt + Off, sizeof(aSalt) - Off);
			if(r <= 0)
				break;
			Off += (size_t)r;
		}
		close(Fd);
	}
	if(Fd < 0)
	{
		for(unsigned char &i : aSalt)
			i = (unsigned char)(rand() % 256);
	}
#endif
	unsigned char Dk[DK_BYTES];
	pbkdf2_hmac_sha256(pPassword, aSalt, SALT_BYTES, Iterations, Dk, DK_BYTES);
	char SaltHex[SALT_BYTES * 2 + 1];
	char DkHex[DK_BYTES * 2 + 1];
	bin_to_hex(aSalt, SALT_BYTES, SaltHex, sizeof(SaltHex));
	bin_to_hex(Dk, DK_BYTES, DkHex, sizeof(DkHex));
	str_format(pOut, OutSize, "PBKDF2$%d$%s$%s", Iterations, SaltHex, DkHex);
	mem_zero(aSalt, sizeof(aSalt));
	mem_zero(Dk, sizeof(Dk));
	return 0;
}

int pw_hash_verify(const char *pPassword, const char *pStored)
{
	if(!pw_hash_is_pbkdf2(pStored))
		return 0;
	char Copy[512];
	str_copy(Copy, pStored, sizeof(Copy));
	char *Save = nullptr;
	(void)strtok_r(Copy, "$", &Save); // the "PBKDF2" tag, not needed
	char *Tok = strtok_r(nullptr, "$", &Save);
	if(!Tok)
		return 0;
	int Iter = str_toint(Tok);
	Tok = strtok_r(nullptr, "$", &Save);
	if(!Tok)
		return 0;
	char *SaltHex = Tok;
	Tok = strtok_r(nullptr, "$", &Save);
	if(!Tok)
		return 0;
	char *DkHex = Tok;
	unsigned char Salt[SALT_BYTES];
	unsigned char Dk[DK_BYTES];
	unsigned char Test[DK_BYTES];
	if(!hex_to_bin(SaltHex, Salt, SALT_BYTES) || !hex_to_bin(DkHex, Dk, DK_BYTES))
		return 0;
	pbkdf2_hmac_sha256(pPassword, Salt, SALT_BYTES, Iter, Test, DK_BYTES);
	unsigned char Diff = 0;
	for(int i = 0; i < DK_BYTES; i++)
		Diff |= (unsigned char)(Test[i] ^ Dk[i]);
	// wipe sensitive
	mem_zero(Salt, sizeof(Salt));
	mem_zero(Dk, sizeof(Dk));
	mem_zero(Test, sizeof(Test));
	return Diff == 0;
}
