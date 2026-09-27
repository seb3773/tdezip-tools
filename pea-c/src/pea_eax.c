#include "pea_eax.h"
#include "pea.h"
#include "pea_block.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <openssl/provider.h>
#include <openssl/kdf.h>
#include <nettle/twofish.h>
#include <nettle/serpent.h>

enum {
    PEA_KDF_FIXED = 0,
    PEA_KDF_PBKDF2_P,
    PEA_KDF_SCRYPT,
    PEA_KDF_HYBRID
};

struct pea_crypto {
    int             algo;
    int             hmac;
    int             use_blk;
    int             cascade;
    size_t          keylen;
    size_t          noncelen;
    EVP_CIPHER_CTX *ecb;
    EVP_MAC_CTX    *hdr;
    EVP_MAC_CTX    *msg;
    uint8_t         ctr[16];
    uint8_t         ks[16];
    uint8_t         nonce_tag[16];
    int             ks_off;
    pea_eaxblk      eax;
    union {
        struct twofish_ctx tf;
        struct serpent_ctx sp;
    } blk;
    struct pea_crypto *layer[3];
};

static void inc_msb_full(uint8_t ctr[16])
{
    int i;
    for (i = 15; i >= 0; i--) {
        if (++ctr[i])
            break;
    }
}

static int aes_ecb_block(pea_crypto *e, const uint8_t in[16], uint8_t out[16])
{
    int outl = 0;
    if (EVP_EncryptUpdate(e->ecb, out, &outl, in, 16) != 1 || outl != 16)
        return -1;
    return 0;
}

static void load_legacy(void)
{
    static int once = 0;
    if (once)
        return;
    once = 1;
    OSSL_PROVIDER_load(NULL, "legacy");
    OSSL_PROVIDER_load(NULL, "default");
}

static EVP_MAC_CTX *cmac_new(const uint8_t *key, size_t keylen)
{
    EVP_MAC *mac;
    EVP_MAC_CTX *ctx;
    OSSL_PARAM params[2];
    char cipher[16];

    if (keylen == 32)
        memcpy(cipher, "AES-256-CBC", 12);
    else
        memcpy(cipher, "AES-128-CBC", 12);

    mac = EVP_MAC_fetch(NULL, "CMAC", NULL);
    if (!mac)
        return NULL;
    ctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (!ctx)
        return NULL;
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_CIPHER,
                                                 cipher, 0);
    params[1] = OSSL_PARAM_construct_end();
    if (EVP_MAC_init(ctx, key, keylen, params) != 1) {
        EVP_MAC_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

static EVP_MAC_CTX *hmac_sha1_new(const uint8_t *key, size_t keylen)
{
    EVP_MAC *mac;
    EVP_MAC_CTX *ctx;
    OSSL_PARAM params[2];
    char digest[] = "SHA1";

    mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    if (!mac)
        return NULL;
    ctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (!ctx)
        return NULL;
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
                                                 digest, 0);
    params[1] = OSSL_PARAM_construct_end();
    if (EVP_MAC_init(ctx, key, keylen, params) != 1) {
        EVP_MAC_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

static int mac_update(EVP_MAC_CTX *ctx, const void *b, size_t n)
{
    return EVP_MAC_update(ctx, (const unsigned char *)b, n) == 1 ? 0 : -1;
}

static int cmac_final(EVP_MAC_CTX *ctx, uint8_t tag[16])
{
    size_t outl = 16;
    if (EVP_MAC_final(ctx, tag, &outl, 16) != 1 || outl != 16)
        return -1;
    return 0;
}

static int pbkdf2(const uint8_t *pw, size_t pwlen,
                  const uint8_t *salt, size_t saltlen,
                  unsigned int iter, const EVP_MD *md,
                  uint8_t *out, size_t outlen)
{
    if (!md)
        return -1;
    if (PKCS5_PBKDF2_HMAC((const char *)pw, (int)pwlen,
                          salt, (int)saltlen, (int)iter, md,
                          (int)outlen, out) != 1)
        return -1;
    return 0;
}

static unsigned scrypt_memiter(uint8_t niter)
{
    switch (niter) {
    case 1: return 128u * 1024u;
    case 2: return 256u * 1024u;
    case 3: return 512u * 1024u;
    case 4:
    case 5:
    case 6:
    case 7: return 1024u * 1024u;
    default: return 64u * 1024u;
    }
}

static unsigned scrypt_piter(uint8_t niter)
{
    switch (niter) {
    case 5: return 2;
    case 6: return 4;
    case 7: return 8;
    default: return 1;
    }
}

static unsigned pbkdf2_intiter(int cid, uint8_t niter, int hybrid_sp)
{
    unsigned base = (cid == 0) ? 25000u : (cid == 1) ? 50000u : 75000u;
    if (hybrid_sp && cid == 2) {
        switch (niter) {
        case 1: return 275000u;
        case 2: return 575000u;
        case 3: return 1075000u;
        case 4: return 2075000u;
        case 5: return 5075000u;
        case 6: return 10075000u;
        case 7: return 25075000u;
        default: return 75000u;
        }
    }
    return (unsigned)niter * 100000u + base;
}

static int scrypt_derive(const uint8_t *pw, size_t pwlen,
                         const uint8_t *salt, size_t saltlen,
                         uint64_t n, uint32_t r, uint32_t p,
                         uint8_t *out, size_t outlen)
{
    EVP_KDF *kdf;
    EVP_KDF_CTX *ctx;
    OSSL_PARAM params[6];
    int rc = -1;
    uint64_t nn = n;
    uint32_t rr = r, pp = p;

    kdf = EVP_KDF_fetch(NULL, "SCRYPT", NULL);
    if (!kdf)
        return -1;
    ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx)
        return -1;
    params[0] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD,
                                                  (void *)pw, pwlen);
    params[1] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                                  (void *)salt, saltlen);
    params[2] = OSSL_PARAM_construct_uint64(OSSL_KDF_PARAM_SCRYPT_N, &nn);
    params[3] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_SCRYPT_R, &rr);
    params[4] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_SCRYPT_P, &pp);
    params[5] = OSSL_PARAM_construct_end();
    if (EVP_KDF_derive(ctx, out, outlen, params) == 1)
        rc = 0;
    EVP_KDF_CTX_free(ctx);
    return rc;
}

static int eax_setup(pea_crypto *e, const uint8_t *ak, size_t aklen,
                     const uint8_t *hk, size_t hklen)
{
    EVP_MAC_CTX *nonce_omac = NULL;
    uint8_t t_n[16];
    const EVP_CIPHER *cipher;
    int rc = -1;

    nonce_omac = cmac_new(ak, aklen);
    e->hdr = cmac_new(ak, aklen);
    e->msg = cmac_new(ak, aklen);
    if (!nonce_omac || !e->hdr || !e->msg)
        goto out;

    memset(t_n, 0, 16);
    if (mac_update(nonce_omac, t_n, 16) ||
        mac_update(nonce_omac, hk, hklen) ||
        cmac_final(nonce_omac, e->nonce_tag))
        goto out;

    t_n[15] = 1;
    if (mac_update(e->hdr, t_n, 16))
        goto out;
    t_n[15] = 2;
    if (mac_update(e->msg, t_n, 16))
        goto out;

    cipher = (aklen == 32) ? EVP_aes_256_ecb() : EVP_aes_128_ecb();
    e->ecb = EVP_CIPHER_CTX_new();
    if (!e->ecb)
        goto out;
    if (EVP_EncryptInit_ex(e->ecb, cipher, NULL, ak, NULL) != 1)
        goto out;
    EVP_CIPHER_CTX_set_padding(e->ecb, 0);

    memcpy(e->ctr, e->nonce_tag, 16);
    if (aes_ecb_block(e, e->ctr, e->ks))
        goto out;
    e->ks_off = 0;
    rc = 0;

out:
    EVP_MAC_CTX_free(nonce_omac);
    if (rc != 0) {
        EVP_MAC_CTX_free(e->hdr);
        EVP_MAC_CTX_free(e->msg);
        e->hdr = NULL;
        e->msg = NULL;
        if (e->ecb) {
            EVP_CIPHER_CTX_free(e->ecb);
            e->ecb = NULL;
        }
    }
    return rc;
}

static int hmac_setup(pea_crypto *e, const uint8_t *ak, size_t aklen,
                      const uint8_t *hk, size_t hklen)
{
    const EVP_CIPHER *cipher;

    e->msg = hmac_sha1_new(hk, hklen);
    if (!e->msg)
        return -1;

    cipher = (aklen == 32) ? EVP_aes_256_ecb() : EVP_aes_128_ecb();
    e->ecb = EVP_CIPHER_CTX_new();
    if (!e->ecb)
        return -1;
    if (EVP_EncryptInit_ex(e->ecb, cipher, NULL, ak, NULL) != 1)
        return -1;
    EVP_CIPHER_CTX_set_padding(e->ecb, 0);

    memset(e->ctr, 0, 16);
    if (aes_ecb_block(e, e->ctr, e->ks))
        return -1;
    e->ks_off = 0;
    return 0;
}

static int ctr_crypt(pea_crypto *e, uint8_t *buf, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (e->ks_off >= 16) {
            inc_msb_full(e->ctr);
            if (aes_ecb_block(e, e->ctr, e->ks))
                return -1;
            e->ks_off = 0;
        }
        buf[i] ^= e->ks[e->ks_off++];
    }
    return 0;
}

static int crypto_params(int algo, size_t *keylen, size_t *xkeylen, int *hmac)
{
    switch (algo) {
    case PEA_ALGO_EAX256:
    case PEA_ALGO_TF256:
    case PEA_ALGO_SP256:
        *keylen = 32;
        *xkeylen = 66;
        *hmac = 0;
        return 0;
    case PEA_ALGO_EAX:
    case PEA_ALGO_TF:
    case PEA_ALGO_SP:
        *keylen = 16;
        *xkeylen = 34;
        *hmac = 0;
        return 0;
    case PEA_ALGO_HMAC:
        *keylen = 16;
        *xkeylen = 34;
        *hmac = 1;
        return 0;
    default:
        return -1;
    }
}

static const EVP_MD *kdf_md(int algo)
{
    if (algo == PEA_ALGO_EAX256 ||
        algo == PEA_ALGO_TF256 ||
        algo == PEA_ALGO_SP256) {
        load_legacy();
        return EVP_whirlpool();
    }
    return EVP_sha1();
}

enum { CID_AES = 0, CID_TF = 1, CID_SP = 2 };

static int cascade_kdf(int algo)
{
    switch (algo) {
    case PEA_ALGO_TRIATS:
    case PEA_ALGO_TRITSA:
    case PEA_ALGO_TRISAT:
        return PEA_KDF_PBKDF2_P;
    case PEA_ALGO_SRIATS:
    case PEA_ALGO_SRITSA:
    case PEA_ALGO_SRISAT:
        return PEA_KDF_SCRYPT;
    case PEA_ALGO_HRIATS:
    case PEA_ALGO_HRITSA:
    case PEA_ALGO_HRISAT:
        return PEA_KDF_HYBRID;
    default:
        return -1;
    }
}

static void cascade_cids(int algo, int cid[3])
{
    switch (algo) {
    case PEA_ALGO_TRITSA:
    case PEA_ALGO_SRITSA:
    case PEA_ALGO_HRITSA:
        cid[0] = CID_TF; cid[1] = CID_SP; cid[2] = CID_AES;
        break;
    case PEA_ALGO_TRISAT:
    case PEA_ALGO_SRISAT:
    case PEA_ALGO_HRISAT:
        cid[0] = CID_SP; cid[1] = CID_AES; cid[2] = CID_TF;
        break;
    default:
        cid[0] = CID_AES; cid[1] = CID_TF; cid[2] = CID_SP;
        break;
    }
}

static int cid_algo(int cid)
{
    if (cid == CID_TF)
        return PEA_ALGO_TF256;
    if (cid == CID_SP)
        return PEA_ALGO_SP256;
    return PEA_ALGO_EAX256;
}

static const EVP_MD *cid_pbkdf2_md(int cid)
{
    if (cid == CID_TF)
        return EVP_sha512();
    if (cid == CID_SP)
        return EVP_sha3_512();
    load_legacy();
    return EVP_whirlpool();
}

static int derive_xkey_p(int cid, int kdf, uint8_t niter,
                         const uint8_t *pw, size_t pwlen,
                         const uint8_t *salt, uint8_t xkey[66])
{
    unsigned mem = scrypt_memiter(niter);
    unsigned p = scrypt_piter(niter);
    unsigned iter;
    uint64_t n;

    if (kdf == PEA_KDF_PBKDF2_P) {
        iter = pbkdf2_intiter(cid, niter, 0);
        return pbkdf2(pw, pwlen, salt, PEA_CRYPTO_SALT_LEN,
                      iter, cid_pbkdf2_md(cid), xkey, 66);
    }
    if (kdf == PEA_KDF_SCRYPT) {
        n = mem;
        return scrypt_derive(pw, pwlen, salt, PEA_CRYPTO_SALT_LEN,
                             n, 8, p, xkey, 66);
    }
    if (kdf == PEA_KDF_HYBRID) {
        if (cid == CID_AES) {
            n = mem;
            return scrypt_derive(pw, pwlen, salt, PEA_CRYPTO_SALT_LEN,
                                 n, 8, p, xkey, 66);
        }
        if (cid == CID_TF) {
            n = mem / 2;
            return scrypt_derive(pw, pwlen, salt, PEA_CRYPTO_SALT_LEN,
                                 n, 16, p, xkey, 66);
        }
        iter = pbkdf2_intiter(cid, niter, 1);
        return pbkdf2(pw, pwlen, salt, PEA_CRYPTO_SALT_LEN,
                      iter, cid_pbkdf2_md(cid), xkey, 66);
    }
    return -1;
}

static void xor_pw(uint8_t *dst, const uint8_t *src, size_t n,
                   int which, const char *aname)
{
    size_t i, alen = strlen(aname);
    uint8_t c;

    memcpy(dst, src, n);
    if (which == 0 || alen < 2)
        return;
    c = (uint8_t)aname[which == 1 ? alen - 2 : alen - 1];
    if (c >= 'a' && c <= 'z')
        c = (uint8_t)(c - 'a' + 'A');
    for (i = 0; i < n; i++) {
        if (which == 1)
            dst[i] ^= (uint8_t)(n + i) ^ c;
        else
            dst[i] ^= (uint8_t)(n ^ i) ^ c;
    }
}

static int is_tf(int algo)
{
    return algo == PEA_ALGO_TF || algo == PEA_ALGO_TF256;
}

static int is_sp(int algo)
{
    return algo == PEA_ALGO_SP || algo == PEA_ALGO_SP256;
}

static void tf_enc(void *c, const uint8_t in[16], uint8_t out[16])
{
    twofish_encrypt((struct twofish_ctx *)c, 16, out, in);
}

static void sp_enc(void *c, const uint8_t in[16], uint8_t out[16])
{
    serpent_encrypt((struct serpent_ctx *)c, 16, out, in);
}

static int blk_setup(pea_crypto *e, const uint8_t *ak, size_t aklen,
                     const uint8_t *hk, size_t hklen)
{
    e->use_blk = 1;
    if (is_tf(e->algo)) {
        twofish_set_key(&e->blk.tf, aklen, ak);
        return pea_eaxblk_init(&e->eax, tf_enc, &e->blk.tf, hk, hklen);
    }
    if (is_sp(e->algo)) {
        serpent_set_key(&e->blk.sp, aklen, ak);
        return pea_eaxblk_init(&e->eax, sp_enc, &e->blk.sp, hk, hklen);
    }
    return -1;
}

pea_crypto *pea_crypto_new(void)
{
    return calloc(1, sizeof(pea_crypto));
}

void pea_crypto_free(pea_crypto *e)
{
    int i;
    if (!e)
        return;
    for (i = 0; i < 3; i++) {
        if (e->layer[i]) {
            pea_crypto_free(e->layer[i]);
            e->layer[i] = NULL;
        }
    }
    if (e->ecb)
        EVP_CIPHER_CTX_free(e->ecb);
    EVP_MAC_CTX_free(e->hdr);
    EVP_MAC_CTX_free(e->msg);
    memset(e, 0, sizeof(*e));
    free(e);
}

static int init_common(pea_crypto *e, int algo,
                       const uint8_t *pw, size_t pwlen,
                       uint8_t *subhdr, int encrypt,
                       int kdf, uint8_t niter, int cid, int check_pwver,
                       uint16_t *out_pv)
{
    uint8_t xkey[66];
    size_t keylen = 0, xkeylen = 0;
    int hmac = 0, rc;
    const uint8_t *salt;

    if (!pw || pwlen == 0)
        return -1;
    if (crypto_params(algo, &keylen, &xkeylen, &hmac))
        return -1;

    e->algo = algo;
    e->hmac = hmac;
    e->keylen = keylen;
    e->noncelen = keylen;

    if (encrypt) {
        uint8_t newsalt[PEA_CRYPTO_SALT_LEN];
        if (RAND_bytes(newsalt, PEA_CRYPTO_SALT_LEN) != 1)
            return -1;
        memset(subhdr, 0, PEA_CRYPTO_HDR_LEN);
        memcpy(subhdr + 2, newsalt, PEA_CRYPTO_SALT_LEN);
    }
    salt = subhdr + 2;

    if (kdf == PEA_KDF_FIXED) {
        if (pbkdf2(pw, pwlen, salt, PEA_CRYPTO_SALT_LEN,
                   PEA_CRYPTO_PBKDF2_ITER, kdf_md(algo), xkey, xkeylen))
            return -1;
    } else {
        if (derive_xkey_p(cid, kdf, niter, pw, pwlen, salt, xkey))
            return -1;
    }

    if (out_pv)
        *out_pv = (uint16_t)xkey[xkeylen - 2] |
                  ((uint16_t)xkey[xkeylen - 1] << 8);

    if (encrypt) {
        subhdr[14] = xkey[xkeylen - 2];
        subhdr[15] = xkey[xkeylen - 1];
    } else if (check_pwver &&
               (xkey[xkeylen - 2] != subhdr[14] ||
                xkey[xkeylen - 1] != subhdr[15])) {
        memset(xkey, 0, sizeof(xkey));
        return -2;
    }

    if (hmac)
        rc = hmac_setup(e, xkey, keylen, xkey + keylen, keylen);
    else if (is_tf(algo) || is_sp(algo))
        rc = blk_setup(e, xkey, keylen, xkey + keylen, keylen);
    else
        rc = eax_setup(e, xkey, keylen, xkey + keylen, keylen);
    memset(xkey, 0, sizeof(xkey));
    return rc;
}

static int layer_final(pea_crypto *e, uint8_t tag[16])
{
    if (e->use_blk)
        return pea_eaxblk_final(&e->eax, tag);
    if (e->hmac) {
        uint8_t mac[20];
        size_t outl = 20;
        if (EVP_MAC_final(e->msg, mac, &outl, sizeof(mac)) != 1 || outl < 16)
            return -1;
        memcpy(tag, mac, 16);
        return 0;
    } else {
        uint8_t ht[16];
        int i;
        if (cmac_final(e->hdr, ht) || cmac_final(e->msg, tag))
            return -1;
        for (i = 0; i < 16; i++)
            tag[i] ^= ht[i] ^ e->nonce_tag[i];
        return 0;
    }
}

static int sha3_384(const uint8_t *in, size_t n, uint8_t out[48])
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned int outl = 48;
    int rc = -1;
    if (!ctx)
        return -1;
    if (EVP_DigestInit_ex(ctx, EVP_sha3_384(), NULL) == 1 &&
        EVP_DigestUpdate(ctx, in, n) == 1 &&
        EVP_DigestFinal_ex(ctx, out, &outl) == 1 && outl == 48)
        rc = 0;
    EVP_MD_CTX_free(ctx);
    return rc;
}

static int init_cascade(pea_crypto *e, int algo,
                        const uint8_t *pw, size_t pwlen,
                        uint8_t niter, uint8_t *subhdr, int encrypt)
{
    int cid[3], kdf, i;
    uint8_t *tmp = NULL;
    uint16_t pv[3];
    uint16_t stored;
    const char *aname = pea_algo_name(algo);

    kdf = cascade_kdf(algo);
    if (kdf < 0)
        return -1;
    cascade_cids(algo, cid);
    e->algo = algo;
    e->cascade = 1;

    tmp = malloc(pwlen);
    if (!tmp)
        return -1;

    for (i = 0; i < 3; i++) {
        int rc;
        e->layer[i] = pea_crypto_new();
        if (!e->layer[i]) {
            free(tmp);
            return -1;
        }
        xor_pw(tmp, pw, pwlen, i, aname);
        rc = init_common(e->layer[i], cid_algo(cid[i]), tmp, pwlen,
                         subhdr + i * PEA_CRYPTO_HDR_LEN, encrypt,
                         kdf, niter, cid[i], 0, &pv[i]);
        if (rc) {
            memset(tmp, 0, pwlen);
            free(tmp);
            return rc;
        }
    }
    memset(tmp, 0, pwlen);
    free(tmp);

    if (encrypt) {
        uint16_t verw = (uint16_t)(pv[0] ^ pv[1] ^ pv[2]);
        subhdr[0 * 16 + 14] = 0;
        subhdr[0 * 16 + 15] = 0;
        subhdr[1 * 16 + 14] = 0;
        subhdr[1 * 16 + 15] = 0;
        subhdr[2 * 16 + 14] = (uint8_t)verw;
        subhdr[2 * 16 + 15] = (uint8_t)(verw >> 8);
    } else {
        stored = (uint16_t)subhdr[2 * 16 + 14] |
                 ((uint16_t)subhdr[2 * 16 + 15] << 8);
        if (stored != (uint16_t)(pv[0] ^ pv[1] ^ pv[2]))
            return -2;
    }
    return 0;
}

int pea_crypto_init_encrypt(pea_crypto *e, int algo,
                            const uint8_t *pw, size_t pwlen,
                            uint8_t niter, uint8_t *subhdr)
{
    if (pea_algo_is_cascade(algo))
        return init_cascade(e, algo, pw, pwlen, niter, subhdr, 1);
    return init_common(e, algo, pw, pwlen, subhdr, 1,
                       PEA_KDF_FIXED, 0, 0, 1, NULL);
}

int pea_crypto_init_decrypt(pea_crypto *e, int algo,
                            const uint8_t *pw, size_t pwlen,
                            uint8_t niter, const uint8_t *subhdr)
{
    if (pea_algo_is_cascade(algo))
        return init_cascade(e, algo, pw, pwlen, niter,
                            (uint8_t *)subhdr, 0);
    return init_common(e, algo, pw, pwlen, (uint8_t *)subhdr, 0,
                       PEA_KDF_FIXED, 0, 0, 1, NULL);
}

int pea_crypto_encrypt(pea_crypto *e, void *buf, size_t n)
{
    int i;
    if (n == 0)
        return 0;
    if (e->cascade) {
        for (i = 0; i < 3; i++) {
            if (pea_crypto_encrypt(e->layer[i], buf, n))
                return -1;
        }
        return 0;
    }
    if (e->use_blk)
        return pea_eaxblk_encrypt(&e->eax, buf, n);
    if (ctr_crypt(e, (uint8_t *)buf, n))
        return -1;
    return mac_update(e->msg, buf, n);
}

int pea_crypto_decrypt(pea_crypto *e, void *buf, size_t n)
{
    int i;
    if (n == 0)
        return 0;
    if (e->cascade) {
        for (i = 2; i >= 0; i--) {
            if (pea_crypto_decrypt(e->layer[i], buf, n))
                return -1;
        }
        return 0;
    }
    if (e->use_blk)
        return pea_eaxblk_decrypt(&e->eax, buf, n);
    if (mac_update(e->msg, buf, n))
        return -1;
    return ctr_crypt(e, (uint8_t *)buf, n);
}

int pea_crypto_final(pea_crypto *e, uint8_t *tag)
{
    if (e->cascade) {
        uint8_t raw[48];
        int i;
        for (i = 0; i < 3; i++) {
            if (layer_final(e->layer[i], raw + i * 16))
                return -1;
        }
        return sha3_384(raw, 48, tag);
    }
    return layer_final(e, tag);
}

int pea_make_pw_material(const char *password, const char *keyfile,
                         const uint8_t hdr[10], const uint8_t shdr[10],
                         uint8_t **out, size_t *outlen)
{
    size_t pwlen, extra = 0;
    uint8_t kf[PEA_KEYFILE_MAX];
    uint8_t *m;

    if (!password || !password[0])
        return -1;
    pwlen = strlen(password);

    if (keyfile && keyfile[0]) {
        FILE *fp = fopen(keyfile, "rb");
        size_t n;
        if (!fp) {
            fprintf(stderr, "pea: cannot open keyfile %s: %s\n",
                    keyfile, strerror(errno));
            return -1;
        }
        n = fread(kf, 1, PEA_KEYFILE_MAX, fp);
        fclose(fp);
        extra = n;
    }

    m = malloc(pwlen + 22 + extra);
    if (!m)
        return -1;
    memcpy(m, password, pwlen);
    memcpy(m + pwlen, hdr, 10);
    memcpy(m + pwlen + 10, shdr, 10);
    m[pwlen + 20] = 0;
    m[pwlen + 21] = 0;
    if (extra)
        memcpy(m + pwlen + 22, kf, extra);
    *out = m;
    *outlen = pwlen + 22 + extra;
    return 0;
}
