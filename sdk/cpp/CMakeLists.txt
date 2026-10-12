# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/cpp — header-only C++20 SDK over libzxv (zxv_api.h).
#
#   cmake -S sdk/cpp -B build && cmake --build build && ctest --test-dir build
#
# libzxv is found in this order (SDK_DESIGN.md section 1):
#   1. -DZXV_LIBRARY=/path/to/libzxv.so   (and optionally -DZXV_INCLUDE_DIR=...)
#   2. the ZXV_LIBRARY_PATH environment variable (a file or a directory)
#   3. the in-repo build, ../libzxv/build (the "bundled" copy)
#   4. the system linker path (find_library(zxv))
cmake_minimum_required(VERSION 3.20)
project(zxv_cpp VERSION 1.5.0 LANGUAGES CXX)

option(ZXV_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(ZXV_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(ZXV_SANITIZE "Build tests and examples with ASan + UBSan" OFF)
option(ZXV_STATIC "Link libzxv.a instead of the shared library" OFF)

set(_zxv_repo_lib "${CMAKE_CURRENT_SOURCE_DIR}/../libzxv")

if(NOT ZXV_INCLUDE_DIR)
  find_path(ZXV_INCLUDE_DIR zxv_api.h
            HINTS "$ENV{ZXV_LIBRARY_PATH}" "$ENV{ZXV_LIBRARY_PATH}/../include"
                  "${_zxv_repo_lib}/include")
endif()

if(ZXV_STATIC)
  set(_zxv_names libzxv.a zxv_static)
else()
  set(_zxv_names zxv)
endif()
if(NOT ZXV_LIBRARY AND DEFINED ENV{ZXV_LIBRARY_PATH})
  if(EXISTS "$ENV{ZXV_LIBRARY_PATH}" AND NOT IS_DIRECTORY "$ENV{ZXV_LIBRARY_PATH}")
    set(ZXV_LIBRARY "$ENV{ZXV_LIBRARY_PATH}" CACHE FILEPATH "libzxv" FORCE)
  else()
    find_library(ZXV_LIBRARY NAMES ${_zxv_names} PATHS "$ENV{ZXV_LIBRARY_PATH}" NO_DEFAULT_PATH)
  endif()
endif()
if(NOT ZXV_LIBRARY)
  find_library(ZXV_LIBRARY NAMES ${_zxv_names} PATHS "${_zxv_repo_lib}/build" NO_DEFAULT_PATH)
endif()
if(NOT ZXV_LIBRARY)
  find_library(ZXV_LIBRARY NAMES ${_zxv_names})
endif()
if(NOT ZXV_LIBRARY OR NOT ZXV_INCLUDE_DIR)
  message(FATAL_ERROR "libzxv not found: build it (make -C sdk/libzxv) or pass -DZXV_LIBRARY=... "
                      "-DZXV_INCLUDE_DIR=..., or set ZXV_LIBRARY_PATH")
endif()
message(STATUS "zxv: library ${ZXV_LIBRARY}, header ${ZXV_INCLUDE_DIR}")

find_package(Threads REQUIRED)

# The SDK target: header-only, links libzxv.
add_library(zxv INTERFACE)
add_library(zxv::zxv ALIAS zxv)
target_include_directories(zxv INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${ZXV_INCLUDE_DIR}>)
target_compile_features(zxv INTERFACE cxx_std_20)
target_link_libraries(zxv INTERFACE "${ZXV_LIBRARY}" Threads::Threads)
if(ZXV_STATIC AND UNIX)
  target_link_libraries(zxv INTERFACE m)
endif()

function(zxv_strict target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion)
    if(ZXV_SANITIZE)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all
                                               -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  endif()
  get_filename_component(_libdir "${ZXV_LIBRARY}" DIRECTORY)
  set_target_properties(${target} PROPERTIES BUILD_RPATH "${_libdir}")
endfunction()

if(ZXV_BUILD_TESTS)
  enable_testing()
  add_executable(test_zxv tests/test_zxv.cpp)
  target_link_libraries(test_zxv PRIVATE zxv::zxv)
  zxv_strict(test_zxv)
  add_test(NAME test_zxv COMMAND test_zxv "${CMAKE_CURRENT_SOURCE_DIR}/../conformance")
  # The same tests with the bundled stop_token/jthread fallback forced on
  # (what Apple's libc++ without std::jthread gets).
  add_executable(test_zxv_stop_fallback tests/test_zxv.cpp)
  target_link_libraries(test_zxv_stop_fallback PRIVATE zxv::zxv)
  target_compile_definitions(test_zxv_stop_fallback PRIVATE ZXV_FORCE_STOP_TOKEN_FALLBACK)
  zxv_strict(test_zxv_stop_fallback)
  add_test(NAME test_zxv_stop_fallback COMMAND test_zxv_stop_fallback "${CMAKE_CURRENT_SOURCE_DIR}/../conformance")
  # ABI 1.5 groups (treaty, cid, aipi, coin/token, fee, vhub, freight, web4, paradox).
  add_executable(test_zxv_abi15 tests/test_zxv_abi15.cpp)
  target_link_libraries(test_zxv_abi15 PRIVATE zxv::zxv)
  zxv_strict(test_zxv_abi15)
  add_test(NAME test_zxv_abi15 COMMAND test_zxv_abi15 "${CMAKE_CURRENT_SOURCE_DIR}/../conformance")
endif()

if(ZXV_BUILD_EXAMPLES)
  foreach(ex quickstart lockstep)
    add_executable(zxv_${ex} examples/${ex}.cpp)
    target_link_libraries(zxv_${ex} PRIVATE zxv::zxv)
    zxv_strict(zxv_${ex})
  endforeach()
  if(ZXV_BUILD_TESTS)
    add_test(NAME example_quickstart COMMAND zxv_quickstart)
    add_test(NAME example_lockstep COMMAND zxv_lockstep "${CMAKE_CURRENT_SOURCE_DIR}/../conformance/fixtures")
  endif()
endif()

include(GNUInstallDirs)
install(FILES include/zxv.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
