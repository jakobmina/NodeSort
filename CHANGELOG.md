# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0]

### Added
- `nodesort_i32` / `nodesort_u32`: parallel in-place LSD radix sort with sequential fallback,
  insertion sort for tiny inputs and fast paths for already sorted / reversed input.
- `nodesort_options` (thread count, sequential threshold), `nodesort_strerror`,
  `nodesort_version`.
- Status codes instead of crashes; the array is left untouched on any failure.
- Portable threading layer (POSIX threads, Win32) for Linux, macOS, Windows MSVC and MinGW.
- CMake package (`nodesort::nodesort`), pkg-config file, CPack `.deb`/`.rpm`/archives.
- Test suite (property tests against `qsort`, fault injection, concurrency), benchmark CLI,
  example, libFuzzer harness, CI and release workflows.

### Changed
- Replaces the original prototype (range-bucket sort, variants v1/v2), which crashed on empty input,
  on arrays with all-equal values and on value ranges that overflow `int`. The prototype and the
  text-sorting variant are archived, unbuilt, in `legacy/`.
