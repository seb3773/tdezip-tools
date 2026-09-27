#ifndef PEA_VOL_H
#define PEA_VOL_H

#include "pea_digest.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

typedef struct pea_vstream {
    FILE        *fp;
    int          split;
    int          index;
    int          algo;
    int          authsize;
    uint64_t     left;
    uint64_t     bytes_read;
    pea_ctl      vol;
    char         dir[PATH_MAX];
    char         base[PATH_MAX];
} pea_vstream;

int  pea_vstream_open(pea_vstream *vs, const char *archive);
int  pea_vstream_read_header(pea_vstream *vs, uint8_t hdr[10]);
int  pea_vstream_begin(pea_vstream *vs, int volume_algo,
                       const void *already, size_t already_len);
int  pea_vstream_read(pea_vstream *vs, void *buf, size_t n);
int  pea_vstream_finish(pea_vstream *vs);
void pea_vstream_close(pea_vstream *vs);

#endif
