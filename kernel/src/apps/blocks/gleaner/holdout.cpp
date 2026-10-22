// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Does this row go to the test set (gleaner_rules.h GR1), in C++ compiled
// to WebAssembly (build: tools/apps/build_wasm_blocks.sh): 1 when
// floor((row + 1) * share / 10000) is past floor(row * share / 10000),
// else 0, so every row lands in exactly one set and the first n rows hold
// floor(n * share / 10000) test rows. share is the test set's share in
// hundredths of a percent. A row outside 0 .. 999999999 or a share outside
// 0 .. 10000 is no split: the module traps, which the runtime turns into a
// refusal (zxapp_wasm.c W3), never an answer. KRON answers there, so the
// two disagree and the answer is held (BOTH). Arithmetic only: Z-Wasm's
// validator refuses wasm's untyped select, which a conditional would
// compile to.
extern "C" long long run(long long row, long long share)
{
    if ((unsigned long long) row > 999999999ull) __builtin_trap();
    if ((unsigned long long) share > 10000ull) __builtin_trap();
    return (row + 1) * share / 10000 - row * share / 10000;
}
