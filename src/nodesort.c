/* Public API: argument validation and option resolution.
 * SPDX-License-Identifier: MIT */
#include "nodesort/nodesort.h"

#include <stddef.h>

#include "sort.h"
#include "thread.h"

_Static_assert(sizeof(float) == sizeof(uint32_t), "nodesort_f32 needs 32-bit IEEE 754 floats");
_Static_assert(sizeof(double) == sizeof(uint64_t), "nodesort_f64 needs 64-bit IEEE 754 doubles");

#define NS_DEFAULT_SEQ_THRESHOLD ((size_t)1 << 16)

/* Size of the oldest nodesort_options layout this library accepts. */
#define NS_OPTIONS_MIN_SIZE (offsetof(nodesort_options, seq_threshold) + sizeof(size_t))

void nodesort_options_init(nodesort_options *opt) {
    if (!opt) return;
    opt->struct_size = sizeof *opt;
    opt->threads = 0;
    opt->seq_threshold = 0;
    opt->descending = 0;
}

/* Reads a field only when the caller's struct (possibly from an older header) contains it. */
#define NS_HAS_FIELD(opt, field)                                                                   \
    ((opt)->struct_size >= offsetof(nodesort_options, field) + sizeof((opt)->field))

typedef struct ns_resolved {
    ns_keymap km;
    unsigned threads;
    size_t seq_threshold;
} ns_resolved;

static nodesort_status resolve(const nodesort_options *opt, ns_keymap km, ns_resolved *out) {
    unsigned threads = 0;
    size_t seq_threshold = 0;
    if (opt) {
        if (opt->struct_size < NS_OPTIONS_MIN_SIZE) return NODESORT_EINVAL;
        threads = opt->threads;
        seq_threshold = opt->seq_threshold;
        if (NS_HAS_FIELD(opt, descending) && opt->descending) km.xor_mask = ~km.xor_mask;
    }
    out->km = km;
    out->threads = threads != 0 ? threads : ns_cpu_count();
    out->seq_threshold = seq_threshold != 0 ? seq_threshold : NS_DEFAULT_SEQ_THRESHOLD;
    return NODESORT_OK;
}

/* Values move as raw 4- or 8-byte words, so they must be naturally aligned. */
static int values_ok(const void *values, size_t value_size, size_t n) {
    if (value_size != 4 && value_size != 8) return 0;
    if (n == 0) return 1;
    return values != NULL && ((uintptr_t)values % value_size) == 0;
}

static nodesort_status sort32(uint32_t *keys, void *values, size_t value_size, size_t n,
                              ns_keymap km, const nodesort_options *opt) {
    if (!keys && n > 0) return NODESORT_EINVAL;
    ns_resolved r;
    nodesort_status st = resolve(opt, km, &r);
    if (st != NODESORT_OK) return st;
    return ns_sort32(keys, values, value_size, n, r.km, r.threads, r.seq_threshold);
}

static nodesort_status sort64(uint64_t *keys, void *values, size_t value_size, size_t n,
                              ns_keymap km, const nodesort_options *opt) {
    if (!keys && n > 0) return NODESORT_EINVAL;
    ns_resolved r;
    nodesort_status st = resolve(opt, km, &r);
    if (st != NODESORT_OK) return st;
    return ns_sort64(keys, values, value_size, n, r.km, r.threads, r.seq_threshold);
}

static const ns_keymap KM_UNSIGNED = {0u, 0u, 0u};
static const ns_keymap KM_SIGNED32 = {UINT64_C(0x80000000), 0u, 0u};
static const ns_keymap KM_FLOAT32 = {UINT64_C(0x80000000), UINT64_C(0x7FFFFFFF),
                                     UINT64_C(0xFFFFFFFF)};
static const ns_keymap KM_FLOAT64 = {UINT64_C(0x8000000000000000), UINT64_C(0x7FFFFFFFFFFFFFFF),
                                     UINT64_C(0xFFFFFFFFFFFFFFFF)};

/* Element arrays are accessed as same-width unsigned words: int32_t may alias uint32_t
 * (C11 6.5p7); floats and carried values rely on the library's -fno-strict-aliasing. */

nodesort_status nodesort_u32(uint32_t *data, size_t n, const nodesort_options *opt) {
    return sort32(data, NULL, 0, n, KM_UNSIGNED, opt);
}

nodesort_status nodesort_i32(int32_t *data, size_t n, const nodesort_options *opt) {
    return sort32((uint32_t *)data, NULL, 0, n, KM_SIGNED32, opt);
}

nodesort_status nodesort_f32(float *data, size_t n, const nodesort_options *opt) {
    return sort32((uint32_t *)(void *)data, NULL, 0, n, KM_FLOAT32, opt);
}

nodesort_status nodesort_f64(double *data, size_t n, const nodesort_options *opt) {
    return sort64((uint64_t *)(void *)data, NULL, 0, n, KM_FLOAT64, opt);
}

nodesort_status nodesort_kv_u32(uint32_t *keys, void *values, size_t value_size, size_t n,
                                const nodesort_options *opt) {
    if (!values_ok(values, value_size, n)) return NODESORT_EINVAL;
    return sort32(keys, values, value_size, n, KM_UNSIGNED, opt);
}

nodesort_status nodesort_kv_f32(float *keys, void *values, size_t value_size, size_t n,
                                const nodesort_options *opt) {
    if (!values_ok(values, value_size, n)) return NODESORT_EINVAL;
    return sort32((uint32_t *)(void *)keys, values, value_size, n, KM_FLOAT32, opt);
}

nodesort_status nodesort_kv_u64(uint64_t *keys, void *values, size_t value_size, size_t n,
                                const nodesort_options *opt) {
    if (!values_ok(values, value_size, n)) return NODESORT_EINVAL;
    return sort64(keys, values, value_size, n, KM_UNSIGNED, opt);
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
