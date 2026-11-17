/* Public-API tests: argument handling, edge cases and property tests against a reference model.
 * SPDX-License-Identifier: MIT */
#include <limits.h>
#include <stddef.h>

#include "harness.h"
#include "nodesort/nodesort.h"

enum dist {
    D_UNIFORM,
    D_FEW,
    D_EQUAL,
    D_SORTED,
    D_REVERSED,
    D_EXTREMES,
    D_LOW_BYTE,
    D_HIGH_BYTE,
    D_SKEWED,
    D_SAWTOOTH,
    D_SCORES,
    D_FLOAT_SPECIAL,
    D_COUNT
};
static const char *const dist_name[D_COUNT] = {"uniform",  "few",      "equal",    "sorted",
                                               "reversed", "extremes", "low_byte", "high_byte",
                                               "skewed",   "sawtooth", "scores",   "float_special"};

/* Similarity-like scores in [-1, 1] with some NaNs (both signs, distinct payloads) and zeros. */
static uint32_t score_bits(th_rng *r) {
    uint32_t pick = th_u32(r) % 100u;
    if (pick < 3u) return 0x7F800001u | (th_u32(r) & 0x803FFFFFu); /* NaN, any sign/payload */
    if (pick < 5u) return (th_u32(r) & 1u) ? 0x80000000u : 0u;     /* -0.0 / +0.0 */
    float f = (float)(int32_t)th_u32(r) / 2147483648.0f;
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

static void fill(uint32_t *a, size_t n, enum dist d, th_rng *r) {
    static const uint32_t extremes[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFFu};
    static const uint32_t float_special[] = {0x00000000u, 0x80000000u, 0x7F800000u, 0xFF800000u,
                                             0x7FC00000u, 0xFFC00000u, 0x7F800001u, 0x00000001u,
                                             0x80000001u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x3F800000u};
    for (size_t i = 0; i < n; i++) {
        switch (d) {
        case D_UNIFORM: a[i] = th_u32(r); break;
        case D_FEW: a[i] = (th_u32(r) % 7u) * 0x01010101u; break;
        case D_EQUAL: a[i] = 0xDEADBEEFu; break;
        case D_SORTED: a[i] = (uint32_t)i * 3u; break;
        case D_REVERSED: a[i] = (uint32_t)(n - i) * 3u; break;
        case D_EXTREMES: a[i] = extremes[th_u32(r) % 6u]; break;
        case D_LOW_BYTE: a[i] = th_u32(r) & 0xFFu; break;
        case D_HIGH_BYTE: a[i] = th_u32(r) & 0xFF000000u; break;
        case D_SKEWED: a[i] = (th_u32(r) % 10u) ? 42u : th_u32(r); break;
        case D_SAWTOOTH: a[i] = (uint32_t)(i % 1000u); break;
        case D_SCORES: a[i] = score_bits(r); break;
        case D_FLOAT_SPECIAL: a[i] = float_special[th_u32(r) % 12u]; break;
        case D_COUNT: break;
        }
    }
}

/* ── Arguments and metadata ────────────────────────────────────────────── */
static void test_null_and_empty(void) {
    CHECK(nodesort_i32(NULL, 0, NULL) == NODESORT_OK);
    CHECK(nodesort_u32(NULL, 0, NULL) == NODESORT_OK);
    CHECK(nodesort_i32(NULL, 5, NULL) == NODESORT_EINVAL);
    CHECK(nodesort_u32(NULL, 5, NULL) == NODESORT_EINVAL);
    int32_t one = -7;
    CHECK(nodesort_i32(&one, 1, NULL) == NODESORT_OK);
    CHECK(one == -7);
}

static void test_options(void) {
    nodesort_options opt;
    memset(&opt, 0xAB, sizeof opt);
    nodesort_options_init(&opt);
    CHECK(opt.struct_size == sizeof opt);
    CHECK(opt.threads == 0);
    CHECK(opt.seq_threshold == 0);
    nodesort_options_init(NULL); /* must not crash */

    int32_t a[3] = {3, 1, 2};
    nodesort_options bad = opt;
    bad.struct_size = 0;
    CHECK(nodesort_i32(a, 3, &bad) == NODESORT_EINVAL);
    CHECK(a[0] == 3 && a[1] == 1 && a[2] == 2);

    nodesort_options huge = opt;
    huge.threads = 100000; /* clamped, not an error */
    CHECK(nodesort_i32(a, 3, &huge) == NODESORT_OK);
    CHECK(a[0] == 1 && a[1] == 2 && a[2] == 3);
}

static void test_strerror_and_version(void) {
    CHECK(strcmp(nodesort_strerror(NODESORT_OK), "success") == 0);
    CHECK(nodesort_strerror(NODESORT_EINVAL)[0] != '\0');
    CHECK(nodesort_strerror(NODESORT_ENOMEM)[0] != '\0');
    CHECK(nodesort_strerror(NODESORT_ETHREAD)[0] != '\0');
    CHECK(nodesort_strerror((nodesort_status)99) != NULL);
    CHECK(strcmp(nodesort_version(), NODESORT_VERSION_STRING) == 0);
}

static void test_signed_extremes(void) {
    int32_t a[] = {INT32_MAX, 0, INT32_MIN, -1, 1, INT32_MIN + 1, INT32_MAX - 1, -1, 0};
    const int32_t want[] = {INT32_MIN, INT32_MIN + 1, -1, -1, 0, 0, 1, INT32_MAX - 1, INT32_MAX};
    CHECK(nodesort_i32(a, sizeof a / sizeof a[0], NULL) == NODESORT_OK);
    CHECK(memcmp(a, want, sizeof a) == 0);
}

/* ── Reference model ───────────────────────────────────────────────────── */
enum elem_type { T_U32, T_I32, T_F32, T_COUNT };
static const char *const type_name[T_COUNT] = {"u32", "i32", "f32"};

static int is_nan_bits(uint32_t b) { return (b & 0x7FFFFFFFu) > 0x7F800000u; }

/* The documented order, written from the spec rather than from the bit trick under test. */
static int ref_cmp(uint32_t a, uint32_t b, enum elem_type type, int descending) {
    int c;
    if (type == T_F32) {
        int na = is_nan_bits(a), nb = is_nan_bits(b);
        if (na || nb) return na - nb; /* NaNs last in both directions */
        float fa, fb;
        memcpy(&fa, &a, sizeof fa);
        memcpy(&fb, &b, sizeof fb);
        if (fa < fb)
            c = -1;
        else if (fa > fb)
            c = 1;
        else
            c = (int)(b >> 31) - (int)(a >> 31); /* -0.0 before +0.0 */
    } else if (type == T_I32) {
        int32_t x = (int32_t)a, y = (int32_t)b;
        c = (x > y) - (x < y);
    } else {
        c = (a > b) - (a < b);
    }
    return descending ? -c : c;
}

/* Stable bottom-up merge sort: the reference for bit-exact comparison (NaN payload order). */
static void ref_sort(uint32_t *a, size_t n, enum elem_type type, int descending) {
    uint32_t *tmp = (uint32_t *)th_xmalloc(n * sizeof *tmp);
    uint32_t *src = a, *dst = tmp;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                dst[k++] = ref_cmp(src[j], src[i], type, descending) < 0 ? src[j++] : src[i++];
            while (i < mid) dst[k++] = src[i++];
            while (j < hi) dst[k++] = src[j++];
        }
        uint32_t *t = src;
        src = dst;
        dst = t;
    }
    if (src != a) memcpy(a, src, n * sizeof *a);
    free(tmp);
}

static nodesort_status run_sort(uint32_t *data, size_t n, enum elem_type type,
                                const nodesort_options *opt) {
    switch (type) {
    case T_I32: return nodesort_i32((int32_t *)data, n, opt);
    case T_F32: return nodesort_f32((float *)(void *)data, n, opt);
    default: return nodesort_u32(data, n, opt);
    }
}

/* ── Property tests ────────────────────────────────────────────────────── */
static const size_t sizes[] = {
    2, 3, 63, 64, 65, 100, 1000, 65535, 65536, 65537, 131071, 200003, (1u << 19) + 7u};
static const unsigned thread_counts[] = {1, 2, 3, 4, 7, 8};
static const unsigned thread_counts_desc[] = {1, 4};

static void check_against_reference(enum elem_type type, int descending) {
    th_rng rng;
    rng.s = 1234u + (uint64_t)type * 17u + (uint64_t)descending;
    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.seq_threshold = 1; /* exercise the parallel path whenever the size allows it */
    opt.descending = descending;
    const unsigned *threads = descending ? thread_counts_desc : thread_counts;
    size_t nthreads = descending ? sizeof thread_counts_desc / sizeof thread_counts_desc[0]
                                 : sizeof thread_counts / sizeof thread_counts[0];

    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        size_t n = sizes[s];
        uint32_t *input = (uint32_t *)th_xmalloc(n * sizeof *input);
        uint32_t *want = (uint32_t *)th_xmalloc(n * sizeof *want);
        uint32_t *got = (uint32_t *)th_xmalloc(n * sizeof *got);

        for (int d = 0; d < D_COUNT; d++) {
            fill(input, n, (enum dist)d, &rng);
            memcpy(want, input, n * sizeof *want);
            ref_sort(want, n, type, descending);

            for (size_t t = 0; t < nthreads; t++) {
                opt.threads = threads[t];
                memcpy(got, input, n * sizeof *got);
                nodesort_status st = run_sort(got, n, type, &opt);
                CHECK_MSG(st == NODESORT_OK, "status %d", (int)st);
                CHECK_MSG(memcmp(got, want, n * sizeof *got) == 0,
                          "type=%s desc=%d n=%zu dist=%s threads=%u", type_name[type], descending,
                          n, dist_name[d], opt.threads);
            }
        }
        free(input);
        free(want);
        free(got);
    }
}

static void test_property_u32(void) { check_against_reference(T_U32, 0); }
static void test_property_i32(void) { check_against_reference(T_I32, 0); }
static void test_property_f32(void) { check_against_reference(T_F32, 0); }
static void test_property_u32_desc(void) { check_against_reference(T_U32, 1); }
static void test_property_i32_desc(void) { check_against_reference(T_I32, 1); }
static void test_property_f32_desc(void) { check_against_reference(T_F32, 1); }

/* ── Float specifics ───────────────────────────────────────────────────── */
static uint32_t bits_of(float f) {
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

static void test_f32_special_values(void) {
    const uint32_t qnan = 0x7FC00000u, neg_nan = 0xFFC00001u, nan_payload = 0x7F800123u;
    uint32_t a[] = {qnan,          bits_of(1.5f),          0x80000000u /* -0 */,
                    neg_nan,       0xFF800000u /* -inf */, 0x00000000u /* +0 */,
                    nan_payload,   0x7F800000u /* +inf */, 0x00000001u /* min subnormal */,
                    bits_of(-2.0f)};
    const uint32_t asc[] = {0xFF800000u,   bits_of(-2.0f), 0x80000000u, 0x00000000u, 0x00000001u,
                            bits_of(1.5f), 0x7F800000u,    qnan,        neg_nan,     nan_payload};
    const uint32_t desc[] = {0x7F800000u,    bits_of(1.5f), 0x00000001u, 0x00000000u, 0x80000000u,
                             bits_of(-2.0f), 0xFF800000u,   qnan,        neg_nan,     nan_payload};
    size_t n = sizeof a / sizeof a[0];
    uint32_t b[sizeof a / sizeof a[0]];

    memcpy(b, a, sizeof a);
    CHECK(nodesort_f32((float *)(void *)b, n, NULL) == NODESORT_OK);
    CHECK(memcmp(b, asc, sizeof b) == 0);

    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.descending = 1;
    memcpy(b, a, sizeof a);
    CHECK(nodesort_f32((float *)(void *)b, n, &opt) == NODESORT_OK);
    CHECK(memcmp(b, desc, sizeof b) == 0);

    CHECK(nodesort_f32(NULL, 0, NULL) == NODESORT_OK);
    CHECK(nodesort_f32(NULL, 3, NULL) == NODESORT_EINVAL);
}

/* Equal keys must never be reversed: the "already in reverse order" fast path must not fire on a
 * run that is only non-strictly reversed (here two NaNs, which share a key). */
static void test_equal_keys_keep_order_in_reverse_run(void) {
    enum { N = 100 };
    uint32_t a[N], want[N];
    a[0] = 0x7FC0000Au; /* NaN, payload A */
    a[1] = 0x7FC0000Bu; /* NaN, payload B */
    for (unsigned i = 2; i < N; i++) a[i] = bits_of((float)(i - 1));
    for (unsigned i = 0; i < N - 2; i++) want[i] = bits_of((float)(N - 2 - i));
    want[N - 2] = 0x7FC0000Au;
    want[N - 1] = 0x7FC0000Bu;

    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.descending = 1;
    CHECK(nodesort_f32((float *)(void *)a, N, &opt) == NODESORT_OK);
    CHECK(memcmp(a, want, sizeof a) == 0);
}

/* A caller built against the 0.1 header passes a struct without `descending`. */
static void test_options_from_older_header(void) {
    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.struct_size = offsetof(nodesort_options, descending);
    opt.descending = 1; /* stands for whatever bytes follow an old struct: must be ignored */
    int32_t a[] = {3, -1, 2};
    CHECK(nodesort_i32(a, 3, &opt) == NODESORT_OK);
    CHECK(a[0] == -1 && a[1] == 2 && a[2] == 3);
}

static void test_default_options_large(void) {
    size_t n = 1u << 20;
    th_rng rng = {99};
    int32_t *a = (int32_t *)th_xmalloc(n * sizeof *a);
    int32_t *want = (int32_t *)th_xmalloc(n * sizeof *want);
    for (size_t i = 0; i < n; i++) a[i] = (int32_t)th_u32(&rng);
    memcpy(want, a, n * sizeof *a);
    qsort(want, n, sizeof *want, th_cmp_i32);
    CHECK(nodesort_i32(a, n, NULL) == NODESORT_OK);
    CHECK(memcmp(a, want, n * sizeof *a) == 0);
    free(a);
    free(want);
}

int main(void) {
    RUN(test_null_and_empty);
    RUN(test_options);
    RUN(test_strerror_and_version);
    RUN(test_signed_extremes);
    RUN(test_f32_special_values);
    RUN(test_equal_keys_keep_order_in_reverse_run);
    RUN(test_options_from_older_header);
    RUN(test_property_u32);
    RUN(test_property_i32);
    RUN(test_property_f32);
    RUN(test_property_u32_desc);
    RUN(test_property_i32_desc);
    RUN(test_property_f32_desc);
    RUN(test_default_options_large);
    return TEST_EXIT();
}
