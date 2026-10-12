# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/engines/unreal-zxv — the engine-neutral C++ layer under the Unreal
# Engine 5 plugin, built and tested WITHOUT Unreal. The plugin itself
# (ZxvUnreal.uplugin, Source/ZxvUnreal/*.Build.cs and the UCLASS/USTRUCT
# wrappers) is built by Unreal's own build tool, not by this file.
#
#   cmake -S . -B build
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure
#   cmake -S . -B build-san -DZXV_SANITIZE=ON     the same under ASan + UBSan
#
# Targets: zxv_ue_core (the layer), test_core (its checks), test_cross_cpp
# (cross-checks against the header-only C++ SDK, sdk/cpp/include/zxv.hpp),
# the no-float check over the layer's sources and tests, and a structural
# check of the Unreal wrapper (tools/check_ue_layout.py; not a compile).
#
# libzxv: -DZXV_LIBRARY=/path/libzxv.so -DZXV_INCLUDE_DIR=/path/include, else
# the in-repo build sdk/libzxv/build (make -C sdk/libzxv).
cmake_minimum_required(VERSION 3.20)
project(unreal_zxv LANGUAGES C CXX)

option(ZXV_UNREAL_TESTS "Build the core tests (no Unreal needed)" ON)
option(ZXV_UNREAL_CROSS_CPP "Also cross-check against sdk/cpp (needs C++20)" ON)
option(ZXV_SANITIZE "Build the core and its tests with ASan + UBSan" OFF)

set(ZXV_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(ZXV_EMU_DIR "${ZXV_SDK_DIR}/engines/emu-state")
set(ZXV_UE_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source/ZxvUnreal")

# ---- libzxv -------------------------------------------------------------
if(NOT ZXV_INCLUDE_DIR)
  set(ZXV_INCLUDE_DIR "${ZXV_SDK_DIR}/libzxv/include")
endif()
if(NOT ZXV_LIBRARY)
  if(APPLE)
    set(ZXV_LIBRARY "${ZXV_SDK_DIR}/libzxv/build/libzxv.dylib")
  else()
    set(ZXV_LIBRARY "${ZXV_SDK_DIR}/libzxv/build/libzxv.so")
  endif()
endif()
if(NOT EXISTS "${ZXV_LIBRARY}")
  message(FATAL_ERROR "libzxv not found at ${ZXV_LIBRARY}: run 'make -C sdk/libzxv' or pass -DZXV_LIBRARY=")
endif()
add_library(zxv SHARED IMPORTED)
set_target_properties(zxv PROPERTIES IMPORTED_LOCATION "${ZXV_LIBRARY}"
                                     INTERFACE_INCLUDE_DIRECTORIES "${ZXV_INCLUDE_DIR}")

set(ZXV_SAN_FLAGS "")
if(ZXV_SANITIZE)
  set(ZXV_SAN_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()

# ---- emu-state (compiled in, static) -------------------------------------
add_library(zxv_emu STATIC "${ZXV_EMU_DIR}/src/zxv_emu.c")
target_include_directories(zxv_emu PUBLIC "${ZXV_EMU_DIR}/include")
target_link_libraries(zxv_emu PUBLIC zxv)
set_target_properties(zxv_emu PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_compile_options(zxv_emu PRIVATE ${ZXV_SAN_FLAGS})

# ---- the engine-neutral C++ layer ----------------------------------------
# Built as C++20 because Unreal Engine 5.4 compiles modules as C++20; the
# layer itself needs only C++17.
add_library(zxv_ue_core STATIC "${ZXV_UE_MODULE_DIR}/Private/ZxvCore/zxv_ue_core.cpp")
target_include_directories(zxv_ue_core PUBLIC "${ZXV_UE_MODULE_DIR}/Public")
target_link_libraries(zxv_ue_core PUBLIC zxv_emu)
target_compile_features(zxv_ue_core PUBLIC cxx_std_20)
set_target_properties(zxv_ue_core PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # -fno-exceptions: Unreal builds modules without exceptions by default;
  # prove the core never needs them.
  target_compile_options(zxv_ue_core PRIVATE -Wall -Wextra -Werror -Wshadow -fno-exceptions ${ZXV_SAN_FLAGS})
endif()

if(ZXV_UNREAL_TESTS)
  enable_testing()
  get_filename_component(ZXV_LIB_DIR "${ZXV_LIBRARY}" DIRECTORY)

  add_executable(test_core test/test_core.cpp)
  target_link_libraries(test_core PRIVATE zxv_ue_core)
  set_target_properties(test_core PROPERTIES BUILD_RPATH "${ZXV_LIB_DIR}" CXX_EXTENSIONS OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # The tests are built without exceptions too, like the plugin.
    target_compile_options(test_core PRIVATE -Wall -Wextra -Werror -Wshadow -fno-exceptions ${ZXV_SAN_FLAGS})
    target_link_options(test_core PRIVATE ${ZXV_SAN_FLAGS})
  endif()
  add_test(NAME test_core COMMAND test_core)

  if(ZXV_UNREAL_CROSS_CPP)
    # The header-only C++ SDK throws, so this one test is built WITH
    # exceptions; it checks that the layer and zxv.hpp agree value for value.
    add_executable(test_cross_cpp test/test_cross_cpp.cpp)
    target_link_libraries(test_cross_cpp PRIVATE zxv_ue_core)
    target_include_directories(test_cross_cpp PRIVATE "${ZXV_SDK_DIR}/cpp/include")
    set_target_properties(test_cross_cpp PROPERTIES BUILD_RPATH "${ZXV_LIB_DIR}" CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(test_cross_cpp PRIVATE -Wall -Wextra -Werror ${ZXV_SAN_FLAGS})
      target_link_options(test_cross_cpp PRIVATE ${ZXV_SAN_FLAGS})
    endif()
    add_test(NAME test_cross_cpp COMMAND test_cross_cpp)
  endif()

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME check_nofloat_core
             COMMAND "${Python3_EXECUTABLE}" "${ZXV_SDK_DIR}/kits/z-wasm/tools/check_nofloat.py"
                     "${ZXV_UE_MODULE_DIR}/Public/ZxvCore/zxv_ue_core.hpp"
                     "${ZXV_UE_MODULE_DIR}/Private/ZxvCore/zxv_ue_core.cpp"
                     "${CMAKE_CURRENT_SOURCE_DIR}/test/test_core.cpp"
                     "${CMAKE_CURRENT_SOURCE_DIR}/test/test_cross_cpp.cpp")
    # Structure of the Unreal wrapper (not a compile: no Unreal here).
    add_test(NAME check_ue_layout
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_ue_layout.py"
                     "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
