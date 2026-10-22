// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// May this held suggestion be applied (sieve_rules.h SR2), as a number to
// count with, in C++ compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh): 1 when the owner accepted it (1), it
// has seconds left (above 0) and its target has not drifted (0), else 0.
// An accept that is no flag (not 0 or 1), or more seconds left than any
// hold lasts (past 86400, a day), traps, which the runtime turns into a
// refusal (zxapp_wasm.c W3), never an answer. Arithmetic only: Z-Wasm's
// validator refuses wasm's untyped select, which a conditional would
// compile to.
extern "C" long long run(long long accepted, long long secondsleft, long long drift)
{
    if ((unsigned long long) accepted > 1ull) __builtin_trap();
    if (secondsleft > 86400) __builtin_trap();
    long long live = (long long) (secondsleft > 0);
    long long still = (long long) (drift == 0);
    return accepted * live * still;
}
