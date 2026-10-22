// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Where a node sits down, on its mind-map ring, in C++ compiled to
// WebAssembly (bramble_model.h BR2): the same ring and place as ring_x.cpp
// (radius r = depth * step, p = floor(order * 8 * r / count) units
// clockwise from the top-left corner, y down):
//   top    p in [0, 2r):   y = -r
//   right  p in [2r, 4r):  y = p - 3r
//   bottom p in [4r, 6r):  y = r
//   left   p in [6r, 8r):  y = 7r - p
// The root (depth 0) is the centre. The domain is depth 0 .. 64, step
// 1 .. 4096, count 1 .. 65536 and order 0 .. count - 1; anything else traps
// (a refusal), where KRON still answers, so the two disagree: held (BOTH).
// Written with branches only: Z-Wasm's validator refuses wasm's untyped
// select, which a conditional assignment would compile to.
extern "C" long long run(long long depth, long long order, long long count, long long step)
{
    if (depth < 0) __builtin_trap();
    if (depth > 64) __builtin_trap();
    if (step < 1) __builtin_trap();
    if (step > 4096) __builtin_trap();
    if (count < 1) __builtin_trap();
    if (count > 65536) __builtin_trap();
    if (order < 0) __builtin_trap();
    if (order >= count) __builtin_trap();
    long long r = depth * step;
    long long p = order * 8 * r / count;
    if (p < 2 * r) return 0 - r;
    if (p < 4 * r) return p - 3 * r;
    if (p < 6 * r) return r;
    return 7 * r - p;
}
