/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* stdio.h for the passport's WebAssembly build: the vendored HQC unit
 * includes it and compiles its printers out (pq_hqc5.c), so it only has to
 * exist. Nothing is declared: a call to a stdio function fails the build. */
#ifndef ZXV_WASM_STDIO_H
#define ZXV_WASM_STDIO_H
#endif
