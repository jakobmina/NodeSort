# NodeSort

[![CI](https://github.com/jakobmina/NodeSort/actions/workflows/ci.yml/badge.svg)](https://github.com/jakobmina/NodeSort/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**Parallel, in-place sorting of integer and float arrays for C — alone or carrying a parallel array
of values (IDs, pointers, payloads).** One call, every core, no dependencies — typically 5–20×
faster than `qsort` on large arrays.

```c
#include <nodesort/nodesort.h>

int32_t values[] = {42, -7, 13, 0};
nodesort_status st = nodesort_i32(values, 4, NULL);   /* NULL = use all cores */
if (st != NODESORT_OK) fprintf(stderr, "%s\n", nodesort_strerror(st));
```

- **Fast** — parallel LSD radix sort (8-bit digits), O(n), load-balanced on any distribution;
  already sorted / reversed input is detected in a single read.
- **Stable key-value sorts** — sort scores and drag their document IDs, or 64-bit state indices and
  their amplitudes, in one call.
- **Safe to embed** — reentrant, no global state, never prints or aborts. Every failure is a status
  code and **leaves the array exactly as it was**.
- **Portable** — C11 on Linux, macOS and Windows (MSVC and MinGW); POSIX threads or Win32.
- **Easy to consume** — CMake `find_package`, `pkg-config`, `.deb`/`.rpm`, static or shared.
- **Command-line tool** — sort NumPy `.npy` / raw binary files, find the top-k most similar
  embeddings, or watch a live race against `qsort`, without writing any C.

## Command-line tool

The packages install a `nodesort` command (`tools/cli`; build option `NODESORT_BUILD_CLI`).

```console
$ nodesort demo                       # live race on random data, 1K → 10M elements
       elements         qsort      nodesort   speedup  check
          1,000       0.65 ms       0.06 ms     11.6x  ok
            ...
     10,000,000    4701.88 ms     273.97 ms     17.2x  ok

$ nodesort sort scores.npy --desc -o scores_sorted.npy
sorted 2,000,000 float32 values in 90.78 ms (descending, up to 4 threads)
wrote scores_sorted.npy

$ nodesort top embeddings.npy --query q.npy -k 5 -o top.npy
top 5 of 200,000 rows (384 dims), metric cosine: scored in 218.45 ms, ranked in 15.88 ms

    rank                    id         score
       1                    42      0.995026
       2                136913      0.209383
       ...
```

| Command | What it does |
|---|---|
| `nodesort sort INPUT [-o OUT] [--desc]` | Sorts a 1-D array. Without `-o` it only prints a summary; `-o` may be the input file itself. |
| `nodesort top MATRIX --query Q [-k K] [--metric cosine\|dot\|l2] [--ids IDS] [-o OUT.npy]` | Ranks the rows of an N×D embedding matrix by similarity to a query. |
| `nodesort demo [--max N] [--type T]` | Races `qsort` and nodesort on random data of growing size, verifying every result. |

- **Files:** NumPy `.npy` (`float32`, `float64`, `int32`, `uint32`; IDs also `uint64`) or raw
  little-endian binaries named `.f32 .f64 .i32 .u32` (or `--type`). Raw matrices need `--dim D`.
  CSV is not supported.
- **`top`:**
  - Scores are computed in double precision on all cores and ranked with the stable key-value sort,
    so equal scores keep row order. Rows with an undefined score (for example a zero vector under
    cosine) go last.
  - Rows are identified by their row number, or by your own IDs with `--ids ids.npy`.
  - `-o` writes a structured `.npy`: `r = np.load("top.npy"); r["id"], r["score"]`.
- **Exit codes:** 0 ok, 1 usage error, 2 file or data error, 3 sort error. `nodesort <command>
  --help` lists every option.

### Mahalanobis distance via whitening

`top` has no Mahalanobis metric, but L2 on *whitened* vectors is exactly the Mahalanobis distance.
Whiten once with NumPy, then rank with `--metric l2`:

```python
import numpy as np
X = np.load("embeddings.npy").astype(np.float64)
S = np.cov(X, rowvar=False) + 1e-6 * np.eye(X.shape[1])   # regularize: embeddings are often near-singular
L = np.linalg.cholesky(np.linalg.inv(S))                   # S⁻¹ = L Lᵀ
np.save("emb_white.npy", (X @ L).astype(np.float32))
np.save("q_white.npy", (np.load("q.npy").astype(np.float64) @ L).astype(np.float32))
```

```console
$ nodesort top emb_white.npy --query q_white.npy --metric l2 -k 10
```

The covariance step costs O(N·D²) once; every query afterwards costs the same as cosine.

## API

```c
nodesort_status nodesort_i32(int32_t  *data, size_t n, const nodesort_options *opt);
nodesort_status nodesort_u32(uint32_t *data, size_t n, const nodesort_options *opt);
nodesort_status nodesort_f32(float    *data, size_t n, const nodesort_options *opt);
nodesort_status nodesort_f64(double   *data, size_t n, const nodesort_options *opt);

/* Key-value: `values` is a parallel array of n elements of value_size (4 or 8) bytes. */
nodesort_status nodesort_kv_u32(uint32_t *keys, void *values, size_t value_size, size_t n,
                                const nodesort_options *opt);
nodesort_status nodesort_kv_f32(float    *keys, void *values, size_t value_size, size_t n,
                                const nodesort_options *opt);
nodesort_status nodesort_kv_u64(uint64_t *keys, void *values, size_t value_size, size_t n,
                                const nodesort_options *opt);

void        nodesort_options_init(nodesort_options *opt);
const char *nodesort_strerror(nodesort_status status);
const char *nodesort_version(void);
```

| Status | Meaning |
|---|---|
| `NODESORT_OK` | Sorted. |
| `NODESORT_EINVAL` | `NULL` array with `n > 0`, options not initialised, `value_size` not 4/8, or `values` not aligned to `value_size`. |
| `NODESORT_ENOMEM` | No memory for the temporary buffers (n keys, plus n values for key-value sorts). |
| `NODESORT_ETHREAD` | The OS refused to create worker threads. |

Options (always start from `nodesort_options_init`):

```c
nodesort_options opt;
nodesort_options_init(&opt);
opt.threads = 4;               /* 0 = all available cores (affinity-aware on Linux) */
opt.seq_threshold = 1 << 16;   /* below this many elements, sort on the calling thread */
opt.descending = 1;            /* largest first (since 0.2.0) */
nodesort_i32(data, n, &opt);
```

### Floats

`nodesort_f32` and `nodesort_f64` sort by numeric value, with `-0.0` before `+0.0`. **NaNs always go last** — in
ascending *and* descending order — and keep their input order, so a top-k of similarity scores
never starts with a NaN:

```c
float scores[] = {0.12f, NAN, 0.98f, -0.40f};
nodesort_options opt;
nodesort_options_init(&opt);
opt.descending = 1;
nodesort_f32(scores, 4, &opt);   /* 0.98, 0.12, -0.40, NaN */
```

### Key-value

The keys are sorted and the **same permutation is applied to `values`**: any 4- or 8-byte payload
(`uint32_t`, `float`, `uint64_t`, `double`, pointers). The sort is **stable**: values whose keys are
equal keep their input order, also in descending order. Keys follow the rules of the matching
single-array function (`kv_f32` keys: NaNs last).

```c
/* Top-k document IDs by similarity score */
float    score[N];   /* computed by you      */
uint32_t doc_id[N];  /* 0..N-1 or your own IDs */
nodesort_options opt;
nodesort_options_init(&opt);
opt.descending = 1;
nodesort_kv_f32(score, doc_id, sizeof doc_id[0], N, &opt);   /* doc_id[0..k) = best k */

/* Quantum state indices (> 32 qubits) with their amplitudes */
uint64_t basis[M];
double   amplitude[M];
nodesort_kv_u64(basis, amplitude, sizeof amplitude[0], M, NULL);
```

Memory: temporary buffers of `n` keys (and `n` values for key-value sorts) are allocated per call. Threads are created per call
(no global pool); concurrent calls on distinct arrays are safe.

## Performance

`nodesort_bench --dist all --n 1000000,10000000 --reps 3` (and `--type f32|f64|kv-*`) on a 4-core
laptop (gcc 13, `-O3`). Key-value rows compare against `qsort` on `{key, value}` structs:

| n | distribution | qsort | nodesort (1 thread) | nodesort (4 threads) |
|---|---|---:|---:|---:|
| 1M | uniform | 190 ms | 12 ms | 10 ms (18×) |
| 10M | uniform | 2221 ms | 168 ms | 120 ms (18×) |
| 10M | few distinct | 1345 ms | 121 ms | 73 ms (18×) |
| 10M | skewed | 1447 ms | 199 ms | 148 ms (10×) |
| 10M | sorted | 721 ms | 14 ms | 15 ms (47×) |
| 10M | uniform `float` | 2558 ms | 199 ms | 157 ms (16×) |
| 10M | uniform `double` | 3314 ms | 514 ms | 388 ms (8.5×) |
| 10M | `float` score + `uint32_t` ID | 3316 ms | 494 ms | 290 ms (11×) |
| 10M | `uint64_t` key + `uint64_t` value | 3484 ms | 1066 ms | 569 ms (6×) |

Run it on your own hardware — results are verified against `qsort` on every run:

```sh
./build/bench/nodesort_bench --help
```

## Building

Requires CMake ≥ 3.16 and a C11 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release          # add -DBUILD_SHARED_LIBS=ON for a shared lib
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --prefix /usr/local
```

| Option | Default | |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Shared instead of static library. |
| `NODESORT_BUILD_TESTS` / `_BENCH` / `_EXAMPLES` | `ON` (top-level) | Extra targets. |
| `NODESORT_BUILD_FUZZ` | `OFF` | libFuzzer harness (Clang). |
| `NODESORT_SANITIZE` | empty | e.g. `address,undefined` or `thread`. |
| `NODESORT_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX`. |

Packages: `cd build && cpack` produces `.tar.gz`, `.deb` and (with `rpmbuild`) `.rpm` on Linux,
`.zip` on Windows.

## Using it from your project

**CMake** (installed):

```cmake
find_package(nodesort 0.3 REQUIRED)
target_link_libraries(app PRIVATE nodesort::nodesort)
```

**CMake** (vendored): `add_subdirectory(nodesort)` and link `nodesort::nodesort`.

**pkg-config**:

```sh
cc app.c $(pkg-config --cflags --libs nodesort)
```

When linking the static library without CMake or pkg-config, define `NODESORT_STATIC`.

## History

NodeSort started as a *node-based* bucket sort: split the known value range `[min, max]` into
nodes, let each element classify itself into its node in O(1), then sort the nodes in parallel.
That prototype (versions v1/v2 and a text-sorting variant) is kept in [`legacy/`](legacy/).

The library keeps the core idea — classify by value instead of comparing elements — but in the
form of a radix sort, where the "nodes" are the 256 buckets of each 8-bit digit. This fixes the two
problems of fixed range nodes: skewed data no longer piles up on a single thread, and there is no
`max - min` arithmetic to overflow or divide by zero.

## Versioning

[Semantic Versioning](https://semver.org). While the version is `0.x`, a minor release may change
the API; the shared library's SONAME carries `0.<minor>`. Changes are listed in
[CHANGELOG.md](CHANGELOG.md).

## License

[MIT](LICENSE)
