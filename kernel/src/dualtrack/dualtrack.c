/*
 * dualtrack.c — Dual-Track Linear/Nonlinear Pipeline Implementation
 *
 * Runs linear and nonlinear execution tracks simultaneously with
 * phase gates for cross-coordination. Integrated OS primitive.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "dualtrack.h"

/* ===== Helpers ===== */

static void dt_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void dt_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static uint32_t dt_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void dt_strcpy(char *dst, const char *src) {
    uint32_t i = 0; while (src[i]) { dst[i] = src[i]; i++; } dst[i] = '\0';
}

static int dt_strcmp(const char *a, const char *b) {
    uint32_t i = 0; while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

/* ===== Init ===== */

void dualtrack_init(dualtrack_t *dt) {
    dt_memset(dt, 0, sizeof(dualtrack_t));
    dt->linear_active = true;
    dt->nonlinear_active = true;
    dt->initialized = true;
    dt->current_stage = 0;
    dt->phase_tick = 0;
    dt->triad_recorder_active = true;
    dt->triad_skeptic_active = true;
    dt->triad_operator_active = true;
}

/* ===== Stage Management ===== */

int32_t dualtrack_stage_add(dualtrack_t *dt, const char *name, bool has_gate) {
    if (dt->num_stages >= DT_MAX_STAGES || !name) return -1;
    int32_t idx = (int32_t)dt->num_stages;
    dt_stage_t *s = &dt->stages[idx];
    dt_memset(s, 0, sizeof(dt_stage_t));
    dt_strcpy(s->name, name);
    s->linear_state = DT_STAGE_PENDING;
    s->nonlinear_state = DT_STAGE_PENDING;
    s->gate_result = DT_GATE_NOT_REACHED;
    s->has_gate = has_gate;
    s->active = true;
    s->sq_phase = DT_SQ_CREATE;
    dt->num_stages++;
    return idx;
}

int dualtrack_stage_set_notes(dualtrack_t *dt, uint32_t stage_idx,
                               const char *notes) {
    if (stage_idx >= dt->num_stages || !notes) return -1;
    dt_strcpy(dt->stages[stage_idx].notes, notes);
    return 0;
}

int dualtrack_stage_set_sq_phase(dualtrack_t *dt, uint32_t stage_idx,
                                  dt_sq_phase_t phase) {
    if (stage_idx >= dt->num_stages) return -1;
    dt->stages[stage_idx].sq_phase = (uint32_t)phase;
    return 0;
}

/* ===== Phase Gate Evaluation ===== */

dt_gate_result_t dualtrack_eval_gate(dualtrack_t *dt, uint32_t stage_idx,
                                      bool linear_ok, bool nonlinear_ok,
                                      const char *linear_summary,
                                      const char *nonlinear_summary) {
    if (stage_idx >= dt->num_stages) return DT_GATE_NOT_REACHED;

    dt_gate_result_t result;

    if (linear_ok && nonlinear_ok) {
        /* Both succeeded — check if they agree */
        if (linear_summary && nonlinear_summary &&
            dt_strcmp(linear_summary, nonlinear_summary) == 0) {
            result = DT_GATE_AGREE;
            dt->gates_agreed++;
        } else {
            /* Both succeeded but different output → speculative divergence */
            result = DT_GATE_DIVERGE;
            dt->gates_diverged++;
            /* Mark nonlinear stage as GLUT+ (speculative continues) */
            dt->stages[stage_idx].nonlinear_state = DT_STAGE_GLUT_PLUS;
        }
    } else if (linear_ok && !nonlinear_ok) {
        /* Only linear succeeded */
        result = DT_GATE_LINEAR_ONLY;
        dt->stages[stage_idx].nonlinear_state = DT_STAGE_FAILED;
    } else if (!linear_ok && nonlinear_ok) {
        /* Only nonlinear succeeded — use speculative result */
        result = DT_GATE_NONLINEAR_ONLY;
        dt->stages[stage_idx].linear_state = DT_STAGE_FAILED;
    } else {
        /* Both failed → freeze */
        result = DT_GATE_BOTH_FAIL;
        dt->gates_both_failed++;
        dt->stages[stage_idx].linear_state = DT_STAGE_GLUT_ZERO;
        dt->stages[stage_idx].nonlinear_state = DT_STAGE_GLUT_ZERO;
    }

    /* Record gate */
    if (dt->num_gates < DT_MAX_PHASE_GATES) {
        dt_phase_gate_t *g = &dt->gates[dt->num_gates];
        g->stage_idx = stage_idx;
        g->result = result;
        g->resolved = true;
        g->agreement_score = (result == DT_GATE_AGREE) ? 100 :
                             (result == DT_GATE_DIVERGE) ? 50 :
                             (result == DT_GATE_BOTH_FAIL) ? 0 : 75;
        g->phase_tick = dt->phase_tick;
        g->recorder_checked = false;
        g->skeptic_validated = false;
        g->operator_merged = false;
        if (linear_summary) dt_strcpy(g->linear_summary, linear_summary);
        if (nonlinear_summary) dt_strcpy(g->nonlinear_summary, nonlinear_summary);
        dt->num_gates++;
    }

    dt->stages[stage_idx].gate_result = result;
    return result;
}

const dt_phase_gate_t *dualtrack_get_gate(dualtrack_t *dt, uint32_t gate_idx) {
    if (gate_idx >= dt->num_gates) return NULL;
    return &dt->gates[gate_idx];
}

/* ===== Stage Execution ===== */

int dualtrack_run_stage(dualtrack_t *dt, uint32_t stage_idx,
                         bool linear_ok, uint32_t linear_ms,
                         bool nonlinear_ok, uint32_t nonlinear_ms,
                         const char *linear_summary,
                         const char *nonlinear_summary) {
    if (stage_idx >= dt->num_stages) return -1;
    dt_stage_t *s = &dt->stages[stage_idx];

    /* Update linear track */
    if (dt->linear_active) {
        s->linear_state = linear_ok ? DT_STAGE_COMPLETE : DT_STAGE_FAILED;
        s->linear_duration_ms = linear_ms;
        dt->linear_stats.stages_run++;
        if (linear_ok) dt->linear_stats.stages_passed++;
        else dt->linear_stats.stages_failed++;
        dt->linear_stats.total_duration_ms += linear_ms;
    } else {
        s->linear_state = DT_STAGE_SKIPPED;
        dt->linear_stats.stages_skipped++;
    }

    /* Update nonlinear track */
    if (dt->nonlinear_active) {
        s->nonlinear_state = nonlinear_ok ? DT_STAGE_COMPLETE : DT_STAGE_FAILED;
        s->nonlinear_duration_ms = nonlinear_ms;
        dt->nonlinear_stats.stages_run++;
        if (nonlinear_ok) dt->nonlinear_stats.stages_passed++;
        else dt->nonlinear_stats.stages_failed++;
        dt->nonlinear_stats.total_duration_ms += nonlinear_ms;
    } else {
        s->nonlinear_state = DT_STAGE_SKIPPED;
        dt->nonlinear_stats.stages_skipped++;
    }

    /* Evaluate phase gate if present */
    if (s->has_gate) {
        dt_gate_result_t gate = dualtrack_eval_gate(dt, stage_idx,
                                                     linear_ok, nonlinear_ok,
                                                     linear_summary,
                                                     nonlinear_summary);

        /* Handle gate results */
        switch (gate) {
            case DT_GATE_DIVERGE:
                /* Both succeeded but differently — nonlinear continues speculatively */
                dt->nonlinear_stats.glut_plus_count++;
                break;
            case DT_GATE_LINEAR_ONLY:
                /* Nonlinear failed — disable nonlinear track for next stage */
                /* But don't disable entirely — it may recover */
                break;
            case DT_GATE_NONLINEAR_ONLY:
                /* Linear failed — use nonlinear result as fallback */
                /* stages_failed already counted in run_stage above */
                break;
            case DT_GATE_BOTH_FAIL:
                /* Both failed — freeze */
                dt->linear_stats.glut_zero_count++;
                dt->nonlinear_stats.glut_zero_count++;
                break;
            default:
                break;
        }
    }

    dt->current_stage = stage_idx + 1;
    dt->total_duration_ms += (linear_ms > nonlinear_ms) ? linear_ms : nonlinear_ms;
    return 0;
}

/* ===== Divergence Resolution ===== */

int dualtrack_resolve_divergence(dualtrack_t *dt, uint32_t stage_idx,
                                  bool prefer_nonlinear) {
    if (stage_idx >= dt->num_stages) return -1;
    dt_stage_t *s = &dt->stages[stage_idx];

    if (s->gate_result == DT_GATE_DIVERGE) {
        /* Resolve: pick one track's result */
        if (prefer_nonlinear) {
            s->linear_state = DT_STAGE_GLUT_MINUS;  /* Linear result dropped */
            s->nonlinear_state = DT_STAGE_COMPLETE;  /* Nonlinear result accepted */
        } else {
            s->linear_state = DT_STAGE_COMPLETE;
            s->nonlinear_state = DT_STAGE_GLUT_MINUS;
        }
        s->gate_result = DT_GATE_AGREE;  /* Resolved */
        return 0;
    }

    if (s->gate_result == DT_GATE_BOTH_FAIL) {
        /* Can't resolve — both failed */
        return -1;
    }

    return 0;
}

/* ===== Full Pipeline Run ===== */

int dualtrack_run_all(dualtrack_t *dt,
                       bool (*stage_fn)(dualtrack_t *dt, uint32_t stage_idx,
                                        dt_track_t track,
                                        char *summary, uint32_t max_len)) {
    if (!dt || !stage_fn) return -1;
    uint32_t i;
    for (i = 0; i < dt->num_stages; i++) {
        char lin_sum[DT_MAX_NOTES] = {0};
        char nonlin_sum[DT_MAX_NOTES] = {0};

        /* Run both tracks "simultaneously" (sequential simulation) */
        bool lin_ok = stage_fn(dt, i, DT_TRACK_LINEAR, lin_sum, DT_MAX_NOTES);
        bool nonlin_ok = stage_fn(dt, i, DT_TRACK_NONLINEAR, nonlin_sum, DT_MAX_NOTES);

        /* Use simulated durations */
        uint32_t lin_ms = dt->stages[i].linear_duration_ms;
        uint32_t nonlin_ms = dt->stages[i].nonlinear_duration_ms;

        dualtrack_run_stage(dt, i, lin_ok, lin_ms, nonlin_ok, nonlin_ms,
                            lin_sum, nonlin_sum);

        /* If both failed at a gate, halt */
        if (dt->stages[i].gate_result == DT_GATE_BOTH_FAIL) {
            dt->completed = false;
            return -1;
        }
    }
    dt->completed = true;
    return 0;
}

/* ===== Artifact Management ===== */

int32_t dualtrack_artifact_add(dualtrack_t *dt, const char *name,
                                const char *path, dt_artifact_type_t type,
                                dt_track_t produced_by, uint8_t key_index,
                                uint32_t size, const uint8_t *hash) {
    if (dt->num_artifacts >= DT_MAX_ARTIFACTS || !name) return -1;
    int32_t idx = (int32_t)dt->num_artifacts;
    dt_artifact_t *a = &dt->artifacts[idx];
    dt_memset(a, 0, sizeof(dt_artifact_t));
    dt_strcpy(a->name, name);
    if (path) dt_strcpy(a->path, path);
    a->type = type;
    a->produced_by = produced_by;
    a->key_index = key_index;
    a->size = size;
    a->validated = false;
    a->linked = false;
    if (hash) dt_memcpy(a->hash, hash, DT_HASH_SIZE);
    dt->num_artifacts++;
    if (produced_by == DT_TRACK_LINEAR)
        dt->linear_stats.artifacts_produced++;
    else
        dt->nonlinear_stats.artifacts_produced++;
    return idx;
}

int dualtrack_artifact_validate(dualtrack_t *dt, uint32_t artifact_idx) {
    if (artifact_idx >= dt->num_artifacts) return -1;
    dt->artifacts[artifact_idx].validated = true;
    if (dt->artifacts[artifact_idx].produced_by == DT_TRACK_LINEAR)
        dt->linear_stats.artifacts_validated++;
    else
        dt->nonlinear_stats.artifacts_validated++;
    return 0;
}

int dualtrack_artifact_link(dualtrack_t *dt, uint32_t artifact_idx) {
    if (artifact_idx >= dt->num_artifacts) return -1;
    if (!dt->artifacts[artifact_idx].validated) return -1;
    /* O(1) pointer swap in Merkle VFS — here we just mark as linked */
    dt->artifacts[artifact_idx].linked = true;
    if (dt->artifacts[artifact_idx].produced_by == DT_TRACK_LINEAR)
        dt->linear_stats.artifacts_linked++;
    else
        dt->nonlinear_stats.artifacts_linked++;
    dt->total_artifacts_linked++;
    return 0;
}

int dualtrack_stage_add_artifact(dualtrack_t *dt, uint32_t stage_idx,
                                  uint32_t artifact_idx) {
    if (stage_idx >= dt->num_stages || artifact_idx >= dt->num_artifacts) return -1;
    dt_stage_t *s = &dt->stages[stage_idx];
    if (s->num_artifacts >= 8) return -1;
    s->artifact_indices[s->num_artifacts] = artifact_idx;
    s->num_artifacts++;
    return 0;
}

const dt_artifact_t *dualtrack_artifact_get(dualtrack_t *dt, uint32_t idx) {
    if (idx >= dt->num_artifacts) return NULL;
    return &dt->artifacts[idx];
}

/* ===== Triad Protocol ===== */

int dualtrack_recorder_check(dualtrack_t *dt, uint32_t stage_idx) {
    /* Recorder (+0): Hash all artifacts from this stage, submit signatures */
    if (stage_idx >= dt->num_stages) return -1;
    dt_stage_t *s = &dt->stages[stage_idx];
    uint32_t i;
    for (i = 0; i < s->num_artifacts; i++) {
        uint32_t aidx = s->artifact_indices[i];
        if (aidx < dt->num_artifacts) {
            /* Hash is already set at artifact creation; mark as checked */
            dt->artifacts[aidx].validated = false;  /* Recorder checks, Skeptic validates */
        }
    }
    /* Mark gate as recorder-checked */
    if (dt->num_gates > 0) {
        dt_phase_gate_t *g = &dt->gates[dt->num_gates - 1];
        g->recorder_checked = true;
    }
    return 0;
}

int dualtrack_skeptic_validate(dualtrack_t *dt, uint32_t stage_idx) {
    /* Skeptic (-1): Validate artifacts against formal safety constraints */
    if (stage_idx >= dt->num_stages) return -1;
    dt_stage_t *s = &dt->stages[stage_idx];
    uint32_t i;
    for (i = 0; i < s->num_artifacts; i++) {
        uint32_t aidx = s->artifact_indices[i];
        if (aidx < dt->num_artifacts) {
            /* Validate: check hash is non-zero, size > 0, key index valid */
            dt_artifact_t *a = &dt->artifacts[aidx];
            bool valid = true;
            uint32_t j;
            bool has_hash = false;
            for (j = 0; j < DT_HASH_SIZE; j++) {
                if (a->hash[j] != 0) { has_hash = true; break; }
            }
            if (!has_hash) valid = false;
            if (a->size == 0) valid = false;
            if (a->key_index > DT_MAX_KEY_INDEX) valid = false;
            if (valid) {
                dualtrack_artifact_validate(dt, aidx);
            } else {
                /* G- (safe drop): invalid artifact discarded */
                a->validated = false;
                if (a->produced_by == DT_TRACK_NONLINEAR)
                    dt->nonlinear_stats.glut_minus_count++;
            }
        }
    }
    /* Mark gate as skeptic-validated */
    if (dt->num_gates > 0) {
        dt->gates[dt->num_gates - 1].skeptic_validated = true;
    }
    return 0;
}

int dualtrack_operator_merge(dualtrack_t *dt, uint32_t stage_idx) {
    /* Operator: Link validated artifacts into final output (O(1) swap) */
    if (stage_idx >= dt->num_stages) return -1;
    dt_stage_t *s = &dt->stages[stage_idx];
    uint32_t i;
    for (i = 0; i < s->num_artifacts; i++) {
        uint32_t aidx = s->artifact_indices[i];
        if (aidx < dt->num_artifacts && dt->artifacts[aidx].validated) {
            dualtrack_artifact_link(dt, aidx);
        }
    }
    /* Mark gate as operator-merged */
    if (dt->num_gates > 0) {
        dt->gates[dt->num_gates - 1].operator_merged = true;
    }
    return 0;
}

int dualtrack_triad_run(dualtrack_t *dt, uint32_t stage_idx) {
    /* Full Triad Protocol: Recorder → Skeptic → Operator */
    if (dualtrack_recorder_check(dt, stage_idx) != 0) return -1;
    if (dualtrack_skeptic_validate(dt, stage_idx) != 0) return -1;
    if (dualtrack_operator_merge(dt, stage_idx) != 0) return -1;
    return 0;
}

/* ===== Phase Clock ===== */

void dualtrack_tick(dualtrack_t *dt) {
    dt->phase_tick += DT_PHASE_CLOCK_MS;
}

uint32_t dualtrack_phase_clock(const dualtrack_t *dt) {
    return dt->phase_tick;
}

/* ===== Utility ===== */

const char *dualtrack_track_name(dt_track_t track) {
    switch (track) {
        case DT_TRACK_LINEAR:    return "LINEAR";
        case DT_TRACK_NONLINEAR: return "NONLINEAR";
        default: return "UNKNOWN";
    }
}

const char *dualtrack_stage_state_name(dt_stage_state_t state) {
    switch (state) {
        case DT_STAGE_PENDING:    return "PENDING";
        case DT_STAGE_RUNNING:    return "RUNNING";
        case DT_STAGE_COMPLETE:   return "COMPLETE";
        case DT_STAGE_FAILED:     return "FAILED";
        case DT_STAGE_SKIPPED:    return "SKIPPED";
        case DT_STAGE_GLUT_PLUS:  return "GLUT+";
        case DT_STAGE_GLUT_MINUS: return "GLUT-";
        case DT_STAGE_GLUT_ZERO:  return "GLUT0";
        default: return "UNKNOWN";
    }
}

const char *dualtrack_gate_result_name(dt_gate_result_t result) {
    switch (result) {
        case DT_GATE_AGREE:          return "AGREE";
        case DT_GATE_DIVERGE:        return "DIVERGE";
        case DT_GATE_LINEAR_ONLY:    return "LINEAR_ONLY";
        case DT_GATE_NONLINEAR_ONLY: return "NONLINEAR_ONLY";
        case DT_GATE_BOTH_FAIL:      return "BOTH_FAIL";
        case DT_GATE_NOT_REACHED:    return "NOT_REACHED";
        default: return "UNKNOWN";
    }
}

const char *dualtrack_sq_phase_name(dt_sq_phase_t phase) {
    switch (phase) {
        case DT_SQ_CREATE:   return "CREATE";
        case DT_SQ_ENTANGLE: return "ENTANGLE";
        case DT_SQ_MEASURE:  return "MEASURE";
        default: return "UNKNOWN";
    }
}

const char *dualtrack_triad_role_name(dt_triad_role_t role) {
    switch (role) {
        case DT_TRIAD_OPERATOR: return "Operator (build)";
        case DT_TRIAD_RECORDER: return "Recorder (+0, hash)";
        case DT_TRIAD_SKEPTIC:  return "Skeptic (-1, validate)";
        default: return "UNKNOWN";
    }
}

const char *dualtrack_artifact_type_name(dt_artifact_type_t type) {
    switch (type) {
        case DT_ARTIFACT_BINARY:   return "binary (.bin/.elf/.iso)";
        case DT_ARTIFACT_HDL:      return "HDL (SystemVerilog)";
        case DT_ARTIFACT_SMAP:     return "S-Map (.smap)";
        case DT_ARTIFACT_HDCM:     return "HDCM matrix";
        case DT_ARTIFACT_RTL_BIT:  return "FPGA bitstream";
        case DT_ARTIFACT_VM:       return "VM runtime";
        case DT_ARTIFACT_KERNEL:   return "kernel module";
        case DT_ARTIFACT_USERLAND: return "userland utility";
        default: return "UNKNOWN";
    }
}

/* ===== Reporting ===== */

int dualtrack_report(const dualtrack_t *dt, char *buf, uint32_t max_len) {
    if (!dt || !buf) return -1;
    uint32_t pos = 0;

    pos += (uint32_t)snprintf(buf + pos, max_len - pos,
        "Dual-Track Pipeline Report\n"
        "  Stages: %u | Gates: %u | Artifacts: %u\n"
        "  Linear:    %u passed, %u failed, %u skipped (%u ms) | %u artifacts\n"
        "  Nonlinear: %u passed, %u failed, %u skipped (%u ms) | %u artifacts\n"
        "  Gates: %u agreed, %u diverged, %u both_failed\n"
        "  Artifacts: %u validated, %u linked\n"
        "  Agreement rate: %.1f%%\n"
        "  Phase clock: %u ms\n"
        "  Total duration: %u ms\n"
        "  Completed: %s\n",
        dt->num_stages, dt->num_gates, dt->num_artifacts,
        dt->linear_stats.stages_passed, dt->linear_stats.stages_failed,
        dt->linear_stats.stages_skipped, dt->linear_stats.total_duration_ms,
        dt->linear_stats.artifacts_produced,
        dt->nonlinear_stats.stages_passed, dt->nonlinear_stats.stages_failed,
        dt->nonlinear_stats.stages_skipped, dt->nonlinear_stats.total_duration_ms,
        dt->nonlinear_stats.artifacts_produced,
        dt->gates_agreed, dt->gates_diverged, dt->gates_both_failed,
        dt->linear_stats.artifacts_validated + dt->nonlinear_stats.artifacts_validated,
        dt->total_artifacts_linked,
        dualtrack_agreement_rate(dt) * 100.0f,
        dt->phase_tick,
        dt->total_duration_ms,
        dt->completed ? "YES" : "NO");

    /* Per-stage details */
    uint32_t i;
    for (i = 0; i < dt->num_stages && pos < max_len - 100; i++) {
        const dt_stage_t *s = &dt->stages[i];
        pos += (uint32_t)snprintf(buf + pos, max_len - pos,
            "\n  [%u] %s\n"
            "    Linear:    %s (%u ms)\n"
            "    Nonlinear: %s (%u ms)\n"
            "    Gate: %s\n",
            i, s->name,
            dualtrack_stage_state_name(s->linear_state), s->linear_duration_ms,
            dualtrack_stage_state_name(s->nonlinear_state), s->nonlinear_duration_ms,
            s->has_gate ? dualtrack_gate_result_name(s->gate_result) : "none");
    }

    return (int)pos;
}

float dualtrack_agreement_rate(const dualtrack_t *dt) {
    if (!dt || dt->num_gates == 0) return 1.0f;
    return (float)dt->gates_agreed / (float)dt->num_gates;
}

bool dualtrack_all_gates_resolved(const dualtrack_t *dt) {
    if (!dt) return false;
    uint32_t i;
    for (i = 0; i < dt->num_gates; i++)
        if (!dt->gates[i].resolved) return false;
    return true;
}
