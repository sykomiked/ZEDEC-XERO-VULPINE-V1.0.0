/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* string.h for the passport's WebAssembly build (tools/web/build_web_root.sh):
 * the four memory functions the vendored PQ code and the compiler call,
 * defined in web/session/zxv_passport_wasm.c. Nothing else is declared, so a
 * source that needs more fails to build instead of linking a stand-in. */
#ifndef ZXV_WASM_STRING_H
#define ZXV_WASM_STRING_H
#include <stddef.h>
void *memcpy(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);
void *memmove(void *d, const void *s, unsigned long n);
int memcmp(const void *x, const void *y, unsigned long n);
#endif
