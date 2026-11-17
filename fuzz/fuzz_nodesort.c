/* libFuzzer harness: the first input byte picks type and thread count, the rest is the array.
 * Build with -DNODESORT_BUILD_FUZZ=ON using Clang, then run ./fuzz_nodesort.
 * SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nodesort/nodesort.h"

int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size);

static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}
static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size) {
    if (size < 1) return 0;
    uint8_t ctl = bytes[0];
    size_t n = (size - 1) / sizeof(uint32_t);
    uint32_t *got = (uint32_t *)malloc(n * sizeof *got + 1);
    uint32_t *want = (uint32_t *)malloc(n * sizeof *want + 1);
    if (!got || !want) abort();
    memcpy(got, bytes + 1, n * sizeof *got);
    memcpy(want, got, n * sizeof *want);

    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.threads = (unsigned)(ctl & 7u) + 1u;
    opt.seq_threshold = 1;

    int is_signed = ctl & 0x80u;
    nodesort_status st =
        is_signed ? nodesort_i32((int32_t *)got, n, &opt) : nodesort_u32(got, n, &opt);
    qsort(want, n, sizeof *want, is_signed ? cmp_i32 : cmp_u32);
    if (st != NODESORT_OK || memcmp(got, want, n * sizeof *got) != 0) abort();

    free(got);
    free(want);
    return 0;
}
