/* Key-value sorts and nodesort_f64 against a stable reference model.
 * SPDX-License-Identifier: MIT */
#include "harness.h"
#include "nodesort/nodesort.h"

enum key_type { K_U32, K_F32, K_U64, K_F64, K_COUNT };
static const char *const key_name[K_COUNT] = {"u32", "f32", "u64", "f64"};

typedef struct {
    uint64_t key; /* raw bits; 32-bit keys use the low half */
    uint64_t val;
} rec;

static int is_nan32(uint64_t b) { return (b & 0x7FFFFFFFu) > 0x7F800000u; }
static int is_nan64(uint64_t b) {
    return (b & UINT64_C(0x7FFFFFFFFFFFFFFF)) > UINT64_C(0x7FF0000000000000);
}

/* The documented order, written from the spec rather than from the engine's bit tricks. */
static int ref_cmp(uint64_t a, uint64_t b, enum key_type t, int descending) {
    int c;
    if (t == K_F32 || t == K_F64) {
        int na = t == K_F32 ? is_nan32(a) : is_nan64(a);
        int nb = t == K_F32 ? is_nan32(b) : is_nan64(b);
        if (na || nb) return na - nb; /* NaNs last in both directions */
        double fa, fb;
        int sa, sb;
        if (t == K_F32) {
            uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
            float x, y;
            memcpy(&x, &ua, sizeof x);
            memcpy(&y, &ub, sizeof y);
            fa = x;
            fb = y;
            sa = (int)(ua >> 31);
            sb = (int)(ub >> 31);
        } else {
            memcpy(&fa, &a, sizeof fa);
            memcpy(&fb, &b, sizeof fb);
            sa = (int)(a >> 63);
            sb = (int)(b >> 63);
        }
        if (fa < fb)
            c = -1;
        else if (fa > fb)
            c = 1;
        else
            c = sb - sa; /* -0.0 before +0.0 */
    } else {
        c = (a > b) - (a < b);
    }
    return descending ? -c : c;
}

static void ref_sort(rec *r, size_t n, enum key_type t, int descending) {
    rec *tmp = (rec *)th_xmalloc(n * sizeof *tmp);
    rec *src = r, *dst = tmp;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                dst[k++] = ref_cmp(src[j].key, src[i].key, t, descending) < 0 ? src[j++] : src[i++];
            while (i < mid) dst[k++] = src[i++];
            while (j < hi) dst[k++] = src[j++];
        }
        rec *x = src;
        src = dst;
        dst = x;
    }
    if (src != r) memcpy(r, src, n * sizeof *r);
    free(tmp);
}

/* ── Key generation (many duplicates so stability is observable) ───────── */
enum dist { D_UNIFORM, D_FEW, D_SPECIAL, D_SORTED, D_REVERSED, D_REVERSED_DUPS, D_SMALL, D_COUNT };
static const char *const dist_name[D_COUNT] = {"uniform",  "few",           "special", "sorted",
                                               "reversed", "reversed_dups", "small"};

static uint64_t gen_key(enum key_type t, enum dist d, size_t i, size_t n, th_rng *r) {
    static const uint64_t special32[] = {0x00000000u, 0x80000000u, 0x7F800000u, 0xFF800000u,
                                         0x7FC00000u, 0xFFC00001u, 0x7F800123u, 0x00000001u,
                                         0x3F800000u, 0xBF800000u};
    static const uint64_t special64[] = {0,
                                         UINT64_C(0x8000000000000000),
                                         UINT64_C(0x7FF0000000000000),
                                         UINT64_C(0xFFF0000000000000),
                                         UINT64_C(0x7FF8000000000000),
                                         UINT64_C(0xFFF8000000000001),
                                         UINT64_C(0x7FF0000000000123),
                                         1,
                                         UINT64_C(0x3FF0000000000000),
                                         UINT64_C(0xBFF0000000000000)};
    int wide = t == K_U64 || t == K_F64;
    int flt = t == K_F32 || t == K_F64;
    uint64_t x = th_next(r);
    switch (d) {
    case D_UNIFORM: break;
    case D_FEW: x = flt ? special32[8 + x % 2] : x % 8u; break; /* +-1.0 or 0..7 */
    case D_SPECIAL: x = wide ? special64[x % 10u] : special32[x % 10u]; break;
    case D_SORTED: x = flt ? 0 : i; break;
    case D_REVERSED: x = flt ? 0 : (uint64_t)(n - i); break;
    case D_REVERSED_DUPS: x = flt ? 0 : (uint64_t)((n - i) / 3u); break;
    case D_SMALL: x = x % 100000u; break;
    case D_COUNT: break;
    }
    if (flt && (d == D_SORTED || d == D_REVERSED || d == D_REVERSED_DUPS)) {
        double v = d == D_SORTED     ? (double)i
                   : d == D_REVERSED ? (double)(n - i)
                                     : (double)((n - i) / 3u);
        if (wide) {
            memcpy(&x, &v, sizeof x);
        } else {
            float f = (float)v;
            uint32_t b;
            memcpy(&b, &f, sizeof b);
            x = b;
        }
    }
    if (flt && d == D_FEW && wide) x = special64[8 + x % 2];
    return wide ? x : (x & 0xFFFFFFFFu);
}

/* ── Runner ────────────────────────────────────────────────────────────── */
static nodesort_status run_kv(enum key_type t, void *keys, void *vals, size_t vsize, size_t n,
                              const nodesort_options *opt) {
    switch (t) {
    case K_U32: return nodesort_kv_u32((uint32_t *)keys, vals, vsize, n, opt);
    case K_F32: return nodesort_kv_f32((float *)keys, vals, vsize, n, opt);
    case K_U64: return nodesort_kv_u64((uint64_t *)keys, vals, vsize, n, opt);
    default: return nodesort_f64((double *)keys, n, opt); /* keys only */
    }
}

static const size_t sizes[] = {2, 63, 65, 1000, 65537, 200003};
static const unsigned thread_counts[] = {1, 3, 4};

/* vsize 0 = keys only (f64). Values are the input position, so any instability shows up. */
static void check(enum key_type t, size_t vsize, int descending) {
    th_rng rng;
    rng.s = 99u + (uint64_t)t * 31u + vsize + (uint64_t)descending;
    size_t ksize = (t == K_U64 || t == K_F64) ? 8 : 4;
    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.seq_threshold = 1;
    opt.descending = descending;

    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        size_t n = sizes[s];
        rec *want = (rec *)th_xmalloc(n * sizeof *want);
        rec *input = (rec *)th_xmalloc(n * sizeof *input);
        unsigned char *keys = (unsigned char *)th_xmalloc(n * 8);
        unsigned char *vals = (unsigned char *)th_xmalloc(n * 8);

        for (int d = 0; d < D_COUNT; d++) {
            for (size_t i = 0; i < n; i++) {
                input[i].key = gen_key(t, (enum dist)d, i, n, &rng);
                input[i].val = vsize == 8 ? ((uint64_t)i << 32) | (i ^ 0xABCDu) : i;
            }
            memcpy(want, input, n * sizeof *want);
            ref_sort(want, n, t, descending);

            for (size_t ti = 0; ti < sizeof thread_counts / sizeof thread_counts[0]; ti++) {
                opt.threads = thread_counts[ti];
                for (size_t i = 0; i < n; i++) {
                    if (ksize == 8)
                        memcpy(keys + i * 8, &input[i].key, 8);
                    else {
                        uint32_t k = (uint32_t)input[i].key;
                        memcpy(keys + i * 4, &k, 4);
                    }
                    if (vsize == 8)
                        memcpy(vals + i * 8, &input[i].val, 8);
                    else if (vsize == 4) {
                        uint32_t v = (uint32_t)input[i].val;
                        memcpy(vals + i * 4, &v, 4);
                    }
                }
                nodesort_status st = run_kv(t, keys, vals, vsize, n, &opt);
                CHECK_MSG(st == NODESORT_OK, "status %d", (int)st);

                size_t bad = n;
                for (size_t i = 0; i < n && bad == n; i++) {
                    uint64_t k = 0, v = 0;
                    memcpy(&k, keys + i * ksize, ksize);
                    if (vsize) memcpy(&v, vals + i * vsize, vsize);
                    if (k != want[i].key || (vsize && v != want[i].val)) bad = i;
                }
                CHECK_MSG(bad == n, "key=%s vsize=%zu desc=%d n=%zu dist=%s threads=%u at %zu",
                          key_name[t], vsize, descending, n, dist_name[d], opt.threads, bad);
            }
        }
        free(want);
        free(input);
        free(keys);
        free(vals);
    }
}

static void test_kv_u32(void) {
    for (int desc = 0; desc <= 1; desc++) {
        check(K_U32, 4, desc);
        check(K_U32, 8, desc);
    }
}
static void test_kv_f32(void) {
    for (int desc = 0; desc <= 1; desc++) {
        check(K_F32, 4, desc);
        check(K_F32, 8, desc);
    }
}
static void test_kv_u64(void) {
    for (int desc = 0; desc <= 1; desc++) {
        check(K_U64, 4, desc);
        check(K_U64, 8, desc);
    }
}
static void test_f64(void) {
    check(K_F64, 0, 0);
    check(K_F64, 0, 1);
}

static void test_invalid_arguments(void) {
    uint32_t k[4] = {4, 3, 2, 1};
    uint64_t v8[4] = {1, 2, 3, 4};
    CHECK(nodesort_kv_u32(k, v8, 2, 4, NULL) == NODESORT_EINVAL);
    CHECK(nodesort_kv_u32(k, v8, 16, 4, NULL) == NODESORT_EINVAL);
    CHECK(nodesort_kv_u32(k, NULL, 4, 4, NULL) == NODESORT_EINVAL);
    CHECK(nodesort_kv_u32(NULL, v8, 4, 4, NULL) == NODESORT_EINVAL);
    CHECK(nodesort_kv_u64(NULL, v8, 8, 4, NULL) == NODESORT_EINVAL);
    CHECK(nodesort_kv_u32(k, (unsigned char *)v8 + 1, 4, 3, NULL) == NODESORT_EINVAL);
    uint64_t k64[2] = {2, 1};
    CHECK(nodesort_kv_u64(k64, (unsigned char *)v8 + 4, 8, 1, NULL) == NODESORT_EINVAL);
    CHECK(k[0] == 4 && k[3] == 1 && v8[0] == 1); /* untouched */
    CHECK(nodesort_kv_u32(NULL, NULL, 4, 0, NULL) == NODESORT_OK);
    CHECK(nodesort_kv_f32(NULL, NULL, 8, 0, NULL) == NODESORT_OK);
    CHECK(nodesort_f64(NULL, 0, NULL) == NODESORT_OK);
    CHECK(nodesort_f64(NULL, 2, NULL) == NODESORT_EINVAL);
}

int main(void) {
    RUN(test_invalid_arguments);
    RUN(test_kv_u32);
    RUN(test_kv_f32);
    RUN(test_kv_u64);
    RUN(test_f64);
    return TEST_EXIT();
}
