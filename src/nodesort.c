/* Public API: argument validation and option resolution.
 * SPDX-License-Identifier: MIT */
#include "nodesort/nodesort.h"

#include <stddef.h>

#include "sort.h"
#include "thread.h"

#define NS_DEFAULT_SEQ_THRESHOLD ((size_t)1 << 16)

/* Size of the oldest nodesort_options layout this library accepts. */
#define NS_OPTIONS_MIN_SIZE (offsetof(nodesort_options, seq_threshold) + sizeof(size_t))

void nodesort_options_init(nodesort_options *opt) {
    if (!opt) return;
    opt->struct_size = sizeof *opt;
    opt->threads = 0;
    opt->seq_threshold = 0;
}

static nodesort_status run(uint32_t *data, size_t n, uint32_t sign_mask,
                           const nodesort_options *opt) {
    if (!data && n > 0) return NODESORT_EINVAL;

    unsigned threads = 0;
    size_t seq_threshold = 0;
    if (opt) {
        if (opt->struct_size < NS_OPTIONS_MIN_SIZE) return NODESORT_EINVAL;
        threads = opt->threads;
        seq_threshold = opt->seq_threshold;
    }
    if (threads == 0) threads = ns_cpu_count();
    if (seq_threshold == 0) seq_threshold = NS_DEFAULT_SEQ_THRESHOLD;

    return ns_sort_u32(data, n, sign_mask, threads, seq_threshold);
}

nodesort_status nodesort_u32(uint32_t *data, size_t n, const nodesort_options *opt) {
    return run(data, n, 0u, opt);
}

nodesort_status nodesort_i32(int32_t *data, size_t n, const nodesort_options *opt) {
    /* int32_t and uint32_t may alias (C11 6.5p7); flipping the sign bit maps signed order onto
     * unsigned. */
    return run((uint32_t *)data, n, UINT32_C(0x80000000), opt);
}

const char *nodesort_strerror(nodesort_status status) {
    switch (status) {
    case NODESORT_OK: return "success";
    case NODESORT_EINVAL: return "invalid argument";
    case NODESORT_ENOMEM: return "out of memory";
    case NODESORT_ETHREAD: return "could not create threads";
    }
    return "unknown status";
}

const char *nodesort_version(void) { return NODESORT_VERSION_STRING; }
