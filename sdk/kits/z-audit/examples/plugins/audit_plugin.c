/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* audit_plugin.c — a small plugin for the z-audit demo (see
 * examples/audit_plugin.zc). Two of its exports are deliberately wrong
 * for their contract, so `z-audit` fails it and says why.
 *
 * Build (the kit Makefile does this):
 *   python3 -m ziglang cc --target=wasm32-freestanding -O2 -nostdlib \
 *     -mno-bulk-memory -Wl,--no-entry -Wl,--max-memory=131072 ... */
#include "zxv_plugin.h"

/* 2 for a pass mark, 1 otherwise: straight-line code (a compare and a select). */
ZXV_EXPORT("level") int32_t level(int32_t pct) { return pct >= 50 ? 2 : 1; }

/* The same shape, but its contract promises 0..1. */
ZXV_EXPORT("bucket") int32_t bucket(int32_t pct) { return pct >= 90 ? 2 : 1; }

/* Proposes 3/2, which its contract does not allow. */
ZXV_EXPORT("half") int32_t half(void) {
    static const char label[] = "half";
    return zxv_propose(1, 3, 2, label, 4);
}

/* A loop: outside what the bounded analysis looks at. */
ZXV_EXPORT("hash") int64_t hash(int64_t n) {
    uint64_t h = 1469598103934665603ull;
    for (int64_t i = 0; i < n; i++) {
        h ^= (uint64_t) i;
        h *= 1099511628211ull;
    }
    return (int64_t) h;
}
