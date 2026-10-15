// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Is this use allowed (gatehouse_rules.h GR3), as a number to count with,
// in C++ compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh):
// 1 when the app asked for the capability, the owner granted it and has
// not revoked it, else 0. Each input is a flag, 0 or 1 (GR2); anything
// else is no flag and the module traps, which the runtime turns into a
// refusal (zxapp_wasm.c W3), never an answer. Arithmetic only: Z-Wasm's
// validator refuses wasm's untyped select, which a conditional would
// compile to.
extern "C" long long run(long long asked, long long granted, long long revoked)
{
    if ((unsigned long long) asked > 1ull) __builtin_trap();
    if ((unsigned long long) granted > 1ull) __builtin_trap();
    if ((unsigned long long) revoked > 1ull) __builtin_trap();
    return asked * granted * (1 - revoked);
}
