CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
if (POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif (POLICY CMP0048)

PROJECT(group_commit_bench)
SET(CMAKE_CXX_STANDARD 14)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(group_commit_bench group_commit_bench.cc)
target_link_libraries(group_commit_bench gtest_main gflags)