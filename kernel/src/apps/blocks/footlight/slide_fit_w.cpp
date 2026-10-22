// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The width of a 16:9 slide fitted whole inside a window (footlight_rules.h
// FL1), in C++ compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh): min(winw, floor(16 * winh / 9)), every
// side in pixels. The domain is a window of 0 .. 65535 by 0 .. 65535;
// anything else traps, which the runtime turns into a refusal
// (zxapp_wasm.c W3), never an answer. slide_fit_w.kron answers past the
// domain: there the two disagree, held. Arithmetic and branches only:
// Z-Wasm's validator refuses wasm's untyped select, which a conditional
// value would compile to, so the smaller of the two is taken with a mask.
extern "C" long long run(long long winw, long long winh)
{
    if (winw < 0) __builtin_trap();
    if (winw > 65535) __builtin_trap();
    if (winh < 0) __builtin_trap();
    if (winh > 65535) __builtin_trap();
    long long w = 16 * winh / 9;
    long long d = winw - w;
    // d >> 63 is all ones when the window is narrower than the fit, else 0
    return w + (d & (d >> 63));
}
