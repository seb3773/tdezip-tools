/*
 * pea_digest.h - streaming control algorithms (object/stream/volume tags).
 */
#ifndef PEA_DIGEST_H
#define PEA_DIGEST_H

#include <stdint.h>
#include <stddef.h>

typedef struct pea_ctl {
    int      algo;    /* PEA_ALGO_* */
    int      active;  /* 0 == no-op (NOALGO) */
    uint32_t adler;   /* ADLER32 running value */
    uint32_t crc;     /* CRC32 running value */
    uint64_t crc64;   /* CRC64-ECMA running value */
    void    *md;      /* EVP_MD_CTX * for the hash family, else NULL */
} pea_ctl;

/* Initialise a control context. Returns 0 on success, -1 if the algorithm is
 * recognised by the format but not implemented by this build. */
int  pea_ctl_init(pea_ctl *c, int algo);

/* Feed data. Returns 0 on success. */
int  pea_ctl_update(pea_ctl *c, const void *buf, size_t n);

/* Produce the final tag. `outlen` must equal pea_*_authsize(algo). Returns 0
 * on success, -1 on size mismatch. */
int  pea_ctl_final(pea_ctl *c, uint8_t *out, int outlen);

void pea_ctl_free(pea_ctl *c);

#endif /* PEA_DIGEST_H */
