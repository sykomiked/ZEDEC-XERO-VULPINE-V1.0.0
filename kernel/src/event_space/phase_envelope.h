/* phase_envelope.h — ZXV Thirteen-Phase Logic Pipeline Envelope ABI
 *
 * The common phase envelope shared by all 13 logic phases (K1-K6 kernel,
 * O1-O7 operating system). Each phase consumes an input envelope and
 * produces an output envelope with a decision, evidence, and routing
 * information for the next phase.
 *
 * This is a logical admission and transformation pipeline, not thirteen
 * CPU clock cycles. Independent work may execute concurrently, but an
 * externally visible effect must carry evidence for every mandatory
 * preceding phase.
 *
 * Phase route: K1 → K2 → K3 → K4 → K5 → K6 → O1 → O2 → O3 → O4 → O5 → O6 → O7
 *
 * Optional phases (O3, O5, O6, marketplace in O7) may be bypassed with a
 * signed bypass record. K1-K6, O1 integrity, O2 state policy, and O7
 * user/system result boundary are mandatory.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */
#ifndef PHASE_ENVELOPE_H
#define PHASE_ENVELOPE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== Phase Identifiers ===== */

typedef enum {
    PHASE_K1_OSEQ       = 0,   /* Ordinal Sequencer */
    PHASE_K2_RMAG       = 1,   /* Rational Magnitude Engine */
    PHASE_K3_LPRES      = 2,   /* Logical Presence Attestor */
    PHASE_K4_IPHASE     = 3,   /* Imaginary Phase Router */
    PHASE_K5_CHOICE     = 4,   /* Choice-Collapse Scheduler */
    PHASE_K6_PHASE_COORD = 5,  /* Phase Coordinator and Harmonizer */
    PHASE_O1_CRIT_168   = 6,   /* Canonical Representation, Integrity, Translation */
    PHASE_O2_FS         = 7,   /* Tri-Space Filesystem and State Services */
    PHASE_O3_AGP        = 8,   /* Audio Genomics and Multimodal Signal Processing */
    PHASE_O4_MESH       = 9,   /* Network, Device, and Sovereign Mesh Coordination */
    PHASE_O5_CHIGLET_AI = 10,  /* Chiglet AI Interpretation and Assistance */
    PHASE_O6_VINO       = 11,  /* Transaction, Value, and Settlement Services */
    PHASE_O7_KIKAN      = 12,  /* Marketplace, Exchange, and User-Facing Fulfillment */
    PHASE_NONE          = 0xFF
} phase_id_t;

#define PHASE_COUNT 13
#define PHASE_ID_MAX_LEN 16

/* ===== Phase Decisions ===== */

typedef enum {
    PHASE_DECISION_ADVANCE       = 0,  /* proceed to next phase */
    PHASE_DECISION_REJECT_S_MINUS = 1,  /* reject to S- (veto/deny) */
    PHASE_DECISION_DEFER_S_ZERO  = 2,  /* defer to S0 (unresolved) */
    PHASE_DECISION_COMPENSATE    = 3,  /* trigger compensation/rollback */
    PHASE_DECISION_QUARANTINE    = 4,  /* quarantine the event */
} phase_decision_t;

/* ===== Phase Envelope Constants ===== */

#define PHASE_DIGEST_SIZE     32    /* 256-bit evidence/digest fields */
#define PHASE_REASON_LEN      64    /* max reason code length */
#define PHASE_RESOLVER_LEN    64    /* max resolver identity length */
#define PHASE_SUBJECT_LEN     64    /* max subject identity length */
#define PHASE_TRIAD_ID_LEN    64    /* max triad identifier length */
#define PHASE_CAPABILITY_LEN  64    /* max capability string */
#define PHASE_MAX_CAPABILITIES 16   /* max requested capabilities */

/* ===== Phase Envelope Input ===== */

typedef struct phase_input {
    /* Event identity and ordering */
    ev_event_id_t event_id;
    ev_event_id_t causal_parent;
    uint64_t ordinal;

    /* Triad binding */
    char triad_id[PHASE_TRIAD_ID_LEN];
    uint8_t s_plus_digest[PHASE_DIGEST_SIZE];
    uint8_t s_minus_digest[PHASE_DIGEST_SIZE];
    uint8_t s_zero_digest[PHASE_DIGEST_SIZE];
    bool has_s_zero;                /* false for dual-space-only pairs */

    /* Subject and capabilities */
    char subject_identity[PHASE_SUBJECT_LEN];
    char requested_capabilities[PHASE_MAX_CAPABILITIES][PHASE_CAPABILITY_LEN];
    uint32_t num_requested_capabilities;

    /* Schema */
    char schema[EV_SCHEMA_LEN];
    uint16_t schema_version;

    /* Which phase produced this input */
    phase_id_t source_phase;
} phase_input_t;

/* ===== Phase Envelope Output ===== */

typedef struct phase_output {
    /* Phase identification */
    phase_id_t phase_id;
    uint16_t phase_version;

    /* Decision */
    phase_decision_t decision;

    /* Evidence */
    uint8_t evidence_digest[PHASE_DIGEST_SIZE];
    char reason_code[PHASE_REASON_LEN];

    /* Resource charge (exact units consumed) */
    uint64_t resource_charge;

    /* Routing */
    phase_id_t next_phase;

    /* Resolver identity (who/what made the decision) */
    char resolver_identity[PHASE_RESOLVER_LEN];

    /* Whether this output is signed/attested */
    bool signed_attestation;
} phase_output_t;

/* ===== Phase Envelope (Input + Output) ===== */

typedef struct phase_envelope {
    phase_input_t input;
    phase_output_t output;
    bool valid;                     /* false if phase could not process */
} phase_envelope_t;

/* ===== Phase Route Helpers ===== */

static inline const char *phase_name(phase_id_t id) {
    switch (id) {
        case PHASE_K1_OSEQ:        return "K1_OSEQ";
        case PHASE_K2_RMAG:        return "K2_RMAG";
        case PHASE_K3_LPRES:       return "K3_LPRES";
        case PHASE_K4_IPHASE:      return "K4_IPHASE";
        case PHASE_K5_CHOICE:      return "K5_CHOICE";
        case PHASE_K6_PHASE_COORD: return "K6_PHASE_COORD";
        case PHASE_O1_CRIT_168:    return "O1_CRIT_168";
        case PHASE_O2_FS:          return "O2_FS";
        case PHASE_O3_AGP:         return "O3_AGP";
        case PHASE_O4_MESH:        return "O4_MESH";
        case PHASE_O5_CHIGLET_AI:  return "O5_CHIGLET_AI";
        case PHASE_O6_VINO:        return "O6_VINO";
        case PHASE_O7_KIKAN:       return "O7_KIKAN";
        default:                   return "UNKNOWN";
    }
}

static inline const char *phase_decision_name(phase_decision_t d) {
    switch (d) {
        case PHASE_DECISION_ADVANCE:        return "advance";
        case PHASE_DECISION_REJECT_S_MINUS: return "reject_to_S_minus";
        case PHASE_DECISION_DEFER_S_ZERO:   return "defer_to_S_zero";
        case PHASE_DECISION_COMPENSATE:     return "compensate";
        case PHASE_DECISION_QUARANTINE:     return "quarantine";
        default:                             return "unknown";
    }
}

static inline phase_id_t next_mandatory_phase(phase_id_t current) {
    switch (current) {
        case PHASE_K1_OSEQ:        return PHASE_K2_RMAG;
        case PHASE_K2_RMAG:        return PHASE_K3_LPRES;
        case PHASE_K3_LPRES:       return PHASE_K4_IPHASE;
        case PHASE_K4_IPHASE:      return PHASE_K5_CHOICE;
        case PHASE_K5_CHOICE:      return PHASE_K6_PHASE_COORD;
        case PHASE_K6_PHASE_COORD: return PHASE_O1_CRIT_168;
        case PHASE_O1_CRIT_168:    return PHASE_O2_FS;
        case PHASE_O2_FS:          return PHASE_O3_AGP;
        case PHASE_O3_AGP:         return PHASE_O4_MESH;
        case PHASE_O4_MESH:        return PHASE_O5_CHIGLET_AI;
        case PHASE_O5_CHIGLET_AI:  return PHASE_O6_VINO;
        case PHASE_O6_VINO:        return PHASE_O7_KIKAN;
        case PHASE_O7_KIKAN:       return PHASE_NONE;
        default:                   return PHASE_NONE;
    }
}

static inline bool phase_is_mandatory(phase_id_t id) {
    switch (id) {
        case PHASE_K1_OSEQ:
        case PHASE_K2_RMAG:
        case PHASE_K3_LPRES:
        case PHASE_K4_IPHASE:
        case PHASE_K5_CHOICE:
        case PHASE_K6_PHASE_COORD:
        case PHASE_O1_CRIT_168:
        case PHASE_O2_FS:
        case PHASE_O4_MESH:
        case PHASE_O7_KIKAN:
            return true;
        case PHASE_O3_AGP:
        case PHASE_O5_CHIGLET_AI:
        case PHASE_O6_VINO:
            return false;
        default:
            return false;
    }
}

static inline bool phase_is_kernel(phase_id_t id) {
    return id <= PHASE_K6_PHASE_COORD;
}

static inline bool phase_is_os(phase_id_t id) {
    return id >= PHASE_O1_CRIT_168 && id <= PHASE_O7_KIKAN;
}

static inline void phase_input_init(phase_input_t *in) {
    if (!in) return;
    ev_memset(in, 0, sizeof(*in));
    in->source_phase = PHASE_NONE;
}

static inline void phase_output_init(phase_output_t *out, phase_id_t pid) {
    if (!out) return;
    ev_memset(out, 0, sizeof(*out));
    out->phase_id = pid;
    out->phase_version = 1;
    out->decision = PHASE_DECISION_ADVANCE;
    out->next_phase = next_mandatory_phase(pid);
}

static inline void phase_envelope_init(phase_envelope_t *env, phase_id_t pid) {
    if (!env) return;
    phase_input_init(&env->input);
    phase_output_init(&env->output, pid);
    env->valid = false;
}

/* ===== Bypass Record ===== */

typedef struct phase_bypass_record {
    phase_id_t bypassed_phase;
    char reason[PHASE_REASON_LEN];
    uint8_t evidence_digest[PHASE_DIGEST_SIZE];
    bool signed_attestation;
    char resolver_identity[PHASE_RESOLVER_LEN];
} phase_bypass_record_t;

static inline bool bypass_record_init(phase_bypass_record_t *rec,
                                       phase_id_t bypassed,
                                       const char *reason,
                                       const char *resolver) {
    if (!rec || !reason) return false;
    ev_memset(rec, 0, sizeof(*rec));
    rec->bypassed_phase = bypassed;
    /* Safe string copy */
    uint32_t i;
    if (reason) {
        for (i = 0; i < PHASE_REASON_LEN - 1 && reason[i]; i++)
            rec->reason[i] = reason[i];
        rec->reason[i] = '\0';
    }
    if (resolver) {
        for (i = 0; i < PHASE_RESOLVER_LEN - 1 && resolver[i]; i++)
            rec->resolver_identity[i] = resolver[i];
        rec->resolver_identity[i] = '\0';
    }
    rec->signed_attestation = false;
    return true;
}

#endif /* PHASE_ENVELOPE_H */
