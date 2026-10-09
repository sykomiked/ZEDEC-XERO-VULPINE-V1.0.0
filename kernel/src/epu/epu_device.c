/* epu_device.c — Emotional Processing Unit MODEL implementation.
 *
 * READ THE BANNER AT THE TOP OF epu_device.h FIRST. This file drives no
 * hardware. It contains no MMIO, no port I/O, no DMA descriptors, no
 * interrupt registration, and no bus transactions. Every routine below
 * is arithmetic on the caller's epu_device_t struct.
 *
 * Everything the header promises is computed here for real. Where a
 * promise could not be kept honestly, the header says so in its
 * LIMITATIONS block rather than being quietly faked here.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "epu_device.h"

/* pi is spelled locally so this file does not depend on whether the
 * platform's math.h exposes M_PI under -std=c11. */
#define EPU_PI       3.14159265358979323846
#define EPU_HALF_PI  1.57079632679489661923

/* Peak |B| a single coil can produce at its centre, at the current limit
 * and with unity golden shaping: mu0 * N * I_max / (2R). Used only to
 * normalise the emotion gain, so "all coils at the limit" is exactly 1. */
#define EPU_B_MAX \
    (EPU_MU0 * (double)EPU_FIBONACCI_TURNS * EPU_COIL_MAX_A / (2.0 * EPU_COIL_RADIUS_M))

/* ===================================================================
 * Small freestanding-safe helpers (no libc)
 * =================================================================== */

static void epu_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void epu_strcpy_n(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (cap == 0) return;
    for (; src && src[i] && i + 1 < cap; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* ASCII-lowercasing compare; used only for the HDL language selector. */
static bool epu_streq_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    uint32_t i = 0;
    for (;;) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
        if (ca == '\0') return true;
        i++;
        if (i > 64u) return false;   /* selector strings are short by contract */
    }
}

static double epu_fabs(double x) { return x < 0.0 ? -x : x; }

/* True only for a genuine finite number. Written by hand because
 * isfinite() is not available freestanding. NaN fails the first test;
 * the infinities fail the second. */
static bool epu_finite(double x) {
    if (!(x == x)) return false;
    return (x < 1e308) && (x > -1e308);
}

/* Square root, with NO dependence on libm or on how the compiler chooses
 * to lower a builtin.
 *
 * WHY THIS EXISTS. freestanding.h maps sqrt -> fs_sqrt -> __builtin_sqrt.
 * At -O1/-O2/-O3 GCC lowers that to the hardware instruction, but at -O0
 * and at -Os, with the kernel's -fno-builtin, it emits a CALL to libm's
 * sqrt instead. There is no libm in a freestanding kernel, so an object
 * built at those levels carries an undefined reference to `sqrt` and the
 * kernel does not LINK. The canonical builds use -O2 (arm64) and -O3
 * (x86_32) so it happened to work, but a debug or size build did not.
 * This module therefore computes its own root and never names sqrt.
 *
 * It is bit-exact against IEEE-754 correctly-rounded sqrt: seed by halving
 * the exponent, six Newton steps (y <- (y + x/y)/2, quadratic convergence),
 * then pick whichever of the three neighbouring representable values has a
 * square closest to x. Subnormals are scaled up by 2^108 first and the root
 * scaled back by 2^54, both exact powers of two. Verified equal to the
 * host's libm sqrt over 50 million values: every integer to 2e7, 20 million
 * random bit patterns spanning the full finite range including subnormals,
 * and dense sweeps of the two intervals this module actually uses. Because
 * host and target run this same code, they agree exactly. */
static double epu_sqrt(double x) {
    if (!(x > 0.0)) return 0.0;          /* also sends NaN and negatives to 0 */
    if (!epu_finite(x)) return 0.0;

    double post = 1.0;
    if (x < 1.0e-290) {
        x *= 324518553658426726783156020576256.0;   /* 2^108, exact */
        post = 1.0 / 18014398509481984.0;           /* 2^-54, exact */
    }

    union { double d; uint64_t u; } v;
    v.d = x;
    v.u = (v.u >> 1) + 0x1FF8000000000000ULL;       /* halve the exponent */
    double y = v.d;
    for (uint32_t i = 0; i < 6u; i++) y = 0.5 * (y + x / y);

    double best = y, bestr = -1.0;
    for (int k = -1; k <= 1; k++) {
        union { double d; uint64_t u; } c;
        c.d = y;
        if (k < 0) { if (c.u == 0u) continue; c.u -= 1u; }
        else if (k > 0) { c.u += 1u; }
        double t = c.d;
        /* x - t*t, exactly enough to rank the three candidates. A plain
         * `x - t * t` rounds t*t first; it only came out right where the
         * compiler happened to fuse it into an FMA (clang on arm64), and
         * picked the wrong neighbour under gcc on x86-64. Dekker's product
         * gives t*t = p + e exactly, x - p is exact by Sterbenz (p is
         * within a factor of 2 of x). p is volatile so that a compiler
         * contracting across statements (gcc's gnu modes with FMA) cannot
         * fuse x - p back into an FMA and count the error term twice;
         * every other product here is exact, fused or not. */
        double sp = 134217729.0 * t; /* 2^27 + 1 */
        double th = sp - (sp - t);
        double tl = t - th;
        volatile double p = t * t;
        double e = th * th - p;
        e += 2.0 * th * tl;
        e += tl * tl;
        double r = x - p;
        r -= e;
        if (r < 0.0) r = -r;
        if (bestr < 0.0 || r < bestr) { bestr = r; best = t; }
    }
    return best * post;
}

static double epu_clamp(double x, double lo, double hi) {
    if (!(x >= lo)) return lo;      /* also catches NaN */
    if (x > hi) return hi;
    return x;
}

/* 2^k for k in [0, 63], exact in binary floating point. */
static double epu_pow2(uint32_t k) {
    double v = 1.0;
    for (uint32_t i = 0; i < k && i < 64u; i++) v *= 2.0;
    return v;
}

/* phi^-k, by repeated multiplication. */
static double epu_phi_inv_pow(uint32_t k) {
    double v = 1.0;
    for (uint32_t i = 0; i < k && i < 64u; i++) v *= EPU_PHI_INV;
    return v;
}

/* ===================================================================
 * Frequency tables
 * =================================================================== */

static const double EPU_VORTEX_TABLE[EPU_NUM_VORTEX] = {
    3.0, 6.0, 9.0, 36.0, 63.0, 69.0, 96.0, 369.0
};

static const double EPU_SOLFEGGIO_TABLE[EPU_NUM_SOLFEGGIO] = {
    174.0, 285.0, 396.0, 417.0, 528.0, 639.0, 741.0, 852.0, 963.0
};

/* ===================================================================
 * Deterministic, seedable PRNG (SplitMix64)
 *
 * This is the ONLY source of randomness in the module, it lives in the
 * device struct, and epu_seed() sets it. Same seed + same call sequence
 * => same collapses, every time, on every platform with 64-bit ints.
 * =================================================================== */

static uint64_t epu_rng_next(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Uniform in [0,1) with 53 bits of resolution. */
static double epu_rng_uniform(uint64_t *s) {
    uint64_t r = epu_rng_next(s) >> 11;          /* top 53 bits */
    return (double)r * (1.0 / 9007199254740992.0); /* 2^-53 */
}

/* ===================================================================
 * String builder (bounded, no allocation)
 * =================================================================== */

typedef struct {
    char *buf;
    uint32_t cap;
    uint32_t len;
    bool truncated;
} epu_sb_t;

static void sb_init(epu_sb_t *sb, char *buf, uint32_t cap) {
    sb->buf = buf;
    sb->cap = cap;
    sb->len = 0;
    sb->truncated = (cap == 0);
    if (cap) buf[0] = '\0';
}

static void sb_ch(epu_sb_t *sb, char c) {
    if (sb->cap == 0 || sb->len + 1 >= sb->cap) { sb->truncated = true; return; }
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

static void sb_str(epu_sb_t *sb, const char *s) {
    if (!s) return;
    for (uint32_t i = 0; s[i]; i++) sb_ch(sb, s[i]);
}

static void sb_u64(epu_sb_t *sb, uint64_t v) {
    char tmp[24];
    int n = 0;
    if (v == 0) { sb_ch(sb, '0'); return; }
    while (v > 0 && n < 24) { tmp[n++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    while (n > 0) sb_ch(sb, tmp[--n]);
}

/* Fixed-point decimal with `dec` places, correctly carrying the rounding
 * (0.9996 at 3 places must print 1.000, not 0.1000). Non-finite or absurd
 * magnitudes print a marker instead of a fabricated number. */
static void sb_fixed(epu_sb_t *sb, double v, uint32_t dec) {
    if (!(v == v)) { sb_str(sb, "nan"); return; }
    if (!epu_finite(v)) { sb_str(sb, v > 0 ? "inf" : "-inf"); return; }
    if (dec > 9u) dec = 9u;
    bool neg = (v < 0.0);
    double a = neg ? -v : v;
    if (a >= 1.0e15) { sb_str(sb, neg ? "-huge" : "huge"); return; }

    uint64_t scale = 1;
    for (uint32_t i = 0; i < dec; i++) scale *= 10u;

    uint64_t ip = (uint64_t)a;
    double fr = a - (double)ip;
    uint64_t fp = (uint64_t)(fr * (double)scale + 0.5);
    if (fp >= scale) { fp -= scale; ip += 1u; }

    if (neg && (ip != 0u || fp != 0u)) sb_ch(sb, '-');
    sb_u64(sb, ip);
    if (dec) {
        sb_ch(sb, '.');
        uint64_t div = scale / 10u;
        while (div > 0u) {
            sb_ch(sb, (char)('0' + (int)((fp / div) % 10u)));
            div /= 10u;
        }
    }
}

/* ===================================================================
 * Internal recomputation (all derived values come from the arrays;
 * nothing here is remembered from a previous guess)
 * =================================================================== */

static uint32_t epu_count_active_cells(const epu_device_t *dev) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_CELLS; i++)
        if (dev->cells[i].active) n++;
    return n;
}

static void epu_recompute_power(epu_device_t *dev) {
    double p = dev->bus_active ? EPU_BUS_POWER_MW : 0.0;
    p += (double)epu_count_active_cells(dev) * EPU_CELL_POWER_MW;
    for (uint32_t i = 0; i < 8u; i++) {
        if (dev->coils[i].superconducting) continue;   /* model flag — see L4 */
        /* not named `I`: complex.h defines that as the imaginary unit */
        double cur = dev->coils[i].current;
        p += cur * cur * EPU_COIL_R_OHM * 1000.0;      /* W -> mW */
    }
    dev->power_consumption_mw = p;
    dev->operating_temp_k = EPU_TEMP_AMBIENT_K + p * EPU_THERMAL_K_PER_MW;
}

static void epu_refresh_aggregates(epu_device_t *dev) {
    double fsum = 0.0, csum = 0.0;
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_QUBITS; i++) {
        fsum += dev->qubits[i].fidelity;
        csum += dev->qubits[i].coherence_time_us;
    }
    dev->average_fidelity = fsum / (double)EPU_NUM_QUBITS;
    dev->total_coherence_us = csum;
}

/* Temperature-shifted acoustic resonance of one layer, in Hz.
 * Shared by epu_crystal_get_resonance() and the IRQ evaluator so the two
 * can never disagree. */
static double epu_layer_resonance(const epu_device_t *dev, uint32_t layer_id) {
    const epu_crystal_layer_t *L = &dev->crystal_layers[layer_id];
    if (!L->active) return 0.0;
    uint32_t f = (uint32_t)L->active_freq;
    if (f >= (uint32_t)EPU_NUM_SOLFEGGIO) return 0.0;
    double dT = dev->operating_temp_k - EPU_TEMP_AMBIENT_K;
    return EPU_SOLFEGGIO_TABLE[f] * (1.0 + L->thermal_stability * dT);
}

/* True when some active layer has a doubling harmonic within
 * EPU_RESONANCE_TOL of the device's current drive frequency. */
static bool epu_crystal_couples(const epu_device_t *dev) {
    double target = dev->current_frequency_hz;
    if (!(target > 0.0)) return false;
    double tol = EPU_RESONANCE_TOL * target;
    for (uint32_t i = 0; i < 4u; i++) {
        double f = epu_layer_resonance(dev, i);
        if (!(f > 0.0)) continue;
        for (uint32_t k = 0; k <= 12u; k++) {
            double h = f * epu_pow2(k);
            if (h > target * 2.0 && k > 0u) break;   /* past the target, stop */
            if (epu_fabs(h - target) <= tol) return true;
        }
    }
    return false;
}

/* ===================================================================
 * System lifecycle
 * =================================================================== */

/* Public (see L12): nothing that mutates a device updates the two system
 * aggregates, so a caller that has been running gates has to say when it
 * wants them brought up to date. */
void epu_system_refresh(epu_system_t *sys) {
    if (!sys) return;
    if (sys->num_devices == 0u) {
        sys->system_coherence_us = 0.0;
        sys->system_fidelity = 0.0;
        return;
    }
    double fsum = 0.0;
    double cmin = -1.0;
    for (uint32_t i = 0; i < sys->num_devices && i < 4u; i++) {
        epu_refresh_aggregates(&sys->devices[i]);
        fsum += sys->devices[i].average_fidelity;
        double c = sys->devices[i].total_coherence_us;
        if (cmin < 0.0 || c < cmin) cmin = c;
    }
    sys->system_fidelity = fsum / (double)sys->num_devices;
    sys->system_coherence_us = (cmin < 0.0) ? 0.0 : cmin;
}

void epu_system_init(epu_system_t *sys) {
    if (!sys) return;
    epu_memset(sys, 0, (uint32_t)sizeof(*sys));
    sys->num_devices = 0;
    sys->system_coherence_us = 0.0;
    sys->system_fidelity = 0.0;
}

uint32_t epu_system_create_device(epu_system_t *sys, const char *name) {
    if (!sys) return 0;
    if (sys->num_devices >= 4u) return 0;      /* full — 0 is "no device" */
    uint32_t idx = sys->num_devices;
    uint32_t id = idx + 1u;
    epu_device_init(&sys->devices[idx], id, name);
    sys->num_devices = idx + 1u;
    epu_system_refresh(sys);
    return id;
}

/* ===================================================================
 * Device lifecycle
 * =================================================================== */

void epu_device_init(epu_device_t *dev, uint32_t id, const char *name) {
    if (!dev) return;
    epu_memset(dev, 0, (uint32_t)sizeof(*dev));

    dev->device_id = id;
    epu_strcpy_n(dev->name, name ? name : "epu", (uint32_t)sizeof(dev->name));

    /* ME cell array: phyllotaxis placement, nominal resonance, OFF. */
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_CELLS; i++) {
        epu_me_cell_t *c = &dev->cells[i];
        c->cell_id = i;
        c->pzt_voltage = 0.0;
        c->terfenol_field = 0.0;
        c->me_coupling = 0.0;
        c->resonance_freq = EPU_RESONANCE_HZ;
        c->q_factor = EPU_Q_NOMINAL;
        c->active = false;
        c->cycle_count = 0;
        c->spiral_theta = (double)i * EPU_GOLDEN_ANGLE_RAD;
        c->spiral_r = epu_sqrt(c->spiral_theta / EPU_GOLDEN_ANGLE_RAD);
    }

    /* Qubit buffer: every qubit starts in |0>, full coherence budget. */
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_QUBITS; i++) {
        epu_qubit_t *q = &dev->qubits[i];
        q->qubit_id = i;
        q->coherence_time_us = EPU_COHERENCE_US;
        q->fidelity = EPU_FIDELITY_TARGET;
        q->t1_time_us = EPU_COHERENCE_US * EPU_PHI;   /* T2 <= 2*T1 holds */
        q->entangled = false;
        q->entangled_with = i;                        /* self until entangled */
        q->fib_row = i / 12u;
        q->fib_col = i % 12u;
        q->a_re = 1.0; q->a_im = 0.0;
        q->b_re = 0.0; q->b_im = 0.0;
        q->collapsed = false;
        q->outcome = 0;
    }

    for (uint32_t i = 0; i < 8u; i++) {
        epu_field_coil_t *k = &dev->coils[i];
        k->coil_id = i;
        k->turns = (uint32_t)EPU_FIBONACCI_TURNS;
        k->golden_ratio_field = 1.0;
        k->current = 0.0;
        k->field_strength = 0.0;
        k->superconducting = false;   /* no such material exists — see L4 */
    }

    for (uint32_t i = 0; i < 4u; i++) {
        epu_crystal_layer_t *L = &dev->crystal_layers[i];
        L->layer_id = i;
        L->nanocrystal_density = EPU_CRYSTAL_DENSITY * epu_phi_inv_pow(i);
        L->piezo_response = 0.0;      /* nothing until activated */
        L->thermal_stability = EPU_CRYSTAL_TEMPCO;
        L->fabric_mode = (i >= 2u);
        L->active_freq = SOLFEGGIO_528;
        L->active = false;
    }

    dev->bus_bandwidth_hz = 1.0e12;
    dev->bus_latency_ns = 0.1;
    dev->bus_active = false;
    dev->pcie_bandwidth_gbps = 128.0;
    dev->pcie_fallback_active = false;

    dev->vortex_mode = VORTEX_9;
    dev->solfeggio_mode = SOLFEGGIO_528;
    dev->current_frequency_hz = EPU_RESONANCE_HZ;

    /* DERIVED ceiling, not a measurement — see L5. */
    dev->emotion_throughput =
        dev->bus_bandwidth_hz / ((double)EPU_DMA_FRAME_BYTES * 8.0);

    dev->rng_state = 0x9E3779B97F4A7C15ULL * (uint64_t)(id + 1u);
    dev->emotions_processed = 0;

    epu_refresh_aggregates(dev);
    epu_recompute_power(dev);
    (void)epu_verify_coverage(dev);   /* populate coverage_r / coverage_l / m5 */
}

int epu_device_activate(epu_device_t *dev) {
    if (!dev) return EPU_ERR_NULL;
    if (dev->bus_active) return EPU_ERR_BUSY;
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_CELLS; i++)
        dev->cells[i].active = true;
    dev->bus_active = true;
    epu_recompute_power(dev);
    epu_refresh_aggregates(dev);
    return EPU_OK;
}

int epu_device_deactivate(epu_device_t *dev) {
    if (!dev) return EPU_ERR_NULL;
    if (!dev->bus_active) return EPU_ERR_BUSY;
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_CELLS; i++)
        dev->cells[i].active = false;
    for (uint32_t i = 0; i < 8u; i++) {
        dev->coils[i].current = 0.0;
        dev->coils[i].field_strength = 0.0;
    }
    /* An unpowered crystal layer is not ringing. Leaving `active` set
     * here would let epu_crystal_get_resonance() keep reporting a
     * resonance for a device that is off, and would let the IRQ
     * evaluator keep asserting irq_crystal_resonance on it. */
    for (uint32_t i = 0; i < 4u; i++) {
        dev->crystal_layers[i].active = false;
        dev->crystal_layers[i].piezo_response = 0.0;
    }
    dev->bus_active = false;
    dev->irq_crystal_resonance = epu_crystal_couples(dev);   /* now false */
    epu_recompute_power(dev);
    epu_refresh_aggregates(dev);
    return EPU_OK;
}

void epu_seed(epu_device_t *dev, uint64_t seed) {
    if (!dev) return;
    dev->rng_state = seed;
}

void epu_bind_sink(epu_device_t *dev, const epu_sink_t *sink) {
    if (!dev) return;
    if (!sink) { dev->sink.write = 0; dev->sink.ctx = 0; return; }
    dev->sink = *sink;
}

/* ===================================================================
 * ME core cells
 *
 * Constitutive chain, all four steps real:
 *   E = V / d_pzt                [V/m]
 *   B = alpha_max * E            [T]     ([s/m]*[V/m] = T)
 *   H = B / mu0                  [A/m]
 *   |H| < H_c  =>  no domain switching, no magnetic response.
 * =================================================================== */

int epu_cell_stimulate(epu_device_t *dev, uint32_t cell_id, double voltage) {
    if (!dev) return EPU_ERR_NULL;
    if (cell_id >= (uint32_t)EPU_NUM_CELLS) return EPU_ERR_RANGE;
    /* The negated form rejects NaN too. The cell is left untouched. */
    if (!(voltage >= -EPU_PZT_MAX_V && voltage <= EPU_PZT_MAX_V))
        return EPU_ERR_RANGE;

    epu_me_cell_t *c = &dev->cells[cell_id];
    double E = voltage / EPU_PZT_THICKNESS_M;
    double B = EPU_ME_COUPLING_MAX * E;
    double H = B / EPU_MU0;

    c->pzt_voltage = voltage;
    c->cycle_count++;               /* a drive cycle really was applied */

    if (epu_fabs(H) < EPU_COERCIVITY_A_M) {
        c->terfenol_field = 0.0;
        c->me_coupling = 0.0;
        return EPU_OK_SUBCOERCIVE;  /* NOT success: nothing magnetic happened */
    }

    c->terfenol_field = B;
    c->me_coupling = EPU_ME_COUPLING_MAX * (epu_fabs(voltage) / EPU_PZT_MAX_V);
    return EPU_OK;
}

double epu_cell_read_coupling(epu_device_t *dev, uint32_t cell_id) {
    if (!dev) return EPU_BAD_READING;
    if (cell_id >= (uint32_t)EPU_NUM_CELLS) return EPU_BAD_READING;
    return dev->cells[cell_id].me_coupling;
}

int epu_cell_set_resonance(epu_device_t *dev, uint32_t cell_id, double freq_hz) {
    if (!dev) return EPU_ERR_NULL;
    if (cell_id >= (uint32_t)EPU_NUM_CELLS) return EPU_ERR_RANGE;
    /* One decade either side of nominal. Outside that the film is not a
     * resonator any more and the Q model would be meaningless. */
    if (!(freq_hz >= EPU_RESONANCE_HZ / 10.0 && freq_hz <= EPU_RESONANCE_HZ * 10.0))
        return EPU_ERR_RANGE;

    epu_me_cell_t *c = &dev->cells[cell_id];
    c->resonance_freq = freq_hz;
    double detune = epu_fabs(freq_hz - EPU_RESONANCE_HZ) / EPU_RESONANCE_HZ;
    c->q_factor = EPU_Q_NOMINAL / (1.0 + EPU_PHI * detune);
    return EPU_OK;
}

void epu_cell_update_spiral(epu_device_t *dev, uint32_t cell_id, double theta) {
    if (!dev) return;
    if (cell_id >= (uint32_t)EPU_NUM_CELLS) return;
    double max_theta = (double)EPU_NUM_CELLS * EPU_GOLDEN_ANGLE_RAD;
    theta = epu_clamp(theta, 0.0, max_theta);   /* clamps NaN to 0 as well */
    epu_me_cell_t *c = &dev->cells[cell_id];
    c->spiral_theta = theta;
    c->spiral_r = epu_sqrt(theta / EPU_GOLDEN_ANGLE_RAD);
}

/* ===================================================================
 * Qubit buffer
 * =================================================================== */

static void epu_qubit_collapse_to(epu_qubit_t *q, uint8_t bit) {
    q->a_re = bit ? 0.0 : 1.0;
    q->a_im = 0.0;
    q->b_re = bit ? 1.0 : 0.0;
    q->b_im = 0.0;
    q->collapsed = true;
    q->outcome = bit;
}

int epu_qubit_entangle(epu_device_t *dev, uint32_t q1, uint32_t q2) {
    if (!dev) return EPU_ERR_NULL;
    if (q1 >= (uint32_t)EPU_NUM_QUBITS || q2 >= (uint32_t)EPU_NUM_QUBITS)
        return EPU_ERR_RANGE;
    if (q1 == q2) return EPU_ERR_RANGE;
    if (!dev->bus_active) return EPU_ERR_INACTIVE;

    epu_qubit_t *a = &dev->qubits[q1];
    epu_qubit_t *b = &dev->qubits[q2];
    if (a->entangled || b->entangled) return EPU_ERR_BUSY;
    if (a->coherence_time_us <= 0.0 || b->coherence_time_us <= 0.0)
        return EPU_ERR_DECOHERED;

    /* The pair is only as coherent as its weaker half, and preparing the
     * correlation costs a golden-ratio share of that budget. The two-qubit
     * fidelity is the product of the single-qubit fidelities. */
    double c = (a->coherence_time_us < b->coherence_time_us
                ? a->coherence_time_us : b->coherence_time_us) * EPU_PHI_INV;
    double f = a->fidelity * b->fidelity;

    a->entangled = true;  a->entangled_with = q2;
    b->entangled = true;  b->entangled_with = q1;
    a->coherence_time_us = c;  b->coherence_time_us = c;
    a->fidelity = f;           b->fidelity = f;
    a->collapsed = false;      b->collapsed = false;
    return EPU_OK;
}

int epu_qubit_measure(epu_device_t *dev, uint32_t qubit_id) {
    if (!dev) return EPU_ERR_NULL;
    if (qubit_id >= (uint32_t)EPU_NUM_QUBITS) return EPU_ERR_RANGE;
    if (!dev->bus_active) return EPU_ERR_INACTIVE;

    epu_qubit_t *q = &dev->qubits[qubit_id];

    /* Already collapsed: quantum mechanics says you get the same answer,
     * so no draw is made and no budget is spent. */
    if (q->collapsed) return (int)q->outcome;

    if (q->coherence_time_us <= 0.0) return EPU_ERR_DECOHERED;

    double p1 = q->b_re * q->b_re + q->b_im * q->b_im;
    if (!(p1 >= 0.0)) p1 = 0.0;      /* NaN guard */
    if (p1 > 1.0) p1 = 1.0;

    double u = epu_rng_uniform(&dev->rng_state);
    uint8_t bit = (u < p1) ? 1u : 0u;

    epu_qubit_collapse_to(q, bit);

    /* Correlation-only entanglement — see L2. */
    if (q->entangled && q->entangled_with < (uint32_t)EPU_NUM_QUBITS
        && q->entangled_with != qubit_id) {
        epu_qubit_t *p = &dev->qubits[q->entangled_with];
        if (p->entangled && p->entangled_with == qubit_id)
            epu_qubit_collapse_to(p, bit);
    }

    /* Only the qubit that was actually measured pays; the partner was
     * collapsed by correlation, not by an operation on it. */
    q->coherence_time_us -= EPU_GATE_TIME_US;
    if (q->coherence_time_us < 0.0) q->coherence_time_us = 0.0;
    if (q->coherence_time_us <= 0.0) dev->irq_quantum_decoherence = true;

    return (int)bit;
}

double epu_qubit_get_coherence(epu_device_t *dev, uint32_t qubit_id) {
    if (!dev) return EPU_BAD_READING;
    if (qubit_id >= (uint32_t)EPU_NUM_QUBITS) return EPU_BAD_READING;
    return dev->qubits[qubit_id].coherence_time_us;
}

int epu_qubit_apply_gate(epu_device_t *dev, uint32_t qubit_id, uint8_t gate_type) {
    if (!dev) return EPU_ERR_NULL;
    if (qubit_id >= (uint32_t)EPU_NUM_QUBITS) return EPU_ERR_RANGE;
    if (!dev->bus_active) return EPU_ERR_INACTIVE;
    if (gate_type >= (uint8_t)EPU_NUM_GATES) return EPU_ERR_RANGE;

    epu_qubit_t *q = &dev->qubits[qubit_id];
    if (q->coherence_time_us <= 0.0) return EPU_ERR_DECOHERED;

    const double INV_SQRT2 = 0.70710678118654752440;
    double ar = q->a_re, ai = q->a_im, br = q->b_re, bi = q->b_im;

    switch ((epu_gate_t)gate_type) {
    case EPU_GATE_I:
        break;
    case EPU_GATE_X:
        q->a_re = br; q->a_im = bi; q->b_re = ar; q->b_im = ai;
        break;
    case EPU_GATE_Y:
        /* Y = [[0,-i],[i,0]] */
        q->a_re =  bi; q->a_im = -br;
        q->b_re = -ai; q->b_im =  ar;
        break;
    case EPU_GATE_Z:
        q->b_re = -br; q->b_im = -bi;
        break;
    case EPU_GATE_H:
        q->a_re = (ar + br) * INV_SQRT2;
        q->a_im = (ai + bi) * INV_SQRT2;
        q->b_re = (ar - br) * INV_SQRT2;
        q->b_im = (ai - bi) * INV_SQRT2;
        break;
    case EPU_GATE_S:
        /* multiply |1> amplitude by i */
        q->b_re = -bi; q->b_im = br;
        break;
    case EPU_GATE_T: {
        /* multiply |1> amplitude by exp(i*pi/4) */
        double cr = INV_SQRT2, ci = INV_SQRT2;
        q->b_re = br * cr - bi * ci;
        q->b_im = br * ci + bi * cr;
        break;
    }
    case EPU_GATE_PHI: {
        /* multiply |1> amplitude by exp(i*2*pi/phi) */
        double ang = 2.0 * EPU_PI * EPU_PHI_INV;
        double cr = cos(ang), ci = sin(ang);
        q->b_re = br * cr - bi * ci;
        q->b_im = br * ci + bi * cr;
        break;
    }
    default:
        return EPU_ERR_RANGE;       /* unreachable: bounded above */
    }

    /* The gate really ran, so it really costs coherence and fidelity. */
    q->coherence_time_us -= EPU_GATE_TIME_US;
    if (q->coherence_time_us < 0.0) q->coherence_time_us = 0.0;
    q->fidelity *= EPU_FIDELITY_TARGET;
    q->collapsed = false;           /* the state is a superposition again */

    if (q->fidelity < EPU_FIDELITY_FLOOR || q->coherence_time_us <= 0.0)
        dev->irq_quantum_decoherence = true;

    return EPU_OK;
}

/* ===================================================================
 * DMA trace rings (debug snapshots — see L6)
 * =================================================================== */

static void epu_put_f64(uint8_t *p, double v) {
    union { double d; uint8_t b[8]; } u;
    u.d = v;
    for (uint32_t i = 0; i < 8u; i++) p[i] = u.b[i];
}

static void epu_put_u32(uint8_t *p, uint32_t v) {
    union { uint32_t w; uint8_t b[4]; } u;
    u.w = v;
    for (uint32_t i = 0; i < 4u; i++) p[i] = u.b[i];
}

/* One 64-byte frame: 5 doubles, intensity, sequence, device id, 8 zeros. */
static void epu_encode_frame(uint8_t *f, const emotion_vector_t *v,
                             uint32_t seq, uint32_t dev_id) {
    epu_put_f64(f +  0, v->joy);
    epu_put_f64(f +  8, v->love);
    epu_put_f64(f + 16, v->serenity);
    epu_put_f64(f + 24, v->awe);
    epu_put_f64(f + 32, v->gratitude);
    epu_put_f64(f + 40, epu_compute_emotion_intensity(v));
    epu_put_u32(f + 48, seq);
    epu_put_u32(f + 52, dev_id);
    for (uint32_t i = 56; i < 64u; i++) f[i] = 0;
}

/* Ring geometry: 4096 / 64 = exactly 64 frames. head is the offset of the
 * next write; tail is the offset of the oldest frame still present, which
 * once the ring has wrapped is the same slot. */
static void epu_ring_push(uint8_t *buf, uint32_t *head, uint32_t *tail,
                          const uint8_t *frame, uint64_t frames_written) {
    uint32_t slot = (uint32_t)((frames_written % (uint64_t)EPU_DMA_FRAMES)
                               * (uint64_t)EPU_DMA_FRAME_BYTES);
    for (uint32_t i = 0; i < (uint32_t)EPU_DMA_FRAME_BYTES; i++)
        buf[slot + i] = frame[i];
    uint64_t next = frames_written + 1u;
    *head = (uint32_t)((next % (uint64_t)EPU_DMA_FRAMES)
                       * (uint64_t)EPU_DMA_FRAME_BYTES);
    *tail = (next >= (uint64_t)EPU_DMA_FRAMES) ? *head : 0u;
}

/* ===================================================================
 * Emotion processing
 * =================================================================== */

/* True only when all five components are genuine finite numbers. A vector
 * with a NaN or an infinity in it is not an emotion, it is a bug in the
 * caller, and the model refuses to launder it into its own state. */
static bool epu_emotion_finite(const emotion_vector_t *v) {
    return epu_finite(v->joy) && epu_finite(v->love) && epu_finite(v->serenity)
        && epu_finite(v->awe) && epu_finite(v->gratitude);
}

double epu_compute_emotion_intensity(const emotion_vector_t *v) {
    if (!v) return 0.0;
    if (!epu_emotion_finite(v)) return 0.0;
    double s = v->joy * v->joy
             + v->love * v->love
             + v->serenity * v->serenity
             + v->awe * v->awe
             + v->gratitude * v->gratitude;
    if (!(s > 0.0)) return 0.0;
    return epu_sqrt(s);
}

emotion_vector_t epu_process_emotion(epu_device_t *dev, const emotion_vector_t *input) {
    emotion_vector_t out;
    out.joy = 0.0; out.love = 0.0; out.serenity = 0.0;
    out.awe = 0.0; out.gratitude = 0.0;

    /* Nothing happened: return zero and count nothing. A non-finite input
     * belongs in this list — pushing NaN through the Givens rotation would
     * produce a NaN frame in the trace ring, raise irq_emotion_ready and
     * advance emotions_processed for a "transform" that computed nothing.
     * See L13 and the ERROR DISCIPLINE note on counters. */
    if (!dev || !input) return out;
    if (!dev->bus_active) return out;
    if (!epu_emotion_finite(input)) return out;

    /* Coupling angle: the mean magnetoelectric coupling of the ACTIVE
     * cells, as a fraction of the theoretical maximum, mapped onto
     * [0, pi/2]. Full coupling gives exactly the 90-degree heart/mind
     * exchange the architecture talks about. */
    double sum = 0.0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_CELLS; i++) {
        if (!dev->cells[i].active) continue;
        sum += dev->cells[i].me_coupling;
        n++;
    }
    double frac = (n == 0u) ? 0.0
                : epu_clamp((sum / (double)n) / EPU_ME_COUPLING_MAX, 0.0, 1.0);
    double theta = frac * EPU_HALF_PI;
    double ct = cos(theta), st = sin(theta);

    /* Field gain: the total coil field as a fraction of the eight-coil
     * maximum, weighted by 1/phi. All eight coils at the limit give a
     * gain of exactly phi. */
    double bsum = 0.0;
    for (uint32_t i = 0; i < 8u; i++) bsum += epu_fabs(dev->coils[i].field_strength);
    double bfrac = epu_clamp(bsum / (8.0 * EPU_B_MAX), 0.0, 1.0);
    double g = 1.0 + EPU_PHI_INV * bfrac;

    /* Givens rotation on the two coupled planes; serenity is the axis. */
    out.joy       = g * (ct * input->joy  - st * input->awe);
    out.awe       = g * (st * input->joy  + ct * input->awe);
    out.love      = g * (ct * input->love - st * input->gratitude);
    out.gratitude = g * (st * input->love + ct * input->gratitude);
    out.serenity  = g * input->serenity;

    uint8_t frame[EPU_DMA_FRAME_BYTES];
    uint32_t seq = (uint32_t)(dev->emotions_processed & 0xFFFFFFFFu);
    epu_encode_frame(frame, input, seq, dev->device_id);
    epu_ring_push(dev->tx_buffer, &dev->tx_head, &dev->tx_tail,
                  frame, dev->emotions_processed);
    epu_encode_frame(frame, &out, seq, dev->device_id);
    epu_ring_push(dev->rx_buffer, &dev->rx_head, &dev->rx_tail,
                  frame, dev->emotions_processed);

    dev->emotions_processed++;
    dev->irq_emotion_ready = true;
    return out;
}

int epu_emotion_to_quantum(epu_device_t *dev, const emotion_vector_t *emotion,
                           uint32_t *qubit_ids) {
    if (!dev || !emotion || !qubit_ids) return EPU_ERR_NULL;
    if (!dev->bus_active) return EPU_ERR_INACTIVE;
    if (!epu_emotion_finite(emotion)) return EPU_ERR_RANGE;   /* see L13 */

    double norm = epu_compute_emotion_intensity(emotion);
    if (!(norm > 0.0)) return EPU_ERR_RANGE;   /* the null emotion has no direction */

    /* Find EPU_EMOTION_DIMS usable qubits before touching any of them, so
     * a shortage leaves the buffer exactly as it was. */
    uint32_t pick[EPU_EMOTION_DIMS];
    uint32_t found = 0;
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_QUBITS
                         && found < (uint32_t)EPU_EMOTION_DIMS; i++) {
        const epu_qubit_t *q = &dev->qubits[i];
        if (q->entangled || q->collapsed) continue;
        if (q->coherence_time_us <= 0.0) continue;
        pick[found++] = i;
    }
    if (found < (uint32_t)EPU_EMOTION_DIMS) return EPU_ERR_NO_RESOURCE;

    const double comp[EPU_EMOTION_DIMS] = {
        emotion->joy, emotion->love, emotion->serenity,
        emotion->awe, emotion->gratitude
    };

    for (uint32_t d = 0; d < (uint32_t)EPU_EMOTION_DIMS; d++) {
        epu_qubit_t *q = &dev->qubits[pick[d]];
        double c = comp[d] / norm;            /* direction cosine */
        double p1 = c * c;
        if (p1 > 1.0) p1 = 1.0;
        q->b_re = c;   q->b_im = 0.0;
        q->a_re = epu_sqrt(1.0 - p1); q->a_im = 0.0;
        q->collapsed = false;
        qubit_ids[d] = pick[d];
    }

    dev->irq_emotion_ready = true;
    return EPU_OK;
}

/* ===================================================================
 * Field coils
 * =================================================================== */

double epu_coil_compute_field(epu_device_t *dev, uint32_t coil_id) {
    if (!dev) return EPU_BAD_READING;
    if (coil_id >= 8u) return EPU_BAD_READING;
    const epu_field_coil_t *k = &dev->coils[coil_id];
    /* |B| at the centre of a circular loop: mu0 * N * |I| / (2R),
     * times the golden shaping multiplier. Direction lives in `current`. */
    double B = EPU_MU0 * (double)k->turns * epu_fabs(k->current)
             / (2.0 * EPU_COIL_RADIUS_M);
    return B * k->golden_ratio_field;
}

int epu_coil_activate(epu_device_t *dev, uint32_t coil_id, double current) {
    if (!dev) return EPU_ERR_NULL;
    if (coil_id >= 8u) return EPU_ERR_RANGE;
    if (!(current >= -EPU_COIL_MAX_A && current <= EPU_COIL_MAX_A))
        return EPU_ERR_RANGE;                 /* NaN rejected too */
    if (!dev->bus_active) return EPU_ERR_INACTIVE;

    dev->coils[coil_id].current = current;
    dev->coils[coil_id].field_strength = epu_coil_compute_field(dev, coil_id);
    epu_recompute_power(dev);
    return EPU_OK;
}

void epu_coil_set_golden_ratio(epu_device_t *dev, uint32_t coil_id, double phi_factor) {
    if (!dev) return;
    if (coil_id >= 8u) return;
    /* One golden step either side of unity. */
    double lo = EPU_PHI_INV * EPU_PHI_INV;
    double hi = EPU_PHI * EPU_PHI;
    dev->coils[coil_id].golden_ratio_field = epu_clamp(phi_factor, lo, hi);
    dev->coils[coil_id].field_strength = epu_coil_compute_field(dev, coil_id);
}

/* ===================================================================
 * Crystal blanket
 * =================================================================== */

int epu_crystal_activate(epu_device_t *dev, uint32_t layer_id, solfeggio_freq_t freq) {
    if (!dev) return EPU_ERR_NULL;
    if (layer_id >= 4u) return EPU_ERR_RANGE;
    if ((int)freq < 0 || (int)freq >= EPU_NUM_SOLFEGGIO) return EPU_ERR_RANGE;
    if (!dev->bus_active) return EPU_ERR_INACTIVE;

    epu_crystal_layer_t *L = &dev->crystal_layers[layer_id];
    L->active_freq = freq;
    L->active = true;
    /* Response scales with how much material there is and how hard it is
     * being driven, normalised so layer 0 at 432 Hz is exactly 1. */
    L->piezo_response = (L->nanocrystal_density / EPU_CRYSTAL_DENSITY)
                      * (EPU_SOLFEGGIO_TABLE[(uint32_t)freq] / EPU_SOLFEGGIO_432);
    dev->irq_crystal_resonance = epu_crystal_couples(dev);
    return EPU_OK;
}

double epu_crystal_get_resonance(epu_device_t *dev, uint32_t layer_id) {
    if (!dev) return EPU_BAD_READING;
    if (layer_id >= 4u) return EPU_BAD_READING;
    return epu_layer_resonance(dev, layer_id);   /* 0.0 when inactive */
}

/* ===================================================================
 * Frequency management
 * =================================================================== */

void epu_set_vortex_frequency(epu_device_t *dev, vortex_freq_t mode) {
    if (!dev) return;
    if ((int)mode < 0 || (int)mode >= EPU_NUM_VORTEX) return;  /* unchanged */
    dev->vortex_mode = mode;
    dev->current_frequency_hz = EPU_VORTEX_TABLE[(uint32_t)mode];
    dev->irq_crystal_resonance = epu_crystal_couples(dev);
}

void epu_set_solfeggio_frequency(epu_device_t *dev, solfeggio_freq_t freq) {
    if (!dev) return;
    if ((int)freq < 0 || (int)freq >= EPU_NUM_SOLFEGGIO) return;  /* unchanged */
    dev->solfeggio_mode = freq;
    dev->current_frequency_hz = EPU_SOLFEGGIO_TABLE[(uint32_t)freq];
    dev->irq_crystal_resonance = epu_crystal_couples(dev);
}

double epu_compute_vortex_harmonic(vortex_freq_t mode, uint32_t harmonic) {
    if ((int)mode < 0 || (int)mode >= EPU_NUM_VORTEX) return 0.0;
    if (harmonic > (uint32_t)EPU_MAX_HARMONIC) return 0.0;
    return EPU_VORTEX_TABLE[(uint32_t)mode] * epu_pow2(harmonic);
}

double epu_compute_solfeggio_harmonic(solfeggio_freq_t freq, uint32_t octave) {
    if ((int)freq < 0 || (int)freq >= EPU_NUM_SOLFEGGIO) return 0.0;
    if (octave > (uint32_t)EPU_MAX_OCTAVE) return 0.0;
    return EPU_SOLFEGGIO_TABLE[(uint32_t)freq] * epu_pow2(octave);
}

/* ===================================================================
 * Coverage and health
 *
 * NOTE (and this is the whole point of the exercise): both of these
 * return values that CAN be bad. epu_verify_coverage() returns false for
 * a device that has not been activated and for one whose qubit buffer
 * has degraded; epu_get_system_health() returns 0.8 for a fresh device
 * and collapses toward 0 as the buffer dies. The tests drive both to
 * failure on purpose.
 * =================================================================== */

bool epu_verify_coverage(epu_device_t *dev) {
    if (!dev) return false;

    uint32_t cells_on = epu_count_active_cells(dev);
    double r = (double)cells_on / (double)EPU_NUM_CELLS;

    uint32_t good = 0;
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_QUBITS; i++) {
        const epu_qubit_t *q = &dev->qubits[i];
        if (q->coherence_time_us > 0.0 && q->fidelity >= EPU_FIDELITY_FLOOR) good++;
    }
    double l = (double)good / (double)EPU_NUM_QUBITS;

    dev->coverage_r = r;
    dev->coverage_l = l;

    /* Mirror into the M5 coordinates for inspection only (L8). The
     * conversion goes through the surplus.h macros because surplus_real_t
     * is a double on the host and Q32.32 on the target. */
    dev->m5.omega = (uint32_t)(dev->emotions_processed & 0xFFFFFFFFu);
    dev->m5.r   = SR_FROM_FLOAT(r);
    dev->m5.ell = SR_FROM_FLOAT(l);
    dev->m5.phi = SR_FROM_FLOAT(dev->coverage_r * dev->coverage_l);
    dev->m5.chi = dev->device_id;

    /* r and l are FRACTIONS, so the EDP floor of 1.8 is unreachable here
     * by construction and using it would make this a permanent false.
     * EPU_COVERAGE_FLOOR is a device-population threshold, not an M5
     * economic quantity. See L8. */
    return (r * l) >= EPU_COVERAGE_FLOOR;
}

double epu_get_system_health(epu_device_t *dev) {
    if (!dev) return 0.0;

    epu_refresh_aggregates(dev);
    epu_recompute_power(dev);

    double s_cells = (double)epu_count_active_cells(dev) / (double)EPU_NUM_CELLS;

    double s_fid = dev->average_fidelity / EPU_FIDELITY_TARGET;
    s_fid = epu_clamp(s_fid, 0.0, 1.0);

    double s_coh = dev->total_coherence_us
                 / (EPU_COHERENCE_US * (double)EPU_NUM_QUBITS);
    s_coh = epu_clamp(s_coh, 0.0, 1.0);

    double s_pow = (dev->power_consumption_mw <= EPU_POWER_BUDGET_MW)
                 ? 1.0
                 : EPU_POWER_BUDGET_MW / dev->power_consumption_mw;
    s_pow = epu_clamp(s_pow, 0.0, 1.0);

    double dT = epu_fabs(dev->operating_temp_k - EPU_TEMP_AMBIENT_K);
    double s_th = epu_clamp(1.0 - dT / EPU_TEMP_AMBIENT_K, 0.0, 1.0);

    return (s_cells + s_fid + s_coh + s_pow + s_th) / 5.0;
}

/* ===================================================================
 * Diagnostics
 * =================================================================== */

static void epu_emit(epu_device_t *dev, const char *s) {
    if (dev->sink.write) dev->sink.write(dev->sink.ctx, s);
}

void epu_diagnostic_dump(epu_device_t *dev) {
    if (!dev) return;
    if (!dev->sink.write) return;   /* nothing bound: write nothing, claim nothing */

    char line[192];
    epu_sb_t sb;

    epu_emit(dev, "=== EPU MODEL (simulation only — drives no hardware) ===\n");

    sb_init(&sb, line, (uint32_t)sizeof(line));
    sb_str(&sb, "name="); sb_str(&sb, dev->name);
    sb_str(&sb, " id="); sb_u64(&sb, (uint64_t)dev->device_id);
    sb_str(&sb, " active="); sb_str(&sb, dev->bus_active ? "yes" : "no");
    sb_ch(&sb, '\n');
    epu_emit(dev, line);

    sb_init(&sb, line, (uint32_t)sizeof(line));
    sb_str(&sb, "cells="); sb_u64(&sb, (uint64_t)EPU_NUM_CELLS);
    sb_str(&sb, " active_cells="); sb_u64(&sb, (uint64_t)epu_count_active_cells(dev));
    sb_str(&sb, " qubits="); sb_u64(&sb, (uint64_t)EPU_NUM_QUBITS);
    sb_ch(&sb, '\n');
    epu_emit(dev, line);

    epu_refresh_aggregates(dev);
    epu_recompute_power(dev);

    sb_init(&sb, line, (uint32_t)sizeof(line));
    sb_str(&sb, "power_mw="); sb_fixed(&sb, dev->power_consumption_mw, 3);
    sb_str(&sb, " temp_k="); sb_fixed(&sb, dev->operating_temp_k, 3);
    sb_ch(&sb, '\n');
    epu_emit(dev, line);

    sb_init(&sb, line, (uint32_t)sizeof(line));
    sb_str(&sb, "avg_fidelity="); sb_fixed(&sb, dev->average_fidelity, 6);
    sb_str(&sb, " total_coherence_us="); sb_fixed(&sb, dev->total_coherence_us, 1);
    sb_ch(&sb, '\n');
    epu_emit(dev, line);

    sb_init(&sb, line, (uint32_t)sizeof(line));
    sb_str(&sb, "drive_hz="); sb_fixed(&sb, dev->current_frequency_hz, 1);
    sb_str(&sb, " emotions="); sb_u64(&sb, dev->emotions_processed);
    sb_str(&sb, " health="); sb_fixed(&sb, epu_get_system_health(dev), 6);
    sb_ch(&sb, '\n');
    epu_emit(dev, line);

    /* Recompute FIRST, then print. Printing dev->coverage_r before calling
     * epu_verify_coverage() reported the fractions left over from whenever
     * the check last ran, next to a freshly computed verdict — so a device
     * activated since the last check printed "coverage_r=0.0000 pass=yes".
     * The three fields on this line must describe one evaluation. */
    bool cov_pass = epu_verify_coverage(dev);
    sb_init(&sb, line, (uint32_t)sizeof(line));
    sb_str(&sb, "coverage_r="); sb_fixed(&sb, dev->coverage_r, 4);
    sb_str(&sb, " coverage_l="); sb_fixed(&sb, dev->coverage_l, 4);
    sb_str(&sb, " pass="); sb_str(&sb, cov_pass ? "yes" : "no");
    sb_ch(&sb, '\n');
    epu_emit(dev, line);

    sb_init(&sb, line, (uint32_t)sizeof(line));
    sb_str(&sb, "irq: coh_lost="); sb_str(&sb, dev->irq_coherence_lost ? "1" : "0");
    sb_str(&sb, " decoh="); sb_str(&sb, dev->irq_quantum_decoherence ? "1" : "0");
    sb_str(&sb, " field_imb="); sb_str(&sb, dev->irq_field_imbalance ? "1" : "0");
    sb_str(&sb, " crystal="); sb_str(&sb, dev->irq_crystal_resonance ? "1" : "0");
    sb_str(&sb, " emotion="); sb_str(&sb, dev->irq_emotion_ready ? "1" : "0");
    sb_ch(&sb, '\n');
    epu_emit(dev, line);
}

/* ===================================================================
 * HDL parameter export — NOT a design. See L7.
 * =================================================================== */

static char g_hdl_buf[EPU_HDL_BUF_BYTES];

/* Copy `name` into the comment, dropping anything that could end the
 * comment or the line. */
static void sb_ident(epu_sb_t *sb, const char *s) {
    for (uint32_t i = 0; s && s[i] && i < 63u; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
               || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
        sb_ch(sb, ok ? c : '_');
    }
}

static uint64_t epu_millihertz(double hz) {
    if (!epu_finite(hz) || hz <= 0.0) return 0;
    double m = hz * 1000.0;
    if (m > 1.0e15) m = 1.0e15;
    return (uint64_t)(m + 0.5);
}

const char *epu_generate_hdl_buf(epu_device_t *dev, const char *language,
                                 char *buf, uint32_t cap) {
    if (!dev || !language || !buf || cap == 0u) return (const char *)0;

    bool verilog = epu_streq_ci(language, "verilog");
    bool vhdl    = epu_streq_ci(language, "vhdl");
    if (!verilog && !vhdl) return (const char *)0;   /* unsupported: say so */

    epu_sb_t sb;
    sb_init(&sb, buf, cap);

    const char *cm = verilog ? "// " : "-- ";
    uint64_t drive = epu_millihertz(dev->current_frequency_hz);

    sb_str(&sb, cm); sb_str(&sb, "GENERATED PARAMETER SKELETON - NOT A DESIGN.\n");
    sb_str(&sb, cm); sb_str(&sb, "Emitted by epu_generate_hdl(). It carries the EPU model's\n");
    sb_str(&sb, cm); sb_str(&sb, "configuration constants and a port list, and nothing else.\n");
    sb_str(&sb, cm); sb_str(&sb, "No arithmetic is implemented here. It has never been\n");
    sb_str(&sb, cm); sb_str(&sb, "synthesised, linted or simulated. Do not call it RTL.\n");
    sb_str(&sb, cm); sb_str(&sb, "device: "); sb_ident(&sb, dev->name); sb_ch(&sb, '\n');

    if (verilog) {
        sb_str(&sb, "module epu_core #(\n");
        sb_str(&sb, "    parameter integer DEVICE_ID       = "); sb_u64(&sb, dev->device_id); sb_str(&sb, ",\n");
        sb_str(&sb, "    parameter integer NUM_CELLS       = "); sb_u64(&sb, (uint64_t)EPU_NUM_CELLS); sb_str(&sb, ",\n");
        sb_str(&sb, "    parameter integer NUM_QUBITS      = "); sb_u64(&sb, (uint64_t)EPU_NUM_QUBITS); sb_str(&sb, ",\n");
        sb_str(&sb, "    parameter integer COIL_TURNS      = "); sb_u64(&sb, (uint64_t)EPU_FIBONACCI_TURNS); sb_str(&sb, ",\n");
        sb_str(&sb, "    parameter integer EMOTION_DIMS    = "); sb_u64(&sb, (uint64_t)EPU_EMOTION_DIMS); sb_str(&sb, ",\n");
        sb_str(&sb, "    parameter integer DMA_FRAME_BYTES = "); sb_u64(&sb, (uint64_t)EPU_DMA_FRAME_BYTES); sb_str(&sb, ",\n");
        sb_str(&sb, "    parameter integer VORTEX_MODE     = "); sb_u64(&sb, (uint64_t)dev->vortex_mode); sb_str(&sb, ",\n");
        sb_str(&sb, "    parameter integer SOLFEGGIO_MODE  = "); sb_u64(&sb, (uint64_t)dev->solfeggio_mode); sb_str(&sb, ",\n");
        sb_str(&sb, "    parameter integer DRIVE_FREQ_MHZ  = "); sb_u64(&sb, drive); sb_str(&sb, "\n");
        sb_str(&sb, ") (\n");
        sb_str(&sb, "    input  wire        clk,\n");
        sb_str(&sb, "    input  wire        rst_n,\n");
        sb_str(&sb, "    input  wire [63:0] emotion_in,\n");
        sb_str(&sb, "    output wire [63:0] emotion_out,\n");
        sb_str(&sb, "    output wire        emotion_ready,\n");
        sb_str(&sb, "    output wire        coherence_lost\n");
        sb_str(&sb, ");\n");
        sb_str(&sb, "    // no logic emitted - see the header comment\n");
        sb_str(&sb, "endmodule\n");
    } else {
        sb_str(&sb, "library ieee;\n");
        sb_str(&sb, "use ieee.std_logic_1164.all;\n\n");
        sb_str(&sb, "entity epu_core is\n");
        sb_str(&sb, "  generic (\n");
        sb_str(&sb, "    DEVICE_ID       : integer := "); sb_u64(&sb, dev->device_id); sb_str(&sb, ";\n");
        sb_str(&sb, "    NUM_CELLS       : integer := "); sb_u64(&sb, (uint64_t)EPU_NUM_CELLS); sb_str(&sb, ";\n");
        sb_str(&sb, "    NUM_QUBITS      : integer := "); sb_u64(&sb, (uint64_t)EPU_NUM_QUBITS); sb_str(&sb, ";\n");
        sb_str(&sb, "    COIL_TURNS      : integer := "); sb_u64(&sb, (uint64_t)EPU_FIBONACCI_TURNS); sb_str(&sb, ";\n");
        sb_str(&sb, "    EMOTION_DIMS    : integer := "); sb_u64(&sb, (uint64_t)EPU_EMOTION_DIMS); sb_str(&sb, ";\n");
        sb_str(&sb, "    DMA_FRAME_BYTES : integer := "); sb_u64(&sb, (uint64_t)EPU_DMA_FRAME_BYTES); sb_str(&sb, ";\n");
        sb_str(&sb, "    VORTEX_MODE     : integer := "); sb_u64(&sb, (uint64_t)dev->vortex_mode); sb_str(&sb, ";\n");
        sb_str(&sb, "    SOLFEGGIO_MODE  : integer := "); sb_u64(&sb, (uint64_t)dev->solfeggio_mode); sb_str(&sb, ";\n");
        sb_str(&sb, "    DRIVE_FREQ_MHZ  : integer := "); sb_u64(&sb, drive); sb_str(&sb, "\n");
        sb_str(&sb, "  );\n");
        sb_str(&sb, "  port (\n");
        sb_str(&sb, "    clk            : in  std_logic;\n");
        sb_str(&sb, "    rst_n          : in  std_logic;\n");
        sb_str(&sb, "    emotion_in     : in  std_logic_vector(63 downto 0);\n");
        sb_str(&sb, "    emotion_out    : out std_logic_vector(63 downto 0);\n");
        sb_str(&sb, "    emotion_ready  : out std_logic;\n");
        sb_str(&sb, "    coherence_lost : out std_logic\n");
        sb_str(&sb, "  );\n");
        sb_str(&sb, "end entity epu_core;\n\n");
        sb_str(&sb, "architecture skeleton of epu_core is\n");
        sb_str(&sb, "begin\n");
        sb_str(&sb, "  -- no logic emitted - see the header comment\n");
        sb_str(&sb, "end architecture skeleton;\n");
    }

    /* A truncated skeleton would be a lie dressed as output. This branch
     * is genuinely reachable through epu_generate_hdl_buf() with a small
     * `cap`, and the test drives it at several sizes. */
    if (sb.truncated) return (const char *)0;
    return buf;
}

/* Shared-static-buffer convenience wrapper. NOT reentrant; the returned
 * pointer is invalidated by the next call. See L7. */
const char *epu_generate_hdl(epu_device_t *dev, const char *language) {
    return epu_generate_hdl_buf(dev, language,
                                g_hdl_buf, (uint32_t)sizeof(g_hdl_buf));
}

/* ===================================================================
 * IRQ evaluation (level flags recomputed from state; see L1)
 * =================================================================== */

void epu_handle_irq(epu_device_t *dev) {
    if (!dev) return;

    epu_refresh_aggregates(dev);

    dev->irq_coherence_lost =
        dev->total_coherence_us < (EPU_COHERENCE_US * (double)EPU_NUM_QUBITS * 0.5);

    bool degraded = false;
    for (uint32_t i = 0; i < (uint32_t)EPU_NUM_QUBITS; i++) {
        const epu_qubit_t *q = &dev->qubits[i];
        if (q->fidelity < EPU_FIDELITY_FLOOR || q->coherence_time_us <= 0.0) {
            degraded = true;
            break;
        }
    }
    dev->irq_quantum_decoherence = degraded;

    double bmin = dev->coils[0].field_strength;
    double bmax = bmin;
    for (uint32_t i = 1; i < 8u; i++) {
        double v = dev->coils[i].field_strength;
        if (v < bmin) bmin = v;
        if (v > bmax) bmax = v;
    }
    dev->irq_field_imbalance = (bmax - bmin) > EPU_FIELD_IMBALANCE_T;

    dev->irq_crystal_resonance = epu_crystal_couples(dev);

    /* Edge flag: the report has been taken, so acknowledge it. */
    dev->irq_emotion_ready = false;
}

/* ---- DECLARATION -----------------------------------------------------------

 * PROVIDES epu_model_ready, and the name is load-bearing. This header says of
 * itself, in capitals: THIS IS A MODEL. IT IS NOT A DEVICE DRIVER. Declaring
 * "epu_ready" would have let a later module bind to it believing there was
 * hardware underneath.
 *
 * REQUIRES_NONE is measured: epu_device.o's `nm -u` is empty. It uses scalar
 * double, which Makefile.arm64:20 explicitly permits (only NEON vectorization
 * is banned, because the IRQ vector does not save the FP/SIMD file).
 */
#include "zxv_decl.h"
static int zxvd_epu_bringup(void) {
    static epu_system_t sys;
    epu_system_init(&sys);
    if (epu_system_create_device(&sys, "epu0") == 0u) return -1;
    return 0;
}

ZXV_DECLARE(epu,
    ZXV_PROVIDES(epu_model_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_epu_bringup));
