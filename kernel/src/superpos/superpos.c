/*
 * superpos.c — Superposition Coordinator Implementation
 *
 * The Omni-Presence layer: quantum-inspired build state evolution,
 * Hamiltonian dynamics, and phase-tick measurement collapse.
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

#include "superpos.h"

/* ===== Helpers ===== */

static void sp_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static __attribute__((unused)) void sp_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static void sp_strcpy(char *dst, const char *src) {
    uint32_t i = 0; while (src[i]) { dst[i] = src[i]; i++; } dst[i] = '\0';
}

static __attribute__((unused)) int sp_strcmp(const char *a, const char *b) {
    uint32_t i = 0; while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

/* Fixed-point arithmetic: amplitude_scale = 1000 means 1.0 */
static __attribute__((unused)) int32_t sp_imul(int32_t a, int32_t b) {
    return (a * b) / (int32_t)SP_AMPLITUDE_SCALE;
}

static __attribute__((unused)) int32_t sp_isqrt(int64_t v) {
    if (v <= 0) return 0;
    int32_t lo = 0, hi = 46340; /* sqrt(2^31) */
    while (lo < hi) {
        int32_t mid = (lo + hi + 1) / 2;
        if ((int64_t)mid * mid <= v) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* ===== Init ===== */

void superpos_init(superpos_t *sp, sp_target_t hardware) {
    sp_memset(sp, 0, sizeof(superpos_t));
    sp->detected_hardware = hardware;
    sp->initialized = true;
    sp->collapsed = false;
    sp->phase_tick = 0;
    sp->last_measurement = SP_MEASURE_NOT_READY;
    sp->min_energy = 0x7FFFFFFF;

    /* Initialize default Hamiltonians */
    sp->hamiltonians[0].type = SP_H_DEPS;
    sp_strcpy(sp->hamiltonians[0].name, "H_deps (causal boundaries)");
    sp->hamiltonians[0].energy_cost = 100;
    sp->hamiltonians[0].constraint_count = 0;
    sp->hamiltonians[0].active = true;

    sp->hamiltonians[1].type = SP_H_OPT;
    sp_strcpy(sp->hamiltonians[1].name, "H_opt (min latency/memory)");
    sp->hamiltonians[1].energy_cost = 50;
    sp->hamiltonians[1].constraint_count = 0;
    sp->hamiltonians[1].active = true;

    sp->hamiltonians[2].type = SP_H_TARGET;
    sp_strcpy(sp->hamiltonians[2].name, "H_target (hardware adapt)");
    sp->hamiltonians[2].energy_cost = 75;
    sp->hamiltonians[2].constraint_count = 0;
    sp->hamiltonians[2].active = true;

    sp->num_hamiltonians = 3;
}

/* ===== Eigenstate Management ===== */

int32_t superpos_state_add(superpos_t *sp, const char *name,
                            sp_target_t target, sp_dimension_t dimension,
                            int32_t amp_re, int32_t amp_im,
                            uint32_t energy, bool dep_satisfied) {
    if (sp->num_states >= SP_MAX_EIGENSTATES || !name) return -1;
    int32_t idx = (int32_t)sp->num_states;
    sp_eigenstate_t *s = &sp->states[idx];
    sp_memset(s, 0, sizeof(sp_eigenstate_t));
    sp_strcpy(s->name, name);
    s->amplitude_re = amp_re;
    s->amplitude_im = amp_im;
    s->energy = energy;
    s->target = target;
    s->dimension = dimension;
    s->dependency_satisfied = dep_satisfied;
    s->safety_verified = false;
    s->collapsed = false;
    s->active = true;
    sp->num_states++;

    /* Track minimum energy */
    if ((int32_t)energy < sp->min_energy)
        sp->min_energy = (int32_t)energy;

    /* Compute initial probability */
    superpos_compute_probabilities(sp);
    return idx;
}

int superpos_state_set_notes(superpos_t *sp, uint32_t idx, const char *notes) {
    if (idx >= sp->num_states || !notes) return -1;
    sp_strcpy(sp->states[idx].notes, notes);
    return 0;
}

int superpos_state_set_safety(superpos_t *sp, uint32_t idx, bool verified) {
    if (idx >= sp->num_states) return -1;
    sp->states[idx].safety_verified = verified;
    return 0;
}

const sp_eigenstate_t *superpos_state_get(superpos_t *sp, uint32_t idx) {
    if (idx >= sp->num_states) return NULL;
    return &sp->states[idx];
}

/* ===== Hamiltonian Management ===== */

int superpos_hamiltonian_add(superpos_t *sp, sp_hamiltonian_t type,
                              const char *name, uint32_t energy_cost,
                              uint32_t constraints) {
    if (sp->num_hamiltonians >= SP_MAX_HAMILTONIANS || !name) return -1;
    int idx = (int)sp->num_hamiltonians;
    sp_hamiltonian_term_t *h = &sp->hamiltonians[idx];
    sp_memset(h, 0, sizeof(sp_hamiltonian_term_t));
    h->type = type;
    sp_strcpy(h->name, name);
    h->energy_cost = energy_cost;
    h->constraint_count = constraints;
    h->active = true;
    sp->num_hamiltonians++;
    return 0;
}

int32_t superpos_hamiltonian_energy(const superpos_t *sp, uint32_t state_idx) {
    if (state_idx >= sp->num_states) return -1;
    const sp_eigenstate_t *s = &sp->states[state_idx];
    int32_t total = 0;
    uint32_t i;
    for (i = 0; i < sp->num_hamiltonians; i++) {
        if (!sp->hamiltonians[i].active) continue;
        switch (sp->hamiltonians[i].type) {
            case SP_H_DEPS:
                /* Missing dependency → energy → ∞ (simulate with large cost) */
                if (!s->dependency_satisfied)
                    total += 100000;
                else
                    total += (int32_t)sp->hamiltonians[i].energy_cost;
                break;
            case SP_H_OPT:
                /* Higher energy state = worse optimization */
                total += (int32_t)(sp->hamiltonians[i].energy_cost + s->energy / 100);
                break;
            case SP_H_TARGET:
                /* Mismatched target → higher energy */
                if (s->target != sp->detected_hardware)
                    total += (int32_t)sp->hamiltonians[i].energy_cost * 2;
                else
                    total += (int32_t)sp->hamiltonians[i].energy_cost / 2;
                break;
        }
    }
    return total;
}

int superpos_hamiltonian_evolve(superpos_t *sp) {
    /* Evolve all eigenstate amplitudes based on Hamiltonian energy */
    uint32_t i;
    sp->total_energy = 0;
    sp->min_energy = 0x7FFFFFFF;
    for (i = 0; i < sp->num_states; i++) {
        sp_eigenstate_t *s = &sp->states[i];
        if (!s->active) continue;

        int32_t energy = superpos_hamiltonian_energy(sp, i);
        s->energy = (uint32_t)energy;
        sp->total_energy += energy;
        if (energy < sp->min_energy) sp->min_energy = energy;

        /* Amplitude evolution: lower energy → higher amplitude */
        /* c_i ∝ exp(-E_i / kT) — simplified to inverse energy */
        if (energy > 0 && s->dependency_satisfied) {
            int32_t amp = (int32_t)SP_AMPLITUDE_SCALE * 100 / energy;
            if (amp > SP_AMPLITUDE_SCALE) amp = SP_AMPLITUDE_SCALE;
            /* Blend with existing amplitude (Hamiltonian evolution) */
            s->amplitude_re = (s->amplitude_re + amp) / 2;
        } else {
            /* Unsatisfied dependency → amplitude → 0 (destructive) */
            s->amplitude_re = s->amplitude_re / 10;
            s->amplitude_im = s->amplitude_im / 10;
        }
    }
    superpos_compute_probabilities(sp);
    return 0;
}

/* ===== Probability Computation ===== */

void superpos_compute_probabilities(superpos_t *sp) {
    uint32_t i;
    int64_t total_sq = 0;
    for (i = 0; i < sp->num_states; i++) {
        sp_eigenstate_t *s = &sp->states[i];
        if (!s->active) { s->probability = 0; continue; }
        int64_t re = s->amplitude_re;
        int64_t im = s->amplitude_im;
        int64_t sq = re * re + im * im;
        s->probability = (uint32_t)(sq / (SP_AMPLITUDE_SCALE * SP_AMPLITUDE_SCALE));
        total_sq += sq;
    }
    /* Normalize probabilities */
    if (total_sq > 0) {
        for (i = 0; i < sp->num_states; i++) {
            sp_eigenstate_t *s = &sp->states[i];
            if (!s->active) continue;
            int64_t re = s->amplitude_re;
            int64_t im = s->amplitude_im;
            int64_t sq = re * re + im * im;
            s->probability = (uint32_t)(sq * 1000 / total_sq);
        }
    }
}

uint32_t superpos_probability(const superpos_t *sp, uint32_t state_idx) {
    if (state_idx >= sp->num_states) return 0;
    return sp->states[state_idx].probability;
}

/* ===== Measurement / Collapse ===== */

sp_measurement_t superpos_measure(superpos_t *sp) {
    if (sp->num_states == 0) return SP_MEASURE_DESTRUCTIVE;

    /* Apply Hamiltonian evolution before measurement */
    superpos_hamiltonian_evolve(sp);

    /* Find highest-probability eigenstate that passes safety */
    uint32_t best_idx = 0;
    uint32_t best_prob = 0;
    uint32_t viable_count = 0;
    uint32_t i;
    for (i = 0; i < sp->num_states; i++) {
        sp_eigenstate_t *s = &sp->states[i];
        if (!s->active) continue;
        if (!s->dependency_satisfied) continue;
        if (!s->safety_verified) continue;
        viable_count++;
        if (s->probability > best_prob) {
            best_prob = s->probability;
            best_idx = i;
        }
    }

    if (viable_count == 0) {
        /* All states cancelled → destructive interference */
        sp->last_measurement = SP_MEASURE_DESTRUCTIVE;
        sp->destructive_count++;
        return SP_MEASURE_DESTRUCTIVE;
    }

    if (viable_count == 1 || best_prob >= 500) {
        /* Clear winner → collapse */
        superpos_collapse(sp, best_idx);
        sp->last_measurement = SP_MEASURE_COLLAPSE;
        sp->collapse_count++;
        return SP_MEASURE_COLLAPSE;
    }

    /* Multiple viable states → continue in superposition */
    sp->last_measurement = SP_MEASURE_SUPERPOSE;
    sp->superpose_count++;
    return SP_MEASURE_SUPERPOSE;
}

int superpos_collapse(superpos_t *sp, uint32_t winning_idx) {
    if (winning_idx >= sp->num_states) return -1;
    uint32_t i;
    for (i = 0; i < sp->num_states; i++) {
        if (i == winning_idx) {
            sp->states[i].collapsed = true;
            sp->states[i].amplitude_re = SP_AMPLITUDE_SCALE;
            sp->states[i].amplitude_im = 0;
            sp->states[i].probability = 1000;
        } else {
            sp->states[i].amplitude_re = 0;
            sp->states[i].amplitude_im = 0;
            sp->states[i].probability = 0;
            sp->states[i].active = false;
        }
    }
    sp->collapsed = true;
    sp->winning_state_idx = winning_idx;
    return 0;
}

sp_interference_t superpos_interference(const superpos_t *sp,
                                         uint32_t idx_a, uint32_t idx_b) {
    if (idx_a >= sp->num_states || idx_b >= sp->num_states)
        return SP_INTERF_NONE;
    const sp_eigenstate_t *a = &sp->states[idx_a];
    const sp_eigenstate_t *b = &sp->states[idx_b];

    /* Check if both are safety-verified and dep-satisfied */
    if (a->safety_verified && b->safety_verified &&
        a->dependency_satisfied && b->dependency_satisfied) {
        /* Same target → constructive; different target → destructive */
        if (a->target == b->target) return SP_INTERF_CONSTRUCTIVE;
        return SP_INTERF_DESTRUCTIVE;
    }
    return SP_INTERF_NONE;
}

/* ===== Phase Clock ===== */

void superpos_tick(superpos_t *sp) {
    sp->phase_tick += SP_PHASE_CLOCK_MS;
}

uint32_t superpos_phase_clock(const superpos_t *sp) {
    return sp->phase_tick;
}

/* ===== Hardware Detection ===== */

sp_target_t superpos_detect_hardware(void) {
    /* In host test mode, detect CPU features */
#ifdef TEST_HOST
    /* Check for x86_64 (host is macOS) */
    return SP_TARGET_X86_64;
#else
    /* In kernel: check CPUID / device tree */
    return SP_TARGET_X86_64;
#endif
}

/* ===== Coordination with Dual-Track ===== */

int superpos_coordinate(superpos_t *sp, uint32_t linear_state_idx,
                        uint32_t nonlinear_state_idx,
                        bool linear_ok, bool nonlinear_ok) {
    if (linear_state_idx >= sp->num_states ||
        nonlinear_state_idx >= sp->num_states) return -1;

    sp_eigenstate_t *lin = &sp->states[linear_state_idx];
    sp_eigenstate_t *nonlin = &sp->states[nonlinear_state_idx];

    /* Update safety verification based on track results */
    lin->safety_verified = linear_ok;
    nonlin->safety_verified = nonlinear_ok;

    /* If linear failed, reduce its amplitude (destructive) */
    if (!linear_ok) {
        lin->amplitude_re = lin->amplitude_re / 4;
        lin->amplitude_im = lin->amplitude_im / 4;
    }

    /* If nonlinear failed, reduce its amplitude */
    if (!nonlinear_ok) {
        nonlin->amplitude_re = nonlin->amplitude_re / 4;
        nonlin->amplitude_im = nonlin->amplitude_im / 4;
    }

    /* If both succeeded, check interference */
    if (linear_ok && nonlinear_ok) {
        sp_interference_t interf = superpos_interference(sp,
            linear_state_idx, nonlinear_state_idx);
        if (interf == SP_INTERF_CONSTRUCTIVE) {
            /* Amplify both */
            lin->amplitude_re = lin->amplitude_re * 3 / 2;
            nonlin->amplitude_re = nonlin->amplitude_re * 3 / 2;
            if (lin->amplitude_re > SP_AMPLITUDE_SCALE)
                lin->amplitude_re = SP_AMPLITUDE_SCALE;
            if (nonlin->amplitude_re > SP_AMPLITUDE_SCALE)
                nonlin->amplitude_re = SP_AMPLITUDE_SCALE;
        } else if (interf == SP_INTERF_DESTRUCTIVE) {
            /* Cancel — reduce both */
            lin->amplitude_re = lin->amplitude_re * 3 / 4;
            nonlin->amplitude_re = nonlin->amplitude_re * 3 / 4;
        }
    }

    superpos_compute_probabilities(sp);
    return 0;
}

/* ===== Utility ===== */

const char *superpos_target_name(sp_target_t target) {
    switch (target) {
        case SP_TARGET_X86_64: return "x86_64";
        case SP_TARGET_ARM64:  return "ARM64";
        case SP_TARGET_RISCV:  return "RISC-V";
        case SP_TARGET_FPGA:   return "FPGA";
        case SP_TARGET_GPU:    return "GPU";
        case SP_TARGET_MICRO:  return "Microcontroller";
        default: return "UNKNOWN";
    }
}

const char *superpos_dimension_name(sp_dimension_t dim) {
    switch (dim) {
        case SP_DIM_LOCALITY:      return "Locality (classical)";
        case SP_DIM_NON_LOCALITY:  return "Non-Locality (entangled)";
        case SP_DIM_OMNI_PRESENCE: return "Omni-Presence (field)";
        default: return "UNKNOWN";
    }
}

const char *superpos_hamiltonian_name(sp_hamiltonian_t ham) {
    switch (ham) {
        case SP_H_DEPS:   return "H_deps (causal boundaries)";
        case SP_H_OPT:    return "H_opt (min latency/memory)";
        case SP_H_TARGET: return "H_target (hardware adapt)";
        default: return "UNKNOWN";
    }
}

const char *superpos_measurement_name(sp_measurement_t m) {
    switch (m) {
        case SP_MEASURE_COLLAPSE:    return "COLLAPSE";
        case SP_MEASURE_SUPERPOSE:   return "SUPERPOSE";
        case SP_MEASURE_DESTRUCTIVE: return "DESTRUCTIVE";
        case SP_MEASURE_NOT_READY:   return "NOT_READY";
        default: return "UNKNOWN";
    }
}

const char *superpos_interference_name(sp_interference_t i) {
    switch (i) {
        case SP_INTERF_CONSTRUCTIVE: return "CONSTRUCTIVE";
        case SP_INTERF_DESTRUCTIVE:  return "DESTRUCTIVE";
        case SP_INTERF_NONE:         return "NONE";
        default: return "UNKNOWN";
    }
}

/* ===== Reporting ===== */

int superpos_report(const superpos_t *sp, char *buf, uint32_t max_len) {
    if (!sp || !buf) return -1;
    uint32_t pos = 0;

    pos += (uint32_t)snprintf(buf + pos, max_len - pos,
        "Superposition Coordinator Report\n"
        "  Eigenstates: %u | Hamiltonians: %u\n"
        "  Hardware: %s | Phase clock: %u ms\n"
        "  Last measurement: %s\n"
        "  Collapsed: %s | Winning state: %u\n"
        "  Collapses: %u | Superpositions: %u | Destructive: %u\n"
        "  Total energy: %d | Min energy: %d\n",
        sp->num_states, sp->num_hamiltonians,
        superpos_target_name(sp->detected_hardware), sp->phase_tick,
        superpos_measurement_name(sp->last_measurement),
        sp->collapsed ? "YES" : "NO", sp->winning_state_idx,
        sp->collapse_count, sp->superpose_count, sp->destructive_count,
        sp->total_energy, sp->min_energy);

    uint32_t i;
    for (i = 0; i < sp->num_states && pos < max_len - 120; i++) {
        const sp_eigenstate_t *s = &sp->states[i];
        pos += (uint32_t)snprintf(buf + pos, max_len - pos,
            "\n  [%u] %s\n"
            "    Target: %s | Dimension: %s\n"
            "    Amplitude: %d + %di | Probability: %u/1000\n"
            "    Energy: %u | Dep: %s | Safety: %s | Collapsed: %s\n",
            i, s->name,
            superpos_target_name(s->target),
            superpos_dimension_name(s->dimension),
            s->amplitude_re, s->amplitude_im, s->probability,
            s->energy,
            s->dependency_satisfied ? "YES" : "NO",
            s->safety_verified ? "YES" : "NO",
            s->collapsed ? "YES" : "NO");
    }

    return (int)pos;
}

float superpos_agreement_rate(const superpos_t *sp) {
    if (!sp || sp->num_states == 0) return 1.0f;
    uint32_t verified = 0;
    uint32_t i;
    for (i = 0; i < sp->num_states; i++)
        if (sp->states[i].safety_verified && sp->states[i].active)
            verified++;
    return (float)verified / (float)sp->num_states;
}
