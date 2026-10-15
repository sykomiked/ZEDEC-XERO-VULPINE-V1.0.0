// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The columns of a grid for n windows, in C++ compiled to WebAssembly
// (hollow_rules.h HR1): the least c with c * c >= n, 0 for none. n is 0 ..
// 64 (an 8 by 8 grid at most); anything else traps, which the runtime
// turns into a refusal (zxapp_wasm.c W3), where KRON still answers: the
// two then disagree and the layout is held (BOTH).
extern "C" long long run(long long windows)
{
    if (windows < 0) __builtin_trap();
    if (windows > 64) __builtin_trap();
    long long c = 0;
    while (c * c < windows) c++;
    return c;
}
