add_executable(nodesort_bench nodesort_bench.c)
nodesort_configure_target(nodesort_bench)
target_link_libraries(nodesort_bench PRIVATE nodesort::nodesort)

if(WIN32 AND BUILD_SHARED_LIBS)
  add_custom_command(TARGET nodesort_bench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:nodesort> $<TARGET_FILE_DIR:nodesort_bench>)
endif()

if(NODESORT_BUILD_TESTS)
  add_test(NAME bench_smoke
    COMMAND nodesort_bench --n 1000,200000 --dist all --threads 1,3,0 --reps 1 --csv)
  add_test(NAME bench_smoke_f32_desc
    COMMAND nodesort_bench --n 1000,200000 --dist all --threads 1,3,0 --reps 1 --csv --type f32 --desc)
  foreach(_type f64 kv-u32 kv-f32 kv-u64)
    add_test(NAME bench_smoke_${_type}
      COMMAND nodesort_bench --n 1000,200000 --dist all --threads 1,3 --reps 1 --csv --type ${_type})
  endforeach()
  add_test(NAME bench_smoke_kv-f32_desc
    COMMAND nodesort_bench --n 200000 --dist all --threads 3 --reps 1 --csv --type kv-f32 --desc)
endif()
