/* test_zvt_seed_emu.cpp — the seed-unfold kernels (zvt_seed_kern.h), exactly
 * the source NVRTC compiles, run on the CPU with the same launch geometry
 * (blocks of 256) as the GPU, against the C reference compiled from the SAME
 * source. Bit-identical by construction: integer arithmetic has one answer
 * in any order, so the GPU unfolds the lattice exactly as the C reference.
 *
 * Build (host): g++ -std=c++14 -O1 -Isrc/vtrace -Isrc/zui \
 *     src/vtrace/test_zvt_seed_emu.cpp -o /tmp/test_zvt_seed_emu
 * The C reference is zvt_seed_cpu.c (see below) built from the same header.
 */
#include <cstdint>
#include <cstdio>
#include <vector>

/* the CUDA built-in stand-ins, matching test_zvt_cuda_ptx.h's shape.
 * The externs carry no __device__ here: the emulator runs the kernel on the
 * CPU (the attribute is meaningless and clang warns); the PTX proof adds
 * the attribute via test_zvt_cuda_ptx.h before the same source. */
#define __global__
struct zst_dim3 { unsigned x, y, z; };
static zst_dim3 threadIdx, blockIdx;

/* the generated kernel source: exactly what NVRTC would compile */
#include "zvt_seed_gen.cu"

extern "C" {
/* the C reference: same header, compiled as plain C, one cell per call */
#include "zvt_seed_cpu.h"
}

static uint32_t rnd(uint64_t &rng)
{
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (uint32_t) rng;
}

/* every thread of ceil(count / 256) blocks, as the GPU launch would */
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
    /* the full 144x144 lattice: 20,736 cells, one thread each, plus small
     * odd shapes to prove the launch geometry handles ragged counts. */
    static const uint32_t shapes[][2] = {{144, 144}, {1, 1}, {7, 5}, {100, 101}, {255, 2}};
    int ok = 0, total = 0;

    for (const auto &s : shapes) {
        uint32_t w = s[0], h = s[1], n = w * h;
        std::vector<uint32_t> m1(n), m2(n), e1(n), e2(n);
        std::vector<int32_t>  c1(n), c2(n);

        /* C reference: one cell per call, sequential */
        for (uint32_t i = 0; i < n; i++)
            zvt_seed_cpu_cell(i % w, i / w, &m1[i], &e1[i], &c1[i]);

        /* the kernel: one thread per cell, GPU launch geometry */
        std::fill(m2.begin(), m2.end(), 0xDEADBEEFu);
        std::fill(e2.begin(), e2.end(), 0xDEADBEEFu);
        std::fill(c2.begin(), c2.end(), 0x7FFFFFFF);
        grid(n, [&] { zvk_seed_cell(nullptr, nullptr, nullptr, w, n,
                                    m2.data(), e2.data(), c2.data()); });
        total++; ok += (m1 == m2 && e1 == e2 && c1 == c2);
        if (m1 != m2 || e1 != e2 || c1 != c2) {
            std::printf("  mismatch at %ux%u\n", w, h);
            for (uint32_t i = 0; i < n && i < 8; i++)
                std::printf("    cell %u: ref m=%08x e=%08x c=%d | gpu m=%08x e=%08x c=%d\n",
                            i, m1[i], e1[i], c1[i], m2[i], e2[i], c2[i]);
        }
    }

    /* invariants on the full lattice: every Cassini determinant is +-1 and
     * the Zeckendorf mode never exceeds the 12-rung mask. */
    {
        uint32_t n = 144u * 144u;
        std::vector<uint32_t> m(n), e(n);
        std::vector<int32_t> c(n);
        for (uint32_t i = 0; i < n; i++)
            zvt_seed_cpu_cell(i % 144u, i / 144u, &m[i], &e[i], &c[i]);
        uint32_t bad_cassini = 0, bad_mask = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (c[i] != 1 && c[i] != -1) bad_cassini++;
            if ((m[i] & ~0xFFFu) != 0u) bad_mask++;   /* mask fits 12 bits  */
        }
        total++; ok += (bad_cassini == 0u && bad_mask == 0u);
        std::printf("  lattice invariants: %u cells, %u bad cassini, %u bad masks\n",
                    n, bad_cassini, bad_mask);
    }

    std::printf("%s test_zvt_seed_emu: %d/%d kernel runs bit-identical to the C reference\n",
                ok == total ? "[PASS]" : "[FAIL]", ok, total);
    return ok == total ? 0 : 1;
}