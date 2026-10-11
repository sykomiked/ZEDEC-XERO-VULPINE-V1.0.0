# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/engines/godot-zxv — a Godot 4 GDExtension over libzxv and emu-state.
#
#   cmake -S . -B build                          core library + core tests,
#                                                and the GDExtension (fetches godot-cpp)
#   cmake -S . -B build -DZXV_GODOT_EXTENSION=OFF   core + tests only, no download
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure
#
# The extension is written to demo/bin/ (git-ignored), where demo/zxv.gdextension
# looks for it, next to a copy of libzxv.
#
# libzxv: -DZXV_LIBRARY=/path/libzxv.so -DZXV_INCLUDE_DIR=/path/include, else
# the in-repo build sdk/libzxv/build (make -C sdk/libzxv).
cmake_minimum_required(VERSION 3.20)
project(godot_zxv LANGUAGES C CXX)

option(ZXV_GODOT_EXTENSION "Fetch godot-cpp and build the GDExtension" ON)
option(ZXV_GODOT_TESTS "Build the core tests (no Godot needed)" ON)
option(ZXV_SANITIZE "Build the core and its tests with ASan + UBSan" OFF)

# godot-cpp is fetched at configure time, never vendored. The tag is pinned
# AND checked against its commit, so a moved tag fails the build.
set(ZXV_GODOT_CPP_TAG "godot-4.3-stable" CACHE STRING "godot-cpp tag")
set(ZXV_GODOT_CPP_COMMIT "fbbf9ec4efd8f1055d00edb8d926eef8ba4c2cce" CACHE STRING "commit of that tag")

set(ZXV_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(ZXV_EMU_DIR "${ZXV_SDK_DIR}/engines/emu-state")

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
add_library(zxv_gd_core STATIC src/core/zxv_gd_core.cpp)
target_include_directories(zxv_gd_core PUBLIC src/core)
target_link_libraries(zxv_gd_core PUBLIC zxv_emu)
target_compile_features(zxv_gd_core PUBLIC cxx_std_17)
set_target_properties(zxv_gd_core PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # -fno-exceptions: Godot builds extensions without exceptions; prove the
  # core never needs them.
  target_compile_options(zxv_gd_core PRIVATE -Wall -Wextra -Werror -Wshadow -fno-exceptions ${ZXV_SAN_FLAGS})
endif()

if(ZXV_GODOT_TESTS)
  enable_testing()
  add_executable(test_core test/test_core.cpp)
  target_link_libraries(test_core PRIVATE zxv_gd_core)
  get_filename_component(ZXV_LIB_DIR "${ZXV_LIBRARY}" DIRECTORY)
  set_target_properties(test_core PROPERTIES BUILD_RPATH "${ZXV_LIB_DIR}")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_core PRIVATE -Wall -Wextra -Werror ${ZXV_SAN_FLAGS})
    target_link_options(test_core PRIVATE ${ZXV_SAN_FLAGS})
  endif()
  add_test(NAME test_core COMMAND test_core)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME check_nofloat_core
             COMMAND "${Python3_EXECUTABLE}" "${ZXV_SDK_DIR}/kits/z-wasm/tools/check_nofloat.py"
                     "${CMAKE_CURRENT_SOURCE_DIR}/src/core/zxv_gd_core.hpp"
                     "${CMAKE_CURRENT_SOURCE_DIR}/src/core/zxv_gd_core.cpp"
                     "${CMAKE_CURRENT_SOURCE_DIR}/test/test_core.cpp")
  endif()
endif()

# ---- the GDExtension --------------------------------------------------------
if(ZXV_GODOT_EXTENSION)
  include(FetchContent)
  FetchContent_Declare(godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG        ${ZXV_GODOT_CPP_TAG}
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(godot-cpp)
  execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${godot-cpp_SOURCE_DIR}"
                  OUTPUT_VARIABLE ZXV_GOT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT ZXV_GOT_COMMIT STREQUAL ZXV_GODOT_CPP_COMMIT)
    message(FATAL_ERROR "godot-cpp ${ZXV_GODOT_CPP_TAG} is ${ZXV_GOT_COMMIT}, expected ${ZXV_GODOT_CPP_COMMIT}")
  endif()

  add_library(zxv_godot SHARED src/godot/zxv_classes.cpp src/godot/register_types.cpp)
  target_link_libraries(zxv_godot PRIVATE godot::cpp zxv_gd_core)
  set(ZXV_GODOT_BIN "${CMAKE_CURRENT_SOURCE_DIR}/demo/bin")
  set_target_properties(zxv_godot PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${ZXV_GODOT_BIN}"
    RUNTIME_OUTPUT_DIRECTORY "${ZXV_GODOT_BIN}"
    CXX_VISIBILITY_PRESET hidden)
  if(APPLE)
    set_target_properties(zxv_godot PROPERTIES INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
  elseif(UNIX)
    set_target_properties(zxv_godot PROPERTIES INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
  endif()
  # Godot loads the extension; the extension loads libzxv from beside it.
  add_custom_command(TARGET zxv_godot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ZXV_LIBRARY}" "${ZXV_GODOT_BIN}/")
endif()
