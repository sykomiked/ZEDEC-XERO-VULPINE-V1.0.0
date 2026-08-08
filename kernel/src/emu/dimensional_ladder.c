/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* dimensional_ladder.c — the 0d..13d cosmology. See dimensional_ladder.h. */
#include "dimensional_ladder.h"
#include "surplus.h"

/* micro/macro correspondence poles encode the Hermetic "as above, so below": the
 * same pattern at PHI-separated scales. */
static const dimension_t g_dims[DL_LEVELS] = {
 { 0,  "Higgs field / presence-at-zero", "quantum vacuum",       "the ground of being",   1, -1 },
 { 1,  "line",                            "worldline",            "axis",                  1, -1 },
 { 2,  "plane",                           "membrane",             "sheet",                 1, -1 },
 { 3,  "space",                           "atomic volume",        "spatial cosmos",        1, -1 },
 { 4,  "event space",                     "quantum event",        "history",               0, -1 },
 { 5,  "photonic reinforcement",          "photon",               "radiant field",         1, -1 },
 { 6,  "timeline",                        "decay clock",          "epoch",                 0, -1 },
 { 7,  "continuum of continuity",         "coherence",            "persistence field",     0, -1 },
 { 8,  "gravitation (2nd order magnetism)","spin-orbit",          "gravity well",          1,  2 },
 { 9,  "continuity of presence",          "identity",             "enduring self",         0, -1 },
 {10,  "galactic <-> electron field",     "electron field",       "the galactic",          0, -1 },
 {11,  "2nd quantization <-> supercluster","2nd quantization",    "galactic supercluster", 0, -1 },
 {12,  "cosmos <-> string",               "string",               "the cosmos",            0, -1 },
 {13,  "universal container / multiverse gateway", "seed of scale","multiverse gateway",   1, -1 },
};

const dimension_t *dimensional_ladder(int *levels_out){
    if (levels_out) *levels_out = DL_LEVELS;
    return g_dims;
}

/* the n-th Fibonacci number (0,1,1,2,3,5,8,13,21,34,55,89,144,…) */
int64_t dl_prime_dimension(int n){
    if (n <= 0) return 0;
    int64_t a = 0, b = 1;
    /* map the primary-dimension index onto the DISTINCT Fibonacci values, skipping
     * the repeated 1: primary dims are 0,1,2,3,5,8,13,… */
    static const int64_t seed[] = {0,1,2,3};
    if (n < 4) return seed[n];
    a = 3; b = 5;                 /* primary dim 3 -> 3, dim 4 -> 5 */
    for (int i = 4; i < n; i++){ int64_t t = a + b; a = b; b = t; }
    return b;
}
int dl_is_prime_dimension(int64_t d){
    if (d < 0) return 0;
    /* d is a Fibonacci number iff 5d^2 ± 4 is a perfect square */
    for (int64_t x = 5*d*d - 4, y = 5*d*d + 4, k = 0; k < 2; k++){
        int64_t v = k ? y : x; if (v < 0) continue;
        int64_t s = 0; while (s*s < v) s++;      /* isqrt */
        if (s*s == v) return 1;
    }
    return 0;
}

uint32_t dl_phi_permille(void){ return 1618; }   /* PHI ≈ 1.618… */

int dimensional_ladder_selfcheck(uint32_t *primes_out, uint32_t *phi_permille_out){
    /* (1) the table's prime flags are EXACTLY the Fibonacci numbers */
    int primes = 0, flags_ok = 1;
    for (int i = 0; i < DL_LEVELS; i++){
        if (g_dims[i].is_prime) primes++;
        if ((int)g_dims[i].is_prime != dl_is_prime_dimension(g_dims[i].d)) flags_ok = 0;
    }
    if (primes_out) *primes_out = (uint32_t)primes;

    /* (2) consecutive Fibonacci-dimension ratios CONVERGE to PHI. The nonzero
     * Fibonacci dims are 1,2,3,5,8,13; the ratios 2/1,3/2,5/3,8/5,13/8 approach
     * the golden ratio. Verify 13/8 is nearer PHI than 3/2, and within epsilon. */
    static const int fib[6] = { 1, 2, 3, 5, 8, 13 };
    surplus_real_t PHI = SR_FROM_FLOAT(1.6180339887);
    surplus_real_t r_first = SR_DIV(SR_FROM_INT(fib[2]), SR_FROM_INT(fib[1])); /* 3/2  */
    surplus_real_t r_last  = SR_DIV(SR_FROM_INT(fib[5]), SR_FROM_INT(fib[4])); /* 13/8 */
    surplus_real_t e_first = SR_SUB(r_first, PHI); if (e_first < SR_ZERO) e_first = SR_SUB(SR_ZERO, e_first);
    surplus_real_t e_last  = SR_SUB(r_last,  PHI); if (e_last  < SR_ZERO) e_last  = SR_SUB(SR_ZERO, e_last);
    int converges = (e_last < e_first) && (e_last < SR_FROM_FLOAT(0.01));
    if (phi_permille_out)
        *phi_permille_out = (uint32_t)((double)r_last / (double)SR_ONE * 1000.0);

    /* (3) micro<->macro correspondence present on every level (as above, so below) */
    int correspondence = 1;
    for (int i = 0; i < DL_LEVELS; i++)
        if (!g_dims[i].micro || !g_dims[i].macro) correspondence = 0;

    /* (4) 8d gravitation carries field value 2 (2nd order of magnetism) */
    int grav_ok = (g_dims[8].field_value == 2);

    /* (5) the ladder is UNBOUNDED — the Fibonacci primary dimensions keep
     * climbing past 13: 21,34,55,89,144,… and Fibonacci membership is detected
     * for any level (a transfinite construct). */
    int unbounded = dl_prime_dimension(6)==13 && dl_prime_dimension(7)==21 &&
                    dl_prime_dimension(8)==34 && dl_prime_dimension(9)==55 &&
                    dl_prime_dimension(10)==89 && dl_prime_dimension(11)==144 &&
                    dl_is_prime_dimension(21) && dl_is_prime_dimension(144) &&
                    !dl_is_prime_dimension(100);

    return flags_ok && converges && correspondence && grav_ok && primes == 7 && unbounded;
}
