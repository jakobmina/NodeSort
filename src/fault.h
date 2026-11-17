/* Test-only fault injection; compiled out unless NODESORT_FAULT_INJECTION is defined.
 * SPDX-License-Identifier: MIT */
#ifndef NODESORT_FAULT_H
#define NODESORT_FAULT_H

#if defined(NODESORT_FAULT_INJECTION)

/* Countdown semantics: -1 = never fail; k >= 0 = the (k+1)-th operation fails. */
extern int ns_fault_alloc_countdown;
extern int ns_fault_thread_countdown;

int ns_fault_hit(int *countdown);
#  define NS_FAULT(counter) ns_fault_hit(&(counter))

#else
#  define NS_FAULT(counter) 0
#endif

#endif /* NODESORT_FAULT_H */
