/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zvt_cuda_emu.cpp — zvt_cuda.c's kernels, exactly the source NVRTC
 * compiles, run on the CPU with zvt_cuda.c's launch geometry (blocks of 256
 * threads, every thread of the grid, the tail block included) against the C
 * reference stages (zvt_cpu_*), bit for bit, with no GPU. It checks the
 * CUDA-only code: the wrappers' index arithmetic and bounds, and that the
 * stringified zvt_kern.h means the same in C++ as in C. The real GPU run is
 * test_zvt_gpu on a CUDA machine.
 *
 *   test_zvt_gpu --source > DIR/zvt_cuda_gen.cu
 *   g++ -std=c++14 -IDIR -Isrc/vtrace test_zvt_cuda_emu.cpp + the C reference objects */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define __global__
struct zst_dim3 {
    unsigned x, y, z;
};
static zst_dim3 threadIdx, blockIdx;
static const zst_dim3 blockDim = {256, 1, 1};

#include "zvt_cuda_gen.cu"

extern "C" {
#include "zui_vtrace.h"
}

static uint64_t rng = 0x656d750000000001ull;
static uint32_t rnd()
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t) rng;
}

/* every thread of ceil(count / 256) blocks, as zvt_cuda.c launches */
template <typename F> static void grid(uint32_t count, F f)
{
    uint32_t blocks = (count + 255u) / 256u;
    for (uint32_t b = 0; b < blocks; b++)
        for (uint32_t t = 0; t < 256u; t++) {
            blockIdx = {b, 0, 0};
            threadIdx = {t, 0, 0};
            f();
        }
}

int main()
{
    static const uint32_t sz[][2] = {{1, 1},   {3, 1},   {1, 9},   {7, 5},
                                     {64, 48}, {257, 3}, {255, 2}, {100, 101}};
    int ok = 0, total = 0;
    for (const auto &s : sz) {
        uint32_t w = s[0], h = s[1], n = w * h;
        std::vector<uint32_t> a(n), b(n), c1(n), c2(n), pal(256);
        std::vector<uint8_t> i1(n), i2(n), e1(n), e2(n), m1(n), m2(n);
        for (uint32_t i = 0; i < n; i++) {
            a[i] = rnd();
            b[i] = (i % 3) ? a[i] ^ (rnd() & 0x00303030u) : rnd();
            if (i % 5 == 0) a[i] |= 0xFF000000u;
        }
        for (auto &p : pal) p = 0xFF000000u | rnd();
        for (uint32_t thr : {0u, 20u, 255u}) {
            zvt_cpu_smooth(a.data(), c1.data(), w, h, thr);
            std::fill(c2.begin(), c2.end(), 0xDEADBEEFu);
            grid(n, [&] { zvk_smooth(a.data(), c2.data(), w, h, thr); });
            total++, ok += c1 == c2;
        }
        for (uint32_t np : {1u, 9u, 256u}) {
            zvt_cpu_quant(a.data(), n, pal.data(), np, i1.data());
            std::fill(i2.begin(), i2.end(), 0x5A);
            grid(n, [&] { zvk_quant(a.data(), n, pal.data(), np, i2.data()); });
            total++, ok += i1 == i2;
        }
        zvt_cpu_quant(a.data(), n, pal.data(), 5, i1.data());
        zvt_cpu_edges(i1.data(), w, h, e1.data());
        std::fill(e2.begin(), e2.end(), 0x77);
        grid(n, [&] { zvk_edges(i1.data(), w, h, e2.data()); });
        total++, ok += e1 == e2;
        uint32_t wx = w < 8 ? w : 8, wy = h < 8 ? h : 8, st = 4;
        uint32_t nwx = (w - wx) / st + 1, nwin = nwx * ((h - wy) / st + 1);
        std::vector<uint32_t> r1(h), r2(h, 1), w1(5 * nwin), w2(5 * nwin, 2);
        zvt_cpu_metric(a.data(), b.data(), w, h, wx, wy, st, r1.data(), w1.data());
        grid(h, [&] { zvk_sse(a.data(), b.data(), w, h, r2.data()); });
        grid(nwin, [&] { zvk_win(a.data(), b.data(), w, wx, wy, st, nwx, nwin, w2.data()); });
        total++, ok += r1 == r2 && w1 == w2;
        for (uint32_t thr : {0u, 40u}) {
            zvt_cpu_diff(a.data(), b.data(), w, h, thr, m1.data(), r1.data());
            std::fill(m2.begin(), m2.end(), 3);
            std::fill(r2.begin(), r2.end(), 3u);
            grid(h, [&] { zvk_diff(a.data(), b.data(), w, h, thr, m2.data(), r2.data()); });
            total++, ok += m1 == m2 && r1 == r2;
        }
    }
    printf("%s test_zvt_cuda_emu: %d/%d kernel runs bit-identical to the C reference\n",
           ok == total ? "[PASS]" : "[FAIL]", ok, total);
    return ok != total;
}
