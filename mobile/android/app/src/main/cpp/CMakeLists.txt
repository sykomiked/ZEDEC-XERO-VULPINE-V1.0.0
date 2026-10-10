# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Builds libzxvcore_jni.so: the JNI glue linked against libzxvcore.a, which
# mobile/core/build_mobile_core.sh android-arm64 produces (the Gradle task
# buildZxvCore runs it and passes ZXV_CORE_DIR).
cmake_minimum_required(VERSION 3.22)
project(zxvcore_jni C)

if(NOT DEFINED ZXV_CORE_DIR)
  message(FATAL_ERROR "ZXV_CORE_DIR not set (run mobile/core/build_mobile_core.sh android-arm64)")
endif()

add_library(zxvcore STATIC IMPORTED)
set_target_properties(zxvcore PROPERTIES IMPORTED_LOCATION ${ZXV_CORE_DIR}/libzxvcore.a)

add_library(zxvcore_jni SHARED zxv_jni.c)
target_include_directories(zxvcore_jni PRIVATE ${ZXV_CORE_DIR}/include)
target_compile_options(zxvcore_jni PRIVATE -std=c11 -Wall -Wextra -Werror -Os)
target_link_options(zxvcore_jni PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
target_link_libraries(zxvcore_jni PRIVATE zxvcore log)
