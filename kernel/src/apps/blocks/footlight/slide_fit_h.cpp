// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The height of a 16:9 slide fitted whole inside a window
// (footlight_rules.h FL2), in C++ compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh): min(winh, floor(9 * winw / 16)), every
// side in pixels. The domain is a window of 0 .. 65535 by 0 .. 65535;
// anything else traps, which the runtime turns into a refusal
// (zxapp_wasm.c W3), never an answer. slide_fit_h.kron answers past the
// domain: there the two disagree, held. Arithmetic and branches only:
// Z-Wasm's validator refuses wasm's untyped select, which a conditional
// value would compile to, so the smaller of the two is taken with a mask.
extern "C" long long run(long long winw, long long winh)
{
    if (winw < 0) __builtin_trap();
    if (winw > 65535) __builtin_trap();
    if (winh < 0) __builtin_trap();
    if (winh > 65535) __builtin_trap();
    long long h = 9 * winw / 16;
    long long d = winh - h;
    // d >> 63 is all ones when the window is shorter than the fit, else 0
    return h + (d & (d >> 63));
}
