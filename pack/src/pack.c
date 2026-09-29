/*
 * pack - Huffman encoding compression program
 * Adapted from Research Unix V8 / System V (T.G. Szymanski, 1978-1979)
 * Modernized for POSIX systems and 64-bit architectures
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <limits.h>
#include <errno.h>

#define END 256
#define BLKSIZE 4096

typedef union {
    uint32_t lng;
    uint8_t c[4];
} FOUR;

/* character counters */
static uint64_t count[END + 1];
static uint32_t insize;
static uint64_t outsize;
static uint64_t dictsize;
static int diffbytes;

/* flags */
static int vflag = 0;
static int force = 0;
static int to_stdout = 0;

static char filename[PATH_MAX];
static int infile = -1;
static int outfile = -1;
static uint8_t inbuff[BLKSIZE];
static uint8_t outbuff[BLKSIZE + 8];

/* tree structures */
static int maxlev;
static int levcount[25];
static int lastnode;
static int parent[2 * END + 1];

/* encoding process */
static uint8_t length[END + 1];
static uint32_t bits[END + 1];
static FOUR mask;
static uint32_t inc;

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
static uint8_t *maskshuff[4] = {&(mask.c[0]), &(mask.c[1]), &(mask.c[2]), &(mask.c[3])};
#else
static uint8_t *maskshuff[4] = {&(mask.c[3]), &(mask.c[2]), &(mask.c[1]), &(mask.c[0])};
#endif

/* heap */
static int n;
struct heap {
    uint64_t count;
    int node;
} heap[END + 2];

#define hmove(a, b) { (b).count = (a).count; (b).node = (a).node; }

static int input_stats(const char *source)
{
    int i;
    for (i = 0; i < END; i++)
        count[i] = 0;

    ssize_t bytes_read;
    while ((bytes_read = read(infile, inbuff, BLKSIZE)) > 0) {
        for (ssize_t j = 0; j < bytes_read; j++) {
            count[inbuff[j]] += 2;
        }
    }
    if (bytes_read == 0)
        return 1;

    fprintf(stderr, "pack: %s: read error: %s\n", source, strerror(errno));
    return 0;
}

static int output_data(const char *source)
{
    int c, i;
    ssize_t inleft;
    uint8_t *inp;
    uint8_t **q, *outp;
    int bitsleft;
    uint32_t temp;

    /* Header: 0x1F, 0x1E */
    outbuff[0] = 037; /* 0x1F */
    outbuff[1] = 036; /* 0x1E */

    /* 4 bytes original uncompressed length, big-endian */
    temp = insize;
    for (i = 5; i >= 2; i--) {
        outbuff[i] = (uint8_t)(temp & 0xFF);
        temp >>= 8;
    }

    outp = &outbuff[6];
    *outp++ = (uint8_t)maxlev;
    for (i = 1; i < maxlev; i++)
        *outp++ = (uint8_t)levcount[i];
    *outp++ = (uint8_t)(levcount[maxlev] - 2);

    for (i = 1; i <= maxlev; i++) {
        for (c = 0; c < END; c++) {
            if (length[c] == i)
                *outp++ = (uint8_t)c;
        }
    }
    dictsize = outp - &outbuff[0];

    /* Rewind input file */
    if (lseek(infile, 0L, SEEK_SET) == (off_t)-1) {
        fprintf(stderr, "pack: %s: cannot seek: %s\n", source, strerror(errno));
        return 0;
    }

    outsize = 0;
    bitsleft = 8;
    inleft = 0;
    do {
        if (inleft <= 0) {
            inleft = read(infile, inbuff, BLKSIZE);
            if (inleft < 0) {
                fprintf(stderr, "pack: %s: read error: %s\n", source, strerror(errno));
                return 0;
            }
            inp = inbuff;
        }
        c = (--inleft < 0) ? END : (*inp++ & 0xFF);
        mask.lng = bits[c] << bitsleft;
        q = &maskshuff[0];
        if (bitsleft == 8)
            *outp = **q++;
        else
            *outp |= **q++;
        bitsleft -= length[c];
        while (bitsleft < 0) {
            *++outp = **q++;
            bitsleft += 8;
        }
        if (outp >= &outbuff[BLKSIZE]) {
            if (write(outfile, outbuff, BLKSIZE) != BLKSIZE) {
                fprintf(stderr, "pack: %s: write error: %s\n", source, strerror(errno));
                return 0;
            }
            memcpy(outbuff, &outbuff[BLKSIZE], 4);
            outp -= BLKSIZE;
            outsize += BLKSIZE;
        }
    } while (c != END);

    if (bitsleft < 8)
        outp++;
    c = outp - outbuff;
    if (write(outfile, outbuff, c) != c) {
        fprintf(stderr, "pack: %s: write error: %s\n", source, strerror(errno));
        return 0;
    }
    outsize += c;
    return 1;
}

static void heapify(int i)
{
    int k;
    int lastparent;
    struct heap heapsubi;
    hmove(heap[i], heapsubi);
    lastparent = n / 2;
    while (i <= lastparent) {
        k = 2 * i;
        if (heap[k].count > heap[k + 1].count && k < n)
            k++;
        if (heapsubi.count < heap[k].count)
            break;
        hmove(heap[k], heap[i]);
        i = k;
    }
    hmove(heapsubi, heap[i]);
}

static int packfile(const char *source)
{
    int c, i, p;
    uint64_t bitsout;

    if (!input_stats(source))
        return 0;

    diffbytes = -1;
    count[END] = 1;
    n = 0;
    uint64_t total_count = 0;

    for (i = END; i >= 0; i--) {
        parent[i] = 0;
        if (count[i] > 0) {
            diffbytes++;
            total_count += count[i];
            heap[++n].count = count[i];
            heap[n].node = i;
        }
    }
    insize = (uint32_t)(total_count >> 1);

    for (i = n / 2; i >= 1; i--)
        heapify(i);

    /* build Huffman tree */
    lastnode = END;
    while (n > 1) {
        parent[heap[1].node] = ++lastnode;
        inc = (uint32_t)heap[1].count;
        hmove(heap[n], heap[1]);
        n--;
        heapify(1);
        parent[heap[1].node] = lastnode;
        heap[1].node = lastnode;
        heap[1].count += inc;
        heapify(1);
    }
    parent[lastnode] = 0;

    /* assign lengths */
    bitsout = 0;
    maxlev = 0;
    for (i = 1; i <= 24; i++)
        levcount[i] = 0;

    for (i = 0; i <= END; i++) {
        c = 0;
        for (p = parent[i]; p != 0; p = parent[p])
            c++;
        levcount[c]++;
        length[i] = (uint8_t)c;
        if (c > maxlev)
            maxlev = c;
        bitsout += (uint64_t)c * (count[i] >> 1);
    }

    if (maxlev > 24) {
        fprintf(stderr, "pack: %s: Huffman tree has too many levels\n", source);
        return 0;
    }

    outsize = ((bitsout + 7) >> 3) + 6 + maxlev + diffbytes;
    if ((insize + BLKSIZE - 1) / BLKSIZE <= (outsize + BLKSIZE - 1) / BLKSIZE && !force) {
        if (!to_stdout)
            fprintf(stderr, "pack: %s: no saving - file unchanged (use -f to force)\n", source);
        return 0;
    }

    /* compute bit patterns */
    inc = 1U << 24;
    inc >>= maxlev;
    mask.lng = 0;
    for (i = maxlev; i > 0; i--) {
        for (c = 0; c <= END; c++) {
            if (length[c] == i) {
                bits[c] = mask.lng;
                mask.lng += inc;
            }
        }
        mask.lng &= ~inc;
        inc <<= 1;
    }

    return output_data(source);
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-f] [-c] [-v] filename ...\n", prog);
    fprintf(stderr, "  -f, --force    force packing even if no savings; overwrite existing .z file\n");
    fprintf(stderr, "  -c, --stdout   write to stdout, keep original file unchanged\n");
    fprintf(stderr, "  -v, --verbose  display compression statistics\n");
}

int main(int argc, char *argv[])
{
    int fcount = 0;
    int first_file = 0;
    struct stat status, ostatus;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "-f") == 0 || strcmp(argv[k], "--force") == 0) {
            force = 1;
            continue;
        }
        if (strcmp(argv[k], "-c") == 0 || strcmp(argv[k], "--stdout") == 0) {
            to_stdout = 1;
            continue;
        }
        if (strcmp(argv[k], "-v") == 0 || strcmp(argv[k], "--verbose") == 0 || strcmp(argv[k], "-") == 0) {
            vflag = 1;
            continue;
        }
        if (strcmp(argv[k], "-h") == 0 || strcmp(argv[k], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[k], "--") == 0) {
            first_file = k + 1;
            break;
        }
        if (argv[k][0] == '-' && argv[k][1] != '\0') {
            fprintf(stderr, "pack: unknown option: %s\n", argv[k]);
            usage(argv[0]);
            return 1;
        }
        if (first_file == 0) {
            first_file = k;
            break;
        }
    }

    if (first_file == 0 || first_file >= argc) {
        usage(argv[0]);
        return 1;
    }

    for (int k = first_file; k < argc; k++) {
        const char *src = argv[k];
        size_t len = strlen(src);

        if (len >= 2 && src[len - 2] == '.' && src[len - 1] == 'z') {
            fprintf(stderr, "pack: %s: already packed\n", src);
            fcount++;
            continue;
        }

        if (stat(src, &status) != 0) {
            fprintf(stderr, "pack: %s: %s\n", src, strerror(errno));
            fcount++;
            continue;
        }

        if (S_ISDIR(status.st_mode)) {
            fprintf(stderr, "pack: %s: cannot pack a directory\n", src);
            fcount++;
            continue;
        }

        infile = open(src, O_RDONLY);
        if (infile < 0) {
            fprintf(stderr, "pack: %s: cannot open: %s\n", src, strerror(errno));
            fcount++;
            continue;
        }

        if (to_stdout) {
            outfile = STDOUT_FILENO;
        } else {
            snprintf(filename, sizeof(filename), "%s.z", src);
            if (!force && stat(filename, &ostatus) == 0) {
                fprintf(stderr, "pack: %s: already exists (use -f to overwrite)\n", filename);
                close(infile);
                fcount++;
                continue;
            }

            outfile = open(filename, O_WRONLY | O_CREAT | O_TRUNC, status.st_mode & 07777);
            if (outfile < 0) {
                fprintf(stderr, "pack: %s: cannot create: %s\n", filename, strerror(errno));
                close(infile);
                fcount++;
                continue;
            }
            if (fchown(outfile, status.st_uid, status.st_gid) != 0) {
                /* ignore failure if not root */
            }
        }

        if (packfile(src)) {
            if (!to_stdout) {
                close(outfile);
                unlink(src);
                struct utimbuf ut;
                ut.actime = status.st_atime;
                ut.modtime = status.st_mtime;
                utime(filename, &ut);

                if (insize != 0) {
                    if (vflag) {
                        double ratio = ((double)((int64_t)insize - (int64_t)outsize) / (double)insize) * 100.0;
                        printf("pack: %s: %.1f%% Compression\n", src, ratio);
                        printf("  from %u to %lu bytes\n", insize, (unsigned long)outsize);
                        printf("  Huffman tree has %d levels below root\n", maxlev);
                        printf("  %d distinct bytes in input\n", diffbytes);
                        printf("  dictionary overhead = %lu bytes\n", (unsigned long)dictsize);
                    }
                }
            }
        } else {
            fcount++;
            if (!to_stdout) {
                close(outfile);
                unlink(filename);
            }
        }

        close(infile);
    }

    return (fcount > 0) ? 1 : 0;
}
