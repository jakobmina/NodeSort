/* Parallel LSD radix sort for 32-bit keys. Design: architecture.md (id nodesort-architecture).
 * SPDX-License-Identifier: MIT */
#include "sort.h"

#include <stdlib.h>
#include <string.h>

#include "fault.h"
#include "thread.h"

#define NS_BUCKETS 256u
#define NS_PASSES 4u
#define NS_INSERTION_MAX 64u
#define NS_MIN_PER_THREAD ((size_t)1 << 15)
#define NS_MAX_THREADS 256u

typedef size_t ns_hist[NS_BUCKETS];
typedef ns_hist ns_hist_all[NS_PASSES];

#if defined(NODESORT_FAULT_INJECTION)
int ns_fault_alloc_countdown = -1;
int ns_fault_thread_countdown = -1;

int ns_fault_hit(int *countdown) {
    if (*countdown < 0) return 0;
    if (*countdown == 0) {
        *countdown = -1;
        return 1;
    }
    --*countdown;
    return 0;
}
#endif

static void *ns_alloc(size_t count, size_t size) {
    if (NS_FAULT(ns_fault_alloc_countdown)) return NULL;
    if (size != 0 && count > SIZE_MAX / size) return NULL;
    return malloc(count * size);
}

static void reverse(uint32_t *a, size_t n) {
    for (size_t i = 0, j = n - 1; i < j; i++, j--) {
        uint32_t t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

/* ── Shared building blocks ────────────────────────────────────────────── */
/* A pass is a no-op when every key has the same digit in it. */
static int pass_is_trivial(const size_t *total, size_t n) {
    for (unsigned d = 0; d < NS_BUCKETS; d++)
        if (total[d] != 0) return total[d] == n;
    return 1;
}

/* ── Parallel radix ────────────────────────────────────────────────────── */
enum { GATE_PENDING, GATE_GO, GATE_ABORT };

typedef struct ns_ctx {
    uint32_t *data;
    uint32_t *buf;
    size_t n;
    ns_keymap km;
    unsigned nthreads;
    void (*run)(struct ns_ctx *c, unsigned id); /* worker body for this key kind */

    ns_hist_all *hist0; /* [nthreads] histograms of each thread's initial chunk */
    ns_hist *count;     /* [nthreads] digit counts of the current pass          */
    ns_hist *offset;    /* [nthreads] scatter start offsets of the current pass */
    unsigned active[NS_PASSES];
    unsigned nactive;

    ns_barrier barrier;
    ns_mutex gate_mutex;
    ns_cond gate_cond;
    int gate;
} ns_ctx;

typedef struct ns_worker {
    ns_ctx *ctx;
    unsigned id;
    ns_thread thread;
} ns_worker;

static void chunk_bounds(size_t n, unsigned nthreads, unsigned t, size_t *begin, size_t *end) {
    size_t q = n / nthreads, r = n % nthreads;
    *begin = q * t + (t < r ? t : r);
    *end = *begin + q + (t < r ? 1u : 0u);
}

static void plan_passes(ns_ctx *c) {
    c->nactive = 0;
    for (unsigned p = 0; p < NS_PASSES; p++) {
        ns_hist total = {0};
        for (unsigned t = 0; t < c->nthreads; t++)
            for (unsigned d = 0; d < NS_BUCKETS; d++) total[d] += c->hist0[t][p][d];
        if (!pass_is_trivial(total, c->n)) c->active[c->nactive++] = p;
    }
}

/* Offsets are digit-major, thread-minor: keeps every pass stable, which LSD radix requires. */
static void compute_offsets(ns_ctx *c) {
    size_t running = 0;
    for (unsigned d = 0; d < NS_BUCKETS; d++)
        for (unsigned t = 0; t < c->nthreads; t++) {
            c->offset[t][d] = running;
            running += c->count[t][d];
        }
}

static void worker_main(void *arg) {
    ns_worker *w = (ns_worker *)arg;
    ns_ctx *c = w->ctx;
    ns_mutex_lock(&c->gate_mutex);
    while (c->gate == GATE_PENDING) ns_cond_wait(&c->gate_cond, &c->gate_mutex);
    int go = c->gate == GATE_GO;
    ns_mutex_unlock(&c->gate_mutex);
    if (go) c->run(c, w->id);
}

static void open_gate(ns_ctx *c, int state) {
    ns_mutex_lock(&c->gate_mutex);
    c->gate = state;
    ns_cond_broadcast(&c->gate_cond);
    ns_mutex_unlock(&c->gate_mutex);
}

/* Every resource is acquired before the first element moves, so any failure leaves data intact. */
static nodesort_status radix_par(uint32_t *data, uint32_t *buf, size_t n, ns_keymap km,
                                 unsigned nthreads, void (*run)(ns_ctx *, unsigned)) {
    nodesort_status status = NODESORT_OK;
    ns_ctx c;
    memset(&c, 0, sizeof c);
    c.data = data;
    c.buf = buf;
    c.n = n;
    c.km = km;
    c.nthreads = nthreads;
    c.run = run;
    c.gate = GATE_PENDING;

    ns_worker *workers = (ns_worker *)ns_alloc(nthreads, sizeof *workers);
    c.hist0 = (ns_hist_all *)ns_alloc(nthreads, sizeof *c.hist0);
    c.count = (ns_hist *)ns_alloc(nthreads, sizeof *c.count);
    c.offset = (ns_hist *)ns_alloc(nthreads, sizeof *c.offset);
    if (!workers || !c.hist0 || !c.count || !c.offset) {
        status = NODESORT_ENOMEM;
        goto free_memory;
    }

    if (ns_barrier_init(&c.barrier, nthreads) != 0) {
        status = NODESORT_ETHREAD;
        goto free_memory;
    }
    if (ns_mutex_init(&c.gate_mutex) != 0) {
        status = NODESORT_ETHREAD;
        goto destroy_barrier;
    }
    if (ns_cond_init(&c.gate_cond) != 0) {
        status = NODESORT_ETHREAD;
        goto destroy_mutex;
    }

    unsigned started = 1; /* worker 0 is the calling thread */
    for (; started < nthreads; started++) {
        workers[started].ctx = &c;
        workers[started].id = started;
        if (ns_thread_start(&workers[started].thread, worker_main, &workers[started]) != 0) break;
    }

    if (started < nthreads) {
        open_gate(&c, GATE_ABORT);
        status = NODESORT_ETHREAD;
    } else {
        open_gate(&c, GATE_GO);
        run(&c, 0);
    }
    for (unsigned t = 1; t < started; t++) ns_thread_join(&workers[t].thread);

    ns_cond_destroy(&c.gate_cond);
destroy_mutex:
    ns_mutex_destroy(&c.gate_mutex);
destroy_barrier:
    ns_barrier_destroy(&c.barrier);
free_memory:
    free(c.offset);
    free(c.count);
    free(c.hist0);
    free(workers);
    return status;
}

/* ── Instantiations ────────────────────────────────────────────────────── */
/* Integers need only an XOR; the float map costs ~15-20% if applied to them too. */
#define NS_FN(name) name##_int
#define NS_KEY(x, km) ((x) ^ (km).xor_mask)
#include "radix_impl.h"
#undef NS_FN
#undef NS_KEY

#define NS_FN(name) name##_flt
#define NS_KEY(x, km) ns_key((x), (km))
#include "radix_impl.h"
#undef NS_FN
#undef NS_KEY

/* ── Entry point ───────────────────────────────────────────────────────── */
nodesort_status ns_sort_u32(uint32_t *data, size_t n, ns_keymap km, unsigned threads,
                            size_t seq_threshold) {
    if (km.float_mask != 0 || km.nan_mask != 0)
        return sort_flt(data, n, km, threads, seq_threshold);
    return sort_int(data, n, km, threads, seq_threshold);
}
