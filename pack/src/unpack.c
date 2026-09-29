/*
 * unpack / pcat - Huffman decompressor
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
#include <setjmp.h>

#define US 037 /* 0x1F */
#define RS 036 /* 0x1E */
#define BLKSIZE 4096

static jmp_buf env;
static struct stat status;

static int pcat_mode = 0;
static int force = 0;
static int vflag = 0;

/* I/O buffers */
static int infile = -1;
static int outfile = -1;
static ssize_t inleft;
static uint8_t *inp;
static uint8_t *outp;
static uint8_t inbuff[BLKSIZE];
static uint8_t outbuff[BLKSIZE];

/* Dictionary structures */
static uint32_t origsize;
static int maxlev;
static int intnodes[25];
static uint8_t *tree[25];
static uint8_t characters[256];
static uint8_t *eof_ptr;

static int decode(const char *source);
static void expand(const char *source);

static int getdict(const char *source)
{
    int c, i, nchildren;

    eof_ptr = &characters[0];

    inbuff[6] = 25;
    inleft = read(infile, inbuff, BLKSIZE);
    if (inleft < 0) {
        fprintf(stderr, "unpack: %s: read error: %s\n", source, strerror(errno));
        return 0;
    }
    if (inleft < 2) {
        fprintf(stderr, "unpack: %s: not in packed format (truncated header)\n", source);
        return 0;
    }

    if (inbuff[0] != US) {
        fprintf(stderr, "unpack: %s: not in packed format (invalid magic 0x%02x)\n", source, inbuff[0]);
        return 0;
    }

    if (inbuff[1] == US) { /* oldstyle packing (0x1F 0x1F) */
        if (setjmp(env))
            return 0;
        expand(source);
        return 1;
    }

    if (inbuff[1] != RS) { /* standard packing (0x1F 0x1E) */
        fprintf(stderr, "unpack: %s: not in packed format (invalid magic 0x%02x 0x%02x)\n", source, inbuff[0], inbuff[1]);
        return 0;
    }

    inp = &inbuff[2];
    origsize = 0;
    for (i = 0; i < 4; i++) {
        origsize = (origsize << 8) | (*inp++ & 0xFF);
    }

    maxlev = *inp++ & 0xFF;
    if (maxlev > 24) {
        fprintf(stderr, "unpack: %s: not in packed format (maxlev %d > 24)\n", source, maxlev);
        return 0;
    }

    for (i = 1; i <= maxlev; i++)
        intnodes[i] = *inp++ & 0xFF;

    for (i = 1; i <= maxlev; i++) {
        tree[i] = eof_ptr;
        for (c = intnodes[i]; c > 0; c--) {
            if (eof_ptr >= &characters[255]) {
                fprintf(stderr, "unpack: %s: not in packed format (corrupt leaf table)\n", source);
                return 0;
            }
            *eof_ptr++ = *inp++;
        }
    }
    *eof_ptr++ = *inp++;
    intnodes[maxlev] += 2;

    inleft -= (inp - inbuff);
    if (inleft < 0) {
        fprintf(stderr, "unpack: %s: not in packed format (header truncated)\n", source);
        return 0;
    }

    nchildren = 0;
    for (i = maxlev; i >= 1; i--) {
        c = intnodes[i];
        intnodes[i] = nchildren /= 2;
        nchildren += c;
    }

    return decode(source);
}

static int decode(const char *source)
{
    int bitsleft, c, i;
    int j, lev;
    uint8_t *p;

    outp = outbuff;
    lev = 1;
    i = 0;

    while (1) {
        if (inleft <= 0) {
            inleft = read(infile, inbuff, BLKSIZE);
            if (inleft < 0) {
                fprintf(stderr, "unpack: %s: read error: %s\n", source, strerror(errno));
                return 0;
            }
            inp = inbuff;
        }

        if (--inleft < 0) {
            if (origsize == 0)
                return 1;
            fprintf(stderr, "unpack: %s: unpacking error (premature EOF, %u bytes remaining)\n", source, origsize);
            return 0;
        }

        c = *inp++;
        bitsleft = 8;
        while (--bitsleft >= 0) {
            i *= 2;
            if (c & 0200)
                i++;
            c <<= 1;
            if ((j = i - intnodes[lev]) >= 0) {
                p = &tree[lev][j];
                if (p == eof_ptr) {
                    /* End of file symbol reached */
                    ssize_t outlen = outp - outbuff;
                    if (outlen > 0) {
                        if (write(outfile, outbuff, outlen) != outlen) {
                            fprintf(stderr, "unpack: %s: write error: %s\n", source, strerror(errno));
                            return 0;
                        }
                    }
                    origsize -= (uint32_t)outlen;
                    if (origsize != 0) {
                        fprintf(stderr, "unpack: %s: unpacking error: size mismatch (%u remaining)\n", source, origsize);
                        return 0;
                    }
                    return 1;
                }

                *outp++ = *p;
                if (outp == &outbuff[BLKSIZE]) {
                    if (write(outfile, outbuff, BLKSIZE) != BLKSIZE) {
                        fprintf(stderr, "unpack: %s: write error: %s\n", source, strerror(errno));
                        return 0;
                    }
                    origsize -= BLKSIZE;
                    outp = outbuff;
                }
                lev = 1;
                i = 0;
            } else {
                lev++;
            }
        }
    }
}

/* Oldstyle packing support (0x1F 0x1F) */
static int Tree[1024];

static int getch_old(const char *source)
{
    if (inleft <= 0) {
        inleft = read(infile, inbuff, BLKSIZE);
        if (inleft < 0) {
            fprintf(stderr, "unpack: %s: read error: %s\n", source, strerror(errno));
            longjmp(env, 1);
        }
        inp = inbuff;
    }
    inleft--;
    return (*inp++ & 0xFF);
}

static int getwd_old(const char *source)
{
    int c = getch_old(source);
    int d = getch_old(source);
    return (d << 8) | (c & 0xFF);
}

static void putch_old(uint8_t c, const char *source)
{
    *outp++ = c;
    if (outp == &outbuff[BLKSIZE]) {
        if (write(outfile, outbuff, BLKSIZE) != BLKSIZE) {
            fprintf(stderr, "unpack: %s: write error: %s\n", source, strerror(errno));
            longjmp(env, 2);
        }
        outp = outbuff;
    }
}

static void expand(const char *source)
{
    int tp, bit;
    int16_t word;
    int keysize, i, *t;

    outp = outbuff;
    inp = &inbuff[2];
    inleft -= 2;

    origsize = ((uint32_t)getwd_old(source)) << 16;
    origsize |= (uint32_t)getwd_old(source);

    t = Tree;
    for (keysize = getwd_old(source); keysize--; ) {
        if ((i = getch_old(source)) == 0377)
            *t++ = getwd_old(source);
        else
            *t++ = i & 0xFF;
    }

    bit = tp = 0;
    word = 0;
    for (;;) {
        if (bit <= 0) {
            word = (int16_t)getwd_old(source);
            bit = 16;
        }
        tp += Tree[tp + (word < 0 ? 1 : 0)];
        word <<= 1;
        bit--;
        if (Tree[tp] == 0) {
            putch_old((uint8_t)Tree[tp + 1], source);
            tp = 0;
            if (--origsize == 0) {
                ssize_t outlen = outp - outbuff;
                if (outlen > 0) {
                    if (write(outfile, outbuff, outlen) != outlen) {
                        fprintf(stderr, "unpack: %s: write error: %s\n", source, strerror(errno));
                    }
                }
                return;
            }
        }
    }
}

static void usage(const char *prog)
{
    if (pcat_mode) {
        fprintf(stderr, "Usage: %s [filename ...]\n", prog);
        fprintf(stderr, "  Decompress packed (.z) files to standard output.\n");
        fprintf(stderr, "  If no files are specified, standard input is read.\n");
    } else {
        fprintf(stderr, "Usage: %s [-f] [-c] filename ...\n", prog);
        fprintf(stderr, "  -f, --force    overwrite existing destination files\n");
        fprintf(stderr, "  -c, --stdout   write to stdout (act like pcat)\n");
    }
}

int main(int argc, char *argv[])
{
    int fcount = 0;
    int first_file = 0;

    /* Detect pcat invocation from program name */
    const char *p = strrchr(argv[0], '/');
    const char *progname = p ? p + 1 : argv[0];
    if (strcmp(progname, "pcat") == 0)
        pcat_mode = 1;

    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "-f") == 0 || strcmp(argv[k], "--force") == 0) {
            force = 1;
            continue;
        }
        if (strcmp(argv[k], "-c") == 0 || strcmp(argv[k], "--stdout") == 0) {
            pcat_mode = 1;
            continue;
        }
        if (strcmp(argv[k], "-v") == 0 || strcmp(argv[k], "--verbose") == 0) {
            vflag = 1;
            continue;
        }
        if (strcmp(argv[k], "-h") == 0 || strcmp(argv[k], "--help") == 0) {
            usage(progname);
            return 0;
        }
        if (strcmp(argv[k], "--") == 0) {
            first_file = k + 1;
            break;
        }
        if (argv[k][0] == '-' && argv[k][1] != '\0') {
            fprintf(stderr, "%s: unknown option: %s\n", progname, argv[k]);
            usage(progname);
            return 1;
        }
        if (first_file == 0) {
            first_file = k;
            break;
        }
    }

    /* pcat reading from standard input */
    if (pcat_mode && (first_file == 0 || first_file >= argc)) {
        infile = STDIN_FILENO;
        outfile = STDOUT_FILENO;
        return getdict("<stdin>") ? 0 : 1;
    }

    if (first_file == 0 || first_file >= argc) {
        usage(progname);
        return 1;
    }

    for (int k = first_file; k < argc; k++) {
        char in_path[PATH_MAX];
        char out_path[PATH_MAX];
        const char *arg = argv[k];
        size_t len = strlen(arg);

        if (len >= 2 && arg[len - 2] == '.' && arg[len - 1] == 'z') {
            snprintf(in_path, sizeof(in_path), "%s", arg);
            snprintf(out_path, sizeof(out_path), "%.*s", (int)(len - 2), arg);
        } else {
            snprintf(in_path, sizeof(in_path), "%s.z", arg);
            snprintf(out_path, sizeof(out_path), "%s", arg);
        }

        if (stat(in_path, &status) != 0) {
            /* Try exact name if in_path doesn't exist */
            if (stat(arg, &status) == 0) {
                snprintf(in_path, sizeof(in_path), "%s", arg);
                if (len >= 2 && arg[len - 2] == '.' && arg[len - 1] == 'z')
                    snprintf(out_path, sizeof(out_path), "%.*s", (int)(len - 2), arg);
                else
                    snprintf(out_path, sizeof(out_path), "%s.out", arg);
            } else {
                fprintf(stderr, "%s: %s: %s\n", progname, in_path, strerror(errno));
                fcount++;
                continue;
            }
        }

        infile = open(in_path, O_RDONLY);
        if (infile < 0) {
            fprintf(stderr, "%s: %s: cannot open: %s\n", progname, in_path, strerror(errno));
            fcount++;
            continue;
        }

        if (pcat_mode) {
            outfile = STDOUT_FILENO;
        } else {
            struct stat ostatus;
            if (!force && stat(out_path, &ostatus) == 0) {
                fprintf(stderr, "%s: %s: already exists (use -f to overwrite)\n", progname, out_path);
                close(infile);
                fcount++;
                continue;
            }

            outfile = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, status.st_mode & 07777);
            if (outfile < 0) {
                fprintf(stderr, "%s: %s: cannot create: %s\n", progname, out_path, strerror(errno));
                close(infile);
                fcount++;
                continue;
            }
            if (fchown(outfile, status.st_uid, status.st_gid) != 0) {
                /* ignore */
            }
        }

        if (getdict(in_path)) {
            if (!pcat_mode) {
                close(outfile);
                unlink(in_path);
                struct utimbuf ut;
                ut.actime = status.st_atime;
                ut.modtime = status.st_mtime;
                utime(out_path, &ut);
                if (vflag) {
                    printf("%s: %s: unpacked\n", progname, out_path);
                }
            }
        } else {
            fcount++;
            if (!pcat_mode) {
                close(outfile);
                unlink(out_path);
            }
        }

        close(infile);
    }

    return (fcount > 0) ? 1 : 0;
}
