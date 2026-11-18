/* SPDX-License-Identifier: MIT */
#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200809L /* clock_gettime, isatty, fileno */
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "thread.h"

#if defined(_WIN32)
#  include <io.h>
#else
#  include <time.h>
#  include <unistd.h>
#endif

void cli_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("nodesort: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

double now_ms(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

int stdout_is_terminal(void) {
#if defined(_WIN32)
    return _isatty(_fileno(stdout));
#else
    return isatty(fileno(stdout));
#endif
}

unsigned cpu_count(void) { return ns_cpu_count(); }

int parse_count(const char *s, uint64_t *out) {
    char digits[64];
    size_t n = 0;
    uint64_t mult = 1;
    for (const char *p = s; *p; p++) {
        if (*p == '_' || *p == ',') continue;
        if (p[1] == '\0' && (*p == 'k' || *p == 'K'))
            mult = 1000;
        else if (p[1] == '\0' && (*p == 'm' || *p == 'M'))
            mult = 1000000;
        else if (p[1] == '\0' && (*p == 'g' || *p == 'G'))
            mult = 1000000000;
        else if (*p < '0' || *p > '9' || n + 1 >= sizeof digits)
            return 0;
        else
            digits[n++] = *p;
    }
    if (n == 0) return 0;
    digits[n] = '\0';
    errno = 0;
    unsigned long long v = strtoull(digits, NULL, 10);
    if (errno || v > UINT64_MAX / mult) return 0;
    *out = (uint64_t)v * mult;
    return 1;
}

const char *fmt_count(uint64_t n, char *buf, size_t len) {
    char tmp[32];
    int digits = snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)n);
    size_t o = 0;
    for (int i = 0; i < digits && o + 2 < len; i++) {
        if (i > 0 && (digits - i) % 3 == 0) buf[o++] = ',';
        buf[o++] = tmp[i];
    }
    buf[o] = '\0';
    return buf;
}

nodesort_status sort_array(array_t *a, const nodesort_options *opt) {
    switch (a->type) {
    case DT_F32: return nodesort_f32((float *)a->data, a->count, opt);
    case DT_F64: return nodesort_f64((double *)a->data, a->count, opt);
    case DT_I32: return nodesort_i32((int32_t *)a->data, a->count, opt);
    case DT_U32: return nodesort_u32((uint32_t *)a->data, a->count, opt);
    default: return NODESORT_EINVAL;
    }
}

void print_element(const array_t *a, size_t i) {
    switch (a->type) {
    case DT_F32: printf("%.9g", (double)((const float *)a->data)[i]); break;
    case DT_F64: printf("%.17g", ((const double *)a->data)[i]); break;
    case DT_I32: printf("%ld", (long)((const int32_t *)a->data)[i]); break;
    case DT_U32: printf("%lu", (unsigned long)((const uint32_t *)a->data)[i]); break;
    case DT_U64: printf("%llu", (unsigned long long)((const uint64_t *)a->data)[i]); break;
    default: break;
    }
}

const char *option_value(int argc, char **argv, int *i) {
    if (*i + 1 >= argc) {
        cli_error("option %s needs a value", argv[*i]);
        return NULL;
    }
    return argv[++*i];
}
