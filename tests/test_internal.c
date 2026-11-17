/* Internal tests against a fault-injection build: the array must stay intact on every
 * failure path, and concurrent callers must not interfere.
 * SPDX-License-Identifier: MIT */
#include "fault.h"
#include "harness.h"
#include "nodesort/nodesort.h"
#include "thread.h"

#define N_PARALLEL ((size_t)1 << 18)

static void make_input(int32_t *a, size_t n, uint64_t seed) {
    th_rng rng;
    rng.s = seed;
    for (size_t i = 0; i < n; i++) a[i] = (int32_t)th_u32(&rng);
}

static int is_sorted(const int32_t *a, size_t n) {
    for (size_t i = 1; i < n; i++)
        if (a[i - 1] > a[i]) return 0;
    return 1;
}

/* Fails the k-th fallible operation for k = 0, 1, ... until a call succeeds. */
static void exercise_faults(int *countdown, nodesort_status expected, unsigned threads) {
    int32_t *orig = (int32_t *)th_xmalloc(N_PARALLEL * sizeof *orig);
    int32_t *a = (int32_t *)th_xmalloc(N_PARALLEL * sizeof *a);
    make_input(orig, N_PARALLEL, 7);

    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.threads = threads;
    opt.seq_threshold = 1;

    int injected = 0;
    for (int k = 0; k < 1000; k++) {
        memcpy(a, orig, N_PARALLEL * sizeof *a);
        *countdown = k;
        nodesort_status st = nodesort_i32(a, N_PARALLEL, &opt);
        int fired = (*countdown == -1) && st != NODESORT_OK;
        *countdown = -1;
        if (st == NODESORT_OK) {
            CHECK(is_sorted(a, N_PARALLEL));
            break;
        }
        injected++;
        CHECK_MSG(fired, "k=%d", k);
        CHECK_MSG(st == expected, "k=%d status=%d", k, (int)st);
        CHECK_MSG(memcmp(a, orig, N_PARALLEL * sizeof *a) == 0, "array modified on failure k=%d",
                  k);
    }
    CHECK(injected > 0);
    free(orig);
    free(a);
}

static void test_alloc_failures_parallel(void) {
    exercise_faults(&ns_fault_alloc_countdown, NODESORT_ENOMEM, 4);
}
static void test_alloc_failures_sequential(void) {
    exercise_faults(&ns_fault_alloc_countdown, NODESORT_ENOMEM, 1);
}
static void test_thread_failures(void) {
    exercise_faults(&ns_fault_thread_countdown, NODESORT_ETHREAD, 8);
}

/* ── Concurrent callers (the library must be reentrant) ────────────────── */
typedef struct {
    int32_t *data;
    size_t n;
    nodesort_status status;
} caller_arg;

static void caller_main(void *p) {
    caller_arg *c = (caller_arg *)p;
    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.threads = 2;
    opt.seq_threshold = 1;
    c->status = nodesort_i32(c->data, c->n, &opt);
}

static void test_concurrent_callers(void) {
    enum { CALLERS = 4 };
    ns_thread threads[CALLERS];
    caller_arg args[CALLERS];
    for (unsigned i = 0; i < CALLERS; i++) {
        args[i].n = N_PARALLEL;
        args[i].data = (int32_t *)th_xmalloc(N_PARALLEL * sizeof(int32_t));
        args[i].status = NODESORT_EINVAL;
        make_input(args[i].data, N_PARALLEL, 100u + i);
    }
    unsigned started = 0;
    for (; started < CALLERS; started++)
        if (ns_thread_start(&threads[started], caller_main, &args[started]) != 0) break;
    CHECK(started == CALLERS);
    for (unsigned i = 0; i < started; i++) ns_thread_join(&threads[i]);
    for (unsigned i = 0; i < CALLERS; i++) {
        if (i < started) {
            CHECK(args[i].status == NODESORT_OK);
            CHECK(is_sorted(args[i].data, args[i].n));
        }
        free(args[i].data);
    }
}

int main(void) {
    RUN(test_alloc_failures_parallel);
    RUN(test_alloc_failures_sequential);
    RUN(test_thread_failures);
    RUN(test_concurrent_callers);
    return TEST_EXIT();
}
