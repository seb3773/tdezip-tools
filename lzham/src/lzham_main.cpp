// lzham_main.cpp - Standalone Unix CLI for LZHAM compressor
// Built for TdeZip and Linux systems
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "lzham_static_lib.h"

#define LZHAM_CLI_VERSION "1.0"
#define BUFFER_SIZE (128 * 1024)

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "LZHAM Archiver & Compressor v%s (Engine v%x)\n"
        "Usage: %s [options] [files...]\n"
        "       %s c [options] input output\n"
        "       %s d [options] input output\n\n"
        "Modes & Options:\n"
        "  -c, --stdout      Write output to standard output (pipe/filter)\n"
        "  -d, --decompress  Decompress archive\n"
        "  -t, --test        Test archive integrity\n"
        "  -f, --force       Force overwrite of existing output files\n"
        "  -k, --keep        Keep original input files (default: true)\n"
        "  -m<0-4>           Compression level: 0=fastest, 1=faster, 2=default, 3=better, 4=uber (def: 2)\n"
        "  -d<15-29>         Log2 dictionary size (15=32KB, 20=1MB, 26=64MB, 28=256MB, 29=512MB, def: 26)\n"
        "  -q, --quiet       Quiet mode, suppress progress and statistics\n"
        "  -h, --help        Show this help summary\n",
        LZHAM_CLI_VERSION, lzham_get_version(), prog, prog, prog);
}

// Compress from in_fp to out_fp
static bool do_compress(FILE *in_fp, FILE *out_fp, uint32_t dict_size_log2, lzham_compress_level level, uint64_t uncomp_size, bool quiet)
{
    // Write 13-byte standard LZHAM container header
    fputc('L', out_fp);
    fputc('Z', out_fp);
    fputc('H', out_fp);
    fputc('0', out_fp);
    fputc((int)dict_size_log2, out_fp);
    for (int i = 0; i < 8; i++) {
        fputc((int)((uncomp_size >> (i * 8)) & 0xFF), out_fp);
    }

    lzham_compress_params params;
    memset(&params, 0, sizeof(params));
    params.m_struct_size = sizeof(params);
    params.m_dict_size_log2 = dict_size_log2;
    params.m_level = level;
    params.m_max_helper_threads = -1; // Auto detect CPU cores

    lzham_compress_state_ptr pState = lzham_compress_init(&params);
    if (!pState) {
        if (!quiet) fprintf(stderr, "Error: Failed initializing LZHAM compressor.\n");
        return false;
    }

    uint8_t *in_buf = (uint8_t*)malloc(BUFFER_SIZE);
    uint8_t *out_buf = (uint8_t*)malloc(BUFFER_SIZE);
    if (!in_buf || !out_buf) {
        if (!quiet) fprintf(stderr, "Error: Memory allocation failure.\n");
        lzham_compress_deinit(pState);
        free(in_buf); free(out_buf);
        return false;
    }

    bool success = true;
    bool no_more_input = false;
    size_t in_buf_size = 0;
    size_t in_buf_ofs = 0;

    for (;;) {
        if (in_buf_ofs == in_buf_size && !no_more_input) {
            in_buf_size = fread(in_buf, 1, BUFFER_SIZE, in_fp);
            in_buf_ofs = 0;
            if (in_buf_size < BUFFER_SIZE) {
                no_more_input = true;
            }
        }

        size_t in_avail = in_buf_size - in_buf_ofs;
        size_t out_avail = BUFFER_SIZE;

        lzham_compress_status_t status = lzham_compress(
            pState,
            in_buf + in_buf_ofs, &in_avail,
            out_buf, &out_avail,
            no_more_input
        );

        in_buf_ofs += in_avail;

        if (out_avail > 0) {
            if (fwrite(out_buf, 1, out_avail, out_fp) != out_avail) {
                if (!quiet) fprintf(stderr, "Error: Failed writing compressed data to output.\n");
                success = false;
                break;
            }
        }

        if (status >= LZHAM_COMP_STATUS_FIRST_SUCCESS_OR_FAILURE_CODE) {
            if (status != LZHAM_COMP_STATUS_SUCCESS) {
                if (!quiet) fprintf(stderr, "Error: Compression failed with status %d\n", status);
                success = false;
            }
            break;
        }
    }

    lzham_compress_deinit(pState);
    free(in_buf);
    free(out_buf);
    return success;
}

// Decompress from in_fp to out_fp (or discard if out_fp == NULL for testing)
static bool do_decompress(FILE *in_fp, FILE *out_fp, bool test_only, bool quiet)
{
    // Read header
    int h0 = fgetc(in_fp);
    int h1 = fgetc(in_fp);
    int h2 = fgetc(in_fp);
    int h3 = fgetc(in_fp);
    if (h0 != 'L' || h1 != 'Z' || h2 != 'H' || h3 != '0') {
        if (!quiet) fprintf(stderr, "Error: Not a valid LZHAM file (invalid magic header).\n");
        return false;
    }

    int dict_size = fgetc(in_fp);
    if (dict_size < LZHAM_MIN_DICT_SIZE_LOG2 || dict_size > LZHAM_MAX_DICT_SIZE_LOG2_X64) {
        if (!quiet) fprintf(stderr, "Error: Invalid dictionary size (%d) in LZHAM header.\n", dict_size);
        return false;
    }

    uint64_t uncomp_size = 0;
    for (int i = 0; i < 8; i++) {
        int b = fgetc(in_fp);
        if (b < 0) {
            if (!quiet) fprintf(stderr, "Error: Truncated header in LZHAM file.\n");
            return false;
        }
        uncomp_size |= ((uint64_t)b << (i * 8));
    }

    lzham_decompress_params params;
    memset(&params, 0, sizeof(params));
    params.m_struct_size = sizeof(params);
    params.m_dict_size_log2 = dict_size;
    params.m_decompress_flags = LZHAM_DECOMP_FLAG_COMPUTE_ADLER32;

    lzham_decompress_state_ptr pState = lzham_decompress_init(&params);
    if (!pState) {
        if (!quiet) fprintf(stderr, "Error: Failed initializing LZHAM decompressor.\n");
        return false;
    }

    uint8_t *in_buf = (uint8_t*)malloc(BUFFER_SIZE);
    uint8_t *out_buf = (uint8_t*)malloc(BUFFER_SIZE);
    if (!in_buf || !out_buf) {
        if (!quiet) fprintf(stderr, "Error: Memory allocation failure.\n");
        lzham_decompress_deinit(pState);
        free(in_buf); free(out_buf);
        return false;
    }

    bool success = true;
    bool no_more_input = false;
    size_t in_buf_size = 0;
    size_t in_buf_ofs = 0;

    for (;;) {
        if (in_buf_ofs == in_buf_size && !no_more_input) {
            in_buf_size = fread(in_buf, 1, BUFFER_SIZE, in_fp);
            in_buf_ofs = 0;
            if (in_buf_size < BUFFER_SIZE) {
                no_more_input = true;
            }
        }

        size_t in_avail = in_buf_size - in_buf_ofs;
        size_t out_avail = BUFFER_SIZE;

        lzham_decompress_status_t status = lzham_decompress(
            pState,
            in_buf + in_buf_ofs, &in_avail,
            out_buf, &out_avail,
            no_more_input
        );

        in_buf_ofs += in_avail;

        if (out_avail > 0 && !test_only && out_fp != NULL) {
            if (fwrite(out_buf, 1, out_avail, out_fp) != out_avail) {
                if (!quiet) fprintf(stderr, "Error: Failed writing decompressed data.\n");
                success = false;
                break;
            }
        }

        if (status >= LZHAM_DECOMP_STATUS_FIRST_SUCCESS_OR_FAILURE_CODE) {
            if (status != LZHAM_DECOMP_STATUS_SUCCESS) {
                if (!quiet) fprintf(stderr, "Error: Decompression failed with status %d\n", status);
                success = false;
            }
            break;
        }
    }

    uint32_t adler = lzham_decompress_deinit(pState);
    (void)adler;
    free(in_buf);
    free(out_buf);
    return success;
}

int main(int argc, char **argv)
{
    bool decompress_mode = false;
    bool test_mode = false;
    bool to_stdout = false;
    bool force = false;
    bool quiet = false;
    uint32_t dict_size = 26; // 64 MB default
    lzham_compress_level level = LZHAM_COMP_LEVEL_DEFAULT; // 2
    int arg_start = 1;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 1;
    }

    // Support legacy syntax: lzham c [options] in out OR lzham d [options] in out
    if (argc > 1 && strcmp(argv[1], "c") == 0) {
        decompress_mode = false;
        arg_start = 2;
    } else if (argc > 1 && strcmp(argv[1], "d") == 0) {
        decompress_mode = true;
        arg_start = 2;
    }

    const char *in_filename = NULL;
    const char *out_filename = NULL;

    for (int i = arg_start; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decompress") == 0) {
            decompress_mode = true;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--test") == 0) {
            test_mode = true;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--stdout") == 0) {
            to_stdout = true;
        } else if (strcmp(argv[i], "-dc") == 0 || strcmp(argv[i], "-cd") == 0) {
            decompress_mode = true;
            to_stdout = true;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = true;
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep") == 0) {
            // keep files, default
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (strncmp(argv[i], "-m", 2) == 0) {
            int l = atoi(argv[i] + 2);
            if (l >= 0 && l <= 4) level = (lzham_compress_level)l;
        } else if (strncmp(argv[i], "-d", 2) == 0 && strlen(argv[i]) > 2 && argv[i][2] >= '0' && argv[i][2] <= '9') {
            int d = atoi(argv[i] + 2);
            if (d >= 15 && d <= 29) dict_size = (uint32_t)d;
        } else if (argv[i][0] != '-') {
            if (!in_filename) in_filename = argv[i];
            else if (!out_filename) out_filename = argv[i];
        }
    }

    // Default to stdin/stdout if no filenames provided
    bool is_stdin = (!in_filename || strcmp(in_filename, "-") == 0);
    FILE *in_fp = stdin;
    uint64_t in_size = 0;

    if (!is_stdin) {
        in_fp = fopen(in_filename, "rb");
        if (!in_fp) {
            fprintf(stderr, "Error: Cannot open input file '%s'\n", in_filename);
            return 1;
        }
        struct stat st;
        if (stat(in_filename, &st) == 0) {
            in_size = (uint64_t)st.st_size;
        }
    }

    FILE *out_fp = stdout;
    char default_out[1024];

    if (test_mode) {
        bool ok = do_decompress(in_fp, NULL, true, quiet);
        if (!is_stdin) fclose(in_fp);
        if (ok) {
            if (!quiet) printf("%s: OK\n", in_filename ? in_filename : "stdin");
            return 0;
        } else {
            if (!quiet) fprintf(stderr, "%s: FAILED\n", in_filename ? in_filename : "stdin");
            return 1;
        }
    }

    if (!to_stdout) {
        if (out_filename) {
            snprintf(default_out, sizeof(default_out), "%s", out_filename);
        } else if (!is_stdin) {
            if (decompress_mode) {
                // Strip .lzham extension
                size_t len = strlen(in_filename);
                if (len > 6 && strcmp(in_filename + len - 6, ".lzham") == 0) {
                    snprintf(default_out, sizeof(default_out), "%.*s", (int)(len - 6), in_filename);
                } else {
                    snprintf(default_out, sizeof(default_out), "%s.out", in_filename);
                }
            } else {
                snprintf(default_out, sizeof(default_out), "%s.lzham", in_filename);
            }
        } else {
            to_stdout = true;
        }

        if (!to_stdout) {
            if (!force && access(default_out, F_OK) == 0) {
                fprintf(stderr, "Error: Output file '%s' already exists (use -f to overwrite)\n", default_out);
                if (!is_stdin) fclose(in_fp);
                return 1;
            }
            out_fp = fopen(default_out, "wb");
            if (!out_fp) {
                fprintf(stderr, "Error: Cannot create output file '%s'\n", default_out);
                if (!is_stdin) fclose(in_fp);
                return 1;
            }
        }
    }

    bool res = false;
    if (decompress_mode) {
        res = do_decompress(in_fp, out_fp, false, quiet);
    } else {
        res = do_compress(in_fp, out_fp, dict_size, level, in_size, quiet);
    }

    if (!is_stdin) fclose(in_fp);
    if (!to_stdout && out_fp != stdout) fclose(out_fp);

    return res ? 0 : 1;
}
