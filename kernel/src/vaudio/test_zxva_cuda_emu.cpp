/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxva_cuda_emu.cpp — zxva_cuda.c's kernel source run on the CPU with no
 * GPU, against the C reference (zxva_stft_cpu), word for word. A block is
 * blockDim host threads; __syncthreads is a std::barrier, __shared__ a
 * static shared by the block's threads, atomicMax an atomic. Blocks run one
 * after another. It checks the CUDA-only parts (the launch geometry, the
 * barriers, the strided loops at several block sizes); the real GPU run is
 * test_zxva_cuda.
 *
 *   test_zxva_cuda --source > $O/zxva_cuda_gen.cu
 *   gcc -c ... zxva_analyse.c zxva.c sha256.c (the C reference)
 *   g++ -std=c++20 -I$O -Isrc/vaudio test_zxva_cuda_emu.cpp *.o -lpthread
 * (tools/zxva/test_zxva_gpu.sh does all of it.) */
#include <atomic>
#include <barrier>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include "zxva_analyse.h"
}

#define __global__
#define __device__
#define __shared__ static
struct zxs_dim3 {
    unsigned x, y, z;
};
static thread_local zxs_dim3 threadIdx, blockIdx;
static zxs_dim3 blockDim = {256, 1, 1};
static std::barrier<> *blk_bar;
static void __syncthreads()
{
    blk_bar->arrive_and_wait();
}
static unsigned atomicMax(unsigned *a, unsigned v)
{
    std::atomic_ref<unsigned> r(*a);
    unsigned o = r.load();
    while (o < v && !r.compare_exchange_weak(o, v)) {
    }
    return o;
}

#include "zxva_cuda_gen.cu"

static unsigned long long rng = 0x637564617a787661ull;
static unsigned rnd()
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned) rng;
}

static void launch(unsigned nblocks, unsigned nt, const zi *x, const zi *win, const zi *tw, zi *scr, zi *re, zi *im,
                   zu *mag, zu *mask, zu *top, zxva_kp p)
{
    blockDim = {nt, 1, 1};
    for (unsigned b = 0; b < nblocks; b++) {
        std::barrier<> bar((std::ptrdiff_t) nt);
        blk_bar = &bar;
        std::vector<std::thread> th;
        for (unsigned t = 0; t < nt; t++)
            th.emplace_back([=] {
                threadIdx = {t, 0, 0};
                blockIdx = {b, 0, 0};
                zxva_stft_k(x, win, tw, scr, re, im, mag, mask, top, p);
            });
        for (auto &t : th) t.join();
    }
}

int main()
{
    int cases = 0, fails = 0;
    static const unsigned nts[] = {256, 64, 32, 96};
    for (int it = 0; it < 24; it++) {
        unsigned L = 4 + rnd() % 8, N = 1u << L, n = N >> (rnd() % 3);
        if (n < 4) n = 4;
        if (rnd() % 3 == 0 && n > 8) n -= 2 * (rnd() % (n / 4));
        unsigned H = N / 2 + 1, mw = zxva_mask_words(N), nf = 1 + rnd() % 6;
        uint64_t len = 1 + rnd() % (3 * N);
        std::vector<int32_t> x(len), win(n), tw(N);
        for (auto &v : x) v = it % 4 == 0 ? 0 : (int32_t) (rnd() % (1u << 24)) - (1 << 23);
        if (it & 1)
            zxva_tables_bh(win.data(), n);
        else
            zxva_tables_hann(win.data(), n);
        zxva_tables_tw(tw.data(), N);
        zxva_stft_plan_t p = {};
        p.n = n, p.N = N, p.L = L, p.num = 1 + rnd() % (n / 2 + 1), p.den = rnd() % 3 ? 1 : 1 + rnd() % 5;
        p.c0 = (int64_t) (rnd() % (2 * N)) - (int64_t) N;
        p.win = win.data(), p.tw = tw.data(), p.x = x.data(), p.len = len;
        p.floor_mag = rnd() % 1000, p.rel = 1 + rnd() % 100000;
        uint64_t k0 = rnd() % 4;
        std::vector<int32_t> re0(nf * H), im0(nf * H), re1(nf * H), im1(nf * H), scr0(2 * N), scr1((size_t) nf * 2 * N);
        std::vector<uint32_t> m0(nf * H), m1(nf * H), k0m(nf * mw), k1m(nf * mw, 0x5a5a5a5au), t0(nf), t1(nf);
        zxva_backend_t cpu;
        zxva_backend_cpu(&cpu);
        cpu.scratch = scr0.data();
        cpu.scratch_words = 2 * N;
        cpu.stft(&cpu, &p, k0, nf, re0.data(), im0.data(), m0.data(), k0m.data(), t0.data());
        zxva_kp kp = {n,
                      N,
                      L,
                      p.num,
                      p.den,
                      (zu) (uint64_t) p.c0,
                      (zu) ((uint64_t) p.c0 >> 32),
                      (zu) len,
                      (zu) (len >> 32),
                      (zu) k0,
                      (zu) (k0 >> 32),
                      p.floor_mag,
                      p.rel,
                      nf};
        launch(nf, nts[it % 4], x.data(), win.data(), tw.data(), scr1.data(), re1.data(), im1.data(), m1.data(),
               k1m.data(), t1.data(), kp);
        bool bad = re0 != re1 || im0 != im1 || m0 != m1 || k0m != k1m || t0 != t1;
        cases++, fails += bad;
        if (bad) printf("[FAIL] n %u N %u nf %u threads %u\n", n, N, nf, nts[it % 4]);
    }
    printf("%d/%d emulated CUDA launches word for word\n", cases - fails, cases);
    return fails != 0;
}
