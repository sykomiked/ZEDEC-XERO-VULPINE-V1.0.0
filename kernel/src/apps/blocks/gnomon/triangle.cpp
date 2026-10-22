// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Twice a triangle's signed area (gnomon_rules.h GR3), in C++ compiled to
// WebAssembly (build: tools/apps/build_wasm_blocks.sh): the shoelace sum of
// the three corners a, b, c, which is the cross product of the sides a to
// b and a to c: (bx - ax) * (cy - ay) - (by - ay) * (cx - ax). Positive
// when a, b, c turn counter-clockwise (a left turn), negative clockwise,
// 0 when they lie on one line. Exact: integers only, nothing divided. The
// domain is every coordinate -1000000 .. 1000000; anything else traps,
// which the runtime turns into a refusal (zxapp_wasm.c W3), where
// triangle.kron still answers: held. Branches only: Z-Wasm's validator
// refuses wasm's untyped select.
static bool coord_ok(long long v)
{
    if (v < -1000000) return false;
    if (v > 1000000) return false;
    return true;
}

extern "C" long long run(long long ax, long long ay, long long bx, long long by, long long cx,
                         long long cy)
{
    if (!coord_ok(ax)) __builtin_trap();
    if (!coord_ok(ay)) __builtin_trap();
    if (!coord_ok(bx)) __builtin_trap();
    if (!coord_ok(by)) __builtin_trap();
    if (!coord_ok(cx)) __builtin_trap();
    if (!coord_ok(cy)) __builtin_trap();
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}
