# Contributing to nodesort

## Development loop

```sh
cmake -S . -B build -DNODESORT_WARNINGS_AS_ERRORS=ON
cmake --build build && ctest --test-dir build --output-on-failure
```

Before opening a pull request, also run:

```sh
# Memory and undefined-behaviour checks
cmake -S . -B build-asan -DNODESORT_SANITIZE=address,undefined && cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

# Data races (on Linux, if TSan fails to start: sudo sysctl vm.mmap_rnd_bits=28)
cmake -S . -B build-tsan -DNODESORT_SANITIZE=thread && cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure

# Formatting
clang-format -i $(git ls-files '*.c' '*.h' ':!:legacy/*')
```

## Rules

- C11, no dependencies beyond the OS thread API. Code must compile warning-free with GCC, Clang
  and MSVC (`/W4`).
- Public API changes go through `include/nodesort/nodesort.h` and the CHANGELOG. Only add fields at
  the end of `nodesort_options`.
- Every behaviour change comes with tests. Performance changes come with `nodesort_bench` numbers
  before and after.
- Design decisions are recorded in `architecture.md` and `goal-and-requirements.md`, not in code
  comments.

## Releasing

1. Bump `NODESORT_VERSION_*` in `include/nodesort/nodesort.h` (the build reads the version from it).
2. Move the `Unreleased` CHANGELOG entries under the new version.
3. Tag `vX.Y.Z` and push the tag; the release workflow builds, tests and publishes packages.
