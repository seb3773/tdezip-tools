/*
 * pea_archive.h - PEA archive creation and extraction (format 1.6).
 */
#ifndef PEA_ARCHIVE_H
#define PEA_ARCHIVE_H

#include <stdint.h>
#include "pea.h"

typedef struct pea_options {
    int         volume_algo; /* PEA_ALGO_* control for each volume (0..25) */
    int         stream_algo; /* PEA_ALGO_* control for the stream */
    int         obj_algo;    /* PEA_ALGO_* control for each object */
    int         compr;       /* PEA_COMP_* */
    uint8_t     niter;       /* KDF iteration multiplier (cascades only) */
    uint64_t    volsize;     /* 0 = single .pea; else volume file size including tag */
    const char *password;    /* required for EAX / EAX256 / HMAC */
    const char *keyfile;     /* optional; first 2048 bytes appended to KDF material */
} pea_options;

void pea_options_default(pea_options *o);

/* Create `archive` from `paths` (files or directories, recursed). */
int pea_create(const char *archive, const char *const *paths, int npaths,
               const pea_options *o);

/* Print the object list. Returns 0 on success. */
int pea_list(const char *archive, const char *password, const char *keyfile);

/* Verify all control tags without extracting. Returns 0 on success. */
int pea_test(const char *archive, const char *password, const char *keyfile);

/* Extract under `outdir`. Returns 0 on success. */
int pea_extract(const char *archive, const char *outdir,
                const char *password, const char *keyfile);

#endif /* PEA_ARCHIVE_H */
