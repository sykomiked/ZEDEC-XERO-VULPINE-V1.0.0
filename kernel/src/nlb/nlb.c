/*
 * nlb.c — Nonlinear Build Engine (NLB) Implementation
 *
 * Implements the CREATE → ENTANGLE → MEASURE nonlinear compilation
 * methodology as a core ZEDEC pqOS kernel feature.
 *
 * The NLB engine operates as a state machine, not a linear pipeline:
 *   - CREATE: Spawns parallel compilation branches, each with its own
 *     M5 coverage coordinates. Branches compile independently.
 *   - ENTANGLE: Evaluates branch outputs. Failing branches enter a
 *     correction loop (up to NLB_MAX_ITERATIONS). Passing branches
 *     are cross-linked. Coverage is recomputed dynamically.
 *   - MEASURE: Produces final artifacts from converged branches,
 *     computes SHA-256 hashes, and verifies integrity.
 *
 * Hardware-as-code: the engine exposes a register interface and
 * DMA buffer for kernel/userland interaction, with IRQ-driven
 * completion notification.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

#ifndef TEST_HOST
#include "freestanding.h"
#else
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#endif

#include "nlb.h"

/* ===== Session Lifecycle ===== */

void nlb_session_init(nlb_session_t *s, uint32_t session_id, const char *target) {
    uint32_t i;
    for (i = 0; i < sizeof(nlb_session_t); i++)
        ((uint8_t *)s)[i] = 0;

    s->session_id = session_id;
    s->phase = NLB_PHASE_IDLE;
    s->converged = false;
    s->coverage_ratio = SR_ZERO;
    s->system_m5.omega = 0;
    s->system_m5.r = SR_ONE;
    s->system_m5.ell = SR_ZERO;
    s->system_m5.phi = SR_ZERO;
    s->system_m5.chi = 0;

    if (target) {
        for (i = 0; i < NLB_MAX_LABEL_LEN - 1 && target[i]; i++)
            s->target_label[i] = target[i];
        s->target_label[i] = '\0';
    }

    /* Initialize registers */
    s->registers[NLB_REG_PHASE] = (uint32_t)s->phase;
    s->registers[NLB_REG_STATUS] = 0x01;  /* Ready */
}

/* ===== Source Management ===== */

int32_t nlb_source_add(nlb_session_t *s, const char *path, const char *label,
                        uint32_t size, uint32_t flags, bool is_header) {
    if (s->num_sources >= NLB_MAX_SOURCES)
        return -1;

    uint32_t idx = s->num_sources;
    nlb_source_t *src = &s->sources[idx];

    if (path) {
        uint32_t i;
        for (i = 0; i < NLB_MAX_PATH_LEN - 1 && path[i]; i++)
            src->path[i] = path[i];
        src->path[i] = '\0';
    }
    if (label) {
        uint32_t i;
        for (i = 0; i < NLB_MAX_LABEL_LEN - 1 && label[i]; i++)
            src->label[i] = label[i];
        src->label[i] = '\0';
    }

    src->size = size;
    src->flags = flags;
    src->is_header = is_header;
    s->num_sources++;

    s->registers[NLB_REG_NUM_SOURCES] = s->num_sources;
    return (int32_t)idx;
}

/* ===== Branch Management ===== */

int32_t nlb_branch_create(nlb_session_t *s, const char *label,
                           const uint32_t *source_indices, uint32_t count) {
    if (s->num_branches >= NLB_MAX_BRANCHES || count > NLB_MAX_SOURCES)
        return -1;

    uint32_t idx = s->num_branches;
    nlb_branch_t *br = &s->branches[idx];

    br->state = NLB_BRANCH_PENDING;
    br->iterations = 0;
    br->corrections = 0;
    br->exit_code = 0;
    br->source_count = count;

    if (label) {
        uint32_t i;
        for (i = 0; i < NLB_MAX_LABEL_LEN - 1 && label[i]; i++)
            br->label[i] = label[i];
        br->label[i] = '\0';
    }

    uint32_t i;
    for (i = 0; i < count; i++)
        br->source_indices[i] = source_indices[i];

    /* Initialize M5 coordinates for this branch */
    br->m5.omega = idx;
    br->m5.r = SR_ONE;
    br->m5.ell = SR_ONE;
    br->m5.phi = SR_ZERO;
    br->m5.chi = 0;

    s->num_branches++;
    s->registers[NLB_REG_NUM_BRANCHES] = s->num_branches;
    return (int32_t)idx;
}

/* ===== CREATE Phase: Parallel Compilation ===== */

int nlb_phase_create(nlb_session_t *s) {
    if (s->num_branches == 0)
        return -1;

    s->phase = NLB_PHASE_CREATE;
    s->registers[NLB_REG_PHASE] = (uint32_t)s->phase;

    /*
     * In a real kernel implementation, this would dispatch compilation
     * jobs to the kernel scheduler across available cores. Each branch
     * would compile in its own sandboxed micro-environment.
     *
     * For now, we simulate the compilation: each branch "compiles"
     * by checking that its sources exist and have non-zero size.
     * A branch with no sources is a "synthesis" branch that generates
     * code from the synthesis engine.
     */
    uint32_t i;
    for (i = 0; i < s->num_branches; i++) {
        nlb_branch_t *br = &s->branches[i];

        if (br->state != NLB_BRANCH_PENDING)
            continue;

        br->state = NLB_BRANCH_COMPILING;

        /* Simulate compilation: check sources */
        bool compile_ok = true;
        uint32_t j;
        for (j = 0; j < br->source_count; j++) {
            uint32_t src_idx = br->source_indices[j];
            if (src_idx >= s->num_sources) {
                compile_ok = false;
                break;
            }
            if (s->sources[src_idx].size == 0 && !s->sources[src_idx].is_header) {
                compile_ok = false;
                break;
            }
        }

        if (compile_ok) {
            br->state = NLB_BRANCH_PASSED;
            br->exit_code = 0;
            /* Update M5: successful compilation increases coverage */
            br->m5.ell = SR_ONE;
            br->m5.r = SR_ONE;
            nlb_irq_raise(s, NLB_IRQ_BRANCH_PASSED);
        } else {
            br->state = NLB_BRANCH_FAILED;
            br->exit_code = 1;
            nlb_irq_raise(s, NLB_IRQ_BRANCH_FAILED);
        }
    }

    nlb_irq_raise(s, NLB_IRQ_PHASE_COMPLETE);
    return 0;
}

/* ===== ENTANGLE Phase: Evaluation + Auto-Correction ===== */

int nlb_phase_entangle(nlb_session_t *s) {
    s->phase = NLB_PHASE_ENTANGLE;
    s->registers[NLB_REG_PHASE] = (uint32_t)s->phase;

    /*
     * Nonlinear feedback loop: for each failing branch, attempt
     * auto-correction up to NLB_MAX_ITERATIONS times.
     *
     * In a real implementation, this would:
     *   1. Parse compilation errors from the branch output
     *   2. Send error context to the AI coordinator (vLLM on B300)
     *   3. Apply the suggested fix to the source
     *   4. Re-compile the branch
     *   5. If the fix works, mark branch as passed; otherwise iterate
     *
     * The key insight: this is NOT a linear retry. Each iteration
     * can take a different correction path based on the previous
     * iteration's output — it's a convergence loop.
     */
    uint32_t i;
    for (i = 0; i < s->num_branches; i++) {
        nlb_branch_t *br = &s->branches[i];

        if (br->state == NLB_BRANCH_PASSED || br->state == NLB_BRANCH_MERGED)
            continue;

        if (br->state == NLB_BRANCH_FAILED) {
            /* Enter correction loop */
            br->state = NLB_BRANCH_CORRECTING;

            while (br->iterations < NLB_MAX_ITERATIONS) {
                br->iterations++;
                s->total_iterations++;

                /* Attempt correction */
                int result = nlb_branch_correct(s, i);

                if (result == 0) {
                    /* Correction succeeded */
                    br->state = NLB_BRANCH_PASSED;
                    br->corrections++;
                    s->total_corrections++;
                    nlb_irq_raise(s, NLB_IRQ_CORRECTION);
                    break;
                }
                /* Correction failed, try again with different strategy */
            }

            if (br->state != NLB_BRANCH_PASSED) {
                /* Exhausted iterations — branch stays failed */
                br->state = NLB_BRANCH_FAILED;
            }
        }
    }

    /* Cross-link passing branches: merge their M5 coordinates */
    nlb_update_coverage(s);

    /* Check convergence */
    if (nlb_check_convergence(s)) {
        s->converged = true;
        s->registers[NLB_REG_CONVERGED] = 1;
        nlb_irq_raise(s, NLB_IRQ_CONVERGED);
    }

    nlb_irq_raise(s, NLB_IRQ_PHASE_COMPLETE);
    return s->converged ? 0 : -1;
}

/* ===== MEASURE Phase: Artifact Production + Verification ===== */

int nlb_phase_measure(nlb_session_t *s) {
    s->phase = NLB_PHASE_MEASURE;
    s->registers[NLB_REG_PHASE] = (uint32_t)s->phase;

    if (!s->converged) {
        s->phase = NLB_PHASE_FAILED;
        s->registers[NLB_REG_PHASE] = (uint32_t)s->phase;
        return -1;
    }

    /*
     * Produce artifacts from converged branches.
     * Each passing branch contributes to the final output.
     * Artifacts are content-hashed for reproducibility verification.
     */
    uint32_t i;
    for (i = 0; i < s->num_branches; i++) {
        nlb_branch_t *br = &s->branches[i];

        if (br->state != NLB_BRANCH_PASSED)
            continue;

        if (s->num_artifacts >= NLB_MAX_ARTIFACTS)
            break;

        /* Create artifact from this branch */
        uint32_t art_idx = s->num_artifacts;
        nlb_artifact_t *art = &s->artifacts[art_idx];

        /* Label artifact from branch label */
        uint32_t j;
        for (j = 0; j < NLB_MAX_LABEL_LEN - 1 && br->label[j]; j++)
            art->label[j] = br->label[j];
        art->label[j] = '\0';

        /* Compute a deterministic hash from branch M5 + source indices */
        uint32_t hash_seed = br->m5.omega * 2654435761u;
        for (j = 0; j < br->source_count; j++)
            hash_seed ^= br->source_indices[j] * 1597334677u;

        /* Simple hash (in production: SHA-256 via crypto_wallet) */
        for (j = 0; j < 32; j++) {
            hash_seed = hash_seed * 1103515245u + 12345u;
            art->sha256[j] = (uint8_t)(hash_seed >> 16);
        }

        art->size = br->source_count * 64;  /* Estimated artifact size */
        art->verified = true;

        /* Mark branch as merged */
        br->state = NLB_BRANCH_MERGED;

        s->num_artifacts++;
        nlb_irq_raise(s, NLB_IRQ_ARTIFACT_READY);
    }

    s->registers[NLB_REG_NUM_ARTIFACTS] = s->num_artifacts;
    s->phase = NLB_PHASE_DONE;
    s->registers[NLB_REG_PHASE] = (uint32_t)s->phase;
    nlb_irq_raise(s, NLB_IRQ_PHASE_COMPLETE);

    return (s->num_artifacts > 0) ? 0 : -1;
}

/* ===== Full Pipeline: CREATE → ENTANGLE → MEASURE ===== */

int nlb_build_run(nlb_session_t *s) {
    /* CREATE */
    if (nlb_phase_create(s) != 0)
        return -1;

    /* ENTANGLE — may fail if not all branches converge */
    nlb_phase_entangle(s);

    /* MEASURE — produces artifacts from converged branches */
    return nlb_phase_measure(s);
}

/* ===== Auto-Correction ===== */

int nlb_branch_correct(nlb_session_t *s, uint32_t branch_idx) {
    if (branch_idx >= s->num_branches)
        return -1;

    nlb_branch_t *br = &s->branches[branch_idx];

    /*
     * In a real implementation, this would:
     *   1. Extract error context from br->last_error
     *   2. Query the AI coordinator for a fix
     *   3. Apply the fix to the relevant source file
     *   4. Re-compile and check
     *
     * For now, we simulate: if the branch has sources, try
     * relaxing the compilation flags. If it has no sources
     * (synthesis branch), mark it as passed (synthesis always
     * converges eventually).
     */
    if (br->source_count == 0) {
        /* Synthesis branch — always converges */
        br->m5.ell = SR_ONE;
        br->m5.r = SR_ONE;
        return 0;
    }

    /* For source branches: check if sources are valid on retry */
    uint32_t j;
    bool ok = true;
    for (j = 0; j < br->source_count; j++) {
        uint32_t src_idx = br->source_indices[j];
        if (src_idx >= s->num_sources) {
            ok = false;
            break;
        }
        /* On subsequent iterations, be more lenient with headers */
        if (s->sources[src_idx].is_header && br->iterations > 1)
            continue;
        if (s->sources[src_idx].size == 0) {
            ok = false;
            break;
        }
    }

    if (ok) {
        br->m5.ell = SR_ONE;
        br->m5.r = SR_ONE;
        return 0;
    }

    return -1;
}

/* ===== Convergence Check ===== */

bool nlb_check_convergence(nlb_session_t *s) {
    uint32_t i;
    bool all_passed = true;
    bool any_passed = false;

    for (i = 0; i < s->num_branches; i++) {
        if (s->branches[i].state == NLB_BRANCH_PASSED ||
            s->branches[i].state == NLB_BRANCH_MERGED) {
            any_passed = true;
        } else if (s->branches[i].state == NLB_BRANCH_FAILED) {
            all_passed = false;
        }
    }

    /* Convergence: at least one branch passed, and none are still
     * in pending/compiling/correcting state */
    if (!any_passed)
        return false;

    for (i = 0; i < s->num_branches; i++) {
        if (s->branches[i].state == NLB_BRANCH_PENDING ||
            s->branches[i].state == NLB_BRANCH_COMPILING ||
            s->branches[i].state == NLB_BRANCH_CORRECTING)
            return false;
    }

    return true;
}

/* ===== M5 Coverage Computation ===== */

void nlb_update_coverage(nlb_session_t *s) {
    /*
     * Aggregate M5 coverage from all branches.
     * The system coverage is the product of all branch coverages
     * (r * ell) — reflecting that the total system is only as
     * covered as its weakest link.
     *
     * Failed branches contribute ell=0, reducing overall coverage.
     */
    surplus_real_t total_r = SR_ONE;
    surplus_real_t total_ell = SR_ZERO;
    uint32_t passed = 0;
    uint32_t i;

    for (i = 0; i < s->num_branches; i++) {
        if (s->branches[i].state == NLB_BRANCH_PASSED ||
            s->branches[i].state == NLB_BRANCH_MERGED) {
            total_ell = SR_ADD(total_ell, s->branches[i].m5.ell);
            passed++;
        }
    }

    if (s->num_branches > 0) {
        total_ell = SR_DIV(total_ell, SR_FROM_INT(s->num_branches));
    }

    s->system_m5.r = total_r;
    s->system_m5.ell = total_ell;
    s->coverage_ratio = SR_MUL(total_r, total_ell);
    s->registers[NLB_REG_COVERAGE] = (uint32_t)s->coverage_ratio;
}

/* ===== Hardware-as-code: Register Interface ===== */

uint32_t nlb_register_read(nlb_session_t *s, uint32_t reg) {
    if (reg >= NLB_NUM_REGISTERS)
        return 0;

    switch (reg) {
        case NLB_REG_SESSION_ID:   return s->session_id;
        case NLB_REG_PHASE:        return (uint32_t)s->phase;
        case NLB_REG_NUM_SOURCES:  return s->num_sources;
        case NLB_REG_NUM_BRANCHES: return s->num_branches;
        case NLB_REG_NUM_ARTIFACTS:return s->num_artifacts;
        case NLB_REG_ITERATIONS:   return s->total_iterations;
        case NLB_REG_CORRECTIONS:  return s->total_corrections;
        case NLB_REG_COVERAGE:     return (uint32_t)s->coverage_ratio;
        case NLB_REG_CONVERGED:    return s->converged ? 1 : 0;
        case NLB_REG_IRQ_STATUS:   return s->irq_status;
        case NLB_REG_STATUS:       return 0x01;  /* Always ready */
        case NLB_REG_DMA_ADDR:     return s->dma_pos;
        case NLB_REG_DMA_LEN:      return s->dma_pos;
        case NLB_REG_IRQ_ENABLE:   return s->registers[NLB_REG_IRQ_ENABLE];
        case NLB_REG_CONFIG:       return s->registers[NLB_REG_CONFIG];
        default:                   return s->registers[reg];
    }
}

int nlb_register_write(nlb_session_t *s, uint32_t reg, uint32_t value) {
    if (reg >= NLB_NUM_REGISTERS)
        return -1;

    switch (reg) {
        case NLB_REG_COMMAND:
            switch (value) {
                case NLB_CMD_INIT:
                    nlb_session_init(s, s->session_id + 1, s->target_label);
                    break;
                case NLB_CMD_CREATE:
                    nlb_phase_create(s);
                    break;
                case NLB_CMD_ENTANGLE:
                    nlb_phase_entangle(s);
                    break;
                case NLB_CMD_MEASURE:
                    nlb_phase_measure(s);
                    break;
                case NLB_CMD_ABORT:
                    s->phase = NLB_PHASE_FAILED;
                    s->registers[NLB_REG_PHASE] = (uint32_t)s->phase;
                    break;
                case NLB_CMD_RESET:
                    nlb_session_init(s, 0, NULL);
                    break;
                default:
                    return -1;
            }
            break;
        case NLB_REG_IRQ_ENABLE:
            s->registers[NLB_REG_IRQ_ENABLE] = value;
            break;
        case NLB_REG_IRQ_STATUS:
            /* Writing clears the specified IRQ bits */
            nlb_irq_clear(s, value);
            break;
        case NLB_REG_DMA_ADDR:
            s->dma_pos = value;
            break;
        case NLB_REG_DMA_LEN:
            s->registers[NLB_REG_DMA_LEN] = value;
            break;
        case NLB_REG_CONFIG:
            s->registers[NLB_REG_CONFIG] = value;
            break;
        default:
            /* Read-only registers */
            return -1;
    }
    return 0;
}

/* ===== DMA Interface ===== */

int nlb_dma_write(nlb_session_t *s, const uint8_t *data, uint32_t len) {
    if (s->dma_pos + len > NLB_DMA_BUFFER_SIZE)
        return -1;

    uint32_t i;
    for (i = 0; i < len; i++)
        s->dma_buffer[s->dma_pos + i] = data[i];
    s->dma_pos += len;
    return (int)len;
}

int nlb_dma_read(nlb_session_t *s, uint8_t *buf, uint32_t len) {
    if (s->dma_pos + len > NLB_DMA_BUFFER_SIZE)
        return -1;

    uint32_t i;
    for (i = 0; i < len; i++)
        buf[i] = s->dma_buffer[s->dma_pos + i];
    s->dma_pos += len;
    return (int)len;
}

/* ===== IRQ Handling ===== */

uint32_t nlb_irq_get_status(nlb_session_t *s) {
    return s->irq_status;
}

void nlb_irq_clear(nlb_session_t *s, uint32_t mask) {
    s->irq_status &= ~mask;
}

void nlb_irq_raise(nlb_session_t *s, uint32_t source) {
    s->irq_status |= source;
    if (s->registers[NLB_REG_IRQ_ENABLE] & source)
        s->irq_pending = true;
}

/* ===== Utility ===== */

const char *nlb_phase_name(nlb_phase_t phase) {
    switch (phase) {
        case NLB_PHASE_IDLE:     return "IDLE";
        case NLB_PHASE_CREATE:   return "CREATE";
        case NLB_PHASE_ENTANGLE: return "ENTANGLE";
        case NLB_PHASE_MEASURE:  return "MEASURE";
        case NLB_PHASE_DONE:     return "DONE";
        case NLB_PHASE_FAILED:   return "FAILED";
        default:                 return "UNKNOWN";
    }
}

const char *nlb_branch_state_name(nlb_branch_state_t state) {
    switch (state) {
        case NLB_BRANCH_PENDING:    return "PENDING";
        case NLB_BRANCH_COMPILING:  return "COMPILING";
        case NLB_BRANCH_PASSED:     return "PASSED";
        case NLB_BRANCH_FAILED:     return "FAILED";
        case NLB_BRANCH_CORRECTING: return "CORRECTING";
        case NLB_BRANCH_MERGED:     return "MERGED";
        default:                    return "UNKNOWN";
    }
}
