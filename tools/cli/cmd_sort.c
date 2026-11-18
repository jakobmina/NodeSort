/* `nodesort sort`: sort a 1-D array read from a file.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>

#include "cli.h"

static void usage(FILE *out) {
    fprintf(out,
            "usage: nodesort sort INPUT [-o OUTPUT] [--desc] [--type T] [--threads N]\n"
            "\n"
            "  INPUT          .npy (1-D float32/float64/int32/uint32) or raw .f32/.f64/.i32/.u32\n"
            "  -o OUTPUT      write the sorted array (same format; may be INPUT itself).\n"
            "                 Without -o only a summary is printed and no file is changed.\n"
            "  --desc         largest first (NaNs stay last)\n"
            "  --type T       element type of a raw file: f32, f64, i32, u32\n"
            "  --threads N    worker threads (default: all cores)\n");
}

int cmd_sort(int argc, char **argv) {
    const char *input = NULL, *output = NULL;
    dtype raw_type = DT_NONE;
    nodesort_options opt;
    nodesort_options_init(&opt);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v;
        uint64_t x;
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
            return EXIT_OK;
        } else if (strcmp(a, "--desc") == 0) {
            opt.descending = 1;
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) {
            if (!(output = option_value(argc, argv, &i))) return EXIT_USAGE;
        } else if (strcmp(a, "--type") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (!dtype_parse(v, &raw_type) || raw_type == DT_U64) {
                cli_error("--type must be f32, f64, i32 or u32 (got '%s')", v);
                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--threads") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (!parse_count(v, &x) || x > 4096) {
                cli_error("--threads must be a number between 0 and 4096 (got '%s')", v);
                return EXIT_USAGE;
            }
            opt.threads = (unsigned)x;
        } else if (a[0] == '-' && a[1] != '\0') {
            cli_error("sort: unknown option '%s'", a);
            usage(stderr);
            return EXIT_USAGE;
        } else if (!input) {
            input = a;
        } else {
            cli_error("sort: only one input file is accepted ('%s')", a);
            return EXIT_USAGE;
        }
    }
    if (!input) {
        usage(stderr);
        return EXIT_USAGE;
    }

    char err[ERR_LEN];
    array_t arr;
    if (array_read(input, raw_type, 0, &arr, err) != 0) {
        cli_error("%s", err);
        return EXIT_FILE;
    }
    if (arr.ndim != 1) {
        cli_error("%s: sort expects a 1-D array, got %zu x %zu (use 'nodesort top' for "
                  "embedding matrices)",
                  input, arr.rows, arr.cols);
        array_free(&arr);
        return EXIT_FILE;
    }
    if (arr.type == DT_U64) {
        cli_error("%s: uint64 arrays cannot be sorted on their own (float32, float64, int32, "
                  "uint32 are supported)",
                  input);
        array_free(&arr);
        return EXIT_FILE;
    }

    double t0 = now_ms();
    nodesort_status st = sort_array(&arr, &opt);
    double ms = now_ms() - t0;
    if (st != NODESORT_OK) {
        cli_error("%s: %s", input, nodesort_strerror(st));
        array_free(&arr);
        return EXIT_SORT;
    }

    char cnt[32];
    unsigned threads = opt.threads ? opt.threads : cpu_count();
    printf("sorted %s %s values in %.2f ms (%s, up to %u threads)\n",
           fmt_count(arr.count, cnt, sizeof cnt), dtype_name(arr.type), ms,
           opt.descending ? "descending" : "ascending", threads);

    if (output) {
        if (array_write(output, &arr, arr.from_npy, err) != 0) {
            cli_error("%s", err);
            array_free(&arr);
            return EXIT_FILE;
        }
        printf("wrote %s\n", output);
    } else {
        size_t show = arr.count < 5 ? arr.count : 5;
        printf("first:");
        for (size_t i = 0; i < show; i++) {
            putchar(' ');
            print_element(&arr, i);
        }
        printf("\nlast: ");
        for (size_t i = arr.count - show; i < arr.count; i++) {
            putchar(' ');
            print_element(&arr, i);
        }
        printf("\n(no file written; use -o to save the result)\n");
    }
    array_free(&arr);
    return EXIT_OK;
}
