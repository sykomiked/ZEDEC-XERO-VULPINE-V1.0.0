// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// One channel value of top laid over base at alpha, in C++ compiled to
// WebAssembly (gesso_rules.h GR3): floor((base * (255 - alpha) + top *
// alpha) / 255), 0 .. 255 whenever its inputs are (0 is all base, 255 all
// top). Each input is 0 .. 255; anything else traps (a refusal), where
// KRON still answers: held (BOTH).
extern "C" long long run(long long base, long long top, long long alpha)
{
    if (base < 0) __builtin_trap();
    if (base > 255) __builtin_trap();
    if (top < 0) __builtin_trap();
    if (top > 255) __builtin_trap();
    if (alpha < 0) __builtin_trap();
    if (alpha > 255) __builtin_trap();
    return (base * (255 - alpha) + top * alpha) / 255;
}
