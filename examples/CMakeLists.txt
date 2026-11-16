add_executable(nodesort_example_basic basic.c)
nodesort_configure_target(nodesort_example_basic)
target_link_libraries(nodesort_example_basic PRIVATE nodesort::nodesort)

if(WIN32 AND BUILD_SHARED_LIBS)
  add_custom_command(TARGET nodesort_example_basic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:nodesort> $<TARGET_FILE_DIR:nodesort_example_basic>)
endif()

if(NODESORT_BUILD_TESTS)
  add_test(NAME example_basic COMMAND nodesort_example_basic)
  set_tests_properties(example_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "-2147483648 -7 -1 0 13 13 42 2147483647")
endif()
