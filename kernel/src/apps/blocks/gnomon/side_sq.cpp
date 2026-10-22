// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A side's squared length (gnomon_rules.h GR4), in C++ compiled to
// WebAssembly (build: tools/apps/build_wasm_blocks.sh): the squared
// distance from a to b, (bx - ax)^2 + (by - ay)^2, exact, with no square
// root taken (a length is compared or summed as its square). The domain is
// every coordinate -1000000 .. 1000000; anything else traps, which the
// runtime turns into a refusal (zxapp_wasm.c W3), where side_sq.kron still
// answers: held. Branches only: Z-Wasm's validator refuses wasm's untyped
// select.
static bool coord_ok(long long v)
{
    if (v < -1000000) return false;
    if (v > 1000000) return false;
    return true;
}

extern "C" long long run(long long ax, long long ay, long long bx, long long by)
{
    if (!coord_ok(ax)) __builtin_trap();
    if (!coord_ok(ay)) __builtin_trap();
    if (!coord_ok(bx)) __builtin_trap();
    if (!coord_ok(by)) __builtin_trap();
    long long dx = bx - ax;
    long long dy = by - ay;
    return dx * dx + dy * dy;
}
