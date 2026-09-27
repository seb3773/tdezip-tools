#include "pea_digest.h"
#include "pea.h"

#include <string.h>
#include <stdlib.h>

#include <zlib.h>
#include <openssl/evp.h>
#include <openssl/provider.h>

static void load_legacy(void)
{
    static int once = 0;
    if (once)
        return;
    once = 1;
    OSSL_PROVIDER_load(NULL, "legacy");
    OSSL_PROVIDER_load(NULL, "default");
}

/* CRC-64/ECMA-182 (Ehrhardt crc64.pas): poly 0x42F0E1EBA9EA3693,
 * init/xorout 0xFFFFFFFFFFFFFFFF. Tag = lo32 then hi32, little-endian. */
static uint64_t crc64_tab[256];
static int crc64_tab_ready;

static void crc64_tab_init(void)
{
    int i, b;
    if (crc64_tab_ready)
        return;
    for (i = 0; i < 256; i++) {
        uint64_t c = (uint64_t)i << 56;
        for (b = 0; b < 8; b++) {
            if (c & 0x8000000000000000ull)
                c = (c << 1) ^ 0x42F0E1EBA9EA3693ull;
            else
                c <<= 1;
        }
        crc64_tab[i] = c;
    }
    crc64_tab_ready = 1;
}

static uint64_t crc64_update(uint64_t crc, const uint8_t *p, size_t n)
{
    size_t i;
    crc64_tab_init();
    for (i = 0; i < n; i++)
        crc = crc64_tab[(crc >> 56) ^ p[i]] ^ (crc << 8);
    return crc;
}

static const EVP_MD *algo_md(int algo)
{
    switch (algo) {
    case PEA_ALGO_MD5:
        return EVP_md5();
    case PEA_ALGO_RIPEMD160:
        load_legacy();
        return EVP_ripemd160();
    case PEA_ALGO_SHA1:
        return EVP_sha1();
    case PEA_ALGO_SHA256:
        return EVP_sha256();
    case PEA_ALGO_SHA512:
        return EVP_sha512();
    case PEA_ALGO_WHIRLPOOL:
        load_legacy();
        return EVP_whirlpool();
    case PEA_ALGO_SHA3_256:
        return EVP_sha3_256();
    case PEA_ALGO_SHA3_512:
        return EVP_sha3_512();
    case PEA_ALGO_BLAKE2S:
        return EVP_blake2s256();
    case PEA_ALGO_BLAKE2B:
        return EVP_blake2b512();
    default:
        return NULL;
    }
}

int pea_stream_authsize(int algo)
{
    switch (algo) {
    case PEA_ALGO_NOALGO:    return 0;
    case PEA_ALGO_ADLER32:
    case PEA_ALGO_CRC32:     return 4;
    case PEA_ALGO_CRC64:     return 8;
    case PEA_ALGO_MD5:       return 16;
    case PEA_ALGO_RIPEMD160: return 20;
    case PEA_ALGO_SHA1:      return 20;
    case PEA_ALGO_SHA256:    return 32;
    case PEA_ALGO_SHA512:    return 64;
    case PEA_ALGO_SHA3_256:  return 32;
    case PEA_ALGO_SHA3_512:  return 64;
    case PEA_ALGO_WHIRLPOOL: return 64;
    case PEA_ALGO_BLAKE2S:   return 32;
    case PEA_ALGO_BLAKE2B:   return 64;
    case PEA_ALGO_HMAC:
    case PEA_ALGO_EAX:
    case PEA_ALGO_TF:
    case PEA_ALGO_SP:
    case PEA_ALGO_EAX256:
    case PEA_ALGO_TF256:
    case PEA_ALGO_SP256:     return 16;
    case PEA_ALGO_TRIATS:
    case PEA_ALGO_TRITSA:
    case PEA_ALGO_TRISAT:
    case PEA_ALGO_SRIATS:
    case PEA_ALGO_SRITSA:
    case PEA_ALGO_SRISAT:
    case PEA_ALGO_HRIATS:
    case PEA_ALGO_HRITSA:
    case PEA_ALGO_HRISAT:    return 48;
    default:                 return -1;
    }
}

int pea_obj_authsize(int algo)
{
    switch (algo) {
    case PEA_ALGO_NOALGO:    return 0;
    case PEA_ALGO_ADLER32:
    case PEA_ALGO_CRC32:     return 4;
    case PEA_ALGO_CRC64:     return 8;
    case PEA_ALGO_MD5:       return 16;
    case PEA_ALGO_RIPEMD160: return 20;
    case PEA_ALGO_SHA1:      return 20;
    case PEA_ALGO_SHA256:    return 32;
    case PEA_ALGO_SHA512:    return 64;
    case PEA_ALGO_SHA3_256:  return 32;
    case PEA_ALGO_SHA3_512:  return 64;
    case PEA_ALGO_WHIRLPOOL: return 64;
    case PEA_ALGO_BLAKE2S:   return 32;
    case PEA_ALGO_BLAKE2B:   return 64;
    default:                 return -1;
    }
}

int pea_algo_needs_password(int algo)
{
    switch (algo) {
    case PEA_ALGO_HMAC:
    case PEA_ALGO_EAX:
    case PEA_ALGO_TF:
    case PEA_ALGO_SP:
    case PEA_ALGO_EAX256:
    case PEA_ALGO_TF256:
    case PEA_ALGO_SP256:
    case PEA_ALGO_TRIATS:
    case PEA_ALGO_TRITSA:
    case PEA_ALGO_TRISAT:
    case PEA_ALGO_SRIATS:
    case PEA_ALGO_SRITSA:
    case PEA_ALGO_SRISAT:
    case PEA_ALGO_HRIATS:
    case PEA_ALGO_HRITSA:
    case PEA_ALGO_HRISAT:
        return 1;
    default:
        return 0;
    }
}

int pea_algo_is_cascade(int algo)
{
    switch (algo) {
    case PEA_ALGO_TRIATS:
    case PEA_ALGO_TRITSA:
    case PEA_ALGO_TRISAT:
    case PEA_ALGO_SRIATS:
    case PEA_ALGO_SRITSA:
    case PEA_ALGO_SRISAT:
    case PEA_ALGO_HRIATS:
    case PEA_ALGO_HRITSA:
    case PEA_ALGO_HRISAT:
        return 1;
    default:
        return 0;
    }
}

int pea_crypto_hdr_len(int algo)
{
    if (pea_algo_is_cascade(algo))
        return 48;
    if (pea_algo_needs_password(algo))
        return 16;
    return 0;
}

const char *pea_algo_name(int algo)
{
    switch (algo) {
    case PEA_ALGO_NOALGO:    return "NOALGO";
    case PEA_ALGO_ADLER32:   return "ADLER32";
    case PEA_ALGO_CRC32:     return "CRC32";
    case PEA_ALGO_CRC64:     return "CRC64";
    case PEA_ALGO_MD5:       return "MD5";
    case PEA_ALGO_RIPEMD160: return "RIPEMD160";
    case PEA_ALGO_SHA1:      return "SHA1";
    case PEA_ALGO_SHA256:    return "SHA256";
    case PEA_ALGO_SHA512:    return "SHA512";
    case PEA_ALGO_WHIRLPOOL: return "WHIRLPOOL";
    case PEA_ALGO_SHA3_256:  return "SHA3_256";
    case PEA_ALGO_SHA3_512:  return "SHA3_512";
    case PEA_ALGO_BLAKE2S:   return "BLAKE2S";
    case PEA_ALGO_BLAKE2B:   return "BLAKE2B";
    case PEA_ALGO_HMAC:      return "HMAC";
    case PEA_ALGO_EAX:       return "EAX";
    case PEA_ALGO_TF:        return "TF";
    case PEA_ALGO_SP:        return "SP";
    case PEA_ALGO_EAX256:    return "EAX256";
    case PEA_ALGO_TF256:     return "TF256";
    case PEA_ALGO_SP256:     return "SP256";
    case PEA_ALGO_TRIATS:    return "TRIATS";
    case PEA_ALGO_TRITSA:    return "TRITSA";
    case PEA_ALGO_TRISAT:    return "TRISAT";
    case PEA_ALGO_SRIATS:    return "SRIATS";
    case PEA_ALGO_SRITSA:    return "SRITSA";
    case PEA_ALGO_SRISAT:    return "SRISAT";
    case PEA_ALGO_HRIATS:    return "HRIATS";
    case PEA_ALGO_HRITSA:    return "HRITSA";
    case PEA_ALGO_HRISAT:    return "HRISAT";
    default:                 return "UNKNOWN";
    }
}

int pea_algo_from_name(const char *name)
{
    static const int codes[] = {
        PEA_ALGO_NOALGO, PEA_ALGO_ADLER32, PEA_ALGO_CRC32, PEA_ALGO_CRC64,
        PEA_ALGO_MD5, PEA_ALGO_RIPEMD160, PEA_ALGO_SHA1, PEA_ALGO_SHA256,
        PEA_ALGO_SHA512, PEA_ALGO_WHIRLPOOL, PEA_ALGO_SHA3_256,
        PEA_ALGO_SHA3_512, PEA_ALGO_BLAKE2S, PEA_ALGO_BLAKE2B,
        PEA_ALGO_HMAC, PEA_ALGO_EAX, PEA_ALGO_TF, PEA_ALGO_SP,
        PEA_ALGO_EAX256, PEA_ALGO_TF256, PEA_ALGO_SP256,
        PEA_ALGO_TRIATS, PEA_ALGO_TRITSA, PEA_ALGO_TRISAT,
        PEA_ALGO_SRIATS, PEA_ALGO_SRITSA, PEA_ALGO_SRISAT,
        PEA_ALGO_HRIATS, PEA_ALGO_HRITSA, PEA_ALGO_HRISAT
    };
    size_t i;
    char *end;
    long num;

    if (!name)
        return -1;

    if (name[0] >= '0' && name[0] <= '9') {
        num = strtol(name, &end, 0);
        if (*end == '\0' && num >= 0 && num <= 255)
            return (int)num;
    }

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *n = pea_algo_name(codes[i]);
        size_t j = 0;
        while (n[j] && name[j]) {
            char a = n[j], b = name[j];
            if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
            if (a != b)
                break;
            j++;
        }
        if (n[j] == '\0' && name[j] == '\0')
            return codes[i];
    }
    return -1;
}

const char *pea_compr_name(int compr)
{
    switch (compr) {
    case PEA_COMP_STORE: return "PCOMPRESS0";
    case PEA_COMP_L3:    return "PCOMPRESS1";
    case PEA_COMP_L6:    return "PCOMPRESS2";
    case PEA_COMP_L9:    return "PCOMPRESS3";
    default:             return "UNKNOWN";
    }
}

int pea_compr_from_name(const char *name)
{
    if (!name)
        return -1;
    if (!strcasecmp(name, "PCOMPRESS0") || !strcasecmp(name, "STORE") || !strcmp(name, "0"))
        return PEA_COMP_STORE;
    if (!strcasecmp(name, "PCOMPRESS1") || !strcmp(name, "1"))
        return PEA_COMP_L3;
    if (!strcasecmp(name, "PCOMPRESS2") || !strcmp(name, "6") || !strcmp(name, "2"))
        return PEA_COMP_L6;
    if (!strcasecmp(name, "PCOMPRESS3") || !strcmp(name, "9") || !strcmp(name, "3"))
        return PEA_COMP_L9;
    return -1;
}

int pea_ctl_init(pea_ctl *c, int algo)
{
    memset(c, 0, sizeof(*c));
    c->algo = algo;

    if (algo == PEA_ALGO_NOALGO)
        return 0;

    if (algo == PEA_ALGO_ADLER32) {
        c->adler = adler32(0L, Z_NULL, 0);
        c->active = 1;
        return 0;
    }
    if (algo == PEA_ALGO_CRC32) {
        c->crc = crc32(0L, Z_NULL, 0);
        c->active = 1;
        return 0;
    }
    if (algo == PEA_ALGO_CRC64) {
        c->crc64 = 0xFFFFFFFFFFFFFFFFull;
        c->active = 1;
        return 0;
    }

    {
        const EVP_MD *md = algo_md(algo);
        EVP_MD_CTX *ctx;
        if (!md)
            return -1;
        ctx = EVP_MD_CTX_new();
        if (!ctx)
            return -1;
        if (EVP_DigestInit_ex(ctx, md, NULL) != 1) {
            EVP_MD_CTX_free(ctx);
            return -1;
        }
        c->md = ctx;
        c->active = 1;
        return 0;
    }
}

int pea_ctl_update(pea_ctl *c, const void *buf, size_t n)
{
    if (!c->active || n == 0)
        return 0;
    if (c->algo == PEA_ALGO_ADLER32) {
        c->adler = adler32(c->adler, (const Bytef *)buf, (uInt)n);
        return 0;
    }
    if (c->algo == PEA_ALGO_CRC32) {
        c->crc = crc32(c->crc, (const Bytef *)buf, (uInt)n);
        return 0;
    }
    if (c->algo == PEA_ALGO_CRC64) {
        c->crc64 = crc64_update(c->crc64, (const uint8_t *)buf, n);
        return 0;
    }
    if (c->md) {
        if (EVP_DigestUpdate((EVP_MD_CTX *)c->md, buf, n) != 1)
            return -1;
        return 0;
    }
    return -1;
}

int pea_ctl_final(pea_ctl *c, uint8_t *out, int outlen)
{
    if (c->algo == PEA_ALGO_NOALGO)
        return outlen == 0 ? 0 : -1;

    if (c->algo == PEA_ALGO_ADLER32) {
        if (outlen != 4)
            return -1;
        out[0] = (uint8_t)(c->adler);
        out[1] = (uint8_t)(c->adler >> 8);
        out[2] = (uint8_t)(c->adler >> 16);
        out[3] = (uint8_t)(c->adler >> 24);
        return 0;
    }
    if (c->algo == PEA_ALGO_CRC32) {
        if (outlen != 4)
            return -1;
        out[0] = (uint8_t)(c->crc);
        out[1] = (uint8_t)(c->crc >> 8);
        out[2] = (uint8_t)(c->crc >> 16);
        out[3] = (uint8_t)(c->crc >> 24);
        return 0;
    }
    if (c->algo == PEA_ALGO_CRC64) {
        uint64_t v;
        if (outlen != 8)
            return -1;
        v = c->crc64 ^ 0xFFFFFFFFFFFFFFFFull;
        out[0] = (uint8_t)(v);
        out[1] = (uint8_t)(v >> 8);
        out[2] = (uint8_t)(v >> 16);
        out[3] = (uint8_t)(v >> 24);
        out[4] = (uint8_t)(v >> 32);
        out[5] = (uint8_t)(v >> 40);
        out[6] = (uint8_t)(v >> 48);
        out[7] = (uint8_t)(v >> 56);
        return 0;
    }
    if (c->md) {
        unsigned int len = 0;
        uint8_t tmp[EVP_MAX_MD_SIZE];
        if (EVP_DigestFinal_ex((EVP_MD_CTX *)c->md, tmp, &len) != 1)
            return -1;
        if ((int)len != outlen)
            return -1;
        memcpy(out, tmp, len);
        return 0;
    }
    return -1;
}

void pea_ctl_free(pea_ctl *c)
{
    if (c->md) {
        EVP_MD_CTX_free((EVP_MD_CTX *)c->md);
        c->md = NULL;
    }
    c->active = 0;
}
