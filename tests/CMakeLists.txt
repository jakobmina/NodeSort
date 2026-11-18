# Public API tests run against the real library (static or shared).
add_executable(test_api test_api.c)
nodesort_configure_target(test_api)
target_link_libraries(test_api PRIVATE nodesort::nodesort)
add_test(NAME api COMMAND test_api)

add_executable(test_kv test_kv.c)
nodesort_configure_target(test_kv)
target_link_libraries(test_kv PRIVATE nodesort::nodesort)
add_test(NAME kv COMMAND test_kv)

# Internal tests run against a static fault-injection build of the same sources.
list(TRANSFORM NODESORT_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE _ns_abs_sources)
add_library(nodesort_fault_injection STATIC ${_ns_abs_sources})
nodesort_configure_target(nodesort_fault_injection)
target_include_directories(nodesort_fault_injection
  PUBLIC ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(nodesort_fault_injection
  PUBLIC NODESORT_STATIC NODESORT_FAULT_INJECTION)
if(WIN32)
  target_compile_definitions(nodesort_fault_injection PRIVATE _WIN32_WINNT=0x0601)
else()
  target_link_libraries(nodesort_fault_injection PUBLIC Threads::Threads)
endif()

add_executable(test_internal test_internal.c)
nodesort_configure_target(test_internal)
target_link_libraries(test_internal PRIVATE nodesort_fault_injection)
add_test(NAME internal COMMAND test_internal)

# On Windows the shared library must sit next to the test executable.
if(WIN32 AND BUILD_SHARED_LIBS)
  add_custom_command(TARGET test_api POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:nodesort> $<TARGET_FILE_DIR:test_api>)
  add_custom_command(TARGET test_kv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:nodesort> $<TARGET_FILE_DIR:test_kv>)
endif()
