cmake_minimum_required(VERSION 3.16)
project(sva_gen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(svagen STATIC
  src/lexer.cpp
  src/verilog_ast.cpp
  src/verilog_parser.cpp
  src/assertion.cpp
  src/graph.cpp
  src/reachability.cpp
  src/methods.cpp
  src/left_edge.cpp)
target_include_directories(svagen PUBLIC src)
if(NOT MSVC)
  target_compile_options(svagen PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(sva_gen src/main.cpp)
target_link_libraries(sva_gen PRIVATE svagen)

option(SVA_GEN_BUILD_TESTS "Build the unit tests" ON)
if(SVA_GEN_BUILD_TESTS)
  # Use an installed GoogleTest when there is one, otherwise download it.
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
    FetchContent_MakeAvailable(googletest)
  endif()

  enable_testing()
  add_executable(sva_gen_tests
    tests/test_parser.cpp
    tests/test_graph.cpp
    tests/test_generator.cpp)
  target_link_libraries(sva_gen_tests PRIVATE svagen GTest::gtest_main)
  target_compile_definitions(sva_gen_tests PRIVATE SVA_GEN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  include(GoogleTest)
  gtest_discover_tests(sva_gen_tests)
endif()
