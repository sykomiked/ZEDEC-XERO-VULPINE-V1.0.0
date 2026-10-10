/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_fixed_point.c -- the integer replacements for kernel floating point.
 *
 * Kernel images are integer-only. This host test checks the fixed-point code
 * that replaced the double/float code, against a double-precision oracle
 * computed HERE on the host (the host may use libm; the kernel may not):
 *
 *   zxv_fixed.h        fx_umuldiv64, fx_ratio_q32, fx_log2_q16, fx_exp2_q16,
 *                      fx_sin_turn, fx_isqrt64, cq16_abs
 *   audiogenomics_pro  element / compound / relationship frequencies, dB to
 *                      amplitude, tone generation, RMS, normalize, FM, AM, EM
 *   digital_dna        phi-proportioned checksum, consonant-grid tick
 *   architectural_dir  triadic field measurement sequence
 *   synthesis_engine   coverage floor (exact), surplus ln(), EDP risk
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_fixed.h"
#include "audiogenomics_pro.h"
#include "digital_dna.h"
#include "architectural_directives.h"
#include "synthesis_engine.h"

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] line %d: ", __LINE__);                                                  \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static const double PHI = 1.6180339887498948482;
static double q16(int64_t v)
{
    return (double) v / 65536.0;
}
static double relerr(double got, double want)
{
    return fabs(got - want) / (fabs(want) > 1e-12 ? fabs(want) : 1.0);
}

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* ---------------------------------------------------------------- helpers */
static void test_helpers(void)
{
    printf("=== zxv_fixed.h helpers ===\n");
    int bad = 0;
    for (int i = 0; i < 20000; i++) {
        uint64_t a = rnd(), b = rnd() >> (rnd() % 64), d = (rnd() >> (rnd() % 64)) | 1u;
        unsigned __int128 q = (unsigned __int128) a * b / d;
        uint64_t want = q >> 64 ? UINT64_MAX : (uint64_t) q;
        if (fx_umuldiv64(a, b, d) != want) bad++;
    }
    CHECK(bad == 0, "fx_umuldiv64 matches a 128-bit oracle on 20000 draws (%d wrong)", bad);
    CHECK(fx_umuldiv64(5, 7, 0) == UINT64_MAX, "fx_umuldiv64 by zero saturates");
    CHECK(fx_ratio_q32(1, 3) == 1431655765u, "fx_ratio_q32(1,3) = floor(2^32/3)");
    CHECK(fx_ratio_q32(7, 2) == (7ull << 31), "fx_ratio_q32(7,2) = 3.5");
    CHECK(fx_ratio_q32(1, 0) == 0, "fx_ratio_q32 with b == 0 is 0");

    double worst = 0;
    for (int i = 0; i < 20000; i++) {
        uint64_t x = (rnd() >> (rnd() % 63)) | 1u;
        double want = log2((double) x / 65536.0);
        double got = q16(fx_log2_q16(x));
        double e = fabs(got - want);
        if (e > worst) worst = e;
    }
    CHECK(worst <= 2.0 / 65536.0, "fx_log2_q16 within 2 ulp of log2 (worst %.3g)", worst);
    CHECK(fx_log2_q16(65536) == 0, "log2(1) == 0");
    CHECK(fx_log2_q16(131072) == 65536, "log2(2) == 1");
    CHECK(fx_log2_q16(32768) == -65536, "log2(0.5) == -1");
    CHECK(fx_log2_q16(0) == INT32_MIN, "log2(0) is the INT32_MIN sentinel");

    worst = 0;
    for (int i = 0; i < 20000; i++) {
        int64_t y = (int64_t) (rnd() % (40u * 65536u)) - 16 * 65536; /* -16 .. 24 */
        double want = exp2((double) y / 65536.0);
        double got = q16((int64_t) fx_exp2_q16(y));
        double e = fabs(got - want) / want;
        if (want > 1.0 && e > worst) worst = e;
    }
    CHECK(worst < 2e-5, "fx_exp2_q16 within 2e-5 relative for results > 1 (worst %.3g)", worst);
    CHECK(fx_exp2_q16(0) == 65536, "2^0 == 1");
    CHECK(fx_exp2_q16(10 * 65536) == 1024u * 65536u, "2^10 == 1024 exactly");
    CHECK(fx_exp2_q16(-65536) == 32768, "2^-1 == 0.5 exactly");
    CHECK(fx_exp2_q16((int64_t) 60 * 65536) == UINT64_MAX >> 1, "2^60 saturates");
    CHECK(fx_exp2_q16((int64_t) -80 * 65536) == 0, "2^-80 underflows to 0");

    worst = 0;
    for (uint32_t k = 0; k < 4096; k++) {
        uint32_t t = k * 1048576u + (uint32_t) (rnd() & 0xFFFFF);
        double want = sin((double) t / 4294967296.0 * 2.0 * 3.14159265358979323846);
        double e = fabs(q16(fx_sin_turn(t)) - want);
        if (e > worst) worst = e;
    }
    CHECK(worst <= 1.0 / 65536.0, "fx_sin_turn within 1 ulp of sin (worst %.3g)", worst);
    CHECK(fx_isqrt64(UINT64_MAX) == 0xFFFFFFFFu, "isqrt(2^64-1) == 2^32-1");
    CHECK(fx_isqrt64(1000000) == 1000, "isqrt(10^6) == 1000");
    CHECK(cq16_abs(cq16(3 * 65536, -4 * 65536)) == 5u * 65536u, "|3-4i| == 5");
}

/* ------------------------------------------------------- audiogenomics_pro */
static double elem_ref(int n)
{
    double v = (n / PHI) * 1.125;
    return v * v;
}

static void test_agp(void)
{
    printf("=== audiogenomics_pro (integer) ===\n");
    double worst = 0;
    for (int n = 1; n <= 118; n++) {
        double e = fabs(q16(agp_chemistry_element_frequency((uint8_t) n)) - elem_ref(n));
        if (e > worst) worst = e;
    }
    CHECK(worst <= 1.0 / 65536.0,
          "element frequency [(N/phi)*1.125]^2 within 1 ulp for N=1..118 (worst %.3g)", worst);
    CHECK(agp_chemistry_element_frequency(0) == 0, "element 0 (neutron) has frequency 0");

    /* H2O: {E_H^(2/3) * E_O^(1/3)}^2 */
    uint8_t an[2] = {1, 8}, cnt[2] = {2, 1};
    agp_compound_t water;
    CHECK(agp_chemistry_build_compound(an, cnt, 2, &water) == 0, "build H2O");
    double wref = pow(pow(elem_ref(1), 2.0 / 3.0) * pow(elem_ref(8), 1.0 / 3.0), 2.0);
    CHECK(relerr(q16(water.frequency), wref) < 1e-4, "H2O compound frequency %.6f vs %.6f",
          q16(water.frequency), wref);
    /* Glucose C6H12O6 */
    uint8_t gan[3] = {6, 1, 8}, gcnt[3] = {6, 12, 6};
    agp_compound_t glu;
    agp_chemistry_build_compound(gan, gcnt, 3, &glu);
    double gref = pow(
        pow(elem_ref(6), 6.0 / 24) * pow(elem_ref(1), 12.0 / 24) * pow(elem_ref(8), 6.0 / 24), 2.0);
    CHECK(relerr(q16(glu.frequency), gref) < 1e-4, "glucose compound frequency %.6f vs %.6f",
          q16(glu.frequency), gref);

    /* relationship: values 3.5 and 12, weights 1 and 3 */
    agp_hz_t rf = agp_relationship_frequency(Q16_CONST(7, 2), 12 * 65536, 1, 3);
    double ea = pow(3.5 / PHI * 1.125, 2), eb = pow(12.0 / PHI * 1.125, 2);
    double rref = pow(pow(ea, 0.25) * pow(eb, 0.75), 2.0);
    CHECK(relerr(q16(rf), rref) < 1e-4, "relationship frequency %.6f vs %.6f", q16(rf), rref);
    CHECK(agp_relationship_frequency(0, 65536, 1, 1) == 0, "relationship with a zero value is 0");
    CHECK(agp_relationship_frequency(65536, 65536, 0, 0) == 0,
          "relationship with zero total weight is 0");

    CHECK(abs(agp_db_to_amplitude(-40) - 655) <= 1, "-40 dB = 0.01 (got %d/65536)",
          agp_db_to_amplitude(-40));
    CHECK(agp_db_to_amplitude(0) == 65536, "0 dB = 1.0");
    CHECK(relerr(q16(agp_db_to_amplitude(-6)), pow(10, -0.3)) < 2e-4, "-6 dB = 0.501");
    CHECK(relerr(q16(agp_db_to_amplitude(20)), 10.0) < 1e-4, "+20 dB = 10");

    /* 432 Hz retune: base frequencies scaled by exactly 54/55 */
    agp_freq_map_t fm;
    agp_init_freq_map(&fm, true);
    CHECK(relerr(q16(fm.base_freq[0]), 146.832383958704 * 432.0 / 440.0) < 2e-7, "432 retune of A");
    CHECK(relerr(q16(agp_get_codon_frequency(&fm, "ACG")),
                 (146.832383958704 + 261.625565300599 + 391.995435981749) / 3 * 432 / 440) < 1e-6,
          "codon frequency is the mean of its bases");

    /* Sine tone: 1 kHz at 48 kHz for 10 ms, compared away from the envelope */
    static agp_sample_t tone[AGP_MAX_AUDIO];
    uint32_t n = 0;
    agp_generate_tone(AGP_HZ(1000), 10, 48000, AGP_WAVE_SINE, tone, &n, AGP_MAX_AUDIO);
    CHECK(n == 480, "10 ms at 48 kHz is 480 samples (got %u)", n);
    worst = 0;
    for (uint32_t i = 60; i < 420; i++) {
        double e = fabs(q16(tone[i]) - sin(2 * 3.14159265358979323846 * 1000.0 * i / 48000.0));
        if (e > worst) worst = e;
    }
    CHECK(worst < 4.0 / 65536.0, "sine tone within 4 ulp of sin() (worst %.3g)", worst);
    CHECK(tone[0] == 0, "attack starts from silence");

    agp_generate_tone(AGP_HZ(1000), 10, 48000, AGP_WAVE_SQUARE, tone, &n, AGP_MAX_AUDIO);
    CHECK(tone[100] == 65536 && tone[130] == -65536, "square wave: +1 then -1");
    agp_generate_tone(AGP_HZ(1000), 10, 48000, AGP_WAVE_SAWTOOTH, tone, &n, AGP_MAX_AUDIO);
    CHECK(abs(tone[96 + 12] + 32768) <= 2 && abs(tone[96 + 36] - 32768) <= 2,
          "sawtooth: 2p-1 at p=1/4 and 3/4");
    agp_generate_tone(AGP_HZ(1000), 10, 48000, AGP_WAVE_TRIANGLE, tone, &n, AGP_MAX_AUDIO);
    CHECK(abs(tone[96 + 24] - 65536) <= 2 && abs(tone[96] + 65536) <= 2,
          "triangle: +1 at p=1/2, -1 at p=0");

    /* RMS and normalize */
    for (int i = 0; i < 1000; i++) tone[i] = 32768;
    CHECK(agp_rms(tone, 1000) == 32768, "RMS of a constant 0.5 is 0.5");
    for (int i = 0; i < 4800; i++)
        tone[i] = fx_sin_turn((uint32_t) i * 89478485u); /* 1/48 turn steps */
    CHECK(relerr(q16(agp_rms(tone, 4800)), sqrt(0.5)) < 1e-4, "RMS of a full sine is 1/sqrt(2)");
    agp_normalize(tone, 4800, Q16_CONST(19, 20));
    int32_t peak = 0;
    for (int i = 0; i < 4800; i++) peak = abs(tone[i]) > peak ? abs(tone[i]) : peak;
    CHECK(peak == Q16_CONST(19, 20), "normalize puts the peak at exactly 0.95 (got %d)", peak);
    agp_subaudible_embed(tone, 4800, -40);
    CHECK(relerr(q16(agp_rms(tone, 4800)), 0.01) < 5e-3,
          "subaudible embed sets RMS to -40 dB (%.6f)", q16(agp_rms(tone, 4800)));
    for (int i = 0; i < 16; i++) tone[i] = INT32_MAX;
    CHECK(agp_rms(tone, 16) >= INT32_MAX - 8, "RMS of full-range samples does not overflow (%d)",
          agp_rms(tone, 16));

    /* FM with index 0 is the bare carrier */
    static agp_sample_t mod[2048], out[2048];
    for (int i = 0; i < 2048; i++) mod[i] = fx_sin_turn((uint32_t) i * 12345678u);
    agp_fm_modulate(mod, 2048, AGP_HZ(528), 0, 48000, out);
    worst = 0;
    for (int i = 0; i < 2048; i++) {
        double e = fabs(q16(out[i]) - sin(2 * 3.14159265358979323846 * 528.0 * i / 48000.0));
        if (e > worst) worst = e;
    }
    CHECK(worst < 8.0 / 65536.0, "FM with index 0 equals the carrier (worst %.3g)", worst);
    /* FM with a constant modulator +1 is the carrier shifted by fdev */
    for (int i = 0; i < 2048; i++) mod[i] = 65536;
    agp_fm_modulate(mod, 2048, AGP_HZ(528), Q16_CONST(1, 10), 48000, out);
    worst = 0;
    for (int i = 0; i < 2048; i++) {
        double ph = 2 * 3.14159265358979323846 * (528.0 * i + 52.8 * (i + 1)) / 48000.0;
        double e = fabs(q16(out[i]) - sin(ph));
        if (e > worst) worst = e;
    }
    CHECK(worst < 2e-3, "FM with a constant modulator matches the double model (worst %.3g)",
          worst);

    /* AM: depth 0 leaves a quiet carrier alone; a loud result is scaled to 0.95 */
    for (int i = 0; i < 2048; i++) mod[i] = fx_sin_turn((uint32_t) i * 7654321u) / 2;
    agp_am_modulate(mod, mod, 2048, 0, out);
    CHECK(memcmp(mod, out, sizeof mod) == 0, "AM with depth 0 is the identity");
    for (int i = 0; i < 2048; i++) mod[i] = 65536;
    agp_am_modulate(mod, mod, 2048, Q16_ONE, out);
    CHECK(out[0] == Q16_CONST(19, 20), "AM result over 0.95 is scaled to 0.95 (got %d)", out[0]);

    /* EM tone vs the double formula */
    agp_em_params_t em;
    agp_init_em_params(&em);
    agp_em_generate_tone(AGP_EM_FREQ_A, 20, 48000, false, &em, tone, &n, AGP_MAX_AUDIO);
    worst = 0;
    for (uint32_t i = 120; i < n - 120; i++) {
        double t = (double) i / 48000.0, w = 2 * 3.14159265358979323846 * 545.6;
        double ref = 0.4 * sin(w * t + 3.14159265358979323846 / 2);
        ref += 0.15 * 0.4 * sin(2 * w * t + 3.14159265358979323846 / 2);
        ref *= 1.0 + 0.05 * sin(2 * 3.14159265358979323846 * 54.56 * t);
        double e = fabs(q16(tone[i]) - ref);
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-3, "EM magnetic tone matches the double model (worst %.3g)", worst);
}

/* ------------------------------------------------------------ digital_dna */
static void test_ddna(void)
{
    printf("=== digital_dna (integer) ===\n");
    static uint8_t buf[4096];
    for (uint32_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t) (i * 31u + 7u);
    static ddna_phi_checksum_t cs;
    CHECK(ddna_phi_checksum_compute(buf, sizeof buf, &cs) == 0, "phi checksum computes");
    uint32_t total = 0;
    for (uint32_t i = 0; i < cs.num_chunks; i++) total += cs.chunks[i].size;
    CHECK(total == sizeof buf, "chunk sizes reconstruct the length exactly");
    CHECK(cs.num_chunks > 3, "more than 3 chunks (got %u)", cs.num_chunks);
    CHECK(cs.coherent, "coherent (score %.5f)", q16(cs.coherence_score));
    /* chunk sizes equal the double computation */
    uint32_t rem = sizeof buf, prev = 0, mism = 0;
    for (uint32_t i = 0; i < cs.num_chunks; i++) {
        uint32_t c =
            prev == 0 ? (uint32_t) ((double) rem / PHI / PHI) : (uint32_t) ((double) prev / PHI);
        if (c < DDNA_PHI_MIN_CHUNK || c >= rem) c = rem;
        if (c != cs.chunks[i].size) mism++;
        rem -= c;
        prev = c;
    }
    CHECK(mism == 0, "every chunk size equals the double model (%u differ)", mism);

    /* 1 MB: the header documents 20 chunks with an average ratio near phi */
    static uint8_t big[1u << 20];
    ddna_phi_checksum_compute(big, sizeof big, &cs);
    CHECK(cs.num_chunks == 20, "1 MB yields 20 chunks (got %u)", cs.num_chunks);
    CHECK(cs.coherent, "1 MB decomposition is coherent");

    /* grid tick frequency: gematria-weighted mean of light frequencies times the vowel weight */
    static ddna_consonant_grid_t grid;
    ddna_grid_init(&grid);
    ddna_phase_tick_t t0 = ddna_grid_tick(&grid); /* light, vowel 0 (weight 0) -> clamped to 1 Hz */
    CHECK(t0.frequency == AGP_HZ_ONE, "vowel weight 0 clamps the tick to 1 Hz");
    ddna_phase_tick_t t1 = ddna_grid_tick(&grid); /* light, vowel 1 (weight 1.0) */
    double num = 0, den = 0;
    for (int i = 0; i < 22; i++) {
        num += q16(grid.consonants[i].light_freq) * grid.consonants[i].gematria;
        den += grid.consonants[i].gematria;
    }
    CHECK(relerr(q16(t1.frequency), num / den) < 1e-5, "tick frequency %.5f vs %.5f",
          q16(t1.frequency), num / den);
    CHECK(t1.phase == fx_turn_frac(1, 360), "tick phase is tick/360 of a turn");

    /* numerology / space-time stay integer and deterministic */
    ddna_space_time_op_t op;
    ddna_lunar_date_t d;
    ddna_lunar_from_gregorian(2026, 10, 10, &d);
    ddna_spacetime_compute("ZEDEC", &d, &op);
    double gm = sqrt(q16(op.metadata.harmonic_freq) * q16(op.spatial.frequency));
    CHECK(fabs(q16(op.resonance_freq) - gm) < 2.0 / 65536.0,
          "resonance is the exact geometric mean");
}

/* ------------------------------------------------- architectural_directives */
static void test_triadic(void)
{
    printf("=== triadic field (integer phases) ===\n");
    ddna_triadic_field_t f;
    ddna_triadic_init(&f);
    double pl = 0, pn = 3.14159265358979323846 / 3, po = 2 * 3.14159265358979323846 / 3;
    int mism = 0;
    for (int k = 0; k < 300; k++) {
        double l = sin(pl), nn = sin(pn), o = sin(po);
        int want = (l >= nn && l >= o) ? 0 : (nn >= l && nn >= o) ? 1 : 2;
        /* skip near-ties, where 1-ulp rounding may legitimately pick the other */
        double m1 = fmax(l, fmax(nn, o)), m2 = (l + nn + o) - m1 - fmin(l, fmin(nn, o));
        int got = ddna_triadic_measure(&f);
        if (m1 - m2 > 1e-4 && got != want) mism++;
        double inc = 2 * 3.14159265358979323846 / 100;
        pl += inc;
        pn += inc;
        po += inc;
    }
    CHECK(mism == 0, "300 measurements pick the same domain as the double model (%d differ)", mism);
}

/* ------------------------------------------------------- synthesis_engine */
static synthesis_engine_t g_eng;

static void test_synth(void)
{
    printf("=== synthesis_engine (integer) ===\n");
    synth_engine_init(&g_eng, 1, "fixed");
    g_eng.coverage_r = 2 * Q16_ONE;
    g_eng.coverage_l = Q16_ONE;
    CHECK(synth_check_coverage(&g_eng), "2.0 x 1.0 >= 1.8");
    g_eng.coverage_r = 117965; /* 1.80000305 */
    CHECK(synth_check_coverage(&g_eng), "1.800003 x 1.0 >= 1.8");
    g_eng.coverage_r = 117964; /* 1.79998779 */
    CHECK(!synth_check_coverage(&g_eng), "1.799988 x 1.0 < 1.8 (exact floor, no rounding slack)");
    g_eng.coverage_r = 3 * Q16_ONE / 2;
    g_eng.coverage_l = 6 * Q16_ONE / 5 + 1;
    CHECK(synth_check_coverage(&g_eng), "1.5 x (1.2 + 1 ulp) >= 1.8");

    double worst = 0;
    for (uint32_t nodes = 2; nodes < 200; nodes += 7) {
        for (int32_t u = 0; u <= 4 * Q16_ONE; u += 9000) {
            g_eng.num_ast_nodes = nodes;
            g_eng.coverage_r = u;
            double want = log(1.0 + (nodes - 1.0) * q16(u));
            double e = fabs(q16(synth_compute_surplus(&g_eng)) - want);
            if (e > worst) worst = e;
        }
    }
    CHECK(worst < 5e-5, "surplus ln(1+(N-1)u) within 5e-5 (worst %.3g)", worst);
    g_eng.num_ast_nodes = 1;
    CHECK(synth_compute_surplus(&g_eng) == 0, "fewer than 2 nodes: surplus 0");

    g_eng.num_ast_nodes = 5;
    g_eng.coverage_r = Q16_ONE / 2;
    g_eng.coverage_l = Q16_ONE / 2;
    g_eng.status = 0;
    int32_t risk = synth_assess_risk(&g_eng);
    CHECK(fabs(q16(risk) - (1 - 0.25 / (0.25 + 1.8))) < 2e-5,
          "risk 1 - c/(c+1.8) at c = 0.25 (%.6f)", q16(risk));
    CHECK((g_eng.status & SYNTH_STATUS_RISK_HIGH) != 0, "risk 0.878 > 0.7 raises RISK_HIGH");
    g_eng.coverage_r = 10 * Q16_ONE;
    g_eng.coverage_l = 10 * Q16_ONE;
    risk = synth_assess_risk(&g_eng);
    CHECK(fabs(q16(risk) - (1 - 100 / 101.8)) < 2e-5, "risk at c = 100 (%.6f)", q16(risk));
}

int main(void)
{
    test_helpers();
    test_agp();
    test_ddna();
    test_triadic();
    test_synth();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) printf("ALL PASS\n");
    return g_fail ? 1 : 0;
}
