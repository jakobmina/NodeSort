# End-to-end tests of the `nodesort` command-line tool: generated files go through the CLI and the
# results are checked by cli_check (library for sort, independent brute force for top-k).
add_executable(cli_check cli_check.c ${PROJECT_SOURCE_DIR}/tools/cli/array_io.c)
nodesort_configure_target(cli_check)
target_include_directories(cli_check PRIVATE ${PROJECT_SOURCE_DIR}/tools/cli)
target_link_libraries(cli_check PRIVATE nodesort::nodesort)
if(NOT WIN32)
  target_link_libraries(cli_check PRIVATE m)
endif()
if(WIN32 AND BUILD_SHARED_LIBS)
  add_custom_command(TARGET cli_check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:nodesort> $<TARGET_FILE_DIR:cli_check>)
endif()

set(D ${CMAKE_CURRENT_BINARY_DIR}/data)
set(CLI $<TARGET_FILE:nodesort_cli>)
set(CHECK $<TARGET_FILE:cli_check>)

add_test(NAME cli_gen COMMAND ${CMAKE_COMMAND} -E make_directory ${D})
add_test(NAME cli_gen_files COMMAND ${CHECK} gen ${D})
set_tests_properties(cli_gen PROPERTIES FIXTURES_SETUP cli_dir)
set_tests_properties(cli_gen_files PROPERTIES FIXTURES_SETUP cli_files FIXTURES_REQUIRED cli_dir)

# nodesort_cli_case(NAME <name> RUN <cli args...> [CHECK <cli_check args...>] [EXPECT <regex>])
function(nodesort_cli_case)
  cmake_parse_arguments(C "" "NAME;EXPECT" "RUN;CHECK" ${ARGN})
  add_test(NAME cli_${C_NAME} COMMAND ${CLI} ${C_RUN} WORKING_DIRECTORY ${D})
  set_tests_properties(cli_${C_NAME} PROPERTIES FIXTURES_REQUIRED cli_files)
  if(C_EXPECT)
    set_tests_properties(cli_${C_NAME} PROPERTIES PASS_REGULAR_EXPRESSION "${C_EXPECT}")
  endif()
  if(C_CHECK)
    set_tests_properties(cli_${C_NAME} PROPERTIES FIXTURES_SETUP cli_${C_NAME}_out)
    add_test(NAME cli_${C_NAME}_check COMMAND ${CHECK} ${C_CHECK} WORKING_DIRECTORY ${D})
    set_tests_properties(cli_${C_NAME}_check PROPERTIES FIXTURES_REQUIRED "cli_files;cli_${C_NAME}_out")
  endif()
endfunction()

# sort
nodesort_cli_case(NAME sort_f32 RUN sort s_f32.npy -o o_f32.npy CHECK check-sort s_f32.npy o_f32.npy asc)
nodesort_cli_case(NAME sort_f32_desc RUN sort s_f32.npy --desc -o o_f32d.npy CHECK check-sort s_f32.npy o_f32d.npy desc)
nodesort_cli_case(NAME sort_f64 RUN sort s_f64.npy -o o_f64.npy --threads 2 CHECK check-sort s_f64.npy o_f64.npy asc)
nodesort_cli_case(NAME sort_i32 RUN sort s_i32.npy -o o_i32.npy CHECK check-sort s_i32.npy o_i32.npy asc)
nodesort_cli_case(NAME sort_u32_desc RUN sort s_u32.npy --desc -o o_u32.npy CHECK check-sort s_u32.npy o_u32.npy desc)
nodesort_cli_case(NAME sort_raw RUN sort s_raw.f32 -o o_raw.f32 CHECK check-sort s_raw.f32 o_raw.f32 asc)
nodesort_cli_case(NAME sort_inplace RUN sort s_f32_inplace.npy -o s_f32_inplace.npy CHECK check-sort s_f32.npy s_f32_inplace.npy asc)
nodesort_cli_case(NAME sort_summary RUN sort s_f32.npy EXPECT "sorted 5,000 float32 values.*no file written")

# top
nodesort_cli_case(NAME top_cosine RUN top emb.npy --query q.npy -k 25 -o t_cos.npy CHECK check-top emb.npy q.npy t_cos.npy cosine 25)
nodesort_cli_case(NAME top_dot RUN top emb.npy -q q.npy -k 25 --metric dot -o t_dot.npy CHECK check-top emb.npy q.npy t_dot.npy dot 25)
nodesort_cli_case(NAME top_l2_raw_ids RUN top emb.f32 --dim 16 -q q.f32 -k 25 --metric l2 --ids ids.npy -o t_l2.npy CHECK check-top emb.npy q.npy t_l2.npy l2 25 ids.npy)
nodesort_cli_case(NAME top_all_rows RUN top emb.npy -q q.npy -k 3000 -o t_all.npy CHECK check-top emb.npy q.npy t_all.npy cosine 3000)
nodesort_cli_case(NAME top_table RUN top emb.npy -q q.npy -k 3 EXPECT "rank +id +score.*\n +1 +50 ")

# errors
nodesort_cli_case(NAME err_dtype RUN sort bad_f16.npy EXPECT "dtype '<f2' not supported")
nodesort_cli_case(NAME err_big_endian RUN sort bad_be.npy EXPECT "big-endian dtype")
nodesort_cli_case(NAME err_3d RUN sort bad_3d.npy EXPECT "more than 2 dimensions")
nodesort_cli_case(NAME err_truncated RUN sort bad_trunc.npy EXPECT "truncated")
nodesort_cli_case(NAME err_matrix_for_sort RUN sort emb.npy EXPECT "expects a 1-D array")
nodesort_cli_case(NAME err_dim_mismatch RUN top emb.npy -q q_short.npy EXPECT "query has 5 values")
nodesort_cli_case(NAME err_zero_query RUN top emb.npy -q q_zero.npy EXPECT "zero length")
nodesort_cli_case(NAME err_raw_without_dim RUN top emb.f32 -q q.f32 EXPECT "need --dim")
nodesort_cli_case(NAME err_unknown_command RUN frobnicate EXPECT "unknown command")

# demo and version
nodesort_cli_case(NAME demo RUN demo --max 100k --type f32 EXPECT "100,000 .* ok.*best: ")
nodesort_cli_case(NAME version RUN --version EXPECT "nodesort ${PROJECT_VERSION}")
