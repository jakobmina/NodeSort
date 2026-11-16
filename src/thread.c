/* SPDX-License-Identifier: MIT */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE /* sched_getaffinity, CPU_COUNT */
#endif

#include "thread.h"

#include "fault.h"

#if defined(_WIN32)
#  include <process.h>
#  include <stdint.h>
#else
#  include <unistd.h>
#  if defined(__linux__)
#    include <sched.h>
#  endif
#endif

/* ── Threads ───────────────────────────────────────────────────────────── */
#if defined(_WIN32)

static unsigned __stdcall ns_thread_trampoline(void *p) {
    ns_thread *t = (ns_thread *)p;
    t->fn(t->arg);
    return 0;
}

int ns_thread_start(ns_thread *t, ns_thread_fn fn, void *arg) {
    if (NS_FAULT(ns_fault_thread_countdown)) return 1;
    t->fn = fn;
    t->arg = arg;
    uintptr_t h = _beginthreadex(NULL, 0, ns_thread_trampoline, t, 0, NULL);
    if (h == 0) return 1;
    t->handle = (HANDLE)h;
    return 0;
}

void ns_thread_join(ns_thread *t) {
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
}

#else

static void *ns_thread_trampoline(void *p) {
    ns_thread *t = (ns_thread *)p;
    t->fn(t->arg);
    return NULL;
}

int ns_thread_start(ns_thread *t, ns_thread_fn fn, void *arg) {
    if (NS_FAULT(ns_fault_thread_countdown)) return 1;
    t->fn = fn;
    t->arg = arg;
    return pthread_create(&t->handle, NULL, ns_thread_trampoline, t) != 0;
}

void ns_thread_join(ns_thread *t) { (void)pthread_join(t->handle, NULL); }

#endif

/* ── Mutex / condition variable ────────────────────────────────────────── */
#if defined(_WIN32)

int ns_mutex_init(ns_mutex *m) {
    InitializeSRWLock(m);
    return 0;
}
void ns_mutex_destroy(ns_mutex *m) { (void)m; }
void ns_mutex_lock(ns_mutex *m) { AcquireSRWLockExclusive(m); }
void ns_mutex_unlock(ns_mutex *m) { ReleaseSRWLockExclusive(m); }

int ns_cond_init(ns_cond *c) {
    InitializeConditionVariable(c);
    return 0;
}
void ns_cond_destroy(ns_cond *c) { (void)c; }
void ns_cond_wait(ns_cond *c, ns_mutex *m) { (void)SleepConditionVariableSRW(c, m, INFINITE, 0); }
void ns_cond_broadcast(ns_cond *c) { WakeAllConditionVariable(c); }

#else

int ns_mutex_init(ns_mutex *m) { return pthread_mutex_init(m, NULL) != 0; }
void ns_mutex_destroy(ns_mutex *m) { (void)pthread_mutex_destroy(m); }
void ns_mutex_lock(ns_mutex *m) { (void)pthread_mutex_lock(m); }
void ns_mutex_unlock(ns_mutex *m) { (void)pthread_mutex_unlock(m); }

int ns_cond_init(ns_cond *c) { return pthread_cond_init(c, NULL) != 0; }
void ns_cond_destroy(ns_cond *c) { (void)pthread_cond_destroy(c); }
void ns_cond_wait(ns_cond *c, ns_mutex *m) { (void)pthread_cond_wait(c, m); }
void ns_cond_broadcast(ns_cond *c) { (void)pthread_cond_broadcast(c); }

#endif

/* ── Barrier ───────────────────────────────────────────────────────────── */
int ns_barrier_init(ns_barrier *b, unsigned count) {
    if (ns_mutex_init(&b->mutex) != 0) return 1;
    if (ns_cond_init(&b->cond) != 0) {
        ns_mutex_destroy(&b->mutex);
        return 1;
    }
    b->count = count;
    b->waiting = 0;
    b->generation = 0;
    return 0;
}

void ns_barrier_destroy(ns_barrier *b) {
    ns_cond_destroy(&b->cond);
    ns_mutex_destroy(&b->mutex);
}

void ns_barrier_wait(ns_barrier *b) {
    ns_mutex_lock(&b->mutex);
    unsigned long gen = b->generation;
    if (++b->waiting == b->count) {
        b->waiting = 0;
        b->generation++;
        ns_cond_broadcast(&b->cond);
    } else {
        while (gen == b->generation) ns_cond_wait(&b->cond, &b->mutex);
    }
    ns_mutex_unlock(&b->mutex);
}

/* ── CPU count ─────────────────────────────────────────────────────────── */
unsigned ns_cpu_count(void) {
    long n = 0;
#if defined(_WIN32)
    n = (long)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#else
#  if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof set, &set) == 0) n = CPU_COUNT(&set);
#  endif
#  if defined(_SC_NPROCESSORS_ONLN)
    if (n <= 0) n = sysconf(_SC_NPROCESSORS_ONLN);
#  endif
#endif
    return n > 0 ? (unsigned)n : 1u;
}
