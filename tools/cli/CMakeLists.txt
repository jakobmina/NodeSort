# The `nodesort` command-line tool. Uses the public API, plus the library's internal thread layer
# (compiled into the executable) for parallel similarity scoring.
add_executable(nodesort_cli
  main.c
  cmd_sort.c
  cmd_demo.c
  cmd_top.c
  array_io.c
  util.c
  ${PROJECT_SOURCE_DIR}/src/thread.c)
nodesort_configure_target(nodesort_cli)
set_target_properties(nodesort_cli PROPERTIES OUTPUT_NAME nodesort)
# Find a shared libnodesort installed next to it in any prefix.
if(APPLE)
  set_target_properties(nodesort_cli PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
  set_target_properties(nodesort_cli PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
target_include_directories(nodesort_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nodesort_cli PRIVATE nodesort::nodesort)
if(WIN32)
  target_compile_definitions(nodesort_cli PRIVATE _WIN32_WINNT=0x0601)
else()
  target_link_libraries(nodesort_cli PRIVATE Threads::Threads m)
endif()

if(WIN32 AND BUILD_SHARED_LIBS)
  add_custom_command(TARGET nodesort_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:nodesort> $<TARGET_FILE_DIR:nodesort_cli>)
endif()
