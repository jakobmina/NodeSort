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

/* ── Element types ─────────────────────────────────────────────────────── */
enum { KEY_U32, KEY_I32, KEY_F32, KEY_U64, KEY_F64 };

typedef struct {
    const char *name;
    int key;      /* KEY_* */
    size_t ksize; /* bytes per key */
    size_t vsize; /* bytes per carried value, 0 = plain sort */
} type_info;

static const type_info types[] = {
    {"u32", KEY_U32, 4, 0},    {"i32", KEY_I32, 4, 0},    {"f32", KEY_F32, 4, 0},
    {"f64", KEY_F64, 8, 0},    {"kv-u32", KEY_U32, 4, 4}, {"kv-f32", KEY_F32, 4, 4},
    {"kv-u64", KEY_U64, 8, 8},
};
enum { TYPE_COUNT = sizeof types / sizeof types[0], TYPE_I32 = 1 };

static uint64_t float_bits(float f) {
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}
static uint64_t double_bits(double d) {
    uint64_t b;
    memcpy(&b, &d, sizeof b);
    return b;
}

/* Key bits per element. Float data are real-valued (no NaNs), so the qsort reference stays exact.
 */
static void generate(uint64_t *a, size_t n, int dist, int key, uint64_t seed) {
    uint64_t s = seed;
    uint64_t range = n * 10u;
    for (size_t i = 0; i < n; i++) {
        uint64_t r64 = rng_next(&s);
        uint32_t r = (uint32_t)(r64 >> 32);
        double v;
        switch (dist) {
        case 0: v = (double)(int32_t)r / 2147483648.0; break;
        case 1: v = (double)(r64 % range); break;
        case 2: v = (double)(r % 16u) * 0.25; break;
        case 3: v = (double)i; break;
        case 4: v = (double)(n - i); break;
        default: v = (r % 10u) ? 0.5 : (double)(int32_t)r; break;
        }
        if (key == KEY_F32) {
            a[i] = float_bits((float)v);
            continue;
        }
        if (key == KEY_F64) {
            a[i] = double_bits(v);
            continue;
        }
        uint64_t x;
        switch (dist) {
        case 0: x = key == KEY_U64 ? r64 : r; break;
        case 1: x = r64 % range; break;
        case 2: x = r % 16u; break;
        case 3: x = i; break;
        case 4: x = n - i; break;
        default: x = (r % 10u) ? 42u : (key == KEY_U64 ? r64 : r); break;
        }
        a[i] = key == KEY_U64 ? x : (x & 0xFFFFFFFFu);
    }
}

static int g_descending, g_key;

/* qsort comparator over records whose key sits at offset 0. */
static int cmp_rec(const void *pa, const void *pb) {
    int c;
    switch (g_key) {
    case KEY_I32: {
        int32_t x, y;
        memcpy(&x, pa, 4);
        memcpy(&y, pb, 4);
        c = (x > y) - (x < y);
        break;
    }
    case KEY_F32: {
        float x, y;
        memcpy(&x, pa, 4);
        memcpy(&y, pb, 4);
        c = (x > y) - (x < y);
        break;
    }
    case KEY_F64: {
        double x, y;
        memcpy(&x, pa, 8);
        memcpy(&y, pb, 8);
        c = (x > y) - (x < y);
        break;
    }
    case KEY_U64: {
        uint64_t x, y;
        memcpy(&x, pa, 8);
        memcpy(&y, pb, 8);
        c = (x > y) - (x < y);
        break;
    }
    default: {
        uint32_t x, y;
        memcpy(&x, pa, 4);
        memcpy(&y, pb, 4);
        c = (x > y) - (x < y);
        break;
    }
    }
    return g_descending ? -c : c;
}

static void put(unsigned char *p, size_t size, uint64_t bits) {
    if (size == 8) {
        memcpy(p, &bits, 8);
    } else {
        uint32_t b = (uint32_t)bits;
        memcpy(p, &b, 4);
    }
}

static uint64_t get(const unsigned char *p, size_t size) {
    if (size == 8) {
        uint64_t b;
        memcpy(&b, p, 8);
        return b;
    }
    uint32_t b;
    memcpy(&b, p, 4);
    return b;
}

static nodesort_status run_nodesort(const type_info *t, unsigned char *keys, unsigned char *vals,
                                    size_t n, const nodesort_options *opt) {
    if (t->vsize) {
        if (t->key == KEY_F32)
            return nodesort_kv_f32((float *)(void *)keys, vals, t->vsize, n, opt);
        if (t->key == KEY_U64)
            return nodesort_kv_u64((uint64_t *)(void *)keys, vals, t->vsize, n, opt);
        return nodesort_kv_u32((uint32_t *)(void *)keys, vals, t->vsize, n, opt);
    }
    switch (t->key) {
    case KEY_I32: return nodesort_i32((int32_t *)(void *)keys, n, opt);
    case KEY_F32: return nodesort_f32((float *)(void *)keys, n, opt);
    case KEY_F64: return nodesort_f64((double *)(void *)keys, n, opt);
    default: return nodesort_u32((uint32_t *)(void *)keys, n, opt);
    }
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
                 "  --type T        u32, i32, f32, f64, kv-u32, kv-f32 or kv-u64 (default i32)\n"
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
            int found = -1;
            for (int t = 0; t < TYPE_COUNT; t++)
                if (strcmp(v, types[t].name) == 0) found = t;
            if (found < 0) goto bad;
            c->type = found;
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
        printf("%zu,%s,%s,%s,%.3f,%.2f,%d\n", n, dist, types[c->type].name, impl, ms, qsort_ms / ms,
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
               types[c.type].name, c.descending ? "desc" : "asc", c.reps,
               (unsigned long long)c.seed);
        printf("  %-11s %-12s %-14s %10s %9s  %s\n", "n", "dist", "impl", "best ms", "speedup",
               "check");
    }

    const type_info *t = &types[c.type];
    size_t rsize = t->ksize + t->vsize; /* qsort baseline sorts {key, value} records */
    g_descending = c.descending;
    g_key = t->key;
    int all_ok = 1;

    for (size_t si = 0; si < c.nsizes; si++) {
        size_t n = c.sizes[si];
        uint64_t *input = (uint64_t *)malloc(n * sizeof *input);
        unsigned char *recs = (unsigned char *)malloc(n * rsize);
        unsigned char *keys = (unsigned char *)malloc(n * t->ksize);
        unsigned char *vals = (unsigned char *)malloc(n * (t->vsize ? t->vsize : 1));
        if (!input || !recs || !keys || !vals) {
            fprintf(stderr, "nodesort_bench: out of memory for n=%zu\n", n);
            free(input);
            free(recs);
            free(keys);
            free(vals);
            return EXIT_FAILURE;
        }

        for (size_t di = 0; di < c.ndists; di++) {
            int dist = c.dists[di];
            generate(input, n, dist, t->key, c.seed);

            double qsort_ms = -1;
            for (unsigned r = 0; r < c.reps; r++) {
                for (size_t i = 0; i < n; i++) {
                    put(recs + i * rsize, t->ksize, input[i]);
                    if (t->vsize) put(recs + i * rsize + t->ksize, t->vsize, i);
                }
                double t0 = now_ms();
                qsort(recs, n, rsize, cmp_rec);
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
                for (unsigned r = 0; r < c.reps; r++) {
                    for (size_t i = 0; i < n; i++) {
                        put(keys + i * t->ksize, t->ksize, input[i]);
                        if (t->vsize) put(vals + i * t->vsize, t->vsize, i);
                    }
                    double t0 = now_ms();
                    nodesort_status st = run_nodesort(t, keys, vals, n, &opt);
                    double el = now_ms() - t0;
                    if (st != NODESORT_OK) {
                        fprintf(stderr, "nodesort_bench: %s\n", nodesort_strerror(st));
                        return EXIT_FAILURE;
                    }
                    if (best < 0 || el < best) best = el;
                }
                /* Keys must match qsort's; each value (an input index) must still own its key. */
                int ok = 1;
                for (size_t i = 0; i < n && ok; i++) {
                    uint64_t k = get(keys + i * t->ksize, t->ksize);
                    ok = k == get(recs + i * rsize, t->ksize);
                    if (ok && t->vsize) {
                        uint64_t v = get(vals + i * t->vsize, t->vsize);
                        ok = v < n && input[v] == k;
                    }
                }
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
        free(recs);
        free(keys);
        free(vals);
    }
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
