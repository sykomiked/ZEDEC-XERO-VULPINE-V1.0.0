// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The next date of a repeating event (solstice_rules.h SO3), in C++
// compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh): the
// first start + k * every (k >= 0) on or after today, all in days since
// 1970-01-01. start and today are 0 .. 2932896 (1970-01-01 to 9999-12-31)
// and every is 1 .. 2932896; anything else traps, which the runtime turns
// into a refusal (zxapp_wasm.c W3), where KRON answers. Written with
// branches only: Z-Wasm's validator refuses wasm's untyped select, which a
// conditional assignment would compile to.
extern "C" long long run(long long start, long long every, long long today)
{
    if (start < 0) __builtin_trap();
    if (start > 2932896) __builtin_trap();
    if (every < 1) __builtin_trap();
    if (every > 2932896) __builtin_trap();
    if (today < 0) __builtin_trap();
    if (today > 2932896) __builtin_trap();
    if (today <= start) return start;
    return start + (today - start + every - 1) / every * every;
}
