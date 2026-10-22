// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Contrast of one channel value about the middle grey, in C++ compiled to
// WebAssembly (gesso_rules.h GR2): 128 + floor((px - 128) * gain / 10000),
// the floor toward minus infinity as KRON's _ is, held inside 0 .. 255.
// gain is in hundredths of a percent (10000 leaves the value as it is).
// The domain is a value of 0 .. 255 and a gain of 0 .. 40000; anything
// else traps (a refusal), where KRON still answers: held (BOTH).
// Written with branches only: Z-Wasm's validator refuses wasm's untyped
// select, which a conditional assignment would compile to.
extern "C" long long run(long long px, long long gain)
{
    if (px < 0) __builtin_trap();
    if (px > 255) __builtin_trap();
    if (gain < 0) __builtin_trap();
    if (gain > 40000) __builtin_trap();
    long long p = (px - 128) * gain;
    if (p >= 0) {
        if (128 + p / 10000 > 255) return 255;
        return 128 + p / 10000;
    }
    if (128 + (p - 9999) / 10000 < 0) return 0;
    return 128 + (p - 9999) / 10000;
}
