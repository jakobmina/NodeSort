if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "NODESORT_BUILD_FUZZ requires Clang (libFuzzer)")
endif()

add_executable(fuzz_nodesort fuzz_nodesort.c)
nodesort_configure_target(fuzz_nodesort)
target_link_libraries(fuzz_nodesort PRIVATE nodesort::nodesort)
target_compile_options(fuzz_nodesort PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_nodesort PRIVATE -fsanitize=fuzzer,address,undefined)
# The library itself must be instrumented too for coverage feedback.
target_compile_options(nodesort PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
