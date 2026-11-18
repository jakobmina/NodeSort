/* Internal sorting engine shared by the public entry points.
 * SPDX-License-Identifier: MIT */
#ifndef NODESORT_SORT_H
#define NODESORT_SORT_H

#include <stddef.h>
#include <stdint.h>

#include "nodesort/nodesort.h"

/* Maps an element to an unsigned radix key whose ascending order is the requested order.
 * 32-bit keys use the low halves of the masks. Rules per element type and direction:
 * architecture.md (id nodesort-architecture). */
typedef struct ns_keymap {
    uint64_t xor_mask;   /* applied to every key                                    */
    uint64_t float_mask; /* IEEE floats: all bits but the sign, flips negatives     */
    uint64_t nan_mask;   /* IEEE floats: all ones, NaN -> largest key               */
} ns_keymap;

static inline uint32_t ns_key32(uint32_t b, ns_keymap km) {
    uint32_t negative = (uint32_t)0 - (b >> 31);
    uint32_t is_nan = (uint32_t)0 - (uint32_t)((b & 0x7FFFFFFFu) > 0x7F800000u);
    return (b ^ (uint32_t)km.xor_mask ^ (negative & (uint32_t)km.float_mask)) |
           (is_nan & (uint32_t)km.nan_mask);
}

static inline uint64_t ns_key64(uint64_t b, ns_keymap km) {
    uint64_t negative = (uint64_t)0 - (b >> 63);
    uint64_t is_nan =
        (uint64_t)0 - (uint64_t)((b & UINT64_C(0x7FFFFFFFFFFFFFFF)) > UINT64_C(0x7FF0000000000000));
    return (b ^ km.xor_mask ^ (negative & km.float_mask)) | (is_nan & km.nan_mask);
}

static inline int ns_keymap_is_float(ns_keymap km) { return km.float_mask != 0; }

/* Sort `keys` and, when value_size is 4 or 8, the parallel array `values` with them.
 * value_size 0 means keys only. Combinations the public API never uses return EINVAL. */
nodesort_status ns_sort32(uint32_t *keys, void *values, size_t value_size, size_t n, ns_keymap km,
                          unsigned threads, size_t seq_threshold);
nodesort_status ns_sort64(uint64_t *keys, void *values, size_t value_size, size_t n, ns_keymap km,
                          unsigned threads, size_t seq_threshold);

#endif /* NODESORT_SORT_H */
