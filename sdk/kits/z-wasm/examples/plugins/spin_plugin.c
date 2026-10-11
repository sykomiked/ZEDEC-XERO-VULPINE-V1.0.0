/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* spin_plugin.c — misbehaving plugins for the sandbox tests.
 *
 *   spin(x)          never returns: stopped by the fuel meter (ZW_E_FUEL)
 *   propose_trap()   proposes a value, then traps: the proposal is
 *                    discarded and the plugin is reported DEAD (ZW_E_TRAP)
 *   poke(addr)       stores to an address: out of bounds traps (ZW_E_TRAP)
 *   recurse(n)       unbounded recursion: a stack-overflow trap or fuel */
#include "zxv_plugin.h"

ZXV_EXPORT("spin") int32_t spin(int32_t x) {
    volatile int32_t acc = 0;
    for (;;) acc += x;
}

ZXV_EXPORT("propose_trap") int32_t propose_trap(void) {
    static const char label[] = "never accepted";
    (void) zxv_propose(9, 1, 2, label, zxv_strlen(label));
    __builtin_trap();
}

ZXV_EXPORT("poke") int32_t poke(int32_t addr) {
    volatile int32_t *p = (volatile int32_t *) addr;
    *p = 42;
    return *p;
}

ZXV_EXPORT("recurse") int64_t recurse(int64_t n) {
    volatile int64_t pad[8];
    pad[0] = n;
    return recurse(pad[0] + 1) + pad[0];
}
