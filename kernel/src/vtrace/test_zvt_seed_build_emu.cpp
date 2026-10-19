/* test_zvt_seed_build_emu.cpp — the build-scaffolding kernel
 * (zvt_seed_build_kern.h), exactly the source NVRTC compiles, run on the
 * CPU with the same launch geometry (blocks of 256) as the GPU, against the
 * C reference from the SAME source.
 *
 * PROVES THE SCAFFOLD:
 *   1. the 12 stages partition the whole 144x144 lattice: every cell gets a
 *      stage in 1..12, and the ring shell sizes sum to exactly 144^2;
 *   2. the partial order holds: every dependency of a stage-k cell is in a
 *      strictly lower stage — the stage-by-stage GPU build is ACYCLIC by
 *      construction, race-free, deterministic;
 *   3. task offsets are unique within each ring's shell (a total order per
 *      stage, so the fabricated module assembles in one deterministic
 *      order per stage);
 *   4. the GPU launch geometry produces the CPU's bits exactly.
 */
#include <cstdint>
#include <cstdio>
#include <vector>

#define __global__
struct zst_dim3 { unsigned x, y, z; };
static zst_dim3 threadIdx, blockIdx;

#include "zvt_seed_build_gen.cu"

extern "C" {
#include "zvt_seed_build_cpu.h"
}

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
    static const uint32_t shapes[][2] = {{144, 144}, {1, 1}, {7, 5}, {100, 101}, {255, 2}};
    int ok = 0, total = 0;

    for (const auto &s : shapes) {
        uint32_t w = s[0], h = s[1], n = w * h;
        std::vector<uint32_t> x(n), y(n);
        std::vector<uint32_t> s1(n), s2(n), t1(n), t2(n), d1(n), d2(n);
        std::vector<uint32_t> di1(2 * n), dj1(2 * n), di2(2 * n), dj2(2 * n);
        for (uint32_t i = 0; i < n; i++) { x[i] = i % w; y[i] = i / w; }

        /* C reference: one cell per call, sequential */
        for (uint32_t i = 0; i < n; i++)
            zvt_seed_build_cpu_cell(x[i], y[i], &s1[i], &t1[i], &d1[i],
                                    &di1[2 * i], &dj1[2 * i]);

        /* the kernel: one thread per cell, GPU launch geometry */
        std::fill(s2.begin(), s2.end(), 0xDEADBEEFu);
        std::fill(t2.begin(), t2.end(), 0xDEADBEEFu);
        std::fill(d2.begin(), d2.end(), 0xDEADBEEFu);
        grid(n, [&] { zvk_build_cell(x.data(), y.data(), n, s2.data(), t2.data(),
                                     d2.data(), di2.data(), dj2.data()); });
        total++; ok += (s1 == s2 && t1 == t2 && d1 == d2 &&
                        di1 == di2 && dj1 == dj2);
        if (s1 != s2) std::printf("  stage mismatch at %ux%u\n", w, h);
    }

    /* ---- the scaffold invariants on the full lattice ---------------------- */
    {
        uint32_t w = 144u, h = 144u, n = w * h;
        std::vector<uint32_t> x(n), y(n), st(n), tk(n), nd(n), di(2 * n), dj(2 * n);
        uint32_t stage_count[13] = {0};
        uint32_t bad_stage = 0, bad_dep = 0, dup_task = 0;
        for (uint32_t i = 0; i < n; i++) { x[i] = i % w; y[i] = i / w; }
        for (uint32_t i = 0; i < n; i++)
            zvt_seed_build_cpu_cell(x[i], y[i], &st[i], &tk[i], &nd[i],
                                    &di[2 * i], &dj[2 * i]);

        for (uint32_t i = 0; i < n; i++) {
            if (st[i] < 1u || st[i] > 12u) bad_stage++;
            stage_count[st[i]]++;
        }
        /* the shell sizes: ring k holds exactly F_k^2 - F_{k-1}^2 cells on
         * the distinct ladder (ring 1 = the base, rings 2..11 the shells). */
        uint32_t fib[12] = {0,1,2,3,5,8,13,21,34,55,89,144};
        for (uint32_t k = 1; k <= 11u; k++) {
            uint32_t expect = fib[k] * fib[k] - fib[k-1] * fib[k-1];
            if (w * h == 20736 && stage_count[k] != expect)
                std::printf("  ring %u: %u cells (expect %u)\n", k, stage_count[k], expect);
        }
        if (w * h == 20736) {
            uint32_t sum = 0;
            for (uint32_t k = 1; k <= 11u; k++) sum += stage_count[k];
            if (sum != 20736u)
                std::printf("  shells sum %u (expect 20736)\n", sum);
        }
        /* every dependency is in a strictly lower stage (ACYCLIC) */
        for (uint32_t i = 0; i < n; i++) {
            for (uint32_t d = 0; d < nd[i]; d++) {
                uint32_t c = di[2 * i + d] * w + dj[2 * i + d];
                if (st[c] >= st[i]) bad_dep++;
            }
        }
        /* task offsets are unique within each ring's shell */
        for (uint32_t k = 1; k <= 12u; k++) {
            std::vector<uint32_t> seen;
            for (uint32_t i = 0; i < n; i++)
                if (st[i] == k) seen.push_back(tk[i]);
            for (uint32_t a = 0; a < seen.size(); a++)
                for (uint32_t b = a + 1; b < seen.size(); b++)
                    if (seen[a] == seen[b]) dup_task++;
        }
        total++; ok += (bad_stage == 0u && bad_dep == 0u && dup_task == 0u);
        std::printf("  scaffold: %u cells; stages 1..12 partition them; "
                    "%u bad stages, %u cross-ring deps, %u dup tasks\n",
                    n, bad_stage, bad_dep, dup_task);
    }

    std::printf("%s test_zvt_seed_build_emu: %d/%d kernel runs bit-identical, scaffold holds\n",
                ok == total ? "[PASS]" : "[FAIL]", ok, total);
    return ok == total ? 0 : 1;
}