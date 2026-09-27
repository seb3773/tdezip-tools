#ifndef PEA_BLOCK_H
#define PEA_BLOCK_H

#include <stddef.h>
#include <stdint.h>

typedef void (*pea_blkfn)(void *cipher, const uint8_t in[16], uint8_t out[16]);

typedef struct pea_omac {
    pea_blkfn enc;
    void     *cipher;
    uint8_t   iv[16];
    uint8_t   buf[16];
    int       blen;
} pea_omac;

typedef struct pea_eaxblk {
    pea_omac  hdr;
    pea_omac  msg;
    pea_blkfn enc;
    void     *cipher;
    uint8_t   ctr[16];
    uint8_t   ks[16];
    uint8_t   nonce_tag[16];
    int       ks_off;
} pea_eaxblk;

int pea_omac_init(pea_omac *o, pea_blkfn enc, void *cipher);
int pea_omac_update(pea_omac *o, const void *b, size_t n);
int pea_omac_final(pea_omac *o, uint8_t tag[16]);

int pea_eaxblk_init(pea_eaxblk *e, pea_blkfn enc, void *cipher,
                    const uint8_t *nonce, size_t nlen);
int pea_eaxblk_encrypt(pea_eaxblk *e, void *buf, size_t n);
int pea_eaxblk_decrypt(pea_eaxblk *e, void *buf, size_t n);
int pea_eaxblk_final(pea_eaxblk *e, uint8_t tag[16]);

#endif
