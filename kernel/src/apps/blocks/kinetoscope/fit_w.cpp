// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The width of a video of vidw by vidh pixels fitted whole inside a window
// of winw by winh, its aspect kept (kinetoscope_rules.h KR1), in C++
// compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh):
// min(winw, floor(winh * vidw / vidh)). The domain is a video of
// 1 .. 65535 by 1 .. 65535 and a window of 0 .. 65535 by 0 .. 65535;
// anything else traps, which the runtime turns into a refusal
// (zxapp_wasm.c W3), never an answer. fit_w.kron answers past the domain:
// there the two disagree, held. Arithmetic and branches only: Z-Wasm's
// validator refuses wasm's untyped select, so the smaller of the two is
// taken with a mask.
extern "C" long long run(long long vidw, long long vidh, long long winw, long long winh)
{
    if (vidw < 1) __builtin_trap();
    if (vidw > 65535) __builtin_trap();
    if (vidh < 1) __builtin_trap();
    if (vidh > 65535) __builtin_trap();
    if (winw < 0) __builtin_trap();
    if (winw > 65535) __builtin_trap();
    if (winh < 0) __builtin_trap();
    if (winh > 65535) __builtin_trap();
    long long w = winh * vidw / vidh;
    long long d = winw - w;
    // d >> 63 is all ones when the window is narrower than the fit, else 0
    return w + (d & (d >> 63));
}
