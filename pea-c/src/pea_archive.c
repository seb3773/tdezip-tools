/*
 * pea_archive.c - PEA archive writer/reader for format 1.6.
 *
 * The implementation mirrors PeaZip's pea.pas "pea_procedure" (writer) and
 * "unpea" (reader) byte for byte:
 *
 *   archive header (10)
 *   stream header  (10)
 *   [crypto subheader (16 or 48)]     - stream algorithms with password
 *   [cascade padding 1..128]          - TRIATS..HRISAT only
 *   [compression buffer size (4)]     - when compression != PCOMPRESS0
 *   objects...
 *   EOA trigger (6)
 *   [stream tag (authsize)]
 *   [volume tag (volume_authsize)]
 *
 * Control scopes:
 *   volume tag = every byte physically written to the volume, tags included
 *   stream tag = headers + WBUFSIZE + object headers + stored data +
 *                object tags + EOA
 *   object tag = object header + uncompressed data
 */
#include "pea_archive.h"
#include "pea_digest.h"
#include "pea_eax.h"
#include "pea_vol.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include <zlib.h>
#include <openssl/rand.h>

/* ------------------------------------------------------------------ */
/* little-endian helpers                                              */
/* ------------------------------------------------------------------ */

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

void pea_options_default(pea_options *o)
{
    o->volume_algo = PEA_ALGO_CRC32;
    o->stream_algo = PEA_ALGO_CRC32;
    o->obj_algo    = PEA_ALGO_CRC32;
    o->compr       = PEA_COMP_L6;
    o->niter       = 0;
    o->volsize     = 0;
    o->password    = NULL;
    o->keyfile     = NULL;
}

/* ================================================================== */
/* Writer                                                             */
/* ================================================================== */

typedef struct pea_writer {
    FILE        *fp;
    pea_options  o;
    pea_ctl      vol;
    pea_ctl      strm;
    pea_ctl      obj;
    pea_crypto  *crypto;
    int          obj_authsize;
    int          strm_authsize;
    int          vol_authsize;
    int          deflate_level;
    int          split;
    int          vol_index;
    uint64_t     ch_size;
    uint64_t     ch_res;
    char         vol_dir[PATH_MAX];
    char         vol_base[PATH_MAX];
} pea_writer;

static void path_split(const char *path, char *dir, size_t dirsz,
                       char *name, size_t namesz)
{
    const char *slash = strrchr(path, '/');
    if (slash) {
        size_t n = (size_t)(slash - path);
        if (n >= dirsz)
            n = dirsz - 1;
        memcpy(dir, path, n);
        dir[n] = '\0';
        snprintf(name, namesz, "%s", slash + 1);
    } else {
        snprintf(dir, dirsz, ".");
        snprintf(name, namesz, "%s", path);
    }
}

static void strip_dot_pea(char *name)
{
    size_t n = strlen(name);
    if (n >= 4 && strcasecmp(name + n - 4, ".pea") == 0)
        name[n - 4] = '\0';
}

static int w_raw(pea_writer *w, const void *b, size_t n)
{
    return (n == 0 || fwrite(b, 1, n, w->fp) == n) ? 0 : -1;
}

static int w_write_volume_tag(pea_writer *w)
{
    if (w->o.volume_algo != PEA_ALGO_NOALGO) {
        uint8_t tag[64];
        if (pea_ctl_final(&w->vol, tag, w->vol_authsize))
            return -1;
        if (w_raw(w, tag, (size_t)w->vol_authsize))
            return -1;
    }
    pea_ctl_free(&w->vol);
    memset(&w->vol, 0, sizeof(w->vol));
    return 0;
}

static int w_open_volume(pea_writer *w, int idx)
{
    char path[PATH_MAX];

    if (w->fp) {
        fclose(w->fp);
        w->fp = NULL;
    }
    w->vol_index = idx;
    if (snprintf(path, sizeof(path), "%s/%s.%06d.pea",
                 w->vol_dir, w->vol_base, idx) >= (int)sizeof(path))
        return -1;
    w->fp = fopen(path, "wb");
    if (!w->fp) {
        fprintf(stderr, "pea: cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (pea_ctl_init(&w->vol, w->o.volume_algo))
        return -1;
    w->ch_res = w->ch_size;
    return 0;
}

/* Emit bytes and feed the volume scope (mirrors write2chunks). */
static int w_emit(pea_writer *w, const void *b, size_t n)
{
    const uint8_t *p = (const uint8_t *)b;

    while (n > 0) {
        size_t chunk = n;
        if (w->split && w->ch_res < (uint64_t)chunk)
            chunk = (size_t)w->ch_res;
        if (w->split && chunk == 0) {
            if (w_write_volume_tag(w))
                return -1;
            if (w_open_volume(w, w->vol_index + 1))
                return -1;
            continue;
        }
        if (w_raw(w, p, chunk))
            return -1;
        if (pea_ctl_update(&w->vol, p, chunk))
            return -1;
        p += chunk;
        n -= chunk;
        if (w->split)
            w->ch_res -= chunk;
    }
    return 0;
}

static int w_strm(pea_writer *w, const void *b, size_t n)
{
    return pea_ctl_update(&w->strm, b, n);
}

static int w_obj(pea_writer *w, const void *b, size_t n)
{
    return pea_ctl_update(&w->obj, b, n);
}

/* Stream payload: EAX-encrypt in place (and authenticate), else hash, then emit. */
static int w_stream_out(pea_writer *w, void *b, size_t n)
{
    if (w->crypto) {
        if (pea_crypto_encrypt(w->crypto, b, n))
            return -1;
        return w_emit(w, b, n);
    }
    if (w_strm(w, b, n))
        return -1;
    return w_emit(w, b, n);
}

/* Object scope on plaintext, then stream-out (possibly encrypted). */
static int w_obj_emit(pea_writer *w, void *b, size_t n)
{
    if (w_obj(w, b, n))
        return -1;
    return w_stream_out(w, b, n);
}

static int w_obj_begin(pea_writer *w)
{
    pea_ctl_free(&w->obj);
    return pea_ctl_init(&w->obj, w->o.obj_algo);
}

/* Finishes the object scope and writes its tag (if any). */
static int w_obj_end(pea_writer *w)
{
    uint8_t tag[64];
    if (pea_ctl_final(&w->obj, tag, w->obj_authsize))
        return -1;
    if (w->obj_authsize > 0) {
        if (w_stream_out(w, tag, w->obj_authsize))
            return -1;
    }
    return 0;
}

static int w_store_data(pea_writer *w, FILE *in, uint64_t size)
{
    uint8_t buf[PEA_SBUFSIZE];
    uint64_t left = size;

    while (left > 0) {
        size_t want = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
        size_t got = fread(buf, 1, want, in);
        if (got == 0)
            return -1;
        if (w_obj_emit(w, buf, got))
            return -1;
        left -= got;
    }
    return 0;
}

static int w_comp_data(pea_writer *w, FILE *in, uint64_t size)
{
    uint8_t *ib = malloc(PEA_WBUFSIZE);
    uint8_t *ob = malloc(PEA_WBUFSIZE + 65536);
    uint64_t left = size;
    uint32_t last_uncomp = 0;
    int rc = -1;

    if (!ib || !ob)
        goto out;

    while (left > 0) {
        size_t want = left > PEA_WBUFSIZE ? PEA_WBUFSIZE : (size_t)left;
        size_t got = fread(ib, 1, want, in);
        uLongf clen = (uLongf)(got + 65536);
        uint8_t hdr[4];

        if (got == 0)
            goto out;
        last_uncomp = (uint32_t)got;

        if (compress2(ob, &clen, ib, (uLong)got, w->deflate_level) != Z_OK ||
            clen >= got) {
            memcpy(ob, ib, got);
            clen = (uLongf)got;
        }

        put32(hdr, (uint32_t)clen);

        /* object scope: compressed size, then uncompressed bytes */
        if (w_obj(w, hdr, 4))
            goto out;
        if (w_obj(w, ib, got))
            goto out;

        /* stream (+ volume): stored block = size field + stored bytes */
        if (w_stream_out(w, hdr, 4))
            goto out;
        if (w_stream_out(w, ob, (size_t)clen))
            goto out;

        left -= got;
    }

    /* trailing uncompressed size of the last block */
    {
        uint8_t tr[4];
        put32(tr, last_uncomp);
        if (w_obj(w, tr, 4))
            goto out;
        if (w_stream_out(w, tr, 4))
            goto out;
    }
    rc = 0;

out:
    free(ib);
    free(ob);
    return rc;
}

static int w_add_file(pea_writer *w, const char *abs, const struct stat *st)
{
    size_t nlen = strlen(abs);
    uint8_t *hdr;
    uint8_t *p;
    FILE *in;

    if (nlen > 65535) {
        fprintf(stderr, "pea: path too long: %s\n", abs);
        return -1;
    }

    in = fopen(abs, "rb");
    if (!in) {
        fprintf(stderr, "pea: cannot open %s: %s\n", abs, strerror(errno));
        return -1;
    }

    hdr = malloc(nlen + 18);
    if (!hdr) {
        fclose(in);
        return -1;
    }
    p = hdr;
    put16(p, (uint16_t)nlen); p += 2;
    memcpy(p, abs, nlen);     p += nlen;
    put32(p, (uint32_t)st->st_mtime); p += 4;
    put32(p, PEA_FA_ARCHIVE); p += 4;             /* FPC faArchive */
    put64(p, (uint64_t)st->st_size);              /* 8-byte size field */

    if (w_obj_begin(w) || w_obj_emit(w, hdr, nlen + 18)) {
        free(hdr);
        fclose(in);
        return -1;
    }
    free(hdr);

    if (st->st_size > 0) {
        int rc = (w->o.compr == PEA_COMP_STORE)
                     ? w_store_data(w, in, (uint64_t)st->st_size)
                     : w_comp_data(w, in, (uint64_t)st->st_size);
        if (rc) {
            fclose(in);
            return -1;
        }
    }
    fclose(in);
    return w_obj_end(w);
}

static int w_add_dir(pea_writer *w, const char *abs)
{
    char stored[PATH_MAX + 2];
    size_t nlen = strlen(abs);
    uint8_t *hdr;
    uint8_t *p;

    if (nlen == 0 || nlen >= PATH_MAX || nlen > 65534)
        return -1;
    memcpy(stored, abs, nlen);
    if (stored[nlen - 1] != '/')
        stored[nlen++] = '/';
    stored[nlen] = '\0';

    hdr = malloc(nlen + 10);
    if (!hdr)
        return -1;
    p = hdr;
    put16(p, (uint16_t)nlen); p += 2;
    memcpy(p, stored, nlen);  p += nlen;
    {
        struct stat st;
        uint32_t mt = 0;
        if (stat(abs, &st) == 0)
            mt = (uint32_t)st.st_mtime;
        put32(p, mt); p += 4;
    }
    put32(p, PEA_FA_DIRECTORY);

    if (w_obj_begin(w) || w_obj_emit(w, hdr, nlen + 10)) {
        free(hdr);
        return -1;
    }
    free(hdr);
    return w_obj_end(w);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int w_add_path(pea_writer *w, const char *path)
{
    char abs[PATH_MAX];
    struct stat st;

    if (!realpath(path, abs)) {
        fprintf(stderr, "pea: cannot resolve %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (stat(abs, &st)) {
        fprintf(stderr, "pea: cannot stat %s: %s\n", abs, strerror(errno));
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *d;
        struct dirent *de;
        char **names = NULL;
        size_t n = 0, cap = 0, i;

        if (w_add_dir(w, abs))
            return -1;

        d = opendir(abs);
        if (!d) {
            fprintf(stderr, "pea: cannot open dir %s: %s\n", abs, strerror(errno));
            return -1;
        }
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                char **tmp = realloc(names, cap * sizeof(char *));
                if (!tmp) {
                    closedir(d);
                    goto oom;
                }
                names = tmp;
            }
            names[n] = strdup(de->d_name);
            if (!names[n]) {
                closedir(d);
                goto oom;
            }
            n++;
        }
        closedir(d);

        qsort(names, n, sizeof(char *), cmp_str);
        for (i = 0; i < n; i++) {
            char child[PATH_MAX];
            snprintf(child, sizeof(child), "%s/%s", abs, names[i]);
            if (w_add_path(w, child)) {
                free(names[i]);
                for (i++; i < n; i++)
                    free(names[i]);
                free(names);
                return -1;
            }
            free(names[i]);
        }
        free(names);
        return 0;

oom:
        for (i = 0; i < n; i++)
            free(names[i]);
        free(names);
        return -1;
    }

    if (S_ISREG(st.st_mode))
        return w_add_file(w, abs, &st);

    fprintf(stderr, "pea: skipping special file %s\n", abs);
    return 0;
}

int pea_create(const char *archive, const char *const *paths, int npaths,
               const pea_options *o)
{
    pea_writer w;
    uint8_t hdr[10];
    uint8_t shdr[10];
    uint8_t auth_buf[20];
    int i;
    int rc = -1;

    memset(&w, 0, sizeof(w));
    w.o = *o;
    w.obj_authsize  = pea_obj_authsize(o->obj_algo);
    w.strm_authsize = pea_stream_authsize(o->stream_algo);
    w.vol_authsize  = pea_stream_authsize(o->volume_algo);
    w.deflate_level = (o->compr == PEA_COMP_L3) ? 3 :
                      (o->compr == PEA_COMP_L9) ? 9 : 6;

    if (w.obj_authsize < 0 || w.strm_authsize < 0 || w.vol_authsize < 0) {
        fprintf(stderr, "pea: requested control algorithm is not supported\n");
        return -1;
    }
    if (pea_algo_needs_password(o->stream_algo) &&
        o->stream_algo != PEA_ALGO_EAX256 &&
        o->stream_algo != PEA_ALGO_EAX &&
        o->stream_algo != PEA_ALGO_HMAC &&
        o->stream_algo != PEA_ALGO_TF &&
        o->stream_algo != PEA_ALGO_TF256 &&
        o->stream_algo != PEA_ALGO_SP &&
        o->stream_algo != PEA_ALGO_SP256 &&
        !pea_algo_is_cascade(o->stream_algo)) {
        fprintf(stderr, "pea: stream algorithm '%s' is not implemented\n",
                pea_algo_name(o->stream_algo));
        return -1;
    }
    if (pea_algo_needs_password(o->stream_algo) &&
        (!o->password || !o->password[0])) {
        fprintf(stderr, "pea: %s requires a password (-p)\n",
                pea_algo_name(o->stream_algo));
        return -1;
    }

    path_split(archive, w.vol_dir, sizeof(w.vol_dir),
               w.vol_base, sizeof(w.vol_base));
    strip_dot_pea(w.vol_base);
    w.split = o->volsize != 0;

    if (w.split) {
        uint64_t vs = o->volsize;
        if (vs < (uint64_t)w.vol_authsize + 10)
            vs = (uint64_t)w.vol_authsize + 10;
        w.ch_size = vs - (uint64_t)w.vol_authsize;
        if (w_open_volume(&w, 1))
            return -1;
    } else {
        w.fp = fopen(archive, "wb");
        if (!w.fp) {
            fprintf(stderr, "pea: cannot create %s: %s\n", archive, strerror(errno));
            return -1;
        }
        if (pea_ctl_init(&w.vol, o->volume_algo))
            goto out;
    }

    if (pea_ctl_init(&w.obj, o->obj_algo))
        goto out;
    if (!pea_algo_needs_password(o->stream_algo)) {
        if (pea_ctl_init(&w.strm, o->stream_algo))
            goto out;
    }

    /* archive header */
    hdr[0] = (uint8_t)PEA_MAGIC;
    hdr[1] = (uint8_t)PEA_FORMAT_VER;
    hdr[2] = (uint8_t)PEA_FORMAT_REV;
    hdr[3] = (uint8_t)o->volume_algo;
    hdr[4] = 0;
    hdr[5] = PEA_OS_LINUX;
    hdr[6] = PEA_DATETIME_UNIX;
    hdr[7] = PEA_CHARSET_ANSI;
    hdr[8] = PEA_CPU_X86_64;
    hdr[9] = o->niter;
    if (w_emit(&w, hdr, 10))
        goto out;
    memcpy(auth_buf, hdr, 10);

    /* stream header */
    shdr[0] = 0; shdr[1] = 0;
    shdr[2] = 'P'; shdr[3] = 'O'; shdr[4] = 'D'; shdr[5] = 0;
    shdr[6] = (uint8_t)o->compr;
    shdr[7] = 0;
    shdr[8] = (uint8_t)o->stream_algo;
    shdr[9] = (uint8_t)o->obj_algo;
    if (w_emit(&w, shdr, 10))
        goto out;
    memcpy(auth_buf + 10, shdr, 10);

    if (pea_algo_needs_password(o->stream_algo)) {
        uint8_t subhdr[PEA_CRYPTO_CASCADE_HDR_LEN];
        int hdrlen = pea_crypto_hdr_len(o->stream_algo);
        uint8_t *mat = NULL;
        size_t matlen = 0;

        if (hdrlen <= 0)
            goto out;
        if (pea_make_pw_material(o->password, o->keyfile, hdr, shdr,
                                 &mat, &matlen))
            goto out;
        w.crypto = pea_crypto_new();
        if (!w.crypto ||
            pea_crypto_init_encrypt(w.crypto, o->stream_algo, mat, matlen,
                                    o->niter, subhdr)) {
            memset(mat, 0, matlen);
            free(mat);
            fprintf(stderr, "pea: %s key derivation failed\n",
                    pea_algo_name(o->stream_algo));
            goto out;
        }
        memset(mat, 0, matlen);
        free(mat);
        if (w_emit(&w, subhdr, (size_t)hdrlen))
            goto out;
        if (pea_algo_is_cascade(o->stream_algo)) {
            uint8_t pad[128];
            unsigned char nbyte;
            size_t plen;
            if (RAND_bytes(&nbyte, 1) != 1)
                goto out;
            plen = (size_t)(nbyte / 2) + 1;
            if (RAND_bytes(pad, (int)plen) != 1)
                goto out;
            pad[0] = (uint8_t)(plen - 1);
            if (w_stream_out(&w, pad, plen))
                goto out;
        }
    } else if (o->stream_algo != PEA_ALGO_NOALGO) {
        if (w_strm(&w, auth_buf, 20))
            goto out;
    }

    if (o->compr != PEA_COMP_STORE) {
        uint8_t sz[4];
        put32(sz, PEA_WBUFSIZE);
        if (w_stream_out(&w, sz, 4))
            goto out;
    }

    for (i = 0; i < npaths; i++) {
        if (w_add_path(&w, paths[i]))
            goto out;
    }

    /* End Of Archive trigger */
    {
        uint8_t eoa[6] = { 0, 0, 'E', 'O', 'A', 0 };
        if (w_stream_out(&w, eoa, 6))
            goto out;
    }

    /* stream tag */
    if (w.crypto) {
        uint8_t tag[PEA_CRYPTO_CASCADE_TAG_LEN];
        int tlen = w.strm_authsize;
        if (tlen <= 0 || tlen > PEA_CRYPTO_CASCADE_TAG_LEN)
            goto out;
        if (pea_crypto_final(w.crypto, tag))
            goto out;
        if (w_emit(&w, tag, (size_t)tlen))
            goto out;
    } else if (o->stream_algo != PEA_ALGO_NOALGO) {
        uint8_t tag[64];
        if (pea_ctl_final(&w.strm, tag, w.strm_authsize))
            goto out;
        if (w_emit(&w, tag, w.strm_authsize))
            goto out;
    }

    if (w_write_volume_tag(&w))
        goto out;

    rc = 0;

out:
    pea_ctl_free(&w.vol);
    pea_ctl_free(&w.strm);
    pea_ctl_free(&w.obj);
    pea_crypto_free(w.crypto);
    w.crypto = NULL;
    if (w.fp && fclose(w.fp) != 0)
        rc = -1;
    return rc;
}

/* ================================================================== */
/* Reader                                                             */
/* ================================================================== */

typedef struct pea_reader {
    pea_vstream  vs;
    int          compr;
    int          stream_algo;
    int          obj_algo;
    int          strm_authsize;
    int          obj_authsize;
    uint32_t     wbufsize;
    pea_ctl      strm;
    pea_ctl      obj;
    pea_crypto  *crypto;
} pea_reader;

/* Read n bytes and feed the volume scope (spans volumes when split). */
static int r_read(pea_reader *r, void *buf, size_t n)
{
    return pea_vstream_read(&r->vs, buf, n);
}

/* Read ciphertext, feed volume, EAX-decrypt (or hash) in place. */
static int r_stream(pea_reader *r, void *buf, size_t n)
{
    if (r_read(r, buf, n))
        return -1;
    if (r->crypto)
        return pea_crypto_decrypt(r->crypto, buf, n);
    return pea_ctl_update(&r->strm, buf, n);
}

static int tag_equal(const uint8_t *a, const uint8_t *b, int n)
{
    int i, d = 0;
    for (i = 0; i < n; i++)
        d |= a[i] ^ b[i];
    return d == 0;
}

static int read_and_check_stream(pea_reader *r)
{
    int n = r->strm_authsize;
    uint8_t expect[64], got[64];

    if (n > 64)
        return -1;

    if (n <= 0)
        return 0;
    if (r->crypto) {
        if (pea_crypto_final(r->crypto, expect))
            return -1;
    } else {
        if (pea_ctl_final(&r->strm, expect, n))
            return -1;
    }
    if (r_read(r, got, (size_t)n))
        return -1;
    if (!tag_equal(expect, got, n)) {
        fprintf(stderr, "pea: stream tag mismatch (archive corrupted or wrong algorithm)\n");
        return -1;
    }
    return 0;
}

/* Object description used for listing. */
typedef struct pea_entry {
    char     name[4096];
    uint64_t size;
    uint32_t attr;
    int      is_dir;
} pea_entry;

typedef int (*pea_obj_cb)(void *ctx, const pea_entry *e, FILE *out, pea_reader *r);

static int read_object_header(pea_reader *r, pea_entry *e)
{
    uint8_t b[10];
    uint16_t nlen;
    uint8_t namebuf[4096];

    if (r_stream(r, b, 2))
        return -2; /* EOF or truncated */
    nlen = get16(b);

    if (nlen == 0) {
        /* trigger object: the whole 6-byte trigger is one stream block */
        uint8_t t[4];
        if (r_stream(r, t, 4))
            return -1;
        if (t[0] == 'E' && t[1] == 'O' && t[2] == 'A' && t[3] == 0)
            return 1; /* end of archive */
        if (t[0] == 'E' && t[1] == 'O' && t[2] == 'S' && t[3] == 0)
            return 0; /* end of stream (unused in 1.0) */
        return -1;
    }

    if (nlen > sizeof(namebuf))
        return -1;
    if (pea_ctl_update(&r->obj, b, 2))
        return -1;

    if (r_stream(r, namebuf, nlen))
        return -1;
    if (pea_ctl_update(&r->obj, namebuf, nlen))
        return -1;

    if (r_stream(r, b, 8))
        return -1;
    if (pea_ctl_update(&r->obj, b, 8))
        return -1;

    memcpy(e->name, namebuf, nlen);
    e->name[nlen] = '\0';
    e->attr = get32(b + 4); /* b = 4-byte mtime + 4-byte attributes */
    e->is_dir = (e->attr & PEA_FA_DIRECTORY) != 0;
    e->size = 0;

    if (!e->is_dir) {
        if (r_stream(r, b, 8))
            return -1;
        if (pea_ctl_update(&r->obj, b, 8))
            return -1;
        e->size = get64(b);
    }
    return 2; /* object header parsed */
}

static int obj_begin(pea_reader *r, int algo)
{
    pea_ctl_free(&r->obj);
    return pea_ctl_init(&r->obj, algo);
}

static int obj_end(pea_reader *r)
{
    int n = r->obj_authsize;
    uint8_t expect[64], got[64];

    if (n <= 0)
        return 0;
    if (pea_ctl_final(&r->obj, expect, n))
        return -1;
    if (r_stream(r, got, (size_t)n))
        return -1;
    if (!tag_equal(expect, got, n)) {
        fprintf(stderr, "pea: object tag mismatch\n");
        return -1;
    }
    return 0;
}

/* read stored (uncompressed) data for one file; `out` may be NULL */
static int read_store_data(pea_reader *r, uint64_t size, FILE *out)
{
    uint8_t buf[PEA_SBUFSIZE];
    uint64_t left = size;
    while (left > 0) {
        size_t want = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
        if (r_stream(r, buf, want))
            return -1;
        if (pea_ctl_update(&r->obj, buf, want))
            return -1;
        if (out && fwrite(buf, 1, want, out) != want)
            return -1;
        left -= want;
    }
    return 0;
}

static int read_comp_data(pea_reader *r, uint64_t size, FILE *out)
{
    uint64_t left = size;
    uint8_t *stored = malloc(PEA_WBUFSIZE + 65536);
    uint8_t *plain = malloc(PEA_WBUFSIZE);
    int rc = -1;

    if (!stored || !plain)
        goto out;

    while (left > 0) {
        uint8_t szb[4];
        uint32_t clen;
        size_t uncompsize = left > r->wbufsize ? r->wbufsize : (size_t)left;

        if (r_stream(r, szb, 4))
            goto out;
        if (pea_ctl_update(&r->obj, szb, 4))
            goto out;
        clen = get32(szb);
        if (clen > PEA_WBUFSIZE + 65536u)
            goto out;
        if (r_stream(r, stored, clen))
            goto out;

        if (clen < uncompsize) {
            uLongf dl = (uLongf)uncompsize;
            if (uncompress(plain, &dl, stored, (uLong)clen) != Z_OK)
                goto out;
            if ((size_t)dl != uncompsize)
                goto out;
        } else {
            memcpy(plain, stored, uncompsize);
        }

        if (pea_ctl_update(&r->obj, plain, uncompsize))
            goto out;
        if (out && fwrite(plain, 1, uncompsize, out) != uncompsize)
            goto out;
        left -= uncompsize;
    }

    /* trailing uncompressed size of the last block */
    {
        uint8_t tr[4];
        if (r_stream(r, tr, 4))
            goto out;
        if (pea_ctl_update(&r->obj, tr, 4))
            goto out;
    }
    rc = 0;

out:
    free(stored);
    free(plain);
    return rc;
}

static int reader_open(pea_reader *r, const char *archive,
                       const char *password, const char *keyfile)
{
    uint8_t hdr[10], shdr[10];
    int algo;

    memset(r, 0, sizeof(*r));
    if (pea_vstream_open(&r->vs, archive))
        return -1;
    if (pea_vstream_read_header(&r->vs, hdr))
        goto bad;
    if (hdr[0] != PEA_MAGIC) {
        fprintf(stderr, "pea: %s is not a PEA archive\n", archive);
        goto bad;
    }
    if (hdr[1] > PEA_FORMAT_VER ||
        (hdr[1] == PEA_FORMAT_VER && hdr[2] > PEA_FORMAT_REV)) {
        fprintf(stderr, "pea: unsupported format %u.%u\n", hdr[1], hdr[2]);
        goto bad;
    }

    {
        int volume_algo = hdr[3];
        int vol_authsize = pea_stream_authsize(volume_algo);
        if (vol_authsize < 0) {
            fprintf(stderr, "pea: unsupported volume algorithm %u\n", hdr[3]);
            goto bad;
        }
        if (pea_vstream_begin(&r->vs, volume_algo, hdr, 10))
            goto bad;
    }

    if (r_read(r, shdr, 10))
        goto bad;
    if (shdr[0] || shdr[1] || shdr[2] != 'P' || shdr[3] != 'O' ||
        shdr[4] != 'D' || shdr[5] != 0) {
        fprintf(stderr, "pea: not a PEA stream (POD marker missing)\n");
        goto bad;
    }

    r->compr       = shdr[6];
    algo           = shdr[8];
    r->obj_algo    = shdr[9];
    r->stream_algo = algo;
    r->strm_authsize = pea_stream_authsize(algo);
    r->obj_authsize  = pea_obj_authsize(r->obj_algo);
    if (r->strm_authsize < 0 || r->obj_authsize < 0) {
        fprintf(stderr, "pea: unsupported control algorithm\n");
        goto bad;
    }
    if (pea_algo_needs_password(algo) &&
        algo != PEA_ALGO_EAX256 &&
        algo != PEA_ALGO_EAX &&
        algo != PEA_ALGO_HMAC &&
        algo != PEA_ALGO_TF &&
        algo != PEA_ALGO_TF256 &&
        algo != PEA_ALGO_SP &&
        algo != PEA_ALGO_SP256 &&
        !pea_algo_is_cascade(algo)) {
        fprintf(stderr, "pea: stream algorithm '%s' is not implemented\n",
                pea_algo_name(algo));
        goto bad;
    }
    r->wbufsize = PEA_WBUFSIZE;

    if (pea_algo_needs_password(algo)) {
        uint8_t subhdr[PEA_CRYPTO_CASCADE_HDR_LEN];
        int hdrlen = pea_crypto_hdr_len(algo);
        uint8_t *mat = NULL;
        size_t matlen = 0;
        int irc;

        if (!password || !password[0]) {
            fprintf(stderr, "pea: %s archive requires a password (-p)\n",
                    pea_algo_name(algo));
            goto bad;
        }
        if (hdrlen <= 0)
            goto bad;
        if (r_read(r, subhdr, (size_t)hdrlen))
            goto bad;
        if (pea_make_pw_material(password, keyfile, hdr, shdr, &mat, &matlen))
            goto bad;
        r->crypto = pea_crypto_new();
        irc = r->crypto
            ? pea_crypto_init_decrypt(r->crypto, algo, mat, matlen, hdr[9], subhdr)
            : -1;
        memset(mat, 0, matlen);
        free(mat);
        if (irc == -2) {
            fprintf(stderr, "pea: wrong password\n");
            goto bad;
        }
        if (irc) {
            fprintf(stderr, "pea: %s key derivation failed\n",
                    pea_algo_name(algo));
            goto bad;
        }
        if (pea_algo_is_cascade(algo)) {
            uint8_t first;
            uint8_t pad[128];
            if (r_stream(r, &first, 1))
                goto bad;
            if (r_stream(r, pad, (size_t)first))
                goto bad;
        }
        if (pea_ctl_init(&r->obj, r->obj_algo))
            goto bad;
    } else {
        if (pea_ctl_init(&r->strm, algo) || pea_ctl_init(&r->obj, r->obj_algo))
            goto bad;
        if (algo != PEA_ALGO_NOALGO) {
            if (pea_ctl_update(&r->strm, hdr, 10))
                goto bad;
            if (pea_ctl_update(&r->strm, shdr, 10))
                goto bad;
        }
    }

    if (r->compr != PEA_COMP_STORE) {
        uint8_t sz[4];
        if (r_stream(r, sz, 4))
            goto bad;
        r->wbufsize = get32(sz);
        if (r->wbufsize == 0 || r->wbufsize > PEA_WBUFSIZE)
            r->wbufsize = PEA_WBUFSIZE;
    }

    return 0;

bad:
    pea_ctl_free(&r->strm);
    pea_ctl_free(&r->obj);
    pea_crypto_free(r->crypto);
    r->crypto = NULL;
    pea_vstream_close(&r->vs);
    return -1;
}

static void reader_close(pea_reader *r)
{
    pea_vstream_close(&r->vs);
    pea_ctl_free(&r->strm);
    pea_ctl_free(&r->obj);
    pea_crypto_free(r->crypto);
    r->crypto = NULL;
}

typedef struct walk_ctx {
    const char *outdir;   /* NULL for list/test */
    char        root[PATH_MAX];
    int         root_set;
    int         verify;   /* run tag checks */
} walk_ctx;

static void strip_trailing_slashes(char *s)
{
    size_t n = strlen(s);
    while (n > 1 && s[n - 1] == '/')
        s[--n] = '\0';
}

static int path_has_dotdot(const char *rel)
{
    const char *p = rel;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0') &&
            (p == rel || p[-1] == '/'))
            return 1;
        p++;
    }
    return 0;
}

/* Build a safe output path from a stored (absolute) object name.
 * Pascal stores directories with a trailing DirectorySeparator; strip it
 * before computing the common root / relative path. */
static int make_out_path(walk_ctx *ctx, const char *name, char *out, size_t outsz)
{
    char namebuf[PATH_MAX];
    const char *rel;

    if (snprintf(namebuf, sizeof(namebuf), "%s", name) >= (int)sizeof(namebuf))
        return -1;
    strip_trailing_slashes(namebuf);
    rel = namebuf;

    if (ctx->root_set) {
        size_t rl = strlen(ctx->root);
        if (rl > 0 && strncmp(namebuf, ctx->root, rl) == 0 &&
            (namebuf[rl] == '\0' || namebuf[rl] == '/')) {
            rel = namebuf + rl;
            while (*rel == '/')
                rel++;
        }
    }
    while (*rel == '/')
        rel++;
    if (*rel == '\0' || path_has_dotdot(rel))
        return -1;
    if (snprintf(out, outsz, "%s/%s", ctx->outdir, rel) >= (int)outsz)
        return -1;
    return 0;
}

static int mkpath(const char *path)
{
    char tmp[PATH_MAX];
    char *p;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0)
        return -1;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) && errno != EEXIST)
        return -1;
    return 0;
}

static int walk_archive(pea_reader *r, walk_ctx *ctx)
{
    pea_entry e;
    int rc;

    while (1) {
        if (obj_begin(r, r->obj_algo))
            return -1;
        rc = read_object_header(r, &e);
        if (rc == -2) {
            fprintf(stderr, "pea: unexpected end of archive\n");
            return -1;
        }
        if (rc == 1) { /* EOA */
            break;
        }
        if (rc < 0)
            return -1;

        if (ctx->outdir && !ctx->root_set) {
            /* root = parent directory of the first stored object */
            char *slash;
            snprintf(ctx->root, sizeof(ctx->root), "%s", e.name);
            strip_trailing_slashes(ctx->root);
            slash = strrchr(ctx->root, '/');
            if (slash)
                *slash = '\0';
            else
                ctx->root[0] = '\0';
            ctx->root_set = 1;
        }

        if (ctx->outdir) {
            char out[PATH_MAX];
            if (!e.is_dir) {
                FILE *fp;
                if (make_out_path(ctx, e.name, out, sizeof(out))) {
                    fprintf(stderr, "pea: unsafe entry %s\n", e.name);
                    return -1;
                }
                {
                    char dir[PATH_MAX];
                    snprintf(dir, sizeof(dir), "%s", out);
                    {
                        char *slash = strrchr(dir, '/');
                        if (slash) {
                            *slash = '\0';
                            if (mkpath(dir))
                                return -1;
                        }
                    }
                }
                fp = fopen(out, "wb");
                if (!fp) {
                    fprintf(stderr, "pea: cannot create %s: %s\n", out, strerror(errno));
                    return -1;
                }
                if (e.size > 0) {
                    int drc = (r->compr == PEA_COMP_STORE)
                                  ? read_store_data(r, e.size, fp)
                                  : read_comp_data(r, e.size, fp);
                    if (drc) {
                        fclose(fp);
                        return -1;
                    }
                }
                fclose(fp);
            } else {
                if (make_out_path(ctx, e.name, out, sizeof(out))) {
                    fprintf(stderr, "pea: unsafe entry %s\n", e.name);
                    return -1;
                }
                if (mkpath(out))
                    return -1;
            }
        } else {
            /* list / test mode: consume the payload */
            if (!e.is_dir && e.size > 0) {
                int drc = (r->compr == PEA_COMP_STORE)
                              ? read_store_data(r, e.size, NULL)
                              : read_comp_data(r, e.size, NULL);
                if (drc)
                    return -1;
            }
            printf("%c %12llu  %s\n", e.is_dir ? 'd' : 'f',
                   (unsigned long long)e.size, e.name);
        }

        if (ctx->verify) {
            if (obj_end(r))
                return -1;
        } else {
            obj_end(r);
        }
    }

    if (r->stream_algo != PEA_ALGO_NOALGO) {
        if (read_and_check_stream(r))
            return -1;
    }
    if (pea_vstream_finish(&r->vs))
        return -1;
    return 0;
}

int pea_list(const char *archive, const char *password, const char *keyfile)
{
    pea_reader r;
    walk_ctx ctx;
    int rc;

    if (reader_open(&r, archive, password, keyfile))
        return -1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.verify = 0;
    rc = walk_archive(&r, &ctx);
    reader_close(&r);
    return rc;
}

int pea_test(const char *archive, const char *password, const char *keyfile)
{
    pea_reader r;
    walk_ctx ctx;
    int rc;

    if (reader_open(&r, archive, password, keyfile))
        return -1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.verify = 1;
    rc = walk_archive(&r, &ctx);
    reader_close(&r);
    return rc;
}

int pea_extract(const char *archive, const char *outdir,
                const char *password, const char *keyfile)
{
    pea_reader r;
    walk_ctx ctx;
    int rc;

    if (reader_open(&r, archive, password, keyfile))
        return -1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.outdir = outdir;
    ctx.verify = 1;
    rc = walk_archive(&r, &ctx);
    reader_close(&r);
    return rc;
}
