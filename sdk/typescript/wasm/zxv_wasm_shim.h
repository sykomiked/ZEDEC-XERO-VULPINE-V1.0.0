/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_wasm_shim.h — force-included (-include) into every C file of the
 * @zedec/zxv WebAssembly build. libzxv's zxv_random has no WASI branch, so
 * the build defines ZXV_HAVE_ARC4RANDOM and this header declares the
 * arc4random_buf that zxv_wasm_shim.c implements over WASI random_get
 * (crypto.getRandomValues in the JS host). */
#ifndef ZXV_WASM_SHIM_H
#define ZXV_WASM_SHIM_H
#include <stddef.h>
void arc4random_buf(void *buf, size_t len);
#endif
