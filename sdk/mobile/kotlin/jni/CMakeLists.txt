# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# libzxvjni: the JNI shim for the Kotlin SDK (zxv_jni.c + zxv_jni15.c + zxv_jni16.c), linked against a
# prebuilt libzxv (built by sdk/libzxv's Makefile, never recompiled here).
#
#   Host (JVM tests):
#     cmake -S jni -B build/jni -DZXV_LIB_DIR=$PWD/../../libzxv/build && cmake --build build/jni
#   Android: the :zxv-android module passes ZXV_ANDROID_LIBS_ROOT through
#     externalNativeBuild and this file picks <root>/<ANDROID_ABI>/libzxv.so,
#     built by scripts/build-libzxv-android.sh.
cmake_minimum_required(VERSION 3.18)
project(zxvjni C)

set(ZXV_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../libzxv/include" CACHE PATH "libzxv headers")
set(ZXV_LIB_DIR "" CACHE PATH "directory holding libzxv.so / libzxv.dylib for this target")
set(ZXV_ANDROID_LIBS_ROOT "" CACHE PATH "Android: directory holding <abi>/libzxv.so")
if(ANDROID AND ZXV_ANDROID_LIBS_ROOT)
  set(ZXV_LIB_DIR "${ZXV_ANDROID_LIBS_ROOT}/${ANDROID_ABI}")
endif()
if(NOT ZXV_LIB_DIR)
  message(FATAL_ERROR "set ZXV_LIB_DIR to the directory holding the prebuilt libzxv for this target")
endif()

add_library(zxv SHARED IMPORTED)
if(APPLE)
  set(_zxv_file "${ZXV_LIB_DIR}/libzxv.dylib")
else()
  set(_zxv_file "${ZXV_LIB_DIR}/libzxv.so")
endif()
set_target_properties(zxv PROPERTIES IMPORTED_LOCATION "${_zxv_file}"
                                     INTERFACE_INCLUDE_DIRECTORIES "${ZXV_INCLUDE_DIR}")
if(APPLE)
  # libzxv's install name is @rpath/libzxv.dylib.
  set_target_properties(zxv PROPERTIES IMPORTED_SONAME "@rpath/libzxv.dylib")
endif()

add_library(zxvjni SHARED zxv_jni.c zxv_jni15.c zxv_jni16.c)
target_link_libraries(zxvjni PRIVATE zxv)
set_target_properties(zxvjni PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_VISIBILITY_PRESET default)
target_compile_options(zxvjni PRIVATE -Wall -Wextra -Werror -O2 -fstack-protector-strong)

if(ANDROID)
  # jni.h comes from the NDK sysroot. 16 KB pages (Android 15+ devices).
  target_link_options(zxvjni PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,-z,defs")
else()
  find_package(JNI REQUIRED COMPONENTS JVM)
  target_include_directories(zxvjni PRIVATE ${JNI_INCLUDE_DIRS})
  if(APPLE)
    set_target_properties(zxvjni PROPERTIES BUILD_RPATH "@loader_path;${ZXV_LIB_DIR}" INSTALL_RPATH "@loader_path")
    # JNI symbols resolve from the loading JVM, never from libjvm at link time.
    target_link_options(zxvjni PRIVATE "-undefined" "dynamic_lookup")
  else()
    set_target_properties(zxvjni PROPERTIES BUILD_RPATH "\$ORIGIN;${ZXV_LIB_DIR}" INSTALL_RPATH "\$ORIGIN")
  endif()
endif()
