# Platform layer: a self-contained CMake project so it can be configured and
# tested without building src/core (which is still Windows-only).
#
#   cmake -S src/platform -B build/platform && cmake --build build/platform
#   ctest --test-dir build/platform --output-on-failure
#
# It is also safe to add_subdirectory(src/platform) from the root project once
# the kernel calls into this layer.
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vd_platform LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Exactly one backend is compiled; the other file is empty by design (its
# content sits behind VD_PLATFORM_*), but compiling it would still need the
# platform headers, so selection happens here.
if(WIN32)
  set(VD_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/win32/platform_win32.cpp")
else()
  set(VD_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/posix/platform_posix.cpp")
endif()

add_library(vd_platform STATIC ${VD_PLATFORM_SOURCES})

target_include_directories(vd_platform PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include")

if(MSVC)
  target_compile_options(vd_platform PRIVATE /W3 /utf-8)
else()
  target_compile_options(vd_platform PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(WIN32)
  target_link_libraries(vd_platform PUBLIC kernel32)
endif()

option(VD_PLATFORM_BUILD_TESTS "Build the platform self-test" ON)
if(VD_PLATFORM_BUILD_TESTS)
  add_executable(vd_platform_selftest
    "${CMAKE_CURRENT_SOURCE_DIR}/selftest/platform_selftest.cpp")
  target_link_libraries(vd_platform_selftest PRIVATE vd_platform)
  set_target_properties(vd_platform_selftest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  enable_testing()
  add_test(NAME platform_selftest COMMAND vd_platform_selftest)
endif()
