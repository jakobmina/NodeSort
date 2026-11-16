cmake_minimum_required(VERSION 3.16)

# Single source of truth for the version: include/nodesort/nodesort.h
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/nodesort/nodesort.h" _ns_version_lines
     REGEX "^#define NODESORT_VERSION_(MAJOR|MINOR|PATCH) ")
foreach(_line IN LISTS _ns_version_lines)
  if(_line MATCHES "^#define NODESORT_VERSION_(MAJOR|MINOR|PATCH) +([0-9]+)")
    set(_ns_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
  endif()
endforeach()

project(nodesort
  VERSION ${_ns_MAJOR}.${_ns_MINOR}.${_ns_PATCH}
  DESCRIPTION "Parallel in-place sorting of 32-bit integer arrays"
  HOMEPAGE_URL "https://github.com/tlacaelel666/nodesort"
  LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(NODESORT_IS_TOP_LEVEL ON)
else()
  set(NODESORT_IS_TOP_LEVEL OFF)
endif()

option(BUILD_SHARED_LIBS "Build nodesort as a shared library" OFF)
option(NODESORT_BUILD_TESTS "Build the test suite" ${NODESORT_IS_TOP_LEVEL})
option(NODESORT_BUILD_BENCH "Build the benchmark CLI" ${NODESORT_IS_TOP_LEVEL})
option(NODESORT_BUILD_EXAMPLES "Build the examples" ${NODESORT_IS_TOP_LEVEL})
option(NODESORT_BUILD_FUZZ "Build the libFuzzer harness (Clang only)" OFF)
option(NODESORT_INSTALL "Generate install and package rules" ${NODESORT_IS_TOP_LEVEL})
option(NODESORT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(NODESORT_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. 'address,undefined' or 'thread'")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
include(cmake/NodesortCompilerOptions.cmake)

if(NOT WIN32)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
endif()

# ── Library ─────────────────────────────────────────────────────────────
set(NODESORT_SOURCES src/nodesort.c src/sort.c src/thread.c)

add_library(nodesort ${NODESORT_SOURCES})
add_library(nodesort::nodesort ALIAS nodesort)
nodesort_configure_target(nodesort)

target_include_directories(nodesort
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(nodesort PRIVATE NODESORT_BUILDING)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(nodesort PUBLIC NODESORT_STATIC)
endif()
if(WIN32)
  target_compile_definitions(nodesort PRIVATE _WIN32_WINNT=0x0601)
else()
  target_link_libraries(nodesort PRIVATE Threads::Threads)
endif()

if(${PROJECT_VERSION_MAJOR} EQUAL 0)
  set(NODESORT_SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
else()
  set(NODESORT_SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

set_target_properties(nodesort PROPERTIES
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${NODESORT_SOVERSION}
  EXPORT_NAME nodesort)

# ── Sub-projects ────────────────────────────────────────────────────────
if(NODESORT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(NODESORT_BUILD_BENCH)
  add_subdirectory(bench)
endif()
if(NODESORT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(NODESORT_BUILD_FUZZ)
  add_subdirectory(fuzz)
endif()

if(NODESORT_INSTALL)
  include(cmake/NodesortInstall.cmake)
  include(cmake/NodesortPackaging.cmake)
endif()
