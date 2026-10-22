// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Brightness of one channel value, in C++ compiled to WebAssembly
// (gesso_rules.h GR1): px + delta, held inside 0 .. 255. The domain is a
// value of 0 .. 255 and a delta of -255 .. 255; anything else traps (a
// refusal), where KRON still answers, so the two disagree: held (BOTH).
// Written with branches only: Z-Wasm's validator refuses wasm's untyped
// select, which a conditional assignment would compile to.
extern "C" long long run(long long px, long long delta)
{
    if (px < 0) __builtin_trap();
    if (px > 255) __builtin_trap();
    if (delta < -255) __builtin_trap();
    if (delta > 255) __builtin_trap();
    long long v = px + delta;
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}
