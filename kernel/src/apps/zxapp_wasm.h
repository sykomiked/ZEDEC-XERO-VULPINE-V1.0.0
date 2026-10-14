/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_wasm.h — register the WebAssembly runtime (sdk/kits/z-wasm) with a
 * manifest host, so `logic wasm ...` entries run: logic written in any
 * language that compiles to WebAssembly (hosted builds only). */
#ifndef ZXAPP_WASM_H
#define ZXAPP_WASM_H

#include "zxapp.h"

/* What the runtime owns: a libzxv context, the capability its fuel budget
 * is charged under, and the Z-Wasm host. Opaque to the caller. */
typedef struct {
    void *ctx, *root, *budget_cap, *rmag, *host;
    uint32_t budget;
} zxapp_wasm_t;

/* fuel: the instructions all of this app's Wasm logic may run (every call
 * is charged; 0 takes a default of 10^8). */
int zxapp_wasm_register(zxapp_host_t *h, zxapp_wasm_t *w, uint64_t fuel);
/* After zxapp_unload of every app that used it. */
void zxapp_wasm_close(zxapp_wasm_t *w);

#endif /* ZXAPP_WASM_H */
