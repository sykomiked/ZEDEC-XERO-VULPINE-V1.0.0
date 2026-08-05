/*
 * nlb.h — Nonlinear Build Engine (NLB)
 *
 * Core OS feature: exposes the CREATE → ENTANGLE → MEASURE
 * nonlinear compilation methodology to ZEDEC pqOS users.
 *
 * Unlike linear build pipelines (make, cmake), the NLB engine:
 *   - Evaluates intermediate compilation states dynamically
 *   - Routes failing branches to correction sub-routines
 *   - Explores multiple compilation strategies in parallel
 *   - Converges on the final artifact via feedback loops
 *
 * Hardware-as-code: the NLB engine is a virtual device with
 * registers, DMA buffers, and IRQ-driven completion notification.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

#ifndef ZEDEC_NLB_H
#define ZEDEC_NLB_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"

/* ===== Constants ===== */

#define NLB_MAX_PHASES        3    /* CREATE, ENTANGLE, MEASURE */
#define NLB_MAX_BRANCHES      8    /* Parallel compilation branches */
#define NLB_MAX_SOURCES      64    /* Source files per build */
#define NLB_MAX_ARTIFACTS    32    /* Output artifacts per build */
#define NLB_MAX_PATH_LEN    128
#define NLB_MAX_LABEL_LEN    64
#define NLB_DMA_BUFFER_SIZE 4096
#define NLB_NUM_REGISTERS    16
#define NLB_MAX_ITERATIONS    5    /* Max convergence iterations */
#define NLB_MAX_ERROR_LEN   256

/* ===== Build Phases (CREATE → ENTANGLE → MEASURE) ===== */

typedef enum {
    NLB_PHASE_IDLE       = 0,
    NLB_PHASE_CREATE     = 1,  /* Compile sources, create intermediate objects */
    NLB_PHASE_ENTANGLE   = 2,  /* Evaluate outputs, cross-link, auto-correct */
    NLB_PHASE_MEASURE    = 3,  /* Produce final artifacts, verify integrity */
    NLB_PHASE_DONE       = 4,
    NLB_PHASE_FAILED     = 5,
} nlb_phase_t;

/* ===== Branch States ===== */

typedef enum {
    NLB_BRANCH_PENDING    = 0,
    NLB_BRANCH_COMPILING  = 1,
    NLB_BRANCH_PASSED     = 2,
    NLB_BRANCH_FAILED     = 3,
    NLB_BRANCH_CORRECTING = 4,  /* AI/feedback loop is fixing errors */
    NLB_BRANCH_MERGED     = 5,  /* Successfully merged into final output */
} nlb_branch_state_t;

/* ===== Source File Entry ===== */

typedef struct {
    char     path[NLB_MAX_PATH_LEN];
    char     label[NLB_MAX_LABEL_LEN];
    uint32_t size;
    uint32_t flags;          /* Compile flags bitmask */
    bool     is_header;
} nlb_source_t;

/* ===== Build Artifact ===== */

typedef struct {
    char     path[NLB_MAX_PATH_LEN];
    char     label[NLB_MAX_LABEL_LEN];
    uint32_t size;
    uint8_t  sha256[32];     /* Content hash for reproducibility */
    bool     verified;       /* Passed integrity check */
} nlb_artifact_t;

/* ===== Compilation Branch ===== */

typedef struct {
    nlb_branch_state_t state;
    char               label[NLB_MAX_LABEL_LEN];
    uint32_t           source_count;
    uint32_t           source_indices[NLB_MAX_SOURCES];
    int32_t            exit_code;
    char               last_error[NLB_MAX_ERROR_LEN];
    uint32_t           iterations;    /* Convergence iterations for this branch */
    uint32_t           corrections;  /* Number of auto-corrections applied */
    m5_coords_t        m5;           /* M5 coverage coordinates for this branch */
} nlb_branch_t;

/* ===== Build Session ===== */

typedef struct {
    uint32_t        session_id;
    nlb_phase_t     phase;
    uint32_t        num_sources;
    nlb_source_t    sources[NLB_MAX_SOURCES];
    uint32_t        num_branches;
    nlb_branch_t    branches[NLB_MAX_BRANCHES];
    uint32_t        num_artifacts;
    nlb_artifact_t  artifacts[NLB_MAX_ARTIFACTS];
    uint32_t        total_iterations;
    uint32_t        total_corrections;
    m5_coords_t     system_m5;       /* Aggregate M5 coverage */
    surplus_real_t  coverage_ratio;  /* Overall build coverage */
    bool            converged;       /* All branches converged */
    char            target_label[NLB_MAX_LABEL_LEN];

    /* Hardware-as-code: virtual device registers */
    uint32_t        registers[NLB_NUM_REGISTERS];
    uint8_t         dma_buffer[NLB_DMA_BUFFER_SIZE];
    uint32_t        dma_pos;
    bool            irq_pending;
    uint32_t        irq_status;
} nlb_session_t;

/* ===== Register Map (hardware-as-code) ===== */

typedef enum {
    NLB_REG_SESSION_ID   = 0,  /* RO: current session ID */
    NLB_REG_PHASE        = 1,  /* RO: current phase */
    NLB_REG_NUM_SOURCES  = 2,  /* RO: source count */
    NLB_REG_NUM_BRANCHES = 3,  /* RO: branch count */
    NLB_REG_NUM_ARTIFACTS= 4,  /* RO: artifact count */
    NLB_REG_ITERATIONS   = 5,  /* RO: total iterations */
    NLB_REG_CORRECTIONS  = 6,  /* RO: total corrections */
    NLB_REG_COVERAGE     = 7,  /* RO: coverage ratio (Q32.32) */
    NLB_REG_CONVERGED    = 8,  /* RO: 1 if converged, 0 if not */
    NLB_REG_IRQ_ENABLE   = 9,  /* RW: IRQ enable bitmask */
    NLB_REG_IRQ_STATUS   = 10, /* RW: IRQ status (write to clear) */
    NLB_REG_COMMAND      = 11, /* WO: write command to trigger action */
    NLB_REG_STATUS       = 12, /* RO: device status */
    NLB_REG_DMA_ADDR     = 13, /* RW: DMA buffer read/write position */
    NLB_REG_DMA_LEN      = 14, /* RW: DMA transfer length */
    NLB_REG_CONFIG       = 15, /* RW: build configuration flags */
} nlb_register_t;

/* ===== Commands (write to NLB_REG_COMMAND) ===== */

typedef enum {
    NLB_CMD_NONE         = 0,
    NLB_CMD_INIT         = 1,  /* Initialize new build session */
    NLB_CMD_ADD_SOURCE   = 2,  /* Add source file via DMA */
    NLB_CMD_CREATE       = 3,  /* Start CREATE phase */
    NLB_CMD_ENTANGLE     = 4,  /* Start ENTANGLE phase */
    NLB_CMD_MEASURE      = 5,  /* Start MEASURE phase */
    NLB_CMD_ABORT        = 6,  /* Abort current build */
    NLB_CMD_RESET        = 7,  /* Reset device */
} nlb_command_t;

/* ===== IRQ Sources ===== */

typedef enum {
    NLB_IRQ_PHASE_COMPLETE = (1 << 0),  /* A phase completed */
    NLB_IRQ_BRANCH_PASSED  = (1 << 1),  /* A branch passed compilation */
    NLB_IRQ_BRANCH_FAILED  = (1 << 2),  /* A branch failed */
    NLB_IRQ_CORRECTION     = (1 << 3),  /* Auto-correction was applied */
    NLB_IRQ_CONVERGED      = (1 << 4),  /* All branches converged */
    NLB_IRQ_ARTIFACT_READY = (1 << 5),  /* An artifact was produced */
} nlb_irq_source_t;

/* ===== API ===== */

void nlb_session_init(nlb_session_t *s, uint32_t session_id, const char *target);
int32_t nlb_source_add(nlb_session_t *s, const char *path, const char *label,
                        uint32_t size, uint32_t flags, bool is_header);
int32_t nlb_branch_create(nlb_session_t *s, const char *label,
                           const uint32_t *source_indices, uint32_t count);

/* Phase execution */
int nlb_phase_create(nlb_session_t *s);     /* Compile all branches */
int nlb_phase_entangle(nlb_session_t *s);   /* Evaluate + auto-correct */
int nlb_phase_measure(nlb_session_t *s);    /* Produce + verify artifacts */

/* Full pipeline: CREATE → ENTANGLE → MEASURE */
int nlb_build_run(nlb_session_t *s);

/* Auto-correction: applies feedback to a failing branch */
int nlb_branch_correct(nlb_session_t *s, uint32_t branch_idx);

/* Convergence check: are all branches in a passing/merged state? */
bool nlb_check_convergence(nlb_session_t *s);

/* M5 coverage computation for the build */
void nlb_update_coverage(nlb_session_t *s);

/* Hardware-as-code register interface */
uint32_t nlb_register_read(nlb_session_t *s, uint32_t reg);
int nlb_register_write(nlb_session_t *s, uint32_t reg, uint32_t value);

/* DMA interface */
int nlb_dma_write(nlb_session_t *s, const uint8_t *data, uint32_t len);
int nlb_dma_read(nlb_session_t *s, uint8_t *buf, uint32_t len);

/* IRQ handling */
uint32_t nlb_irq_get_status(nlb_session_t *s);
void nlb_irq_clear(nlb_session_t *s, uint32_t mask);
void nlb_irq_raise(nlb_session_t *s, uint32_t source);

/* Utility */
const char *nlb_phase_name(nlb_phase_t phase);
const char *nlb_branch_state_name(nlb_branch_state_t state);

#endif /* ZEDEC_NLB_H */
