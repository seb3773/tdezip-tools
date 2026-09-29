#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
/*
 * density_main.c - Fast Density compression utility
 *
 * Based on Guillaume Vaudaux's Density compression library (g1mv/density).
 * Supports Chameleon, Cheetah, and Lion algorithms.
 * Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
 *
 * MIT / BSD-3 License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "density_api.h"

#define DENSITY_CLI_VERSION "1.0"
#define DENSITY_MAGIC       0x54534e44  /* 'D', 'N', 'S', 'T' in little-endian */
#define DENSITY_VERSION_ID  1
#define DENSITY_FLAG_CRC32  0x01
#define BLOCK_SIZE          (2 * 1024 * 1024) /* 2 MB chunk */

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
        "A fast lossless data compressor based on the Density library (g1mv/density).\n\n"
        "Options:\n"
        "  -c, --stdout       Write output on standard output, keep original files\n"
        "  -d, --decompress   Decompress (decode)\n"
        "  -t, --test         Test integrity of compressed files\n"
        "  -i, --input <file> Specify input file\n"
        "  -o, --output <file>Specify output file\n"
        "  -f, --force        Force overwrite of output files\n"
        "  -k, --keep         Keep (don't delete) input files (default)\n"
        "  -1, --chameleon    Chameleon algorithm (ultra-fast, GB/s)\n"
        "  -2, --cheetah      Cheetah algorithm (balanced speed/ratio, default)\n"
        "  -3, -9, --lion, --best Lion algorithm (high compression ratio)\n"
        "  -v, --verbose      Verbose mode (print ratios and statistics)\n"
        "  -V, --version      Display version information\n"
        "  -h, --help         Display this help message\n\n"
        "If no files are specified, or if '-' is given, reads from standard input\n"
        "and writes to standard output.\n",
        prog);
}

static int compress_stream(FILE *in, FILE *out, DENSITY_ALGORITHM algo, int verbose, uint64_t *out_orig, uint64_t *out_comp)
{
    /* 8-byte file header */
    uint8_t hdr[8];
    hdr[0] = 'D';
    hdr[1] = 'N';
    hdr[2] = 'S';
    hdr[3] = 'T';
    hdr[4] = DENSITY_VERSION_ID;
    hdr[5] = DENSITY_FLAG_CRC32;
    hdr[6] = 0;
    hdr[7] = 0;

    if (fwrite(hdr, 1, 8, out) != 8) {
        fprintf(stderr, "density: error writing file header\n");
        return 1;
    }

    uint8_t *in_buf = (uint8_t *)malloc(BLOCK_SIZE);
    uint_fast64_t max_bound = density_compress_safe_size(BLOCK_SIZE);
    uint8_t *comp_buf = (uint8_t *)malloc(max_bound);

    if (!in_buf || !comp_buf) {
        fprintf(stderr, "density: memory allocation failure\n");
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
                fprintf(stderr, "density: error reading input\n");
                free(in_buf);
                free(comp_buf);
                return 1;
            }
            break;
        }

        total_orig += n;
        uint32_t crc = calc_crc32(0, in_buf, n);

        uint_fast64_t bound = density_compress_safe_size(n);
        density_processing_result res = density_compress(in_buf, n, comp_buf, bound, algo);

        uint32_t orig_sz = (uint32_t)n;
        uint32_t comp_sz;
        const uint8_t *data_ptr;

        if (res.state == DENSITY_STATE_OK && res.bytesWritten > 0 && res.bytesWritten < n) {
            comp_sz = (uint32_t)res.bytesWritten;
            data_ptr = comp_buf;
        } else {
            /* Incompressible or didn't shrink: store raw */
            comp_sz = orig_sz;
            data_ptr = in_buf;
        }

        uint8_t block_hdr[12];
        memcpy(block_hdr, &orig_sz, 4);
        memcpy(block_hdr + 4, &comp_sz, 4);
        memcpy(block_hdr + 8, &crc, 4);

        if (fwrite(block_hdr, 1, 12, out) != 12 ||
            fwrite(data_ptr, 1, comp_sz, out) != comp_sz) {
            fprintf(stderr, "density: error writing block data\n");
            free(in_buf);
            free(comp_buf);
            return 1;
        }

        total_comp += 12 + comp_sz;
    }

    /* End-of-Stream marker (orig_sz == 0) */
    uint32_t eos = 0;
    if (fwrite(&eos, 1, 4, out) != 4) {
        fprintf(stderr, "density: error writing end-of-stream marker\n");
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
        fprintf(stderr, "density: input is not in Density format or corrupted (header too short)\n");
        return 1;
    }

    if (hdr[0] != 'D' || hdr[1] != 'N' || hdr[2] != 'S' || hdr[3] != 'T') {
        fprintf(stderr, "density: invalid magic bytes - not a Density compressed file\n");
        return 1;
    }

    if (hdr[4] != DENSITY_VERSION_ID) {
        fprintf(stderr, "density: unsupported Density format version %u\n", (unsigned)hdr[4]);
        return 1;
    }

    size_t in_buf_cap = density_compress_safe_size(BLOCK_SIZE) + 65536;
    size_t out_buf_cap = density_decompress_safe_size(BLOCK_SIZE) + 65536;
    uint8_t *in_buf = (uint8_t *)malloc(in_buf_cap);
    uint8_t *out_buf = (uint8_t *)malloc(out_buf_cap);

    if (!in_buf || !out_buf) {
        fprintf(stderr, "density: memory allocation failure during decompression\n");
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
            fprintf(stderr, "density: unexpected EOF (missing end-of-stream marker)\n");
            free(in_buf);
            free(out_buf);
            return 1;
        }
        if (r != 4) {
            fprintf(stderr, "density: corrupted block header\n");
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
            fprintf(stderr, "density: truncated block header at block %d\n", block_idx);
            free(in_buf);
            free(out_buf);
            return 1;
        }
        total_comp += 12;

        if (comp_sz > in_buf_cap) {
            in_buf_cap = comp_sz + 65536;
            in_buf = (uint8_t *)realloc(in_buf, in_buf_cap);
            if (!in_buf) {
                fprintf(stderr, "density: memory allocation failed for compressed block\n");
                free(out_buf);
                return 1;
            }
        }

        uint_fast64_t safe_decomp_size = density_decompress_safe_size(orig_sz);
        if (safe_decomp_size > out_buf_cap) {
            out_buf_cap = safe_decomp_size + 65536;
            out_buf = (uint8_t *)realloc(out_buf, out_buf_cap);
            if (!out_buf) {
                fprintf(stderr, "density: memory allocation failed for uncompressed block\n");
                free(in_buf);
                return 1;
            }
        }

        if (fread(in_buf, 1, comp_sz, in) != comp_sz) {
            fprintf(stderr, "density: unexpected EOF in block payload at block %d\n", block_idx);
            free(in_buf);
            free(out_buf);
            return 1;
        }
        total_comp += comp_sz;

        if (comp_sz == orig_sz) {
            /* Stored uncompressed */
            uint32_t actual_crc = calc_crc32(0, in_buf, orig_sz);
            if (actual_crc != expected_crc) {
                fprintf(stderr, "density: CRC32 mismatch in raw block %d: computed 0x%08X, expected 0x%08X\n",
                    block_idx, actual_crc, expected_crc);
                free(in_buf);
                free(out_buf);
                return 1;
            }
            if (!test_only && out) {
                if (fwrite(in_buf, 1, orig_sz, out) != orig_sz) {
                    fprintf(stderr, "density: error writing uncompressed output\n");
                    free(in_buf);
                    free(out_buf);
                    return 1;
                }
            }
        } else {
            /* Compressed block */
            density_processing_result res = density_decompress(in_buf, comp_sz, out_buf, safe_decomp_size);
            if (res.state != DENSITY_STATE_OK || res.bytesWritten != orig_sz) {
                fprintf(stderr, "density: decompression error at block %d (state: %d, written: %llu, expected: %u)\n",
                    block_idx, res.state, (unsigned long long)res.bytesWritten, orig_sz);
                free(in_buf);
                free(out_buf);
                return 1;
            }

            uint32_t actual_crc = calc_crc32(0, out_buf, orig_sz);
            if (actual_crc != expected_crc) {
                fprintf(stderr, "density: CRC32 mismatch at block %d: computed 0x%08X, expected 0x%08X\n",
                    block_idx, actual_crc, expected_crc);
                free(in_buf);
                free(out_buf);
                return 1;
            }

            if (!test_only && out) {
                if (fwrite(out_buf, 1, orig_sz, out) != orig_sz) {
                    fprintf(stderr, "density: error writing decompressed output\n");
                    free(in_buf);
                    free(out_buf);
                    return 1;
                }
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
        if (test_only) {
            fprintf(stderr, "Integrity OK: %llu bytes tested across %d blocks\n",
                (unsigned long long)total_orig, block_idx);
        } else {
            fprintf(stderr, "Decompressed %llu -> %llu bytes\n",
                (unsigned long long)total_comp, (unsigned long long)total_orig);
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int opt;
    int decompress = 0;
    int test_only = 0;
    int to_stdout = 0;
    int force = 0;
    int keep = 1;
    int verbose = 0;
    DENSITY_ALGORITHM algo = DENSITY_ALGORITHM_CHEETAH; /* default: balanced */
    const char *input_path = NULL;
    const char *output_path = NULL;

    static struct option long_options[] = {
        {"stdout",     no_argument,       0, 'c'},
        {"decompress", no_argument,       0, 'd'},
        {"test",       no_argument,       0, 't'},
        {"input",      required_argument, 0, 'i'},
        {"output",     required_argument, 0, 'o'},
        {"force",      no_argument,       0, 'f'},
        {"keep",       no_argument,       0, 'k'},
        {"chameleon",  no_argument,       0, '1'},
        {"cheetah",    no_argument,       0, '2'},
        {"lion",       no_argument,       0, '3'},
        {"best",       no_argument,       0, '9'},
        {"verbose",    no_argument,       0, 'v'},
        {"version",    no_argument,       0, 'V'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "cdti:o:fk1239vVh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            to_stdout = 1;
            break;
        case 'd':
            decompress = 1;
            break;
        case 't':
            test_only = 1;
            decompress = 1;
            break;
        case 'i':
            input_path = optarg;
            break;
        case 'o':
            output_path = optarg;
            break;
        case 'f':
            force = 1;
            break;
        case 'k':
            keep = 1;
            break;
        case '1':
            algo = DENSITY_ALGORITHM_CHAMELEON;
            break;
        case '2':
            algo = DENSITY_ALGORITHM_CHEETAH;
            break;
        case '3':
        case '9':
            algo = DENSITY_ALGORITHM_LION;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            printf("density (Density CLI) version %s (libdensity %d.%d.%d)\n",
                   DENSITY_CLI_VERSION,
                   density_version_major(), density_version_minor(), density_version_revision());
            return 0;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Process positional files or input/output paths */
    int num_files = argc - optind;

    if (input_path != NULL || num_files == 0) {
        FILE *in = stdin;
        FILE *out = stdout;

        if (input_path && strcmp(input_path, "-") != 0) {
            in = fopen(input_path, "rb");
            if (!in) {
                fprintf(stderr, "density: cannot open '%s': %s\n", input_path, strerror(errno));
                return 1;
            }
        }

        if (output_path && strcmp(output_path, "-") != 0 && !test_only) {
            if (!force && access(output_path, F_OK) == 0) {
                fprintf(stderr, "density: output file '%s' already exists (use -f to overwrite)\n", output_path);
                if (in != stdin) fclose(in);
                return 1;
            }
            out = fopen(output_path, "wb");
            if (!out) {
                fprintf(stderr, "density: cannot open '%s' for writing: %s\n", output_path, strerror(errno));
                if (in != stdin) fclose(in);
                return 1;
            }
        } else if (!to_stdout && !test_only && isatty(fileno(stdout))) {
            if (decompress) {
                fprintf(stderr, "density: compressed data not read from a terminal. Use -h for help.\n");
                if (in != stdin) fclose(in);
                return 1;
            } else {
                fprintf(stderr, "density: compressed data not written to a terminal. Use -c to force.\n");
                if (in != stdin) fclose(in);
                return 1;
            }
        }

        int ret;
        uint64_t orig = 0, comp = 0;
        if (decompress) {
            ret = decompress_stream(in, test_only ? NULL : out, test_only, verbose, &orig, &comp);
        } else {
            ret = compress_stream(in, out, algo, verbose, &orig, &comp);
        }

        if (in != stdin) fclose(in);
        if (out != stdout && out != NULL) fclose(out);
        return ret;
    }

    /* Process positional file list */
    int overall_status = 0;
    for (int i = optind; i < argc; i++) {
        const char *src = argv[i];
        if (strcmp(src, "-") == 0) {
            int ret;
            if (decompress)
                ret = decompress_stream(stdin, test_only ? NULL : stdout, test_only, verbose, NULL, NULL);
            else
                ret = compress_stream(stdin, stdout, algo, verbose, NULL, NULL);
            if (ret != 0) overall_status = 1;
            continue;
        }

        FILE *in = fopen(src, "rb");
        if (!in) {
            fprintf(stderr, "density: cannot open '%s': %s\n", src, strerror(errno));
            overall_status = 1;
            continue;
        }

        FILE *out = NULL;
        char out_filename[4096];

        if (test_only) {
            int ret = decompress_stream(in, NULL, 1, verbose, NULL, NULL);
            fclose(in);
            if (ret != 0) {
                fprintf(stderr, "density: test failed on '%s'\n", src);
                overall_status = 1;
            } else if (verbose) {
                fprintf(stderr, "density: '%s': OK\n", src);
            }
            continue;
        }

        if (to_stdout) {
            out = stdout;
        } else if (decompress) {
            size_t len = strlen(src);
            if (len > 8 && strcmp(src + len - 8, ".density") == 0) {
                snprintf(out_filename, sizeof(out_filename), "%.*s", (int)(len - 8), src);
            } else {
                snprintf(out_filename, sizeof(out_filename), "%s.out", src);
            }

            if (!force && access(out_filename, F_OK) == 0) {
                fprintf(stderr, "density: '%s' already exists (use -f to overwrite)\n", out_filename);
                fclose(in);
                overall_status = 1;
                continue;
            }
            out = fopen(out_filename, "wb");
            if (!out) {
                fprintf(stderr, "density: cannot open '%s': %s\n", out_filename, strerror(errno));
                fclose(in);
                overall_status = 1;
                continue;
            }
        } else {
            snprintf(out_filename, sizeof(out_filename), "%s.density", src);
            if (!force && access(out_filename, F_OK) == 0) {
                fprintf(stderr, "density: '%s' already exists (use -f to overwrite)\n", out_filename);
                fclose(in);
                overall_status = 1;
                continue;
            }
            out = fopen(out_filename, "wb");
            if (!out) {
                fprintf(stderr, "density: cannot open '%s': %s\n", out_filename, strerror(errno));
                fclose(in);
                overall_status = 1;
                continue;
            }
        }

        int ret;
        if (decompress)
            ret = decompress_stream(in, out, 0, verbose, NULL, NULL);
        else
            ret = compress_stream(in, out, algo, verbose, NULL, NULL);

        fclose(in);
        if (out != stdout) fclose(out);

        if (ret != 0) {
            overall_status = 1;
            if (!to_stdout && access(out_filename, F_OK) == 0) {
                unlink(out_filename);
            }
        } else if (!keep && !to_stdout) {
            unlink(src);
        }
    }

    return overall_status;
}
