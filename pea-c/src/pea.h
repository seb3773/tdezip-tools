/*
 * pea.h - PEA archive format (version 1.6) - shared constants and helpers.
 *
 * Reference: PeaZip's pea_utils.pas / pea.pas (project_pea.lpi).
 * PEA 1.x stores, in order:
 *   10-byte archive header, 10-byte stream header, optional crypto subheader,
 *   optional 4-byte compression buffer size, then a sequence of objects
 *   (trigger / directory / file), an End-Of-Archive trigger, the optional
 *   stream tag and the optional volume tag.
 */
#ifndef PEA_H
#define PEA_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define PEA_MAGIC        0xEAu
#define PEA_FORMAT_VER   1u
#define PEA_FORMAT_REV   6u

/* Compression input buffer used by the writer (WBUFSIZE in pea.pas). */
#define PEA_WBUFSIZE     1048576u
/* Control-update granularity used by the writer (SBUFSIZE in pea.pas). */
#define PEA_SBUFSIZE     65535u

/* FPC SysUtils file attributes (filegetattr). */
#define PEA_FA_DIRECTORY 0x10u
#define PEA_FA_ARCHIVE   0x20u

/* Header byte values for a Linux/x86_64 host (pea_utils.pas get_*). */
#define PEA_OS_LINUX     0x33u
#define PEA_DATETIME_UNIX 0x30u
#define PEA_CHARSET_ANSI 0x01u
#define PEA_CPU_X86_64   0x21u

/* Control algorithm codes (pea_utils.pas decode_control_algo). */
enum pea_algo {
    PEA_ALGO_NOALGO    = 0,
    PEA_ALGO_ADLER32   = 1,
    PEA_ALGO_CRC32     = 2,
    PEA_ALGO_CRC64     = 3,
    PEA_ALGO_MD5       = 16,
    PEA_ALGO_RIPEMD160 = 17,
    PEA_ALGO_SHA1      = 18,
    PEA_ALGO_SHA256    = 19,
    PEA_ALGO_SHA512    = 20,
    PEA_ALGO_WHIRLPOOL = 21,
    PEA_ALGO_SHA3_256  = 22,
    PEA_ALGO_SHA3_512  = 23,
    PEA_ALGO_BLAKE2S   = 24,
    PEA_ALGO_BLAKE2B   = 25,
    PEA_ALGO_HMAC      = 48,
    PEA_ALGO_EAX       = 49,
    PEA_ALGO_TF        = 50,
    PEA_ALGO_SP        = 51,
    PEA_ALGO_EAX256    = 65,
    PEA_ALGO_TF256     = 66,
    PEA_ALGO_SP256     = 67,
    PEA_ALGO_TRIATS    = 68,
    PEA_ALGO_TRITSA    = 69,
    PEA_ALGO_TRISAT    = 70,
    PEA_ALGO_SRIATS    = 71,
    PEA_ALGO_SRITSA    = 72,
    PEA_ALGO_SRISAT    = 73,
    PEA_ALGO_HRIATS    = 74,
    PEA_ALGO_HRITSA    = 75,
    PEA_ALGO_HRISAT    = 76
};

enum pea_compr {
    PEA_COMP_STORE = 0, /* PCOMPRESS0 */
    PEA_COMP_L3    = 1, /* PCOMPRESS1 (deflate level 3) */
    PEA_COMP_L6    = 2, /* PCOMPRESS2 (deflate level 6) */
    PEA_COMP_L9    = 3  /* PCOMPRESS3 (deflate level 9) */
};

/* Returns the authentication tag size for a stream algorithm, or -1 if the
 * algorithm is not supported by this build. */
int pea_stream_authsize(int algo);

/* Returns the authentication tag size for an object algorithm, or -1. */
int pea_obj_authsize(int algo);

/* Non-zero when the stream algorithm requires a password / crypto subheader. */
int pea_algo_needs_password(int algo);

/* Non-zero for TRIATS..HRISAT (three EAX-256 layers). */
int pea_algo_is_cascade(int algo);

/* Crypto subheader size: 48 for cascades, 16 for other password algos, else 0. */
int pea_crypto_hdr_len(int algo);

/* Name <-> code conversions. Return -1 on unknown input. */
const char *pea_algo_name(int algo);
int pea_algo_from_name(const char *name);
const char *pea_compr_name(int compr);
int pea_compr_from_name(const char *name);

#endif /* PEA_H */
