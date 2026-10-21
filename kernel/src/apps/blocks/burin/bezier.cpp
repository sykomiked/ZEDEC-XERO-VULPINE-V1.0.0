// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// One coordinate of a cubic Bezier curve at t = step / steps, in C++
// compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh), by de
// Casteljau in fixed point (burin_rules.h BR1): each of the three rounds of
// interpolation keeps its scale (a * (steps - step) + b * step), so nothing
// is rounded until the end, and the point is the sum over steps^3, floored
// toward minus infinity. The domain is control points -1000000 .. 1000000
// (hundredths of a point), steps 1 .. 1024 and step 0 .. steps; anything
// outside it is refused with -1000001, which no point of such a curve can
// be (it stays inside its control points). KRON answers past the domain,
// so the two disagree there and the answer is held (BOTH). Branches only:
// Z-Wasm's validator refuses wasm's untyped select.
extern "C" long long run(long long p0, long long p1, long long p2, long long p3, long long step,
                         long long steps)
{
    if (p0 < -1000000) return -1000001;
    if (p0 > 1000000) return -1000001;
    if (p1 < -1000000) return -1000001;
    if (p1 > 1000000) return -1000001;
    if (p2 < -1000000) return -1000001;
    if (p2 > 1000000) return -1000001;
    if (p3 < -1000000) return -1000001;
    if (p3 > 1000000) return -1000001;
    if (steps < 1) return -1000001;
    if (steps > 1024) return -1000001;
    if (step < 0) return -1000001;
    if (step > steps) return -1000001;
    long long u = steps - step;
    long long a = p0 * u + p1 * step;
    long long b = p1 * u + p2 * step;
    long long c = p2 * u + p3 * step;
    long long d = a * u + b * step;
    long long e = b * u + c * step;
    long long s = d * u + e * step;
    long long n3 = steps * steps * steps;
    long long q = s / n3;
    if (s % n3 != 0) {
        if (s < 0) return q - 1;
    }
    return q;
}
