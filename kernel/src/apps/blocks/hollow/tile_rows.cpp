// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The rows of a grid for n windows, in C++ compiled to WebAssembly
// (hollow_rules.h HR2): n over the grid's columns (HR1), rounded up, 0 for
// none. n is 0 .. 64; anything else traps (a refusal), where KRON answers.
extern "C" long long run(long long windows)
{
    if (windows < 0) __builtin_trap();
    if (windows > 64) __builtin_trap();
    long long c = 0;
    while (c * c < windows) c++;
    if (c == 0) return 0;
    return (windows + c - 1) / c;
}
