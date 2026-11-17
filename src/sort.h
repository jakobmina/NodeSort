/* Internal sorting engine shared by the public entry points.
 * SPDX-License-Identifier: MIT */
#ifndef NODESORT_SORT_H
#define NODESORT_SORT_H

#include <stddef.h>
#include <stdint.h>

#include "nodesort/nodesort.h"

/* Maps a 32-bit element to an unsigned radix key whose ascending order is the requested order.
 * Rules per element type and direction: architecture.md (id nodesort-architecture). */
typedef struct ns_keymap {
    uint32_t xor_mask;   /* applied to every key                                    */
    uint32_t float_mask; /* 0x7FFFFFFF for IEEE floats: flips magnitude of negatives */
    uint32_t nan_mask;   /* 0xFFFFFFFF for IEEE floats: NaN -> largest key           */
} ns_keymap;

static inline uint32_t ns_key(uint32_t b, ns_keymap km) {
    uint32_t negative = (uint32_t)0 - (b >> 31);
    uint32_t is_nan = (uint32_t)0 - (uint32_t)((b & 0x7FFFFFFFu) > 0x7F800000u);
    return (b ^ km.xor_mask ^ (negative & km.float_mask)) | (is_nan & km.nan_mask);
}

nodesort_status ns_sort_u32(uint32_t *data, size_t n, ns_keymap km, unsigned threads,
                            size_t seq_threshold);

#endif /* NODESORT_SORT_H */
