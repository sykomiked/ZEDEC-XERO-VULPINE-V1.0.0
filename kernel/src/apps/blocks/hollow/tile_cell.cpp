// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// One cell of a grid along one side, in C++ compiled to WebAssembly
// (hollow_rules.h HR3): floor(span / parts) pixels, for a side of 0 ..
// 16384 pixels split into 1 .. 64 parts. Anything else traps (a refusal);
// KRON divides any span, so a side past 16384 is held (BOTH), and both
// refuse 0 parts.
extern "C" long long run(long long span, long long parts)
{
    if (span < 0) __builtin_trap();
    if (span > 16384) __builtin_trap();
    if (parts < 1) __builtin_trap();
    if (parts > 64) __builtin_trap();
    return span / parts;
}
