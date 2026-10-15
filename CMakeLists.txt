cmake_minimum_required(VERSION 3.24)
project(Raytrace CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

# Dear ImGui (MIT licence), downloaded at configure time from a pinned release; the archive is verified against
# a fixed SHA-256 (taken from the first download of this release). Only the core and the SDL2 / SDL_Renderer
# backends are built.
include(FetchContent)
FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.tar.gz
  URL_HASH SHA256=3872a5f90df78fced023c1945f4466b654fd74573370b77b17742149763a7a7c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui_sdl STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(imgui_sdl SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_sdl PUBLIC SDL2::SDL2)

add_executable(raytrace src/main.cpp src/gui.cpp)
target_link_libraries(raytrace PRIVATE imgui_sdl SDL2::SDL2 Threads::Threads)

# Core tests: ./build/raytrace_tests  (or ctest --test-dir build)
enable_testing()
add_executable(raytrace_tests tests/test_main.cpp)
target_compile_definitions(raytrace_tests PRIVATE PROJECT_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(raytrace_tests PRIVATE Threads::Threads)
add_test(NAME raytrace_tests COMMAND raytrace_tests)
