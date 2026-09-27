#include "pea_block.h"

#include <string.h>

static void inc_msb_full(uint8_t ctr[16])
{
    int i;
    for (i = 15; i >= 0; i--) {
        if (++ctr[i])
            break;
    }
}

static void xor16(uint8_t *d, const uint8_t *a, const uint8_t *b)
{
    int i;
    for (i = 0; i < 16; i++)
        d[i] = a[i] ^ b[i];
}

static void mul_u(uint8_t L[16])
{
    uint8_t mask = (uint8_t)((L[0] >> 7) * 0x87);
    int i;
    for (i = 0; i < 15; i++)
        L[i] = (uint8_t)((L[i] << 1) | (L[i + 1] >> 7));
    L[15] = (uint8_t)((L[15] << 1) ^ mask);
}

int pea_omac_init(pea_omac *o, pea_blkfn enc, void *cipher)
{
    memset(o, 0, sizeof(*o));
    o->enc = enc;
    o->cipher = cipher;
    return 0;
}

int pea_omac_update(pea_omac *o, const void *b, size_t n)
{
    const uint8_t *p = (const uint8_t *)b;

    while (n > 0) {
        if (o->blen >= 16) {
            xor16(o->buf, o->buf, o->iv);
            o->enc(o->cipher, o->buf, o->iv);
            o->blen = 0;
            while (n > 16) {
                xor16(o->buf, p, o->iv);
                o->enc(o->cipher, o->buf, o->iv);
                p += 16;
                n -= 16;
            }
        }
        {
            size_t room = (size_t)(16 - o->blen);
            size_t take = n < room ? n : room;
            memcpy(o->buf + o->blen, p, take);
            o->blen += (int)take;
            p += take;
            n -= take;
        }
    }
    return 0;
}

int pea_omac_final(pea_omac *o, uint8_t tag[16])
{
    uint8_t L[16];

    memset(L, 0, 16);
    o->enc(o->cipher, L, L);
    if (o->blen >= 16) {
        mul_u(L);
    } else {
        o->buf[o->blen++] = 0x80;
        while (o->blen < 16)
            o->buf[o->blen++] = 0;
        mul_u(L);
        mul_u(L);
    }
    xor16(o->buf, o->buf, o->iv);
    xor16(o->buf, o->buf, L);
    o->enc(o->cipher, o->buf, tag);
    return 0;
}

static int ctr_crypt(pea_eaxblk *e, uint8_t *buf, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (e->ks_off >= 16) {
            inc_msb_full(e->ctr);
            e->enc(e->cipher, e->ctr, e->ks);
            e->ks_off = 0;
        }
        buf[i] ^= e->ks[e->ks_off++];
    }
    return 0;
}

int pea_eaxblk_init(pea_eaxblk *e, pea_blkfn enc, void *cipher,
                    const uint8_t *nonce, size_t nlen)
{
    uint8_t t_n[16];

    memset(e, 0, sizeof(*e));
    e->enc = enc;
    e->cipher = cipher;
    pea_omac_init(&e->hdr, enc, cipher);
    pea_omac_init(&e->msg, enc, cipher);

    memset(t_n, 0, 16);
    if (pea_omac_update(&e->msg, t_n, 16) ||
        pea_omac_update(&e->msg, nonce, nlen) ||
        pea_omac_final(&e->msg, e->nonce_tag))
        return -1;

    memcpy(e->ctr, e->nonce_tag, 16);
    e->enc(e->cipher, e->ctr, e->ks);
    e->ks_off = 0;

    pea_omac_init(&e->msg, enc, cipher);
    t_n[15] = 2;
    if (pea_omac_update(&e->msg, t_n, 16))
        return -1;
    t_n[15] = 1;
    if (pea_omac_update(&e->hdr, t_n, 16))
        return -1;
    return 0;
}

int pea_eaxblk_encrypt(pea_eaxblk *e, void *buf, size_t n)
{
    if (n == 0)
        return 0;
    if (ctr_crypt(e, (uint8_t *)buf, n))
        return -1;
    return pea_omac_update(&e->msg, buf, n);
}

int pea_eaxblk_decrypt(pea_eaxblk *e, void *buf, size_t n)
{
    if (n == 0)
        return 0;
    if (pea_omac_update(&e->msg, buf, n))
        return -1;
    return ctr_crypt(e, (uint8_t *)buf, n);
}

int pea_eaxblk_final(pea_eaxblk *e, uint8_t tag[16])
{
    uint8_t ht[16];
    int i;

    if (pea_omac_final(&e->hdr, ht) || pea_omac_final(&e->msg, tag))
        return -1;
    for (i = 0; i < 16; i++)
        tag[i] ^= ht[i] ^ e->nonce_tag[i];
    return 0;
}
