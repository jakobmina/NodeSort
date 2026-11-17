/* libFuzzer harness. First byte: bits 0-2 thread count - 1, bits 3-4 type (u32/i32/f32), bit 7
 * descending. The rest is the array. Checks order (per the documented rules) and that the output
 * is a permutation of the input.
 * Build with -DNODESORT_BUILD_FUZZ=ON using Clang, then run ./fuzz_nodesort.
 * SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nodesort/nodesort.h"

int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size);

static int cmp_bits(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int is_nan_bits(uint32_t b) { return (b & 0x7FFFFFFFu) > 0x7F800000u; }

/* -1 / 0 / 1 like a comparator, following the documented order. */
static int order(uint32_t a, uint32_t b, unsigned type, int descending) {
    int c;
    if (type == 2) {
        int na = is_nan_bits(a), nb = is_nan_bits(b);
        if (na || nb) return na - nb;
        float fa, fb;
        memcpy(&fa, &a, sizeof fa);
        memcpy(&fb, &b, sizeof fb);
        c = fa < fb ? -1 : fa > fb ? 1 : (int)(b >> 31) - (int)(a >> 31);
    } else if (type == 1) {
        int32_t x = (int32_t)a, y = (int32_t)b;
        c = (x > y) - (x < y);
    } else {
        c = (a > b) - (a < b);
    }
    return descending ? -c : c;
}

int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size) {
    if (size < 1) return 0;
    uint8_t ctl = bytes[0];
    unsigned type = ((unsigned)ctl >> 3) % 3u;
    int descending = (ctl & 0x80u) != 0;
    size_t n = (size - 1) / sizeof(uint32_t);
    uint32_t *got = (uint32_t *)malloc(n * sizeof *got + 1);
    uint32_t *in = (uint32_t *)malloc(n * sizeof *in + 1);
    if (!got || !in) abort();
    memcpy(got, bytes + 1, n * sizeof *got);
    memcpy(in, got, n * sizeof *in);

    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.threads = (unsigned)(ctl & 7u) + 1u;
    opt.seq_threshold = 1;
    opt.descending = descending;

    nodesort_status st;
    if (type == 2)
        st = nodesort_f32((float *)(void *)got, n, &opt);
    else if (type == 1)
        st = nodesort_i32((int32_t *)got, n, &opt);
    else
        st = nodesort_u32(got, n, &opt);
    if (st != NODESORT_OK) abort();

    for (size_t i = 1; i < n; i++)
        if (order(got[i - 1], got[i], type, descending) > 0) abort();

    qsort(in, n, sizeof *in, cmp_bits);
    qsort(got, n, sizeof *got, cmp_bits);
    if (memcmp(in, got, n * sizeof *got) != 0) abort();

    free(got);
    free(in);
    return 0;
}
