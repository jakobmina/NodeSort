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
| Sort engine | Choosing the strategy and running it; every allocation; the intact-on-failure guarantee. | `src/sort.c`, `src/radix_impl.h`, `src/sort.h` |
| Thread layer | Threads, mutex, condition variable, barrier, CPU count over POSIX threads or Win32. | `src/thread.c` |

Dependencies point downward only. The benchmark, example, fuzzer and tests use the public API
(the internal tests also link a fault-injection build, see Verification).

## Decisions

**Algorithm: LSD radix sort with 8-bit digits** (4 passes for 32-bit keys, 8 for 64-bit keys). It
replaced the prototype's range buckets
("nodes"), which put almost everything on one thread when the data is skewed. Each thread takes an
equal slice of the array; each pass computes a histogram per thread → scatter offsets ordered by
digit first, then thread → a stable scatter into the buffer. The pass must be stable, otherwise LSD
radix gives wrong results. Comparison sorts (sample sort, introsort) and an in-place IPS⁴o-style
variant were rejected: radix is O(n) with no comparator, and in-place is far more complex. Cost: one
temporary buffer of n keys per call, plus n values for key-value sorts (accepted by the user).

**Strategy choice, in order:** n < 2 → nothing; n ≤ 64 → insertion sort, no allocation; already
ascending → return; *strictly* descending → reverse in place (non-strict would reverse elements
with equal keys, e.g. NaNs, breaking their input order); otherwise radix — on the calling thread when
`threads` resolves to 1 or n < `seq_threshold` (default 2¹⁶), else parallel with
min(threads, n / 2¹⁵, 256) threads. Passes where every key has the same digit are skipped. The
ordered-input check exists because the radix scatter is slow on sorted data (10M elements: 513 ms →
14 ms with the check).

**Keys are computed on the fly.** Every element type is sorted as an unsigned 32- or 64-bit radix
key, computed whenever a digit or comparison is needed. The input array is never rewritten in a
prepass, which keeps the intact-on-failure guarantee easy to hold. `ns_keymap` (`src/sort.h`,
64-bit masks; 32-bit keys use the low half):
`key(b) = (b ^ xor ^ (negative(b) & float_mask)) | (nan(b) & nan_mask)`.

| Type | xor | float_mask | nan_mask |
|---|---|---|---|
| `uint32_t`, `uint64_t` | 0 | 0 | 0 |
| `int32_t` | `0x80000000` | 0 | 0 |
| `float` | `0x80000000` | `0x7FFFFFFF` | `0xFFFFFFFF` |
| `double` | `0x8000…0` | `0x7FFF…F` | all ones |

- Descending order inverts `xor` (`~xor`).
- `float`: negative values have every bit flipped, so -0.0 sorts before +0.0. Every NaN gets the
  largest key in both directions; since the radix sort is stable and writes back the original bits,
  NaNs end last in input order. Rejected: moving NaNs to the end in a prepass (it moves data before
  allocation, breaking the invariant) and the plain IEEE totalOrder trick (negative NaNs first).
- `float`/`double` arrays and carried values are accessed as same-width unsigned words, so the
  library builds with `-fno-strict-aliasing`. Static asserts require 4-byte `float` and 8-byte
  `double`.

**One engine template, instantiated per (key, value) kind.** `src/radix_impl.h` is included by
`sort.c` once per combination the API uses. Its parameters: key word (32/64 bits), key map (XOR only
for integers, the full map for floats), and an optional carried value word (4/8 bytes). There are 9
instances: 32-bit int/float keys × values none/4/8, `double` keys only, and `uint64_t` keys ×
values 4/8.
- Integers get their own XOR-only instance because applying the float map to them made them 15–20%
  slower.
- With values, every move of a key (scatter, insertion sort, reverse, copy-back) moves the value at
  the same index. Stability then comes from the stable radix sort and the strict fast paths.
- Values are raw 4/8-byte words, so the API requires them to be aligned to `value_size`.
- Rejected: sorting (key, index) pairs and permuting values afterwards (an extra random-access
  pass), and packing key+value into 64-bit keys (only covers u32 + 4 bytes).
- The parallel driver (`ns_ctx`, `radix_par`) is type-agnostic: it works on `void *` buffers and a
  per-instance pass count, and calls the instance's worker through `ns_ctx.run`.

**Invariant — the array (both arrays, for key-value sorts) stays intact on failure.** Every step that can fail (buffer and bookkeeping
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
the size the caller declares, so a binary built against the 0.1 header (no `descending`) keeps
working. The header is the single source of the version: CMake parses it. While
the version is 0.x, the SONAME is `0.<minor>` and the CMake package compatibility is
`SameMinorVersion`.

## Distribution

CMake ≥ 3.16 exports `nodesort::nodesort` and generates `nodesort.pc`. CPack builds TGZ + DEB (+ RPM
when `rpmbuild` exists) on Linux, TGZ on macOS, ZIP on Windows, plus source archives. CI covers
Linux GCC/Clang, macOS, Windows MSVC (static and shared) and MinGW, sanitizers, a consumer project
built against the installed library (find_package and pkg-config), packaging and fuzzing. Pushing a
`v*` tag that matches the header version publishes a GitHub Release with packages and SHA256SUMS.

## Verification

- Property tests against a stable reference sort whose comparator is written from the documented
  rules, compared bit-for-bit, across types, both directions, sizes, distributions (including IEEE
  special values) and thread counts. Key-value tests carry each element's input position as its
  value, so instability or a misplaced value is visible.
- A fault-injection build (`NODESORT_FAULT_INJECTION`, compiled out of release builds) makes the
  k-th allocation or thread creation fail, for every k, and asserts the array (keys and values) is
  byte-identical afterwards.
- A concurrency test sorts several arrays at once from separate host threads.

## Known limitations

- Needs n·(key size + value size) bytes of extra memory; no in-place mode.
- The fuzzer covers the single-array sorts only; key-value sorts rely on the property and
  fault-injection tests.
- ThreadSanitizer needs `vm.mmap_rnd_bits=28` on recent Linux kernels to start.
- The original prototype lives unbuilt in `legacy/`; it is not maintained and keeps its known
  crashes.
