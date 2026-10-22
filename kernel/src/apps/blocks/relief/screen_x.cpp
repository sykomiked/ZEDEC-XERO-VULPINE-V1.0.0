// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A vertex's screen x (relief_rules.h RR1), in C++ compiled to WebAssembly
// (build: tools/apps/build_wasm_blocks.sh), in fixed point: the vertex x, z
// turned about the vertical axis by cos, sin in 4096ths (xr = x * cos + z *
// sin, zr = z * cos - x * sin), scaled by scale percent and seen in
// perspective from a viewer focal units in front of the screen plane:
// scale * focal * xr / (100 * (4096 * focal + zr)), floored toward minus
// infinity. Nothing is rounded before the one division. The domain is x, z
// -100000 .. 100000 (hundredths of a millimetre), cos and sin -4096 ..
// 4096, scale 1 .. 1000 and focal 1 .. 1000000, with the vertex in front of
// the viewer; anything else traps, which the runtime turns into a refusal
// (zxapp_wasm.c W3), where screen_x.kron still answers: held. Branches
// only: Z-Wasm's validator refuses wasm's untyped select.
extern "C" long long run(long long x, long long z, long long c, long long s, long long scale,
                         long long focal)
{
    if (x < -100000) __builtin_trap();
    if (x > 100000) __builtin_trap();
    if (z < -100000) __builtin_trap();
    if (z > 100000) __builtin_trap();
    if (c < -4096) __builtin_trap();
    if (c > 4096) __builtin_trap();
    if (s < -4096) __builtin_trap();
    if (s > 4096) __builtin_trap();
    if (scale < 1) __builtin_trap();
    if (scale > 1000) __builtin_trap();
    if (focal < 1) __builtin_trap();
    if (focal > 1000000) __builtin_trap();
    long long depth = 4096 * focal + z * c - x * s;
    if (depth < 1) __builtin_trap();
    long long n = scale * focal * (x * c + z * s);
    long long d = 100 * depth;
    long long q = n / d;
    if (n % d != 0) {
        if (n < 0) return q - 1;
    }
    return q;
}
