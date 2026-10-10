/*
 * superpos.h — Superposition Coordinator (Triadic Third Pillar)
 *
 * The Omni-Presence layer that governs the linear and nonlinear build
 * tracks via quantum-inspired state mechanics. Models the entire build
 * space as a Hilbert space spanning all valid permutations of compiler
 * flags, optimization passes, target architectures, and dependency
 * resolutions.
 *
 * State Vector:
 *   |Ψ_build(t)⟩ = Σ_i c_i(t) |φ_i^linear⟩ ⊗ |ψ_i^nonlinear⟩
 *
 * Hamiltonian Evolution:
 *   H_build = H_deps + H_opt + H_target
 *
 *   H_deps:   Enforces causal boundaries (missing dep → energy → ∞)
 *   H_opt:    Drives toward min latency + optimal memory footprint
 *   H_target:  Adapts to physical hardware (FPGA, AVX-512, NEON, etc.)
 *
 * Measurement (Phase Collapse):
 *   At each 10ms phase-tick, operator M̂ collapses the state:
 *   - Constructive interference: valid trajectories amplify |c_i|² → 1
 *   - Destructive interference: faulty trajectories cancel c_i → 0
 *   - Winning eigenstate collapses into Merkle VFS as .36n9 / .smap
 *
 * Triadic Physics Substrate:
 *   LOCALITY:       Classical deterministic (linear track, .36n9, .ula)
 *   NON-LOCALITY:   Entangled distributed (nonlinear track, .zedec, .9n63)
 *   OMNI-PRESENCE:  Unified field (superposition coordinator, .36m9, .vino)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_SUPERPOS_H
#define ZEDEC_SUPERPOS_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define SP_MAX_EIGENSTATES     64
#define SP_MAX_LABEL           64
#define SP_MAX_NOTES          256
#define SP_MAX_TARGETS         16
#define SP_MAX_HAMILTONIANS     3
#define SP_PHASE_CLOCK_MS     10
#define SP_AMPLITUDE_SCALE   1000   /* Fixed-point: 1000 = 1.0 */

/* ===== Physics Dimensions ===== */

typedef enum {
    SP_DIM_LOCALITY       = 0,  /* Classical deterministic (linear track) */
    SP_DIM_NON_LOCALITY   = 1,  /* Entangled distributed (nonlinear track) */
    SP_DIM_OMNI_PRESENCE  = 2,  /* Unified field (superposition coordinator) */
} sp_dimension_t;

/* ===== Target Architectures ===== */

typedef enum {
    SP_TARGET_X86_64   = 0,
    SP_TARGET_ARM64    = 1,
    SP_TARGET_RISCV    = 2,
    SP_TARGET_FPGA     = 3,
    SP_TARGET_GPU      = 4,
    SP_TARGET_MICRO    = 5,  /* Microcontroller */
} sp_target_t;

/* ===== Hamiltonian Components ===== */

typedef enum {
    SP_H_DEPS    = 0,  /* Dependency Hamiltonian: causal boundaries */
    SP_H_OPT     = 1,  /* Optimization Hamiltonian: min latency/memory */
    SP_H_TARGET  = 2,  /* Target Hamiltonian: hardware adaptation */
} sp_hamiltonian_t;

/* ===== Eigenstate (Build Trajectory) ===== */

typedef struct {
    char     name[SP_MAX_LABEL];
    char     notes[SP_MAX_NOTES];
    int32_t  amplitude_re;     /* Real part (fixed-point, 1000=1.0) */
    int32_t  amplitude_im;     /* Imaginary part */
    uint32_t probability;      /* |c_i|² = (re² + im²) / SCALE² */
    uint32_t energy;           /* Hamiltonian energy cost */
    sp_target_t target;
    sp_dimension_t dimension;
    bool     dependency_satisfied;
    bool     safety_verified;
    bool     collapsed;        /* Has this eigenstate been measured? */
    bool     active;
} sp_eigenstate_t;

/* ===== Hamiltonian Term ===== */

typedef struct {
    sp_hamiltonian_t type;
    char     name[SP_MAX_LABEL];
    uint32_t energy_cost;      /* Base energy cost */
    uint32_t constraint_count; /* Number of constraints enforced */
    bool     active;
} sp_hamiltonian_term_t;

/* ===== Measurement Result ===== */

typedef enum {
    SP_MEASURE_COLLAPSE     = 0,  /* Single eigenstate won → collapse */
    SP_MEASURE_SUPERPOSE    = 1,  /* Multiple states still viable → continue */
    SP_MEASURE_DESTRUCTIVE  = 2,  /* All states cancelled → vacuum */
    SP_MEASURE_NOT_READY    = 3,  /* No measurement taken yet */
} sp_measurement_t;

/* ===== Interference Type ===== */

typedef enum {
    SP_INTERF_CONSTRUCTIVE  = 0,  /* Trajectories amplify each other */
    SP_INTERF_DESTRUCTIVE   = 1,  /* Trajectories cancel out */
    SP_INTERF_NONE          = 2,  /* No interference (independent) */
} sp_interference_t;

/* ===== Superposition Coordinator ===== */

typedef struct {
    sp_eigenstate_t     states[SP_MAX_EIGENSTATES];
    uint32_t            num_states;
    sp_hamiltonian_term_t hamiltonians[SP_MAX_HAMILTONIANS];
    uint32_t            num_hamiltonians;
    sp_target_t         detected_hardware;  /* Auto-detected target */
    uint32_t            phase_tick;         /* 10ms phase clock */
    sp_measurement_t    last_measurement;
    uint32_t            collapse_count;
    uint32_t            superpose_count;
    uint32_t            destructive_count;
    int32_t             total_energy;       /* Sum of all state energies */
    int32_t             min_energy;         /* Lowest energy eigenstate */
    uint32_t            winning_state_idx;  /* Index of collapsed eigenstate */
    bool                initialized;
    bool                collapsed;          /* Has the system collapsed? */
} superpos_t;

/* ===== API ===== */

void superpos_init(superpos_t *sp, sp_target_t hardware);

/* Eigenstate management */
int32_t superpos_state_add(superpos_t *sp, const char *name,
                            sp_target_t target, sp_dimension_t dimension,
                            int32_t amp_re, int32_t amp_im,
                            uint32_t energy, bool dep_satisfied);
int superpos_state_set_notes(superpos_t *sp, uint32_t idx, const char *notes);
int superpos_state_set_safety(superpos_t *sp, uint32_t idx, bool verified);
const sp_eigenstate_t *superpos_state_get(superpos_t *sp, uint32_t idx);

/* Hamiltonian management */
int superpos_hamiltonian_add(superpos_t *sp, sp_hamiltonian_t type,
                              const char *name, uint32_t energy_cost,
                              uint32_t constraints);
int superpos_hamiltonian_evolve(superpos_t *sp);
int32_t superpos_hamiltonian_energy(const superpos_t *sp, uint32_t state_idx);

/* Probability computation */
void superpos_compute_probabilities(superpos_t *sp);
uint32_t superpos_probability(const superpos_t *sp, uint32_t state_idx);

/* Measurement / collapse */
sp_measurement_t superpos_measure(superpos_t *sp);
int superpos_collapse(superpos_t *sp, uint32_t winning_idx);
sp_interference_t superpos_interference(const superpos_t *sp,
                                         uint32_t idx_a, uint32_t idx_b);

/* Phase clock */
void superpos_tick(superpos_t *sp);
uint32_t superpos_phase_clock(const superpos_t *sp);

/* Hardware detection */
sp_target_t superpos_detect_hardware(void);
const char *superpos_target_name(sp_target_t target);

/* Utility */
const char *superpos_dimension_name(sp_dimension_t dim);
const char *superpos_hamiltonian_name(sp_hamiltonian_t ham);
const char *superpos_measurement_name(sp_measurement_t m);
const char *superpos_interference_name(sp_interference_t i);

/* Reporting */
int superpos_report(const superpos_t *sp, char *buf, uint32_t max_len);
/* safety-verified active states / num_states in permille (1000 when empty). */
uint32_t superpos_agreement_permille(const superpos_t *sp);

/* Integration: coordinate with dual-track */
int superpos_coordinate(superpos_t *sp, uint32_t linear_state_idx,
                        uint32_t nonlinear_state_idx,
                        bool linear_ok, bool nonlinear_ok);

#endif /* ZEDEC_SUPERPOS_H */
