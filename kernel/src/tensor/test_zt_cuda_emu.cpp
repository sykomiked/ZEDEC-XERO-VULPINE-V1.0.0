/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_cuda_emu.cpp — zt_cuda.c's warp product kernels run on the CPU,
 * against the C reference (zk_mm), bit for bit, with no GPU. A warp is 32
 * host threads; __shfl_down_sync swaps values through a barrier. The launch
 * geometry is zt_cuda.c's: blocks of 4 warps, one row per warp, tiles of
 * 8 tokens when B > 1. It checks the CUDA-only code (the product kernels and
 * how a launch covers the rows and tokens); the real GPU run is test_zt_cuda.
 *
 *   test_zt_cuda --source > $(ZXV_TMP)/zt_cuda_gen.cu
 *   g++ -std=c++20 -O1 -I$(ZXV_TMP) src/tensor/test_zt_cuda_emu.cpp -lpthread
 */
#include <barrier>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#define __global__
struct zst_dim3 {
    unsigned x, y, z;
};
static thread_local zst_dim3 threadIdx, blockIdx;
static const zst_dim3 blockDim = {128, 1, 1};
static std::barrier<> *warp_bar;
static long long warp_x[32];
static long long __shfl_down_sync(unsigned, long long v, unsigned off)
{
    unsigned l = threadIdx.x & 31u;
    warp_x[l] = v;
    warp_bar->arrive_and_wait();
    long long r = l + off < 32 ? warp_x[l + off] : v;
    warp_bar->arrive_and_wait();
    return r;
}
static unsigned atomicAdd(unsigned *a, unsigned v)
{
    return __atomic_fetch_add(a, v, __ATOMIC_SEQ_CST);
}

#include "zt_cuda_gen.cu"

typedef void (*mm_kernel)(const zb *, const zs *, const zb *, const zi *, zi *, zm_p);

static unsigned long long rng = 0x637564612026ull;
static unsigned rnd()
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned) rng;
}

/* a grid of gx x gy blocks, run warp by warp */
static void launch(mm_kernel k, unsigned gx, unsigned gy, const zb *w, const zs *a, const zb *ash,
                   const zi *asum, zi *y, zm_p p)
{
    for (unsigned by = 0; by < gy; by++)
        for (unsigned bx = 0; bx < gx; bx++)
            for (unsigned wp = 0; wp < 4; wp++) {
                std::barrier<> bar(32);
                warp_bar = &bar;
                std::vector<std::thread> lanes;
                for (unsigned l = 0; l < 32; l++)
                    lanes.emplace_back([=] {
                        threadIdx = {wp * 32 + l, 0, 0};
                        blockIdx = {bx, by, 0};
                        k(w, a, ash, asum, y, p);
                    });
                for (auto &t : lanes) t.join();
            }
}

/* a finite f16 scale: exponent 5..24, so terms stay in range */
static unsigned short f16()
{
    return (unsigned short) ((rnd() & 0x8000u) | ((5 + rnd() % 20) << 10) | (rnd() & 0x3FFu));
}

int main()
{
    const unsigned qts[3] = {12, 14, 8};
    const mm_kernel one[3] = {zm_q4k_1, zm_q6k_1, zm_q80_1},
                    tile[3] = {zm_q4k_t, zm_q6k_t, zm_q80_t};
    int cases = 0, fails = 0;
    for (int it = 0; it < 48; it++) {
        int q = it % 3;
        unsigned qt = qts[q];
        /* 256..1280 columns: 8..40 blocks, so lanes hold 0, 1 or 2 blocks */
        unsigned cols = 256u * (1 + rnd() % 5), nb = cols / 32, rows = 1 + rnd() % 13;
        unsigned B = (it / 3) % 2 ? 2 + rnd() % 18 : 1, as = cols + 32u * (rnd() % 2), sb = as / 32;
        unsigned rb = zm_row_bytes(qt, nb);
        std::vector<zb> w((size_t) rb * rows);
        for (auto &x : w) x = (zb) rnd();
        for (unsigned r = 0; r < rows; r++) {
            zb *row = &w[(size_t) r * rb];
            unsigned short d, m;
            for (unsigned b = 0; b < (qt == 8 ? nb : nb / 8); b++) {
                d = f16(), m = f16();
                if (qt == 12) memcpy(row + b * 144, &d, 2), memcpy(row + b * 144 + 2, &m, 2);
                if (qt == 14) memcpy(row + b * 210 + 208, &d, 2);
                if (qt == 8) memcpy(row + b * 34, &d, 2);
            }
        }
        std::vector<zs> a((size_t) B * as);
        for (auto &x : a) x = (zs) ((int) (rnd() % 65535) - 32767);
        std::vector<zb> ash((size_t) B * sb);
        for (auto &x : ash) x = (zb) (it % 4 ? rnd() % 18 : rnd() % 256);
        std::vector<zi> asum((size_t) B * sb);
        for (auto &x : asum) x = (zi) (rnd() % 2000001) - 1000000;
        std::vector<zi> ref((size_t) B * rows), y((size_t) B * rows, 0x5a5a5a5a);
        zg_p gp = {};
        gp.u[1] = rows, gp.u[2] = nb, gp.u[3] = as, gp.u[4] = qt, gp.u[5] = rb;
        for (unsigned i = 0; i < rows * B; i++)
            zk_mm(i, gp, w.data(), (zb *) a.data(), ash.data(), (zb *) asum.data(),
                  (zb *) ref.data(), 0, 0);
        zm_p p = {rows, nb, as, B, rb};
        unsigned T = B > 1 ? 8 : 1;
        launch(B > 1 ? tile[q] : one[q], (rows + 3) / 4, (B + T - 1) / T, w.data(), a.data(),
               ash.data(), asum.data(), y.data(), p);
        bool bad = memcmp(ref.data(), y.data(), ref.size() * 4) != 0;
        cases++, fails += bad;
        if (bad) printf("[FAIL] qtype %u, %u x %u, %u tokens, stride %u\n", qt, rows, cols, B, as);
    }
    printf("%d/%d emulated warp products bit-exact\n", cases - fails, cases);
    return fails != 0;
}
