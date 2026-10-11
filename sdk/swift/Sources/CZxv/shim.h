/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* The one C header the Swift SDK sees. An installed zxv_api.h (on the
 * include path, or inside the xcframework) wins; inside the repository the
 * header is taken from sdk/libzxv so there is exactly one copy of it. */
#ifndef CZXV_SHIM_H
#define CZXV_SHIM_H
#if __has_include(<zxv_api.h>)
#include <zxv_api.h>
#else
#include "../../../libzxv/include/zxv_api.h"
#endif
#endif
