/* Minimal dependency-free test harness.
 * SPDX-License-Identifier: MIT */
#ifndef NODESORT_TEST_HARNESS_H
#define NODESORT_TEST_HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int th_failures = 0;
static const char *th_current = "";

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            th_failures++;                                                                         \
            fprintf(stderr, "%s:%d: [%s] check failed: %s\n", __FILE__, __LINE__, th_current,      \
                    #cond);                                                                        \
        }                                                                                          \
    } while (0)

#define CHECK_MSG(cond, ...)                                                                       \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            th_failures++;                                                                         \
            fprintf(stderr, "%s:%d: [%s] check failed: %s: ", __FILE__, __LINE__, th_current,      \
                    #cond);                                                                        \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

#define RUN(test)                                                                                  \
    do {                                                                                           \
        th_current = #test;                                                                        \
        int before_ = th_failures;                                                                 \
        test();                                                                                    \
        printf("%-48s %s\n", #test, th_failures == before_ ? "ok" : "FAILED");                     \
    } while (0)

#define TEST_EXIT() (th_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE)

/* ── Deterministic PRNG (splitmix64) ───────────────────────────────────── */
typedef struct {
    uint64_t s;
} th_rng;

static inline uint64_t th_next(th_rng *r) {
    uint64_t z = (r->s += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static inline uint32_t th_u32(th_rng *r) { return (uint32_t)(th_next(r) >> 32); }

static inline void *th_xmalloc(size_t bytes) {
    void *p = malloc(bytes ? bytes : 1);
    if (!p) {
        fprintf(stderr, "test harness: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

static inline int th_cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

static inline int th_cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

#endif /* NODESORT_TEST_HARNESS_H */
