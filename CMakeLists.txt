cmake_minimum_required(VERSION 3.16)
project(Raytrace CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

add_executable(raytrace src/main.cpp)
target_link_libraries(raytrace PRIVATE SDL2::SDL2 Threads::Threads)

# Core tests: ./build/raytrace_tests  (or ctest --test-dir build)
enable_testing()
add_executable(raytrace_tests tests/test_main.cpp)
target_compile_definitions(raytrace_tests PRIVATE PROJECT_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(raytrace_tests PRIVATE Threads::Threads)
add_test(NAME raytrace_tests COMMAND raytrace_tests)
