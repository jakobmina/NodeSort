---
id: nodesort-architecture
type: architecture-design
title: nodesort architecture
status: draft
parent: nodesort
---

# nodesort architecture

How the library delivers the product in `nodesort` (goal-and-requirements): three layers in one
static/shared library, plus tools that only consume the public API.

## Topology

| Layer | Owns | Files |
|---|---|---|
| Public API | Argument and option validation, defaults, status strings, version. Only layer with exported symbols. | `include/nodesort/nodesort.h`, `src/nodesort.c` |
| Sort engine | Choosing the strategy and running it; every allocation; the intact-on-failure guarantee. | `src/sort.c` |
| Thread layer | Threads, mutex, condition variable, barrier, CPU count over POSIX threads or Win32. | `src/thread.c` |

Dependencies point downward only. The benchmark, example, fuzzer and tests use the public API
(the internal tests also link a fault-injection build, see Verification).

## Decisions

**Algorithm: LSD radix sort, 4 passes × 8 bits.** It replaced the prototype's range buckets
("nodes"), which put almost everything on one thread when the data is skewed. Each thread takes an
equal slice of the array; each pass computes a histogram per thread → scatter offsets ordered by
digit first, then thread → a stable scatter into the buffer. The pass must be stable, otherwise LSD
radix gives wrong results. Comparison sorts (sample sort, introsort) and an in-place IPS⁴o-style
variant were rejected: radix is O(n) with no comparator, and in-place is far more complex. Cost: one
temporary buffer of n elements per call (accepted by the user).

**Strategy choice, in order:** n < 2 → nothing; n ≤ 64 → insertion sort, no allocation; already
ascending → return; descending → reverse in place; otherwise radix — on the calling thread when
`threads` resolves to 1 or n < `seq_threshold` (default 2¹⁶), else parallel with
min(threads, n / 2¹⁵, 256) threads. Passes where every key has the same digit are skipped. The
ordered-input check exists because the radix scatter is slow on sorted data (10M elements: 513 ms →
14 ms with the check).

**Signed keys** are sorted as unsigned with the sign bit flipped (`x ^ 0x80000000`) at the moment
each digit is read. The input array is never rewritten in a prepass, which keeps the
intact-on-failure guarantee easy to hold.

**Invariant — the array stays intact on failure.** Every step that can fail (buffer and bookkeeping
allocation, barrier and gate initialisation, creating each worker thread) happens before the first
element moves. Workers wait at a start gate. If creating thread k fails, the gate is set to abort,
the threads already started are joined and `NODESORT_ETHREAD` is returned. After the gate opens,
only barrier waits remain, and those cannot fail.

**Threads per call, no pool.** A global pool would be global state, which the embedding requirement
rules out. Creating threads costs tens of µs per call, small next to the parallel threshold. The
calling thread works as worker 0. The barrier is our own because macOS has no `pthread_barrier_t`.
MinGW also uses Win32, which avoids a dependency on `libwinpthread`. On Linux, the CPU count
respects the process affinity mask.

**ABI.** Only symbols carrying `NODESORT_API` are exported (hidden visibility by default;
dllexport/dllimport on Windows; `NODESORT_STATIC` for static linking). `nodesort_options` starts
with `struct_size`; fields are only ever appended, and the library reads only the fields that fit in
the size the caller declares. The header is the single source of the version: CMake parses it. While
the version is 0.x, the SONAME is `0.<minor>` and the CMake package compatibility is
`SameMinorVersion`.

## Distribution

CMake ≥ 3.16 exports `nodesort::nodesort` and generates `nodesort.pc`. CPack builds TGZ + DEB (+ RPM
when `rpmbuild` exists) on Linux, TGZ on macOS, ZIP on Windows, plus source archives. CI covers
Linux GCC/Clang, macOS, Windows MSVC (static and shared) and MinGW, sanitizers, a consumer project
built against the installed library (find_package and pkg-config), packaging and fuzzing. Pushing a
`v*` tag that matches the header version publishes a GitHub Release with packages and SHA256SUMS.

## Verification

- Property tests against `qsort` across sizes, distributions and thread counts.
- A fault-injection build (`NODESORT_FAULT_INJECTION`, compiled out of release builds) makes the
  k-th allocation or thread creation fail, for every k, and asserts the array is byte-identical
  afterwards.
- A concurrency test sorts several arrays at once from separate host threads.

## Known limitations

- Needs 4·n bytes of extra memory; no in-place mode.
- The fuzzer's inputs are too small to reach the parallel path; the property tests cover it.
- ThreadSanitizer needs `vm.mmap_rnd_bits=28` on recent Linux kernels to start.
- The original prototype lives unbuilt in `legacy/`; it is not maintained and keeps its known
  crashes.
