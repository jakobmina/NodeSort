/*
 * nodesort - parallel in-place sorting of 32-bit integer arrays.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef NODESORT_NODESORT_H
#define NODESORT_NODESORT_H

#include <stddef.h>
#include <stdint.h>

/* ── Symbol export ─────────────────────────────────────────────────────── */
#if defined(NODESORT_STATIC)
#  define NODESORT_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#  if defined(NODESORT_BUILDING)
#    define NODESORT_API __declspec(dllexport)
#  else
#    define NODESORT_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define NODESORT_API __attribute__((visibility("default")))
#else
#  define NODESORT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version ───────────────────────────────────────────────────────────── */
#define NODESORT_VERSION_MAJOR 0
#define NODESORT_VERSION_MINOR 1
#define NODESORT_VERSION_PATCH 0
#define NODESORT_VERSION_STRING "0.1.0"

/* ── Status codes ──────────────────────────────────────────────────────── */
typedef enum nodesort_status {
    NODESORT_OK = 0,     /**< Sorted successfully.                                   */
    NODESORT_EINVAL = 1, /**< Invalid argument (NULL data with n > 0, bad options). */
    NODESORT_ENOMEM = 2, /**< Not enough memory for the auxiliary buffer.            */
    NODESORT_ETHREAD = 3 /**< The operating system could not create worker threads. */
} nodesort_status;

/* ── Options ───────────────────────────────────────────────────────────── */
typedef struct nodesort_options {
    /** Must be sizeof(nodesort_options); set by nodesort_options_init(). */
    size_t struct_size;
    /** Worker threads to use; 0 = number of online processors. */
    unsigned threads;
    /** Inputs smaller than this are sorted on the calling thread; 0 = default. */
    size_t seq_threshold;
} nodesort_options;

/** Fill `opt` with the defaults. Always call this before changing fields. */
NODESORT_API void nodesort_options_init(nodesort_options *opt);

/**
 * Sort `data[0..n)` in ascending order, in place.
 *
 * `opt` may be NULL to use the defaults. Needs a temporary buffer of n
 * elements. On any error the array is left exactly as it was.
 *
 * Thread-safe and reentrant: concurrent calls on distinct arrays are safe.
 */
NODESORT_API nodesort_status nodesort_i32(int32_t *data, size_t n, const nodesort_options *opt);

/** Same as nodesort_i32() for unsigned 32-bit integers. */
NODESORT_API nodesort_status nodesort_u32(uint32_t *data, size_t n, const nodesort_options *opt);

/** Human-readable, static description of a status code. Never returns NULL. */
NODESORT_API const char *nodesort_strerror(nodesort_status status);

/** Version of the linked library, e.g. "0.1.0". May differ from the header. */
NODESORT_API const char *nodesort_version(void);

#ifdef __cplusplus
}
#endif

#endif /* NODESORT_NODESORT_H */
