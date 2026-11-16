/* Minimal internal threading layer: POSIX threads or Win32.
 * SPDX-License-Identifier: MIT */
#ifndef NODESORT_THREAD_H
#define NODESORT_THREAD_H

#include <stddef.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
typedef SRWLOCK ns_mutex;
typedef CONDITION_VARIABLE ns_cond;
#else
#  include <pthread.h>
typedef pthread_mutex_t ns_mutex;
typedef pthread_cond_t ns_cond;
#endif

typedef void (*ns_thread_fn)(void *arg);

/* Must stay at a stable address from ns_thread_start() until ns_thread_join(). */
typedef struct ns_thread {
#if defined(_WIN32)
    HANDLE handle;
#else
    pthread_t handle;
#endif
    ns_thread_fn fn;
    void *arg;
} ns_thread;

/* All functions returning int return 0 on success, non-zero on failure. */
int ns_thread_start(ns_thread *t, ns_thread_fn fn, void *arg);
void ns_thread_join(ns_thread *t);

int ns_mutex_init(ns_mutex *m);
void ns_mutex_destroy(ns_mutex *m);
void ns_mutex_lock(ns_mutex *m);
void ns_mutex_unlock(ns_mutex *m);

int ns_cond_init(ns_cond *c);
void ns_cond_destroy(ns_cond *c);
void ns_cond_wait(ns_cond *c, ns_mutex *m);
void ns_cond_broadcast(ns_cond *c);

/* Reusable barrier for a fixed number of participants. */
typedef struct ns_barrier {
    ns_mutex mutex;
    ns_cond cond;
    unsigned count;
    unsigned waiting;
    unsigned long generation;
} ns_barrier;

int ns_barrier_init(ns_barrier *b, unsigned count);
void ns_barrier_destroy(ns_barrier *b);
void ns_barrier_wait(ns_barrier *b);

/* Processors available to this process (affinity-aware where supported); >= 1. */
unsigned ns_cpu_count(void);

#endif /* NODESORT_THREAD_H */
