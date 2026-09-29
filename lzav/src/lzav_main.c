#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
/*
 * lzav_main.c - Fast LZAV compression utility
 *
 * Based on Aleksey Vaneev's LZAV (Lossless Audio-Visual) compression algorithm.
 * Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
 *
 * MIT License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "lzav.h"

#define LZAV_CLI_VERSION "1.0"
#define LZAV_MAGIC       0x56415a4c  /* 'L', 'Z', 'A', 'V' in little-endian */
#define LZAV_VERSION_ID  1
#define LZAV_FLAG_CRC32  0x01
#define BLOCK_SIZE       (2 * 1024 * 1024) /* 2 MB chunk */

static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void init_crc32(void)
{
    if (crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_initialized = 1;
}

static uint32_t calc_crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
    init_crc32();
    crc = crc ^ 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] [files...]\n\n"
        "A fast lossless data compressor based on the LZAV algorithm.\n\n"
        "Options:\n"
        "  -c, --stdout       Write output on standard output, keep original files\n"
        "  -d, --decompress   Decompress (decode)\n"
        "  -t, --test         Test integrity of compressed files\n"
        "  -i, --input <file> Specify input file\n"
        "  -o, --output <file>Specify output file\n"
        "  -f, --force        Force overwrite of output files\n"
        "  -k, --keep         Keep (don't delete) input files (default)\n"
        "  -1, --fast         Fast compression mode (default, lzav_compress_default)\n"
        "  -2, -9, --best     High compression mode (HI mode, lzav_compress_hi)\n"
        "  -v, --verbose      Verbose mode (print ratios and statistics)\n"
        "  -V, --version      Display version information\n"
        "  -h, --help         Display this help message\n\n"
        "If no files are specified, or if '-' is given, reads from standard input\n"
        "and writes to standard output.\n",
        prog);
}

static int compress_stream(FILE *in, FILE *out, int hi_mode, int verbose, uint64_t *out_orig, uint64_t *out_comp)
{
    /* 8-byte file header */
    uint8_t hdr[8];
    hdr[0] = 'L';
    hdr[1] = 'Z';
    hdr[2] = 'A';
    hdr[3] = 'V';
    hdr[4] = LZAV_VERSION_ID;
    hdr[5] = LZAV_FLAG_CRC32;
    hdr[6] = 0;
    hdr[7] = 0;

    if (fwrite(hdr, 1, 8, out) != 8) {
        fprintf(stderr, "lzav: error writing file header\n");
        return 1;
    }

    uint8_t *in_buf = (uint8_t *)malloc(BLOCK_SIZE);
    int max_bound = hi_mode ? lzav_compress_bound_hi(BLOCK_SIZE) : lzav_compress_bound(BLOCK_SIZE);
    uint8_t *comp_buf = (uint8_t *)malloc(max_bound);

    if (!in_buf || !comp_buf) {
        fprintf(stderr, "lzav: memory allocation failure\n");
        if (in_buf) free(in_buf);
        if (comp_buf) free(comp_buf);
        return 1;
    }

    uint64_t total_orig = 0;
    uint64_t total_comp = 8; /* header */

    while (!feof(in)) {
        size_t n = fread(in_buf, 1, BLOCK_SIZE, in);
        if (n == 0) {
            if (ferror(in)) {
                fprintf(stderr, "lzav: error reading input\n");
                free(in_buf);
                free(comp_buf);
                return 1;
            }
            break;
        }

        total_orig += n;
        uint32_t crc = calc_crc32(0, in_buf, n);

        int comp_len = hi_mode ?
            lzav_compress_hi(in_buf, comp_buf, (int)n, max_bound) :
            lzav_compress_default(in_buf, comp_buf, (int)n, max_bound);

        uint32_t orig_sz = (uint32_t)n;
        uint32_t comp_sz;
        const uint8_t *data_ptr;

        if (comp_len > 0 && comp_len < (int)n) {
            comp_sz = (uint32_t)comp_len;
            data_ptr = comp_buf;
        } else {
            /* Incompressible or compression failed to shrink: store raw */
            comp_sz = orig_sz;
            data_ptr = in_buf;
        }

        uint8_t block_hdr[12];
        memcpy(block_hdr, &orig_sz, 4);
        memcpy(block_hdr + 4, &comp_sz, 4);
        memcpy(block_hdr + 8, &crc, 4);

        if (fwrite(block_hdr, 1, 12, out) != 12 ||
            fwrite(data_ptr, 1, comp_sz, out) != comp_sz) {
            fprintf(stderr, "lzav: error writing block data\n");
            free(in_buf);
            free(comp_buf);
            return 1;
        }

        total_comp += 12 + comp_sz;
    }

    /* End-of-Stream marker (orig_sz == 0) */
    uint32_t eos = 0;
    if (fwrite(&eos, 1, 4, out) != 4) {
        fprintf(stderr, "lzav: error writing end-of-stream marker\n");
        free(in_buf);
        free(comp_buf);
        return 1;
    }
    total_comp += 4;

    free(in_buf);
    free(comp_buf);

    if (out_orig) *out_orig = total_orig;
    if (out_comp) *out_comp = total_comp;

    if (verbose) {
        double ratio = (total_orig > 0) ? ((double)total_comp / (double)total_orig * 100.0) : 100.0;
        fprintf(stderr, "Compressed %llu -> %llu bytes (%.2f%%)\n",
            (unsigned long long)total_orig, (unsigned long long)total_comp, ratio);
    }

    return 0;
}

static int decompress_stream(FILE *in, FILE *out, int test_only, int verbose, uint64_t *out_orig, uint64_t *out_comp)
{
    uint8_t hdr[8];
    if (fread(hdr, 1, 8, in) != 8) {
        fprintf(stderr, "lzav: input is not in LZAV format or corrupted (header too short)\n");
        return 1;
    }

    if (hdr[0] != 'L' || hdr[1] != 'Z' || hdr[2] != 'A' || hdr[3] != 'V') {
        fprintf(stderr, "lzav: invalid magic bytes - not an LZAV compressed file\n");
        return 1;
    }

    if (hdr[4] != LZAV_VERSION_ID) {
        fprintf(stderr, "lzav: unsupported LZAV format version %u\n", (unsigned)hdr[4]);
        return 1;
    }

    size_t in_buf_cap = BLOCK_SIZE + (BLOCK_SIZE / 4) + 65536;
    size_t out_buf_cap = BLOCK_SIZE;
    uint8_t *in_buf = (uint8_t *)malloc(in_buf_cap);
    uint8_t *out_buf = (uint8_t *)malloc(out_buf_cap);

    if (!in_buf || !out_buf) {
        fprintf(stderr, "lzav: memory allocation failure during decompression\n");
        if (in_buf) free(in_buf);
        if (out_buf) free(out_buf);
        return 1;
    }

    uint64_t total_orig = 0;
    uint64_t total_comp = 8;
    int block_idx = 0;

    while (1) {
        uint32_t orig_sz = 0;
        size_t r = fread(&orig_sz, 1, 4, in);
        if (r == 0) {
            /* Unexpected EOF without EOS marker */
            fprintf(stderr, "lzav: unexpected EOF (missing end-of-stream marker)\n");
            free(in_buf);
            free(out_buf);
            return 1;
        }
        if (r != 4) {
            fprintf(stderr, "lzav: corrupted block header\n");
            free(in_buf);
            free(out_buf);
            return 1;
        }

        if (orig_sz == 0) {
            /* End of Stream */
            total_comp += 4;
            break;
        }

        uint32_t comp_sz = 0;
        uint32_t expected_crc = 0;
        if (fread(&comp_sz, 1, 4, in) != 4 ||
            fread(&expected_crc, 1, 4, in) != 4) {
            fprintf(stderr, "lzav: truncated block header at block %d\n", block_idx);
            free(in_buf);
            free(out_buf);
            return 1;
        }
        total_comp += 12;

        if (comp_sz > in_buf_cap) {
            in_buf_cap = comp_sz + 65536;
            in_buf = (uint8_t *)realloc(in_buf, in_buf_cap);
            if (!in_buf) {
                fprintf(stderr, "lzav: memory allocation failed for compressed block\n");
                free(out_buf);
                return 1;
            }
        }

        if (orig_sz > out_buf_cap) {
            out_buf_cap = orig_sz + 65536;
            out_buf = (uint8_t *)realloc(out_buf, out_buf_cap);
            if (!out_buf) {
                fprintf(stderr, "lzav: memory allocation failed for uncompressed block\n");
                free(in_buf);
                return 1;
            }
        }

        if (fread(in_buf, 1, comp_sz, in) != comp_sz) {
            fprintf(stderr, "lzav: unexpected EOF in block payload at block %d\n", block_idx);
            free(in_buf);
            free(out_buf);
            return 1;
        }
        total_comp += comp_sz;

        if (comp_sz == orig_sz) {
            /* Raw uncompressed block */
            memcpy(out_buf, in_buf, orig_sz);
        } else {
            int dec_res = lzav_decompress(in_buf, out_buf, (int)comp_sz, (int)orig_sz);
            if (dec_res < 0) {
                fprintf(stderr, "lzav: decompression error (%d) at block %d\n", dec_res, block_idx);
                free(in_buf);
                free(out_buf);
                return 1;
            }
        }

        uint32_t actual_crc = calc_crc32(0, out_buf, orig_sz);
        if (actual_crc != expected_crc) {
            fprintf(stderr, "lzav: CRC32 checksum mismatch at block %d (expected 0x%08X, got 0x%08X)\n",
                block_idx, expected_crc, actual_crc);
            free(in_buf);
            free(out_buf);
            return 1;
        }

        if (!test_only && out != NULL) {
            if (fwrite(out_buf, 1, orig_sz, out) != orig_sz) {
                fprintf(stderr, "lzav: error writing decompressed output\n");
                free(in_buf);
                free(out_buf);
                return 1;
            }
        }

        total_orig += orig_sz;
        block_idx++;
    }

    free(in_buf);
    free(out_buf);

    if (out_orig) *out_orig = total_orig;
    if (out_comp) *out_comp = total_comp;

    if (verbose) {
        fprintf(stderr, "Decompressed %llu -> %llu bytes across %d blocks\n",
            (unsigned long long)total_comp, (unsigned long long)total_orig, block_idx);
    }

    return 0;
}

static int process_file(const char *in_name, const char *out_name,
                        int decompress, int test_only, int hi_mode,
                        int force, int keep, int verbose)
{
    FILE *in = NULL;
    FILE *out = NULL;
    int is_stdin = (in_name == NULL || strcmp(in_name, "-") == 0);
    int is_stdout = (out_name != NULL && strcmp(out_name, "-") == 0);

    if (is_stdin) {
        in = stdin;
    } else {
        in = fopen(in_name, "rb");
        if (!in) {
            fprintf(stderr, "lzav: cannot open '%s': %s\n", in_name, strerror(errno));
            return 1;
        }
    }

    char auto_out_name[1024];
    auto_out_name[0] = '\0';

    if (!test_only) {
        if (out_name == NULL) {
            if (is_stdin) {
                is_stdout = 1;
            } else {
                if (decompress) {
                    /* Strip .lzav suffix */
                    size_t len = strlen(in_name);
                    if (len > 5 && strcmp(in_name + len - 5, ".lzav") == 0) {
                        memcpy(auto_out_name, in_name, len - 5);
                        auto_out_name[len - 5] = '\0';
                    } else {
                        snprintf(auto_out_name, sizeof(auto_out_name), "%s.out", in_name);
                    }
                } else {
                    /* Append .lzav suffix */
                    snprintf(auto_out_name, sizeof(auto_out_name), "%s.lzav", in_name);
                }
                out_name = auto_out_name;
            }
        }

        if (is_stdout) {
            if (!decompress && isatty(fileno(stdout))) {
                fprintf(stderr, "lzav: compressed data not written to a terminal. Use -f to force or redirect to a file.\n");
                if (!is_stdin) fclose(in);
                return 1;
            }
            out = stdout;
        } else {
            if (!force && access(out_name, F_OK) == 0) {
                fprintf(stderr, "lzav: '%s' already exists; not overwritten. Use -f to force overwrite.\n", out_name);
                if (!is_stdin) fclose(in);
                return 1;
            }

            out = fopen(out_name, "wb");
            if (!out) {
                fprintf(stderr, "lzav: cannot create '%s': %s\n", out_name, strerror(errno));
                if (!is_stdin) fclose(in);
                return 1;
            }
        }
    }

    uint64_t total_orig = 0;
    uint64_t total_comp = 0;
    int ret = 0;

    if (decompress || test_only) {
        ret = decompress_stream(in, out, test_only, verbose, &total_orig, &total_comp);
        if (test_only && ret == 0) {
            if (verbose || !is_stdin) {
                fprintf(stderr, "%s: OK\n", is_stdin ? "(stdin)" : in_name);
            }
        }
    } else {
        ret = compress_stream(in, out, hi_mode, verbose, &total_orig, &total_comp);
    }

    if (!is_stdin) fclose(in);
    if (!test_only && !is_stdout && out) fclose(out);

    if (ret != 0) {
        /* On error, remove partial output file if not stdout */
        if (!test_only && !is_stdout && out_name && access(out_name, F_OK) == 0) {
            unlink(out_name);
        }
        return 1;
    }

    if (!keep && !is_stdin && !test_only && !is_stdout) {
        unlink(in_name);
    }

    return 0;
}

int main(int argc, char **argv)
{
    static struct option long_options[] = {
        {"stdout",     no_argument,       0, 'c'},
        {"decompress", no_argument,       0, 'd'},
        {"decode",     no_argument,       0, 'd'},
        {"test",       no_argument,       0, 't'},
        {"input",      required_argument, 0, 'i'},
        {"output",     required_argument, 0, 'o'},
        {"force",      no_argument,       0, 'f'},
        {"keep",       no_argument,       0, 'k'},
        {"fast",       no_argument,       0, '1'},
        {"best",       no_argument,       0, '2'},
        {"hi",         no_argument,       0, '2'},
        {"verbose",    no_argument,       0, 'v'},
        {"version",    no_argument,       0, 'V'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int decompress = 0;
    int test_only = 0;
    int to_stdout = 0;
    int force = 0;
    int keep = 1; /* keep input files by default */
    int hi_mode = 0;
    int verbose = 0;
    const char *explicit_in = NULL;
    const char *explicit_out = NULL;

    int opt;
    while ((opt = getopt_long(argc, argv, "cdti:o:fk123456789vhV", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            to_stdout = 1;
            break;
        case 'd':
            decompress = 1;
            break;
        case 't':
            test_only = 1;
            break;
        case 'i':
            explicit_in = optarg;
            break;
        case 'o':
            explicit_out = optarg;
            break;
        case 'f':
            force = 1;
            break;
        case 'k':
            keep = 1;
            break;
        case '1':
            hi_mode = 0;
            break;
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            hi_mode = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            printf("lzav version %s (using Aleksey Vaneev's LZAV algorithm)\n", LZAV_CLI_VERSION);
            return 0;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (explicit_in != NULL || explicit_out != NULL) {
        const char *out_name = to_stdout ? "-" : explicit_out;
        return process_file(explicit_in, out_name, decompress, test_only, hi_mode, force, keep, verbose);
    }

    if (optind >= argc) {
        /* No files: stream filter on stdin/stdout */
        return process_file(NULL, "-", decompress, test_only, hi_mode, force, keep, verbose);
    }

    int overall_ret = 0;
    for (int i = optind; i < argc; i++) {
        const char *in_name = argv[i];
        const char *out_name = to_stdout ? "-" : NULL;
        int r = process_file(in_name, out_name, decompress, test_only, hi_mode, force, keep, verbose);
        if (r != 0) overall_ret = 1;
    }

    return overall_ret;
}
