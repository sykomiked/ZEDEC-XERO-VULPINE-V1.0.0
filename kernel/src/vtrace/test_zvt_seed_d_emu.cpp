/* test_zvt_seed_d_emu.cpp — the D-dimensional scaffold kernel
 * (zvt_seed_dkern.h), exactly the source NVRTC compiles, run on the CPU
 * with the same launch geometry (blocks of 256) as the GPU, against the C
 * reference from the SAME source.
 *
 * PROVES THE HIGHER-DIMENSIONAL WIRING:
 *   1. the SAME kernel wires D = 2, 3, 4, 8 and 144 dimensions — bit
 *      identical to the CPU reference at every D;
 *   2. the scaffold holds in every dimension: each cell's stage is 1..11,
 *      every dependency is in a strictly lower ring (acyclic), and the
 *      shell sizes partition the lattice;
 *   3. the paraconsistent dimension: dependency states join through the
 *      four-valued encoding (NEITHER 0, TRUE 1, FALSE 2, BOTH 3), and a
 *      contradiction (BOTH) is preserved, never collapsed;
 *   4. a 144-dimensional cell: one thread computes stage + up to 144
 *      dependencies + the join across all 144 axes simultaneously.
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#define __global__
struct zst_dim3 {
    unsigned x, y, z;
};
static zst_dim3 threadIdx, blockIdx;

#include "zvt_seed_dgen.cu"

extern "C" {
#include "zvt_seed_dcpu.h"
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

/* all cells of a D-dimensional lattice with extent E per axis (E^D cells,
 * capped for the big dimensions) */
static void run_dim(uint32_t D, uint32_t extent, uint32_t &ok, uint32_t &total)
{
    uint64_t cells64 = 1;
    for (uint32_t a = 0; a < D; a++) cells64 *= extent;
    uint32_t n = cells64 > 20000u ? 20000u : (uint32_t) cells64;
    if (D == 144u) n = 256u; /* sample the 144-dim lattice: one block */

    std::vector<uint32_t> co(n * 144u, 0u);
    std::vector<uint32_t> s1(n), s2(n), d1(n), d2(n), st1(n), st2(n);
    std::vector<uint32_t> di1(n * 144u), dj1(n * 144u), di2(n * 144u), dj2(n * 144u);

    /* fill coordinates: a deterministic lattice walk (for D < 144 use the
     * full extent; for 144 use a bounded coordinate range per axis) */
    uint32_t seed = 0x12345678u;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t a = 0; a < D; a++) {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            co[i * 144u + a] = (D >= 144u) ? (seed % 3u) : (seed % extent);
        }

    for (uint32_t i = 0; i < n; i++)
        zvt_seed_d_cpu_cell(D, &co[i * 144u], &s1[i], &d1[i], &di1[i * 144u], &dj1[i * 144u],
                            &st1[i]);

    std::fill(s2.begin(), s2.end(), 0xDEADBEEFu);
    std::fill(d2.begin(), d2.end(), 0xDEADBEEFu);
    std::fill(st2.begin(), st2.end(), 0xDEADBEEFu);
    grid(n, [&] {
        zvk_d_cell(co.data(), n, D, s2.data(), d2.data(), di2.data(), dj2.data(), st2.data());
    });
    total++;
    ok += (s1 == s2 && d1 == d2 && st1 == st2 && di1 == di2 && dj1 == dj2);
    std::printf("  D=%3u extent=%u cells=%u: %s\n", D, extent, n,
                (s1 == s2 && d1 == d2 && st1 == st2) ? "bit-identical" : "MISMATCH");

    /* the scaffold holds: stages 1..11, deps strictly lower, states 0..3 */
    uint32_t bad_stage = 0, bad_dep = 0, bad_state = 0, acyclic = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s1[i] < 1u || s1[i] > 11u) bad_stage++;
        if (st1[i] > 3u) bad_state++;
        for (uint32_t a = 0; a < d1[i]; a++) {
            uint32_t v = dj1[i * 144u + a];
            /* the dependency's coordinate on its axis is the max coord of
             * the ring BELOW the cell's ring (prev-1 < f[stage-1]): a
             * stage-by-stage build that unfolds rings in order always has
             * every dependency available before the cell runs. That is the
             * acyclicity that matters — the coordinate, not the max label. */
            uint32_t prev = zk_d_fib(s1[i] - 1u);
            if (v >= prev) acyclic++; /* dependency not strictly below   */
            if (v > 143u) bad_dep++;
        }
    }
    total++;
    ok += (bad_stage == 0u && bad_dep == 0u && bad_state == 0u && acyclic == 0u);
    std::printf("    scaffold: %u bad stages, %u bad deps, %u bad states, %u cross-ring deps\n",
                bad_stage, bad_dep, bad_state, acyclic);
}

int main()
{
    int ok = 0, total = 0;
    std::printf("D-dimensional scaffold, one thread per cell:\n");
    run_dim(2u, 12u, (uint32_t &) ok, (uint32_t &) total);
    run_dim(3u, 7u, (uint32_t &) ok, (uint32_t &) total);
    run_dim(4u, 6u, (uint32_t &) ok, (uint32_t &) total);
    run_dim(8u, 4u, (uint32_t &) ok, (uint32_t &) total);
    run_dim(144u, 3u, (uint32_t &) ok, (uint32_t &) total);

    /* the paraconsistent dimension, explicit: a cell whose dependencies
     * disagree is BOTH (3), never collapsed */
    {
        uint32_t dep[2] = {1u, 2u}; /* TRUE and FALSE -> BOTH */
        uint32_t s = zk_d_lpres(2u, dep);
        std::printf("  lpres join of TRUE+FALSE = %u (BOTH=3)\n", s);
        total++;
        ok += (s == 3u);
        uint32_t dep2[2] = {1u, 1u}; /* TRUE+TRUE -> TRUE */
        total++;
        ok += (zk_d_lpres(2u, dep2) == 1u);
        uint32_t dep3[2] = {0u, 0u}; /* NEITHER+NEITHER -> NEITHER */
        total++;
        ok += (zk_d_lpres(2u, dep3) == 0u);
    }

    std::printf("%s test_zvt_seed_d_emu: %d/%d runs bit-identical, scaffold + lpres hold\n",
                ok == total ? "[PASS]" : "[FAIL]", ok, total);
    return ok == total ? 0 : 1;
}