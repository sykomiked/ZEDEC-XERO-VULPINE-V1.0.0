// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Is this path open (hedgerow_rules.h HR3), as a number to count with, in
// C++ compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh): 1
// when the switch that exposes the path is on and both rooms (mode - far,
// bind - far) are 0..2, else 0. The switch is a flag, 0 or 1, and a room is
// -2..2 for values on the ladders (HR1, HR2); anything else is no input and
// the module traps, which the runtime turns into a refusal (zxapp_wasm.c
// W3), never an answer. Arithmetic only: Z-Wasm's validator refuses wasm's
// untyped select, which a conditional would compile to.

// 1 when a room x of -2..2 is 0..2, else 0
static long long within(long long x)
{
    return (x + 3) / 3;
}

extern "C" long long run(long long sw, long long moderoom, long long bindroom)
{
    if ((unsigned long long) sw > 1ull) __builtin_trap();
    if ((unsigned long long) (moderoom + 2) > 4ull) __builtin_trap();
    if ((unsigned long long) (bindroom + 2) > 4ull) __builtin_trap();
    return sw * within(moderoom) * within(bindroom);
}
