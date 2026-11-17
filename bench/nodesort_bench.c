/* nodesort_bench - reproducible benchmark of nodesort against qsort.
 * SPDX-License-Identifier: MIT */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nodesort/nodesort.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <time.h>
#endif

#define MAX_LIST 32

/* ── Timing ────────────────────────────────────────────────────────────── */
static double now_ms(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

/* ── Data generation (splitmix64: same output on every platform) ───────── */
static uint64_t rng_next(uint64_t *s) {
    uint64_t z = (*s += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static const char *const dist_names[] = {"uniform", "small-range", "few",
                                         "sorted",  "reversed",    "skewed"};
enum { DIST_COUNT = sizeof dist_names / sizeof dist_names[0] };

enum { TYPE_U32, TYPE_I32, TYPE_F32 };
static const char *const type_names[] = {"u32", "i32", "f32"};

static uint32_t float_bits(float f) {
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

/* f32 data are real-valued (no NaNs), so the qsort reference stays exact. */
static void generate(uint32_t *a, size_t n, int dist, int type, uint64_t seed) {
    uint64_t s = seed;
    uint32_t range = (uint32_t)(n * 10u > UINT32_MAX ? UINT32_MAX : n * 10u);
    for (size_t i = 0; i < n; i++) {
        uint32_t r = (uint32_t)(rng_next(&s) >> 32);
        if (type == TYPE_F32) {
            switch (dist) {
            case 0: a[i] = float_bits((float)(int32_t)r / 2147483648.0f); break;
            case 1: a[i] = float_bits((float)(r % range)); break;
            case 2: a[i] = float_bits((float)(r % 16u) * 0.25f); break;
            case 3: a[i] = float_bits((float)i); break;
            case 4: a[i] = float_bits((float)(n - i)); break;
            default: a[i] = float_bits((r % 10u) ? 0.5f : (float)(int32_t)r); break;
            }
            continue;
        }
        switch (dist) {
        case 0: a[i] = r; break;
        case 1: a[i] = r % range; break;
        case 2: a[i] = r % 16u; break;
        case 3: a[i] = (uint32_t)i; break;
        case 4: a[i] = (uint32_t)(n - i); break;
        default: a[i] = (r % 10u) ? 42u : r; break;
        }
    }
}

static int g_descending;

static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    int c = (x > y) - (x < y);
    return g_descending ? -c : c;
}
static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    int c = (x > y) - (x < y);
    return g_descending ? -c : c;
}
static int cmp_f32(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    int c = (x > y) - (x < y);
    return g_descending ? -c : c;
}

/* ── CLI ───────────────────────────────────────────────────────────────── */
typedef struct {
    size_t sizes[MAX_LIST];
    size_t nsizes;
    unsigned threads[MAX_LIST];
    size_t nthreads;
    int dists[DIST_COUNT];
    size_t ndists;
    unsigned reps;
    uint64_t seed;
    int type;
    int descending;
    int csv;
} config;

static void usage(FILE *out) {
    fprintf(out, "usage: nodesort_bench [options]\n"
                 "  --n LIST        element counts, comma-separated (default "
                 "10000,100000,1000000,10000000)\n"
                 "  --dist LIST     uniform,small-range,few,sorted,reversed,skewed or 'all' "
                 "(default uniform)\n"
                 "  --threads LIST  thread counts, 0 = auto (default 1,2,4,0)\n"
                 "  --reps N        repetitions per measurement, best is reported (default 5)\n"
                 "  --seed N        RNG seed (default 7)\n"
                 "  --type T        i32, u32 or f32 (default i32)\n"
                 "  --desc          sort in descending order\n"
                 "  --csv           machine-readable output\n"
                 "  --version       print the library version\n");
}

static int parse_u64(const char *s, uint64_t *out) {
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || end == s || *end != '\0' || s[0] == '-') return 0;
    *out = (uint64_t)v;
    return 1;
}

/* Splits `list` on commas into a writable copy and calls `item` on each token. */
static int parse_list(const char *list, config *c, int (*item)(config *, const char *)) {
    char buf[512];
    size_t len = strlen(list);
    if (len >= sizeof buf) return 0;
    memcpy(buf, list, len + 1);
    char *tok = buf;
    for (;;) {
        char *comma = strchr(tok, ',');
        if (comma) *comma = '\0';
        if (!item(c, tok)) return 0;
        if (!comma) return 1;
        tok = comma + 1;
    }
}

static int add_size(config *c, const char *s) {
    uint64_t v;
    if (c->nsizes == MAX_LIST || !parse_u64(s, &v) || v == 0 || v > SIZE_MAX / 4) return 0;
    c->sizes[c->nsizes++] = (size_t)v;
    return 1;
}

static int add_threads(config *c, const char *s) {
    uint64_t v;
    if (c->nthreads == MAX_LIST || !parse_u64(s, &v) || v > 4096) return 0;
    c->threads[c->nthreads++] = (unsigned)v;
    return 1;
}

static int add_dist(config *c, const char *s) {
    if (strcmp(s, "all") == 0) {
        for (int d = 0; d < DIST_COUNT; d++) c->dists[d] = d;
        c->ndists = DIST_COUNT;
        return 1;
    }
    for (int d = 0; d < DIST_COUNT; d++)
        if (strcmp(s, dist_names[d]) == 0 && c->ndists < DIST_COUNT) {
            c->dists[c->ndists++] = d;
            return 1;
        }
    return 0;
}

static int parse_args(int argc, char **argv, config *c) {
    memset(c, 0, sizeof *c);
    c->reps = 5;
    c->seed = 7;
    c->type = TYPE_I32;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        uint64_t x;
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
            exit(EXIT_SUCCESS);
        } else if (strcmp(a, "--version") == 0) {
            printf("nodesort %s\n", nodesort_version());
            exit(EXIT_SUCCESS);
        } else if (strcmp(a, "--csv") == 0) {
            c->csv = 1;
        } else if (strcmp(a, "--desc") == 0) {
            c->descending = 1;
        } else if (!v) {
            fprintf(stderr, "nodesort_bench: missing value for %s\n", a);
            return 0;
        } else if (strcmp(a, "--n") == 0) {
            if (!parse_list(v, c, add_size)) goto bad;
            i++;
        } else if (strcmp(a, "--threads") == 0) {
            if (!parse_list(v, c, add_threads)) goto bad;
            i++;
        } else if (strcmp(a, "--dist") == 0) {
            if (!parse_list(v, c, add_dist)) goto bad;
            i++;
        } else if (strcmp(a, "--reps") == 0) {
            if (!parse_u64(v, &x) || x == 0 || x > 1000) goto bad;
            c->reps = (unsigned)x;
            i++;
        } else if (strcmp(a, "--seed") == 0) {
            if (!parse_u64(v, &c->seed)) goto bad;
            i++;
        } else if (strcmp(a, "--type") == 0) {
            if (strcmp(v, "i32") == 0)
                c->type = TYPE_I32;
            else if (strcmp(v, "u32") == 0)
                c->type = TYPE_U32;
            else if (strcmp(v, "f32") == 0)
                c->type = TYPE_F32;
            else
                goto bad;
            i++;
        } else {
            fprintf(stderr, "nodesort_bench: unknown option %s\n", a);
            usage(stderr);
            return 0;
        }
        continue;
    bad:
        fprintf(stderr, "nodesort_bench: invalid value '%s' for %s\n", v, a);
        return 0;
    }
    if (c->nsizes == 0) {
        const size_t def[] = {10000, 100000, 1000000, 10000000};
        memcpy(c->sizes, def, sizeof def);
        c->nsizes = 4;
    }
    if (c->nthreads == 0) {
        const unsigned def[] = {1, 2, 4, 0};
        memcpy(c->threads, def, sizeof def);
        c->nthreads = 4;
    }
    if (c->ndists == 0) c->ndists = 1; /* dists[0] == 0 == uniform */
    return 1;
}

/* ── Measurement ───────────────────────────────────────────────────────── */
static void report(const config *c, size_t n, const char *dist, const char *impl, double ms,
                   double qsort_ms, int ok) {
    if (c->csv) {
        printf("%zu,%s,%s,%s,%.3f,%.2f,%d\n", n, dist, type_names[c->type], impl, ms, qsort_ms / ms,
               ok);
    } else {
        printf("  %-11zu %-12s %-14s %10.2f %8.2fx  %s\n", n, dist, impl, ms, qsort_ms / ms,
               ok ? "ok" : "MISMATCH");
    }
}

int main(int argc, char **argv) {
    config c;
    if (!parse_args(argc, argv, &c)) return EXIT_FAILURE;

    if (c.csv) {
        printf("n,dist,type,impl,best_ms,speedup_vs_qsort,correct\n");
    } else {
        printf("nodesort %s  type=%s  order=%s  reps=%u  seed=%llu\n", nodesort_version(),
               type_names[c.type], c.descending ? "desc" : "asc", c.reps,
               (unsigned long long)c.seed);
        printf("  %-11s %-12s %-14s %10s %9s  %s\n", "n", "dist", "impl", "best ms", "speedup",
               "check");
    }

    g_descending = c.descending;
    int (*cmp)(const void *, const void *) = c.type == TYPE_F32   ? cmp_f32
                                             : c.type == TYPE_I32 ? cmp_i32
                                                                  : cmp_u32;
    int all_ok = 1;

    for (size_t si = 0; si < c.nsizes; si++) {
        size_t n = c.sizes[si];
        uint32_t *input = (uint32_t *)malloc(n * sizeof *input);
        uint32_t *ref = (uint32_t *)malloc(n * sizeof *ref);
        uint32_t *work = (uint32_t *)malloc(n * sizeof *work);
        if (!input || !ref || !work) {
            fprintf(stderr, "nodesort_bench: out of memory for n=%zu\n", n);
            free(input);
            free(ref);
            free(work);
            return EXIT_FAILURE;
        }

        for (size_t di = 0; di < c.ndists; di++) {
            int dist = c.dists[di];
            generate(input, n, dist, c.type, c.seed);

            double qsort_ms = -1;
            for (unsigned r = 0; r < c.reps; r++) {
                memcpy(ref, input, n * sizeof *ref);
                double t0 = now_ms();
                qsort(ref, n, sizeof *ref, cmp);
                double el = now_ms() - t0;
                if (qsort_ms < 0 || el < qsort_ms) qsort_ms = el;
            }
            report(&c, n, dist_names[dist], "qsort", qsort_ms, qsort_ms, 1);

            for (size_t ti = 0; ti < c.nthreads; ti++) {
                nodesort_options opt;
                nodesort_options_init(&opt);
                opt.threads = c.threads[ti];
                opt.descending = c.descending;
                double best = -1;
                int ok = 1;
                for (unsigned r = 0; r < c.reps; r++) {
                    memcpy(work, input, n * sizeof *work);
                    double t0 = now_ms();
                    nodesort_status st;
                    if (c.type == TYPE_F32)
                        st = nodesort_f32((float *)(void *)work, n, &opt);
                    else if (c.type == TYPE_I32)
                        st = nodesort_i32((int32_t *)work, n, &opt);
                    else
                        st = nodesort_u32(work, n, &opt);
                    double el = now_ms() - t0;
                    if (st != NODESORT_OK) {
                        fprintf(stderr, "nodesort_bench: %s\n", nodesort_strerror(st));
                        return EXIT_FAILURE;
                    }
                    if (best < 0 || el < best) best = el;
                }
                ok = memcmp(work, ref, n * sizeof *work) == 0;
                all_ok &= ok;
                char label[32];
                if (opt.threads == 0)
                    snprintf(label, sizeof label, "nodesort auto");
                else
                    snprintf(label, sizeof label, "nodesort %ut", opt.threads);
                report(&c, n, dist_names[dist], label, best, qsort_ms, ok);
            }
        }
        free(input);
        free(ref);
        free(work);
    }
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
