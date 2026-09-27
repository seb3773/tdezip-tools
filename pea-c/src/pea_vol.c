#include "pea_vol.h"
#include "pea.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_split_archive(const char *path)
{
    size_t n = strlen(path);
    return n >= 11 && strcasecmp(path + n - 11, ".000001.pea") == 0;
}

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

static int vol_path(char *out, size_t n, const pea_vstream *vs, int idx)
{
    return snprintf(out, n, "%s/%s.%06d.pea", vs->dir, vs->base, idx)
                   >= (int)n ? -1 : 0;
}

static int file_size(FILE *fp, uint64_t *out)
{
    struct stat st;
    if (fstat(fileno(fp), &st))
        return -1;
    if (st.st_size < 0)
        return -1;
    *out = (uint64_t)st.st_size;
    return 0;
}

static int check_volume_tag(pea_vstream *vs)
{
    uint8_t expect[64], got[64];

    if (vs->authsize <= 0)
        return 0;
    if (pea_ctl_final(&vs->vol, expect, vs->authsize))
        return -1;
    if (fread(got, 1, (size_t)vs->authsize, vs->fp) != (size_t)vs->authsize) {
        fprintf(stderr, "pea: truncated volume tag\n");
        return -1;
    }
    vs->bytes_read += (uint64_t)vs->authsize;
    {
        int i, d = 0;
        for (i = 0; i < vs->authsize; i++)
            d |= expect[i] ^ got[i];
        if (d) {
            fprintf(stderr, "pea: volume tag mismatch (archive corrupted)\n");
            return -1;
        }
    }
    return 0;
}

static int open_index(pea_vstream *vs, int idx, int subtract)
{
    char path[PATH_MAX];
    uint64_t sz;

    if (vs->fp) {
        fclose(vs->fp);
        vs->fp = NULL;
    }
    if (vol_path(path, sizeof(path), vs, idx))
        return -1;
    vs->fp = fopen(path, "rb");
    if (!vs->fp) {
        fprintf(stderr, "pea: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    vs->index = idx;
    if (file_size(vs->fp, &sz))
        return -1;
    if (sz < (uint64_t)vs->authsize + (uint64_t)subtract) {
        fprintf(stderr, "pea: volume %s is too small\n", path);
        return -1;
    }
    vs->left = sz - (uint64_t)vs->authsize - (uint64_t)subtract;
    pea_ctl_free(&vs->vol);
    if (pea_ctl_init(&vs->vol, vs->algo))
        return -1;
    return 0;
}

int pea_vstream_open(pea_vstream *vs, const char *archive)
{
    memset(vs, 0, sizeof(*vs));
    vs->split = is_split_archive(archive);
    path_split(archive, vs->dir, sizeof(vs->dir), vs->base, sizeof(vs->base));
    if (vs->split) {
        size_t n = strlen(vs->base);
        if (n >= 11)
            vs->base[n - 11] = '\0';
        vs->index = 1;
    }
    vs->fp = fopen(archive, "rb");
    if (!vs->fp) {
        fprintf(stderr, "pea: cannot open %s: %s\n", archive, strerror(errno));
        return -1;
    }
    return 0;
}

int pea_vstream_read_header(pea_vstream *vs, uint8_t hdr[10])
{
    if (fread(hdr, 1, 10, vs->fp) != 10)
        return -1;
    vs->bytes_read += 10;
    return 0;
}

int pea_vstream_begin(pea_vstream *vs, int volume_algo,
                      const void *already, size_t already_len)
{
    uint64_t sz;

    vs->algo = volume_algo;
    vs->authsize = pea_stream_authsize(volume_algo);
    if (vs->authsize < 0)
        return -1;
    if (pea_ctl_init(&vs->vol, volume_algo))
        return -1;
    if (already_len && pea_ctl_update(&vs->vol, already, already_len))
        return -1;
    vs->bytes_read += already_len;
    if (!vs->split)
        return 0;
    if (file_size(vs->fp, &sz))
        return -1;
    if (sz < (uint64_t)vs->authsize + already_len) {
        fprintf(stderr, "pea: first volume is too small\n");
        return -1;
    }
    vs->left = sz - (uint64_t)vs->authsize - already_len;
    return 0;
}

static int next_volume(pea_vstream *vs)
{
    if (check_volume_tag(vs))
        return -1;
    return open_index(vs, vs->index + 1, 0);
}

int pea_vstream_read(pea_vstream *vs, void *buf, size_t n)
{
    uint8_t *p = buf;

    while (n > 0) {
        size_t chunk = n;

        if (vs->split) {
            if (vs->left == 0) {
                if (next_volume(vs))
                    return -1;
                continue;
            }
            if ((uint64_t)chunk > vs->left)
                chunk = (size_t)vs->left;
        }
        if (chunk > 0 && fread(p, 1, chunk, vs->fp) != chunk)
            return -1;
        if (pea_ctl_update(&vs->vol, p, chunk))
            return -1;
        vs->bytes_read += chunk;
        if (vs->split)
            vs->left -= chunk;
        p += chunk;
        n -= chunk;
    }
    return 0;
}

int pea_vstream_finish(pea_vstream *vs)
{
    if (vs->split && vs->left != 0) {
        fprintf(stderr, "pea: last volume has unexpected extra data\n");
        return -1;
    }
    return check_volume_tag(vs);
}

void pea_vstream_close(pea_vstream *vs)
{
    if (vs->fp) {
        fclose(vs->fp);
        vs->fp = NULL;
    }
    pea_ctl_free(&vs->vol);
}
