/* `nodesort demo`: live race between nodesort and qsort on random data of growing size.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"

static void usage(FILE *out) {
    fprintf(out, "usage: nodesort demo [--max N] [--type T] [--threads N] [--seed S]\n"
                 "\n"
                 "  --max N      largest size to race (default 10M; sizes 1K, 10K, ... up to N)\n"
                 "  --type T     f32, f64, i32 or u32 (default i32)\n"
                 "  --threads N  worker threads for nodesort (default: all cores)\n"
                 "  --seed S     random seed (default 7)\n");
}

static uint64_t rng_next(uint64_t *s) {
    uint64_t z = (*s += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static void fill_random(array_t *a, uint64_t seed) {
    uint64_t s = seed;
    for (size_t i = 0; i < a->count; i++) {
        uint64_t r = rng_next(&s);
        switch (a->type) {
        case DT_F32:
            ((float *)a->data)[i] = (float)((double)(int64_t)r / 9.2233720368547758e18);
            break;
        case DT_F64: ((double *)a->data)[i] = (double)(int64_t)r / 9.2233720368547758e18; break;
        default: ((uint32_t *)a->data)[i] = (uint32_t)(r >> 32); break;
        }
    }
}

static int cmp_f32(const void *pa, const void *pb) {
    float x = *(const float *)pa, y = *(const float *)pb;
    return (x > y) - (x < y);
}
static int cmp_f64(const void *pa, const void *pb) {
    double x = *(const double *)pa, y = *(const double *)pb;
    return (x > y) - (x < y);
}
static int cmp_i32(const void *pa, const void *pb) {
    int32_t x = *(const int32_t *)pa, y = *(const int32_t *)pb;
    return (x > y) - (x < y);
}
static int cmp_u32(const void *pa, const void *pb) {
    uint32_t x = *(const uint32_t *)pa, y = *(const uint32_t *)pb;
    return (x > y) - (x < y);
}

int cmd_demo(int argc, char **argv) {
    uint64_t max = 10000000, seed = 7, x;
    dtype type = DT_I32;
    nodesort_options opt;
    nodesort_options_init(&opt);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v;
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
            return EXIT_OK;
        } else if (strcmp(a, "--max") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (!parse_count(v, &max) || max < 1000 || max > 1000000000) {
                cli_error("--max must be between 1K and 1G (got '%s')", v);
                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--type") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (!dtype_parse(v, &type) || type == DT_U64) {
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
        } else if (strcmp(a, "--seed") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (!parse_count(v, &seed)) {
                cli_error("--seed must be a non-negative number (got '%s')", v);
                return EXIT_USAGE;
            }
        } else {
            cli_error("demo: unknown option '%s'", a);
            usage(stderr);
            return EXIT_USAGE;
        }
    }

    int (*cmp)(const void *, const void *) = type == DT_F32   ? cmp_f32
                                             : type == DT_F64 ? cmp_f64
                                             : type == DT_I32 ? cmp_i32
                                                              : cmp_u32;
    size_t esize = dtype_size(type);
    int live = stdout_is_terminal();
    unsigned threads = opt.threads ? opt.threads : cpu_count();

    printf("nodesort %s demo: random %s data, nodesort with up to %u threads\n\n",
           nodesort_version(), dtype_name(type), threads);
    printf("  %13s  %12s  %12s  %8s  %s\n", "elements", "qsort", "nodesort", "speedup", "check");

    double best_speedup = 0;
    uint64_t best_n = 0;
    int all_ok = 1;
    for (uint64_t n = 1000; n <= max; n *= 10) {
        array_t ref = array_view(type, (size_t)n, 0, malloc((size_t)n * esize), 0);
        array_t work = array_view(type, (size_t)n, 0, malloc((size_t)n * esize), 0);
        if (!ref.data || !work.data) {
            free(ref.data);
            free(work.data);
            cli_error("out of memory at %llu elements", (unsigned long long)n);
            return EXIT_SORT;
        }
        fill_random(&ref, seed + n);
        memcpy(work.data, ref.data, (size_t)n * esize);

        char cnt[32];
        fmt_count(n, cnt, sizeof cnt);
        if (live) {
            printf("  %13s  %12s", cnt, "sorting...");
            fflush(stdout);
        }
        double t0 = now_ms();
        qsort(ref.data, (size_t)n, esize, cmp);
        double qsort_ms = now_ms() - t0;
        if (live) {
            printf("\r  %13s  %9.2f ms  %12s", cnt, qsort_ms, "sorting...");
            fflush(stdout);
        }

        t0 = now_ms();
        nodesort_status st = sort_array(&work, &opt);
        double ns_ms = now_ms() - t0;
        int ok = st == NODESORT_OK && memcmp(ref.data, work.data, (size_t)n * esize) == 0;
        all_ok &= ok;
        double speedup = ns_ms > 0 ? qsort_ms / ns_ms : 0;
        if (speedup > best_speedup) {
            best_speedup = speedup;
            best_n = n;
        }
        printf("%s  %13s  %9.2f ms  %9.2f ms  %7.1fx  %s\n", live ? "\r" : "", cnt, qsort_ms, ns_ms,
               speedup, ok ? "ok" : "MISMATCH");
        fflush(stdout);
        free(ref.data);
        free(work.data);
        if (st != NODESORT_OK) {
            cli_error("%s", nodesort_strerror(st));
            return EXIT_SORT;
        }
    }

    char cnt[32];
    printf("\nbest: %.1fx faster than qsort at %s elements%s\n", best_speedup,
           fmt_count(best_n, cnt, sizeof cnt), all_ok ? "" : "  (MISMATCH: please report a bug)");
    return all_ok ? EXIT_OK : EXIT_SORT;
}
