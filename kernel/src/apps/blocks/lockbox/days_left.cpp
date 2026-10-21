// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Days left before a key expires (lockbox_rules.h LR2), in C++ compiled to
// WebAssembly (build: tools/apps/build_wasm_blocks.sh): floor((expires -
// now) / 86400), never below 0. Both are seconds since 1970, 0 to the last
// second of year 9999; anything else traps, which the runtime turns into a
// refusal (zxapp_wasm.c W3), never an answer. Arithmetic only, no select:
// Z-Wasm's validator refuses wasm's untyped select.
extern "C" long long run(long long expires, long long now)
{
    if (expires < 0) __builtin_trap();
    if (expires > 253402300799ll) __builtin_trap();
    if (now < 0) __builtin_trap();
    if (now > 253402300799ll) __builtin_trap();
    long long gap = expires - now;
    long long ahead = (long long) (gap > 0);
    return ahead * gap / 86400;
}
