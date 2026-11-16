# Out-of-tree project that consumes an *installed* nodesort, as a user would.
cmake_minimum_required(VERSION 3.16)
project(nodesort_consumer LANGUAGES C)

find_package(nodesort 0.1 REQUIRED CONFIG)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE nodesort::nodesort)

enable_testing()
add_test(NAME consumer COMMAND consumer)
