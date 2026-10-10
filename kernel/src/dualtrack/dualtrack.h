/*
 * dualtrack.h — Dual-Track Linear/Nonlinear Build & Execution Engine
 *
 * Integrated OS primitive that runs linear and nonlinear execution
 * tracks simultaneously with cross-coordination at phase checkpoints.
 *
 * LINEAR TRACK (Deterministic Baseline):
 *   - Strict dependency DAG: Bootloader → HAL → Kernel → Userland
 *   - 100% reproducible, signed with root keys (K1)
 *   - Sequential verification of core invariants
 *
 * NONLINEAR TRACK (Speculative / Graph-Driven):
 *   - Runs in G+ (Glut-Plus) speculative state via LPRES
 *   - Parallel synthesis of independent modules
 *   - SystemVerilog RTL for FPGAs + C userland + multi-VM runtimes
 *   - Pre-generates .smap files and HDCM projection matrices
 *   - Pre-compiles probable execution branches ahead of demand
 *
 * CONVERGENCE (Phase Checkpoints @ 10ms phase clock):
 *   1. Recorder Daemon (+0): hashes speculative artifacts, submits sigs
 *   2. Skeptic Arbiter (-1): validates against formal safety constraints
 *   3. Zero-Delay Linking: O(1) pointer swaps in Merkle VFS on validation
 *      If invalid → G- (safe drop) without halting linear track
 *
 * Architecture:
 *
 *                        [ Source & Manifest ]
 *                                │
 *            ┌───────────────────┴───────────────────┐
 *            ▼                                       ▼
 *   ┌─────────────────┐                     ┌─────────────────┐
 *   │  LINEAR TRACK   │                     │ NONLINEAR TRACK │
 *   │ (Deterministic) │                     │  (Speculative)  │
 *   └────────┬────────┘                     └────────┬────────┘
 *            │                                       │
 *   Strict Dependency Tree                 Graph & AI-Driven
 *   (Boot → HAL → Kernel)                  Parallel Synthesis
 *            │                                       │
 *            └───────────────────┬───────────────────┘
 *                                ▼
 *                   [ Phase Checkpoint / Collapse ]
 *                    Recorder(+0) → Skeptic(-1) → Merge
 *                                │
 *                                ▼
 *                     [ Executable Target ]
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_DUALTRACK_H
#define ZEDEC_DUALTRACK_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define DT_MAX_STAGES          32
#define DT_MAX_LABEL           64
#define DT_MAX_NOTES          256
#define DT_MAX_HISTORY        128
#define DT_MAX_PHASE_GATES     32
#define DT_MAX_ARTIFACTS      128
#define DT_HASH_SIZE          32
#define DT_MAX_KEY_INDEX       5
#define DT_PHASE_CLOCK_MS     10

/* ===== Track Types ===== */

typedef enum {
    DT_TRACK_LINEAR    = 0,  /* Sequential, deterministic */
    DT_TRACK_NONLINEAR = 1,  /* Parallel, speculative (second quantization) */
} dt_track_t;

/* ===== Stage States ===== */

typedef enum {
    DT_STAGE_PENDING    = 0,
    DT_STAGE_RUNNING    = 1,
    DT_STAGE_COMPLETE   = 2,
    DT_STAGE_FAILED     = 3,
    DT_STAGE_SKIPPED    = 4,
    DT_STAGE_GLUT_PLUS  = 5,  /* Speculative: continuing while linear stalls */
    DT_STAGE_GLUT_MINUS = 6,  /* Safe drop: result discarded */
    DT_STAGE_GLUT_ZERO  = 7,  /* Freeze: both tracks diverge, locked */
} dt_stage_state_t;

/* ===== Phase Gate Results ===== */

typedef enum {
    DT_GATE_AGREE    = 0,  /* Both tracks agree → TRUE */
    DT_GATE_DIVERGE  = 1,  /* Tracks diverge → GLUT+ */
    DT_GATE_LINEAR_ONLY = 2,  /* Only linear succeeded */
    DT_GATE_NONLINEAR_ONLY = 3,  /* Only nonlinear succeeded */
    DT_GATE_BOTH_FAIL = 4,  /* Both failed → GLUT0 */
    DT_GATE_NOT_REACHED = 5,  /* Gate not yet evaluated */
} dt_gate_result_t;

/* ===== Triad Protocol Roles ===== */

typedef enum {
    DT_TRIAD_OPERATOR  = 0,  /* Executes the build action */
    DT_TRIAD_RECORDER  = 1,  /* +0: Hashes artifacts, submits signatures */
    DT_TRIAD_SKEPTIC   = 2,  /* -1: Validates against safety constraints */
} dt_triad_role_t;

/* ===== Artifact Types ===== */

typedef enum {
    DT_ARTIFACT_BINARY    = 0,  /* Compiled .bin / .elf / .iso */
    DT_ARTIFACT_HDL       = 1,  /* SystemVerilog / VHDL / Chisel */
    DT_ARTIFACT_SMAP      = 2,  /* Reassembly Key File (.smap) */
    DT_ARTIFACT_HDCM      = 3,  /* Hyperdimensional projection matrix */
    DT_ARTIFACT_RTL_BIT   = 4,  /* FPGA bitstream */
    DT_ARTIFACT_VM        = 5,  /* Multi-VM runtime (EVM/WASM/MoveVM) */
    DT_ARTIFACT_KERNEL    = 6,  /* Kernel module / driver */
    DT_ARTIFACT_USERLAND  = 7,  /* User-space utility / library */
} dt_artifact_type_t;

/* ===== Build Artifact ===== */

typedef struct {
    char     name[DT_MAX_LABEL];
    char     path[DT_MAX_NOTES];
    uint8_t  hash[DT_HASH_SIZE];      /* SHA-256 / BLAKE3 hash */
    uint32_t size;
    dt_artifact_type_t type;
    dt_track_t produced_by;           /* Which track produced this */
    bool     validated;               /* Passed Skeptic validation */
    bool     linked;                  /* Merged into final output */
    uint8_t  key_index;               /* K1..K5 signing key */
} dt_artifact_t;

/* ===== Second Quantization Phases ===== */

typedef enum {
    DT_SQ_CREATE    = 0,  /* Encode input into Fock state */
    DT_SQ_ENTANGLE  = 1,  /* Apply transformation matrix */
    DT_SQ_MEASURE   = 2,  /* Collapse to output */
} dt_sq_phase_t;

/* ===== Stage Descriptor ===== */

typedef struct {
    char     name[DT_MAX_LABEL];
    char     notes[DT_MAX_NOTES];
    dt_stage_state_t linear_state;
    dt_stage_state_t nonlinear_state;
    dt_gate_result_t gate_result;
    uint32_t linear_duration_ms;
    uint32_t nonlinear_duration_ms;
    uint32_t sq_phase;               /* Which SQ phase this stage is in */
    bool     has_gate;               /* Phase checkpoint after this stage? */
    bool     active;
    uint32_t artifact_indices[8];    /* Artifacts produced by this stage */
    uint32_t num_artifacts;
} dt_stage_t;

/* ===== Phase Gate ===== */

typedef struct {
    uint32_t stage_idx;       /* Which stage this gate follows */
    dt_gate_result_t result;
    char     linear_summary[DT_MAX_NOTES];
    char     nonlinear_summary[DT_MAX_NOTES];
    uint32_t agreement_score;        /* 0-100 */
    bool     resolved;               /* Has the gate been resolved? */
    /* Triad Protocol */
    bool     recorder_checked;       /* +0: artifacts hashed + signed */
    bool     skeptic_validated;      /* -1: safety constraints checked */
    bool     operator_merged;        /* Build action executed merge */
    uint32_t phase_tick;             /* Phase clock tick at evaluation */
} dt_phase_gate_t;

/* ===== Track Statistics ===== */

typedef struct {
    uint32_t stages_run;
    uint32_t stages_passed;
    uint32_t stages_failed;
    uint32_t stages_skipped;
    uint32_t total_duration_ms;
    uint32_t glut_plus_count;
    uint32_t glut_minus_count;
    uint32_t glut_zero_count;
    uint32_t artifacts_produced;
    uint32_t artifacts_validated;
    uint32_t artifacts_linked;
} dt_track_stats_t;

/* ===== Dual-Track Pipeline ===== */

typedef struct {
    dt_stage_t      stages[DT_MAX_STAGES];
    uint32_t        num_stages;
    dt_phase_gate_t gates[DT_MAX_PHASE_GATES];
    uint32_t        num_gates;
    dt_track_stats_t linear_stats;
    dt_track_stats_t nonlinear_stats;
    uint32_t        current_stage;
    bool            linear_active;
    bool            nonlinear_active;
    bool            initialized;
    bool            completed;
    uint32_t        gates_agreed;
    uint32_t        gates_diverged;
    dt_artifact_t   artifacts[DT_MAX_ARTIFACTS];
    uint32_t        num_artifacts;
    uint32_t        gates_both_failed;
    uint32_t        total_duration_ms;
    uint32_t        total_artifacts_linked;
    uint32_t        phase_tick;          /* 10ms phase clock */
    /* Triad state */
    bool            triad_recorder_active;
    bool            triad_skeptic_active;
    bool            triad_operator_active;
} dualtrack_t;

/* ===== API ===== */

void dualtrack_init(dualtrack_t *dt);

/* Stage management */
int32_t dualtrack_stage_add(dualtrack_t *dt, const char *name,
                             bool has_gate);
int dualtrack_stage_set_notes(dualtrack_t *dt, uint32_t stage_idx,
                               const char *notes);
int dualtrack_stage_set_sq_phase(dualtrack_t *dt, uint32_t stage_idx,
                                  dt_sq_phase_t phase);

/* Execution — runs both tracks simultaneously */
int dualtrack_run_stage(dualtrack_t *dt, uint32_t stage_idx,
                         bool linear_ok, uint32_t linear_ms,
                         bool nonlinear_ok, uint32_t nonlinear_ms,
                         const char *linear_summary,
                         const char *nonlinear_summary);

/* Phase gate evaluation */
dt_gate_result_t dualtrack_eval_gate(dualtrack_t *dt, uint32_t stage_idx,
                                      bool linear_ok, bool nonlinear_ok,
                                      const char *linear_summary,
                                      const char *nonlinear_summary);
const dt_phase_gate_t *dualtrack_get_gate(dualtrack_t *dt, uint32_t gate_idx);

/* Full pipeline run (all stages) */
int dualtrack_run_all(dualtrack_t *dt,
                       bool (*stage_fn)(dualtrack_t *dt, uint32_t stage_idx,
                                        dt_track_t track,
                                        char *summary, uint32_t max_len));

/* Artifact management */
int32_t dualtrack_artifact_add(dualtrack_t *dt, const char *name,
                                const char *path, dt_artifact_type_t type,
                                dt_track_t produced_by, uint8_t key_index,
                                uint32_t size, const uint8_t *hash);
int dualtrack_artifact_validate(dualtrack_t *dt, uint32_t artifact_idx);
int dualtrack_artifact_link(dualtrack_t *dt, uint32_t artifact_idx);
int dualtrack_stage_add_artifact(dualtrack_t *dt, uint32_t stage_idx,
                                  uint32_t artifact_idx);
const dt_artifact_t *dualtrack_artifact_get(dualtrack_t *dt, uint32_t idx);

/* Triad Protocol: Recorder/Skeptic/Operator */
int dualtrack_recorder_check(dualtrack_t *dt, uint32_t stage_idx);
int dualtrack_skeptic_validate(dualtrack_t *dt, uint32_t stage_idx);
int dualtrack_operator_merge(dualtrack_t *dt, uint32_t stage_idx);
int dualtrack_triad_run(dualtrack_t *dt, uint32_t stage_idx);

/* Coordination: resolve divergence */
int dualtrack_resolve_divergence(dualtrack_t *dt, uint32_t stage_idx,
                                  bool prefer_nonlinear);

/* Phase clock */
void dualtrack_tick(dualtrack_t *dt);
uint32_t dualtrack_phase_clock(const dualtrack_t *dt);

/* Utility */
const char *dualtrack_track_name(dt_track_t track);
const char *dualtrack_stage_state_name(dt_stage_state_t state);
const char *dualtrack_gate_result_name(dt_gate_result_t result);
const char *dualtrack_sq_phase_name(dt_sq_phase_t phase);
const char *dualtrack_triad_role_name(dt_triad_role_t role);
const char *dualtrack_artifact_type_name(dt_artifact_type_t type);

/* Reporting */
int dualtrack_report(const dualtrack_t *dt, char *buf, uint32_t max_len);
/* gates_agreed / num_gates in permille (1000 when there are no gates). */
uint32_t dualtrack_agreement_permille(const dualtrack_t *dt);
bool dualtrack_all_gates_resolved(const dualtrack_t *dt);

#endif /* ZEDEC_DUALTRACK_H */
