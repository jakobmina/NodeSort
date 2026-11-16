# NodeSort

[![CI](https://github.com/jakobmina/NodeSort/actions/workflows/ci.yml/badge.svg)](https://github.com/jakobmina/NodeSort/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**Parallel, in-place sorting of 32-bit integer arrays for C.** One call, every core, no
dependencies — typically 10–20× faster than `qsort` on large arrays.

```c
#include <nodesort/nodesort.h>

int32_t values[] = {42, -7, 13, 0};
nodesort_status st = nodesort_i32(values, 4, NULL);   /* NULL = use all cores */
if (st != NODESORT_OK) fprintf(stderr, "%s\n", nodesort_strerror(st));
```

- **Fast** — parallel LSD radix sort (4 × 8-bit passes), O(n), load-balanced on any distribution;
  already sorted / reversed input is detected in a single read.
- **Safe to embed** — reentrant, no global state, never prints or aborts. Every failure is a status
  code and **leaves the array exactly as it was**.
- **Portable** — C11 on Linux, macOS and Windows (MSVC and MinGW); POSIX threads or Win32.
- **Easy to consume** — CMake `find_package`, `pkg-config`, `.deb`/`.rpm`, static or shared.

## API

```c
nodesort_status nodesort_i32(int32_t  *data, size_t n, const nodesort_options *opt);
nodesort_status nodesort_u32(uint32_t *data, size_t n, const nodesort_options *opt);

void        nodesort_options_init(nodesort_options *opt);
const char *nodesort_strerror(nodesort_status status);
const char *nodesort_version(void);
```

| Status | Meaning |
|---|---|
| `NODESORT_OK` | Sorted. |
| `NODESORT_EINVAL` | `data == NULL` with `n > 0`, or options not initialised. |
| `NODESORT_ENOMEM` | No memory for the temporary buffer (n × 4 bytes). |
| `NODESORT_ETHREAD` | The OS refused to create worker threads. |

Options (always start from `nodesort_options_init`):

```c
nodesort_options opt;
nodesort_options_init(&opt);
opt.threads = 4;               /* 0 = all available cores (affinity-aware on Linux) */
opt.seq_threshold = 1 << 16;   /* below this many elements, sort on the calling thread */
nodesort_i32(data, n, &opt);
```

Memory: a temporary buffer of `n` elements is allocated per call. Threads are created per call
(no global pool); concurrent calls on distinct arrays are safe.

## Performance

`nodesort_bench --dist all --n 1000000,10000000 --reps 3` on a 4-core laptop (gcc 13, `-O3`):

| n | distribution | qsort | nodesort (1 thread) | nodesort (4 threads) |
|---|---|---:|---:|---:|
| 1M | uniform | 190 ms | 12 ms | 10 ms (18×) |
| 10M | uniform | 2221 ms | 168 ms | 120 ms (18×) |
| 10M | few distinct | 1345 ms | 121 ms | 73 ms (18×) |
| 10M | skewed | 1447 ms | 199 ms | 148 ms (10×) |
| 10M | sorted | 721 ms | 14 ms | 15 ms (47×) |

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
find_package(nodesort 0.1 REQUIRED)
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
