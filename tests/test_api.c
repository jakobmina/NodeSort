/* Public-API tests: argument handling, edge cases and property tests against qsort.
 * SPDX-License-Identifier: MIT */
#include <limits.h>

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
    D_COUNT
};
static const char *const dist_name[D_COUNT] = {"uniform",  "few",      "equal",    "sorted",
                                               "reversed", "extremes", "low_byte", "high_byte",
                                               "skewed",   "sawtooth"};

static void fill(uint32_t *a, size_t n, enum dist d, th_rng *r) {
    static const uint32_t extremes[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFFu};
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

/* ── Property tests ────────────────────────────────────────────────────── */
static const size_t sizes[] = {
    2, 3, 63, 64, 65, 100, 1000, 65535, 65536, 65537, 131071, 200003, (1u << 19) + 7u};
static const unsigned thread_counts[] = {1, 2, 3, 4, 7, 8};

static void check_against_qsort(int is_signed) {
    th_rng rng;
    rng.s = is_signed ? 1234u : 5678u;
    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.seq_threshold = 1; /* exercise the parallel path whenever the size allows it */

    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        size_t n = sizes[s];
        uint32_t *input = (uint32_t *)th_xmalloc(n * sizeof *input);
        uint32_t *want = (uint32_t *)th_xmalloc(n * sizeof *want);
        uint32_t *got = (uint32_t *)th_xmalloc(n * sizeof *got);

        for (int d = 0; d < D_COUNT; d++) {
            fill(input, n, (enum dist)d, &rng);
            memcpy(want, input, n * sizeof *want);
            qsort(want, n, sizeof *want, is_signed ? th_cmp_i32 : th_cmp_u32);

            for (size_t t = 0; t < sizeof thread_counts / sizeof thread_counts[0]; t++) {
                opt.threads = thread_counts[t];
                memcpy(got, input, n * sizeof *got);
                nodesort_status st =
                    is_signed ? nodesort_i32((int32_t *)got, n, &opt) : nodesort_u32(got, n, &opt);
                CHECK_MSG(st == NODESORT_OK, "status %d", (int)st);
                CHECK_MSG(memcmp(got, want, n * sizeof *got) == 0, "n=%zu dist=%s threads=%u", n,
                          dist_name[d], opt.threads);
            }
        }
        free(input);
        free(want);
        free(got);
    }
}

static void test_property_i32(void) { check_against_qsort(1); }
static void test_property_u32(void) { check_against_qsort(0); }

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
    RUN(test_property_i32);
    RUN(test_property_u32);
    RUN(test_default_options_large);
    return TEST_EXIT();
}
