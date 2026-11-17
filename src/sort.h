/* Internal sorting engine shared by the public entry points.
 * SPDX-License-Identifier: MIT */
#ifndef NODESORT_SORT_H
#define NODESORT_SORT_H

#include <stddef.h>
#include <stdint.h>

#include "nodesort/nodesort.h"

/* Keys are compared as (x ^ sign_mask): 0 for unsigned, 0x80000000 for signed. */
nodesort_status ns_sort_u32(uint32_t *data, size_t n, uint32_t sign_mask, unsigned threads,
                            size_t seq_threshold);

#endif /* NODESORT_SORT_H */
