/*
 * pea_cli.c - command line front-end for the pure C PEA implementation.
 *
 *   pea c [-m method] [-a stream] [-o object] [-v volume]
 *         [-p password] [-k keyfile] archive path...
 *   pea l [-p password] [-k keyfile] archive
 *   pea t [-p password] [-k keyfile] archive
 *   pea x [-p password] [-k keyfile] archive [outdir]
 */
#include "pea_archive.h"
#include "pea.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "pea-c - pure C implementation of the PEA archive format 1.6\n"
        "\n"
        "Usage:\n"
        "  %s c [-m method] [-a algo] [-o algo] [-v algo] [-s volsize] [-n niter] [-p password] [-k keyfile] archive path...\n"
        "  %s l [-p password] [-k keyfile] archive        list objects\n"
        "  %s t [-p password] [-k keyfile] archive        verify all control tags\n"
        "  %s x [-p password] [-k keyfile] archive [dir]  extract (default: current directory)\n"
        "\n"
        "  -m method   PCOMPRESS0 (store), PCOMPRESS1/2/3 or 0/1/2/3\n"
        "  -a algo     stream control algorithm (default CRC32)\n"
        "  -o algo     object control algorithm (default CRC32)\n"
        "  -v algo     volume control algorithm (default CRC32)\n"
        "  -s volsize  volume file size in bytes (0 or omitted: single .pea)\n"
        "  -p password password (required for EAX256 / EAX / HMAC / TF / SP / cascades)\n"
        "  -k keyfile  optional keyfile (first 2048 bytes, appended to KDF material)\n"
        "  -n niter    KDF iteration multiplier 0..7 (cascades; stored in archive header)\n"
        "\n"
        "  Supported algorithms: NOALGO, ADLER32, CRC32, CRC64, MD5, RIPEMD160,\n"
        "                        SHA1, SHA256, SHA512, SHA3_256, SHA3_512,\n"
        "                        WHIRLPOOL, BLAKE2S, BLAKE2B, HMAC, EAX, EAX256,\n"
        "                        TF, TF256, SP, SP256, TRIATS, TRITSA, TRISAT,\n"
        "                        SRIATS, SRITSA, SRISAT, HRIATS, HRITSA, HRISAT\n",
        prog, prog, prog, prog);
}

static int is_legacy_cmd(const char *cmd)
{
    return (!strcasecmp(cmd, "PEA") ||
            !strcasecmp(cmd, "UNPEA") ||
            !strcasecmp(cmd, "TEST") ||
            !strcasecmp(cmd, "LIST"));
}

static const char *resolve_archive_path(const char *in_path, char *buf, size_t bufsz)
{
    size_t len = strlen(in_path);
    if (len >= 4 && strcasecmp(in_path + len - 4, ".pea") == 0)
        return in_path;
    snprintf(buf, bufsz, "%s.pea", in_path);
    return buf;
}

static int handle_legacy_cli(int argc, char **argv)
{
    const char *cmd = argv[1];

    if (!strcasecmp(cmd, "PEA")) {
        /* Syntax: pea PEA <outbase> <volsize> <compr> <vol_algo> <obj_algo> <stream_algo> [flags/pw/kf...] FROMCL <files...> */
        if (argc < 8) {
            fprintf(stderr, "pea: invalid PEA command syntax\n");
            return 2;
        }

        pea_options o;
        pea_options_default(&o);

        char arc_buf[PATH_MAX];
        const char *archive = argv[2];

        char *end = NULL;
        unsigned long long v = strtoull(argv[3], &end, 10);
        if (end && !*end) {
            o.volsize = v;
        }

        if (o.volsize == 0) {
            archive = resolve_archive_path(argv[2], arc_buf, sizeof(arc_buf));
        }

        int c = pea_compr_from_name(argv[4]);
        if (c >= 0) o.compr = c;

        int va = pea_algo_from_name(argv[5]);
        if (va >= 0) o.volume_algo = va;

        int oa = pea_algo_from_name(argv[6]);
        if (oa >= 0) o.obj_algo = oa;

        int sa = pea_algo_from_name(argv[7]);
        if (sa >= 0) o.stream_algo = sa;

        int fromcl_idx = -1;
        const char *pw = NULL;
        const char *kf = NULL;

        for (int i = 8; i < argc; i++) {
            if (!strcasecmp(argv[i], "FROMCL")) {
                fromcl_idx = i;
                break;
            }
            if (!strcasecmp(argv[i], "HIDDEN") ||
                !strcasecmp(argv[i], "BATCH") ||
                !strcasecmp(argv[i], "RESETDATE") ||
                !strcasecmp(argv[i], "RESETATTR") ||
                !strcasecmp(argv[i], "EXTRACT2DIR") ||
                !strcasecmp(argv[i], "EXTRACT2TESTB")) {
                continue;
            }
            if (!pw) {
                if (argv[i][0] != '\0' && strcasecmp(argv[i], "NOKEYFILE") != 0)
                    pw = argv[i];
            } else if (!kf) {
                if (argv[i][0] != '\0' && strcasecmp(argv[i], "NOKEYFILE") != 0)
                    kf = argv[i];
            }
        }

        o.password = pw;
        o.keyfile = kf;

        if (fromcl_idx < 0 || fromcl_idx + 1 >= argc) {
            fprintf(stderr, "pea: no input files after FROMCL\n");
            return 2;
        }

        const char **paths = (const char **)&argv[fromcl_idx + 1];
        int npaths = argc - (fromcl_idx + 1);
        return pea_create(archive, paths, npaths, &o) ? 1 : 0;
    }

    if (!strcasecmp(cmd, "UNPEA")) {
        /* Syntax: pea UNPEA <archive> <outdir> [flags...] [password] [keyfile] */
        if (argc < 4) {
            fprintf(stderr, "pea: missing arguments for UNPEA\n");
            return 2;
        }
        char arc_buf[PATH_MAX];
        const char *archive = resolve_archive_path(argv[2], arc_buf, sizeof(arc_buf));
        const char *outdir = argv[3];
        int is_test = 0;
        const char *pw = NULL;
        const char *kf = NULL;

        for (int i = 4; i < argc; i++) {
            if (!strcasecmp(argv[i], "EXTRACT2TESTB")) {
                is_test = 1;
                continue;
            }
            if (!strcasecmp(argv[i], "HIDDEN") ||
                !strcasecmp(argv[i], "BATCH") ||
                !strcasecmp(argv[i], "RESETDATE") ||
                !strcasecmp(argv[i], "RESETATTR") ||
                !strcasecmp(argv[i], "EXTRACT2DIR")) {
                continue;
            }
            if (!pw) {
                if (argv[i][0] != '\0' && strcasecmp(argv[i], "NOKEYFILE") != 0)
                    pw = argv[i];
            } else if (!kf) {
                if (argv[i][0] != '\0' && strcasecmp(argv[i], "NOKEYFILE") != 0)
                    kf = argv[i];
            }
        }

        if (is_test)
            return pea_test(archive, pw, kf) ? 1 : 0;
        return pea_extract(archive, outdir, pw, kf) ? 1 : 0;
    }

    if (!strcasecmp(cmd, "TEST")) {
        /* Syntax: pea TEST <archive> [flags...] [password] [keyfile] */
        if (argc < 3) {
            fprintf(stderr, "pea: missing archive for TEST\n");
            return 2;
        }
        char arc_buf[PATH_MAX];
        const char *archive = resolve_archive_path(argv[2], arc_buf, sizeof(arc_buf));
        const char *pw = NULL;
        const char *kf = NULL;

        for (int i = 3; i < argc; i++) {
            if (!strcasecmp(argv[i], "HIDDEN") || !strcasecmp(argv[i], "BATCH"))
                continue;
            if (!pw) {
                if (argv[i][0] != '\0' && strcasecmp(argv[i], "NOKEYFILE") != 0)
                    pw = argv[i];
            } else if (!kf) {
                if (argv[i][0] != '\0' && strcasecmp(argv[i], "NOKEYFILE") != 0)
                    kf = argv[i];
            }
        }
        return pea_test(archive, pw, kf) ? 1 : 0;
    }

    if (!strcasecmp(cmd, "LIST")) {
        /* Syntax: pea LIST <archive> [flags...] [password] [keyfile] */
        if (argc < 3) {
            fprintf(stderr, "pea: missing archive for LIST\n");
            return 2;
        }
        char arc_buf[PATH_MAX];
        const char *archive = resolve_archive_path(argv[2], arc_buf, sizeof(arc_buf));
        const char *pw = NULL;
        const char *kf = NULL;

        for (int i = 3; i < argc; i++) {
            if (!strcasecmp(argv[i], "HIDDEN") || !strcasecmp(argv[i], "BATCH"))
                continue;
            if (!pw) {
                if (argv[i][0] != '\0' && strcasecmp(argv[i], "NOKEYFILE") != 0)
                    pw = argv[i];
            } else if (!kf) {
                if (argv[i][0] != '\0' && strcasecmp(argv[i], "NOKEYFILE") != 0)
                    kf = argv[i];
            }
        }
        return pea_list(archive, pw, kf) ? 1 : 0;
    }

    return 2;
}

int main(int argc, char **argv)
{
    const char *password = NULL;
    const char *keyfile = NULL;
    int opt;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    if (is_legacy_cmd(argv[1])) {
        return handle_legacy_cli(argc, argv);
    }

    if (!strcmp(argv[1], "l") || !strcmp(argv[1], "t") || !strcmp(argv[1], "x")) {
        char mode = argv[1][0];

        optind = 2;
        while ((opt = getopt(argc, argv, "p:k:")) != -1) {
            switch (opt) {
            case 'p':
                password = optarg;
                break;
            case 'k':
                keyfile = optarg;
                break;
            default:
                usage(argv[0]);
                return 2;
            }
        }
        if (optind >= argc) {
            usage(argv[0]);
            return 2;
        }
        if (mode == 'l')
            return pea_list(argv[optind], password, keyfile) ? 1 : 0;
        if (mode == 't')
            return pea_test(argv[optind], password, keyfile) ? 1 : 0;
        return pea_extract(argv[optind],
                           argc > optind + 1 ? argv[optind + 1] : ".",
                           password, keyfile) ? 1 : 0;
    }

    if (strcmp(argv[1], "c") != 0) {
        usage(argv[0]);
        return 2;
    }

    {
        pea_options o;
        const char *archive;

        pea_options_default(&o);

        optind = 2;
        while ((opt = getopt(argc, argv, "m:a:o:v:s:p:k:n:")) != -1) {
            switch (opt) {
            case 'm': {
                int c = pea_compr_from_name(optarg);
                if (c < 0) {
                    fprintf(stderr, "pea: unknown method '%s'\n", optarg);
                    return 2;
                }
                o.compr = c;
                break;
            }
            case 'a': {
                int a = pea_algo_from_name(optarg);
                if (a < 0) {
                    fprintf(stderr, "pea: unknown algorithm '%s'\n", optarg);
                    return 2;
                }
                o.stream_algo = a;
                break;
            }
            case 'o': {
                int a = pea_algo_from_name(optarg);
                if (a < 0) {
                    fprintf(stderr, "pea: unknown algorithm '%s'\n", optarg);
                    return 2;
                }
                o.obj_algo = a;
                break;
            }
            case 'v': {
                int a = pea_algo_from_name(optarg);
                if (a < 0) {
                    fprintf(stderr, "pea: unknown algorithm '%s'\n", optarg);
                    return 2;
                }
                o.volume_algo = a;
                break;
            }
            case 'p':
                o.password = optarg;
                break;
            case 'k':
                o.keyfile = optarg;
                break;
            case 's': {
                char *end = NULL;
                unsigned long long v = strtoull(optarg, &end, 10);
                if (!end || *end || (v == 0 && optarg[0] != '0')) {
                    fprintf(stderr, "pea: invalid volume size '%s'\n", optarg);
                    return 2;
                }
                o.volsize = v;
                break;
            }
            case 'n': {
                char *end = NULL;
                long v = strtol(optarg, &end, 10);
                if (!end || *end || v < 0 || v > 7) {
                    fprintf(stderr, "pea: invalid niter '%s' (0..7)\n", optarg);
                    return 2;
                }
                o.niter = (uint8_t)v;
                break;
            }
            default:
                usage(argv[0]);
                return 2;
            }
        }

        if (optind >= argc) {
            fprintf(stderr, "pea: missing archive name\n");
            usage(argv[0]);
            return 2;
        }
        archive = argv[optind++];

        if (optind >= argc) {
            fprintf(stderr, "pea: no input paths\n");
            return 2;
        }

        {
            const char **paths = (const char **)&argv[optind];
            int n = argc - optind;
            return pea_create(archive, paths, n, &o) ? 1 : 0;
        }
    }
}
