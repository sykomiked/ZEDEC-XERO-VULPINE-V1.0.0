/* dual_space.h — ZXV Positive/Negative/Neutral Space Programming (Tri-Space)
 *
 * Every ZXV program is represented by constructive positive-space (S+),
 * restrictive or compensating negative-space (S-), and unresolved or
 * adjudicating neutral-space (S0) components, bound into one
 * cryptographically identifiable program triad.
 *
 * Canonical file triads (12 extensions, 4 roles):
 *
 *   source_or_payload:  .36n9 / .9n63 / .0n0   (S+ / S- / S0)
 *   manifest:           .36m9 / .9m63 / .0m0   (S+ / S- / S0)
 *   transformation:     .zedei / .iedez / .zedez (S+ / S- / S0)
 *   container:          .zedec / .cedez / .cedec (S+ / S- / S0)
 *
 * Neutral extensions are self-mirroring palindromes centered on zero
 * or the shared stem. Their names are provisional pending architecture
 * ratification.
 *
 * Inverse kinds (S- only):
 *   exact         — mathematically reversible
 *   compensating  — counteract external effect
 *   restoring     — restore from checkpoint
 *   constraining  — no inverse; invariants + denials + safe failure
 *   observational — verify/audit without changing state
 *
 * Triad states:
 *   UNBOUND    — only one or two sides registered
 *   BOUND      — all three sides registered, digests cross-bound
 *   VERIFIED   — passed linter checks
 *   ADMITTED   — passed runtime admission (Porter House)
 *   QUARANTINED — S0: unresolved, no production capabilities
 *   REVOKED    — explicitly revoked
 *
 * Event outcomes:
 *   admitted, vetoed, completed, compensated, unresolved, quarantined
 *
 * Neutral resolution:
 *   S0 can resolve to S+, S-, or remain S0.
 *   Resolution requires: policy id+version, resolver identity, causal
 *   lineage, input/output digests, reason code, and signature/attestation.
 *   Timeout: remain in S0, shed capabilities, notify.
 *
 * Hard requirements:
 *   - all triad members MUST bind to the other members' digests before release
 *   - S- MUST NOT receive more capabilities than S+
 *   - S0 MUST have no production effect capability until signed policy resolves it
 *   - irreversible effects MUST declare compensating/constraining/observational
 *   - generated S- material MUST be labeled generated
 *   - unresolved or contradictory triads MUST enter S0 quarantine
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */
#ifndef DUAL_SPACE_H
#define DUAL_SPACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../event_space/event_space.h"

/* ===== Constants ===== */

#define DS_MAX_PAIRS          64    /* max registered pairs */
#define DS_MAX_EVENTS         128   /* max event pairs tracked */
#define DS_MAX_CAPABILITIES   16    /* max capabilities per artifact */
#define DS_MAX_NAME_LEN       64    /* artifact/pair name length */
#define DS_MAX_LANG_LEN       16    /* language identifier length */
#define DS_DIGEST_SIZE        32    /* content/peer digest (256-bit) */
#define DS_MAX_QUARANTINE     16    /* max quarantined pairs */
#define DS_MAX_EXEMPTIONS     8     /* max signed exemptions per release */
#define DS_MAX_REASONS        32    /* max reason code length */

/* ===== Artifact Roles ===== */

typedef enum {
    DS_ROLE_SOURCE_POS = 0,    /* .36n9 — constructive source/payload */
    DS_ROLE_SOURCE_NEG = 1,    /* .9n63 — constraints/inverse/compensator */
    DS_ROLE_MANIFEST_POS = 2,  /* .36m9 — dependencies/exports/targets */
    DS_ROLE_MANIFEST_NEG = 3,  /* .9m63 — exclusions/denials/veto policy */
    DS_ROLE_TRANSFORM_POS = 4, /* .zedei — forward compiler/renderer/loader */
    DS_ROLE_TRANSFORM_NEG = 5, /* .iedez — verifier/deconstructor/audit */
    DS_ROLE_CONTAINER_POS = 6, /* .zedec — installable application container */
    DS_ROLE_CONTAINER_NEG = 7, /* .cedez — verification envelope/recovery */
    /* Neutral space roles (S0) — provisional palindromic extensions */
    DS_ROLE_SOURCE_NEU    = 8,  /* .0n0   — unresolved branches, negotiation, deferred state */
    DS_ROLE_MANIFEST_NEU  = 9,  /* .0m0   — optional deps, adjudication rules, evidence thresholds */
    DS_ROLE_TRANSFORM_NEU = 10, /* .zedez — mediator, dry-run, merge/negotiation, resolution recipe */
    DS_ROLE_CONTAINER_NEU = 11, /* .cedec — staged/unresolved, compatibility bridges, quarantine workspace */
} ds_artifact_role_t;

/* ===== Space Selector ===== */

typedef enum {
    DS_SPACE_POSITIVE = 0,
    DS_SPACE_NEGATIVE = 1,
    DS_SPACE_NEUTRAL  = 2,
} ds_space_t;

/* ===== Neutral Resolution ===== */

typedef enum {
    DS_RESOLUTION_S_PLUS    = 0,  /* resolve to positive — admit */
    DS_RESOLUTION_S_MINUS   = 1,  /* resolve to negative — reject/veto */
    DS_RESOLUTION_REMAIN_S0 = 2,  /* stay unresolved */
} ds_resolution_t;

/* ===== Inverse Kinds ===== */

typedef enum {
    DS_INVERSE_EXACT         = 0,  /* mathematically reversible */
    DS_INVERSE_COMPENSATING  = 1,  /* counteract external effect */
    DS_INVERSE_RESTORING     = 2,  /* restore from checkpoint */
    DS_INVERSE_CONSTRAINING  = 3,  /* no inverse; invariants + denials */
    DS_INVERSE_OBSERVATIONAL = 4,  /* verify/audit without state change */
    DS_INVERSE_NONE          = 5,  /* not yet classified */
} ds_inverse_kind_t;

/* ===== Pair States ===== */

typedef enum {
    DS_PAIR_UNBOUND     = 0,  /* only one side registered */
    DS_PAIR_BOUND       = 1,  /* both sides registered, digests cross-bound */
    DS_PAIR_VERIFIED    = 2,  /* passed linter checks */
    DS_PAIR_ADMITTED    = 3,  /* passed runtime admission */
    DS_PAIR_QUARANTINED = 4,  /* S0: unresolved, no production caps */
    DS_PAIR_REVOKED     = 5,  /* explicitly revoked */
} ds_pair_state_t;

/* ===== Event Outcomes ===== */

typedef enum {
    DS_OUTCOME_ADMITTED     = 0,
    DS_OUTCOME_VETOED       = 1,
    DS_OUTCOME_COMPLETED    = 2,
    DS_OUTCOME_COMPENSATED  = 3,
    DS_OUTCOME_UNRESOLVED   = 4,
    DS_OUTCOME_QUARANTINED  = 5,
} ds_event_outcome_t;

/* ===== Language Tiers ===== */

typedef enum {
    DS_LANG_NATIVE_DUAL    = 0,  /* Sutra or plugin: both sides in source */
    DS_LANG_CONTRACT_SIDECAR = 1, /* S- contract supplied as sidecar */
    DS_LANG_WASM_COMPONENT = 2,  /* WebAssembly component */
    DS_LANG_LEGACY_BINARY  = 3,  /* opaque binary, constrained assurance */
} ds_lang_tier_t;

/* ===== Capability ===== */

typedef struct ds_capability {
    char name[DS_MAX_NAME_LEN];
    bool granted;       /* true if S+ requests this capability */
    bool denied;        /* true if S- denies this capability */
} ds_capability_t;

/* ===== Artifact (one side of a pair) ===== */

typedef struct ds_artifact {
    ds_artifact_role_t role;
    char filename[DS_MAX_NAME_LEN];
    uint8_t content_digest[DS_DIGEST_SIZE];
    uint8_t peer_digest[DS_DIGEST_SIZE];  /* digest of the paired artifact */
    bool has_peer_digest;
    char language[DS_MAX_LANG_LEN];
    ds_lang_tier_t lang_tier;
    char source_event_schema[EV_SCHEMA_LEN];
    char emitted_event_schema[EV_SCHEMA_LEN];

    /* Capabilities */
    ds_capability_t capabilities[DS_MAX_CAPABILITIES];
    uint32_t num_capabilities;

    /* Inverse classification (S- only) */
    ds_inverse_kind_t inverse_kind;
    bool inverse_classified;
    bool generated;       /* true if S- was generated, not hand-written */

    bool registered;
} ds_artifact_t;

/* ===== Neutral Resolution Record ===== */

typedef struct ds_neutral_resolution {
    ds_resolution_t destination;
    char policy_id[DS_MAX_NAME_LEN];
    uint32_t policy_version;
    char resolver_id[DS_MAX_NAME_LEN];
    uint64_t causal_lineage;
    uint8_t input_digest[DS_DIGEST_SIZE];
    uint8_t output_digest[DS_DIGEST_SIZE];
    char reason_code[DS_MAX_REASONS];
    bool signed_attestation;
    bool resolved;
    uint64_t timeout_sequence;   /* 0 = no timeout set */
    bool timed_out;
} ds_neutral_resolution_t;

/* ===== Dual-Space Pair ===== */

typedef struct ds_pair {
    uint32_t id;
    char name[DS_MAX_NAME_LEN];
    ds_pair_state_t state;

    /* The 4 positive/negative/neutral artifact triads */
    ds_artifact_t positive[4];  /* .36n9, .36m9, .zedei, .zedec */
    ds_artifact_t negative[4];  /* .9n63, .9m63, .iedez, .cedez */
    ds_artifact_t neutral[4];   /* .0n0,  .0m0,  .zedez, .cedec */

    /* Which artifact roles are filled */
    bool pos_present[4];
    bool neg_present[4];
    bool neu_present[4];

    /* Pair-level metadata */
    char dual_pair_id[DS_MAX_NAME_LEN];
    char triad_id[DS_MAX_NAME_LEN];     /* v2.0: canonical triad identifier */
    uint32_t pair_schema_version;
    bool signed_release;
    bool has_exemption;         /* signed exemption for missing triad member */

    /* v2.0: Pair-level cross-space digest references */
    uint8_t positive_digest[DS_DIGEST_SIZE];   /* Merkle root over all S+ artifacts */
    uint8_t negative_digest[DS_DIGEST_SIZE];   /* Merkle root over all S- artifacts */
    uint8_t neutral_digest[DS_DIGEST_SIZE];    /* Merkle root over all S0 artifacts */
    bool has_pair_digests;                     /* true when pair-level digests computed */

    /* v2.0: Signature storage */
    uint8_t release_signature[DS_DIGEST_SIZE]; /* release signature over triad */
    bool has_release_signature;

    /* Neutral resolution state */
    ds_neutral_resolution_t neutral_resolution;
    bool has_neutral;           /* true if any neutral artifact is present */

    /* Statistics */
    uint64_t total_events_admitted;
    uint64_t total_events_vetoed;
    uint64_t total_events_compensated;
    uint64_t total_events_quarantined;

    bool registered;
} ds_pair_t;

/* ===== Dual-Space Event Pair ===== */

typedef struct ds_event_pair {
    uint32_t id;
    uint32_t pair_idx;            /* which pair this event belongs to */
    uint64_t event_id;
    uint64_t causal_parent;       /* parent event_id (0 = root) */
    uint32_t phase;               /* execution phase */
    uint64_t logical_sequence;    /* logical ordering */
    char triad_id[DS_MAX_NAME_LEN];  /* v2.0: triad this event belongs to */

    char positive_event_type[EV_SCHEMA_LEN];
    char negative_event_type[EV_SCHEMA_LEN];
    char neutral_event_type[EV_SCHEMA_LEN];   /* S0 event type (empty if no neutral) */

    uint8_t payload_digest[DS_DIGEST_SIZE];
    uint8_t policy_digest[DS_DIGEST_SIZE];

    ds_event_outcome_t outcome;
    bool s_guard_passed;          /* S- guard check before admission */
    bool s_compensation_invoked;  /* S- compensator was triggered */
    bool s0_deferred;             /* S0 deferred the event to neutral */
    bool s0_resolved;             /* S0 resolution was applied */
    ds_resolution_t s0_resolution; /* how S0 resolved (if s0_resolved) */

    uint64_t timestamp_sequence;  /* event-space sequence at time of event */

    bool active;
} ds_event_pair_t;

/* ===== Linter Result ===== */

typedef struct ds_lint_result {
    bool valid;
    bool peer_digests_match;       /* S+ ↔ S- digests match */
    bool triad_digests_match;      /* S+ ↔ S- ↔ S0 digests match */
    bool capability_asymmetry_ok;  /* S- caps <= S+ caps */
    bool s0_no_production_caps;    /* S0 has no production effect capabilities */
    bool inverse_classified;       /* all S- artifacts have inverse kind */
    bool all_triads_present;       /* all 4 pos/neg/neu triads present or exempted */
    bool all_pairs_present;        /* legacy: all 4 pos/neg pairs present or exempted */
    bool no_generated_misrepresented; /* generated S- not claimed as proven */
    uint32_t error_count;
    uint32_t warning_count;
    char errors[8][128];
} ds_lint_result_t;

/* ===== Dual-Space Registry ===== */

typedef struct ds_registry {
    ds_pair_t pairs[DS_MAX_PAIRS];
    uint32_t num_pairs;
    uint32_t next_pair_id;

    /* Event pair tracking */
    ds_event_pair_t events[DS_MAX_EVENTS];
    uint32_t num_events;
    uint32_t next_event_id;

    /* Quarantine */
    uint32_t quarantined_indices[DS_MAX_QUARANTINE];
    uint32_t num_quarantined;

    /* Global statistics */
    uint64_t total_pairs_registered;
    uint64_t total_pairs_verified;
    uint64_t total_pairs_admitted;
    uint64_t total_pairs_revoked;
    uint64_t total_events_processed;
    uint64_t total_vetoes;
    uint64_t total_compensations;
    uint64_t total_quarantine_events;
    uint64_t total_s0_resolutions;     /* S0 resolutions to S+ or S- */
    uint64_t total_s0_timeouts;        /* S0 resolution timeouts */
    uint64_t total_s0_deferred;        /* events deferred to S0 */
} ds_registry_t;

/* ===== API ===== */

/* Initialize the dual-space registry */
void ds_init(ds_registry_t *reg);

/* Pair management */
int32_t ds_register_pair(ds_registry_t *reg, const char *name,
                          const char *dual_pair_id);
ds_pair_t *ds_get_pair(ds_registry_t *reg, uint32_t idx);
ds_pair_t *ds_get_pair_by_id(ds_registry_t *reg, const char *dual_pair_id);

/* Artifact management */
bool ds_add_artifact(ds_registry_t *reg, uint32_t pair_idx,
                      bool positive, ds_artifact_role_t role,
                      const char *filename, const char *language,
                      ds_lang_tier_t lang_tier);
bool ds_set_artifact_digest(ds_registry_t *reg, uint32_t pair_idx,
                             bool positive, ds_artifact_role_t role,
                             const uint8_t *digest, uint32_t digest_len);
bool ds_set_peer_digest(ds_registry_t *reg, uint32_t pair_idx,
                         bool positive, ds_artifact_role_t role,
                         const uint8_t *peer_digest, uint32_t digest_len);
bool ds_set_artifact_schemas(ds_registry_t *reg, uint32_t pair_idx,
                              bool positive, ds_artifact_role_t role,
                              const char *source_schema,
                              const char *emitted_schema);
bool ds_set_inverse_kind(ds_registry_t *reg, uint32_t pair_idx,
                          ds_artifact_role_t role,
                          ds_inverse_kind_t kind, bool generated);

/* Capability management */
bool ds_add_capability(ds_registry_t *reg, uint32_t pair_idx,
                        bool positive, ds_artifact_role_t role,
                        const char *name, bool granted, bool denied);

/* Pair binding — cross-bind digests between S+ and S- */
bool ds_bind_pair(ds_registry_t *reg, uint32_t pair_idx,
                   ds_artifact_role_t role);

/* Linter — validate pair integrity */
ds_lint_result_t ds_lint_pair(ds_registry_t *reg, uint32_t pair_idx);

/* Verify pair — run linter and transition state */
bool ds_verify_pair(ds_registry_t *reg, uint32_t pair_idx);

/* Quarantine pair — move to S0 */
bool ds_quarantine_pair(ds_registry_t *reg, uint32_t pair_idx);

/* Revoke pair */
bool ds_revoke_pair(ds_registry_t *reg, uint32_t pair_idx);

/* Admission — check if pair is ready for production */
bool ds_admit_pair(ds_registry_t *reg, uint32_t pair_idx);

/* Event pair management */
int32_t ds_create_event(ds_registry_t *reg, uint32_t pair_idx,
                         uint64_t event_id, uint64_t causal_parent,
                         uint32_t phase, uint64_t logical_sequence,
                         const char *positive_type,
                         const char *negative_type);
ds_event_pair_t *ds_get_event(ds_registry_t *reg, uint32_t event_idx);

/* S- guard check — evaluate before admitting an event */
bool ds_evaluate_guard(ds_registry_t *reg, uint32_t event_idx);

/* Admit event — run guard, set outcome */
ds_event_outcome_t ds_admit_event(ds_registry_t *reg, uint32_t event_idx,
                                    uint64_t current_sequence);

/* Complete event — mark as completed or trigger compensation */
bool ds_complete_event(ds_registry_t *reg, uint32_t event_idx,
                        bool success, uint64_t current_sequence);

/* Compensate event — invoke S- compensator */
bool ds_compensate_event(ds_registry_t *reg, uint32_t event_idx,
                          uint64_t current_sequence);

/* Quarantine event — move to S0 unresolved */
bool ds_quarantine_event(ds_registry_t *reg, uint32_t event_idx,
                          uint64_t current_sequence);

/* Utility */
const char *ds_role_name(ds_artifact_role_t role);
const char *ds_role_extension(ds_artifact_role_t role);
const char *ds_inverse_kind_name(ds_inverse_kind_t kind);
const char *ds_pair_state_name(ds_pair_state_t state);
const char *ds_outcome_name(ds_event_outcome_t outcome);
const char *ds_lang_tier_name(ds_lang_tier_t tier);

/* Capability asymmetry check — S- MUST NOT have more caps than S+ */
bool ds_check_capability_asymmetry(ds_registry_t *reg, uint32_t pair_idx);

/* Check if all 4 pairs are present or have exemptions */
bool ds_check_pair_completeness(ds_registry_t *reg, uint32_t pair_idx);

/* Get pairs in a given state */
uint32_t ds_get_pairs_in_state(ds_registry_t *reg, ds_pair_state_t state,
                                uint32_t *out_indices, uint32_t max_out);

/* ===== Tri-Space (Neutral) API ===== */

/* Neutral artifact management */
bool ds_add_neutral_artifact(ds_registry_t *reg, uint32_t pair_idx,
                              ds_artifact_role_t role,
                              const char *filename, const char *language,
                              ds_lang_tier_t lang_tier);
bool ds_set_neutral_digest(ds_registry_t *reg, uint32_t pair_idx,
                            ds_artifact_role_t role,
                            const uint8_t *digest, uint32_t digest_len);
bool ds_set_neutral_peer_digest(ds_registry_t *reg, uint32_t pair_idx,
                                 ds_artifact_role_t role,
                                 const uint8_t *peer_digest, uint32_t digest_len);
bool ds_set_neutral_schemas(ds_registry_t *reg, uint32_t pair_idx,
                             ds_artifact_role_t role,
                             const char *source_schema,
                             const char *emitted_schema);
bool ds_add_neutral_capability(ds_registry_t *reg, uint32_t pair_idx,
                                ds_artifact_role_t role,
                                const char *name, bool granted, bool denied);

/* Triad binding — 3-way cross-bind digests between S+, S-, and S0 */
bool ds_bind_triad(ds_registry_t *reg, uint32_t pair_idx,
                    ds_artifact_role_t role);

/* Check if all 4 triads are present or have exemptions */
bool ds_check_triad_completeness(ds_registry_t *reg, uint32_t pair_idx);

/* Check that S0 has no production effect capabilities */
bool ds_check_s0_no_capabilities(ds_registry_t *reg, uint32_t pair_idx);

/* Neutral resolution — resolve S0 to S+, S-, or remain S0 */
bool ds_resolve_neutral(ds_registry_t *reg, uint32_t pair_idx,
                         ds_resolution_t destination,
                         const char *policy_id, uint32_t policy_version,
                         const char *resolver_id, const char *reason_code,
                         bool signed_attestation,
                         uint64_t current_sequence);

/* Set neutral resolution timeout */
bool ds_set_neutral_timeout(ds_registry_t *reg, uint32_t pair_idx,
                             uint64_t timeout_sequence);

/* Check for neutral resolution timeout — remains S0, sheds capabilities */
bool ds_check_neutral_timeout(ds_registry_t *reg, uint32_t pair_idx,
                               uint64_t current_sequence);

/* Create event with neutral type (triad event) */
int32_t ds_create_triad_event(ds_registry_t *reg, uint32_t pair_idx,
                               uint64_t event_id, uint64_t causal_parent,
                               uint32_t phase, uint64_t logical_sequence,
                               const char *positive_type,
                               const char *negative_type,
                               const char *neutral_type);

/* Defer event to S0 neutral space */
bool ds_defer_to_neutral(ds_registry_t *reg, uint32_t event_idx,
                          uint64_t current_sequence);

/* Resolve a deferred event from S0 */
ds_event_outcome_t ds_resolve_event(ds_registry_t *reg, uint32_t event_idx,
                                     ds_resolution_t destination,
                                     uint64_t current_sequence);

/* Utility for resolution names */
const char *ds_resolution_name(ds_resolution_t resolution);
const char *ds_space_name(ds_space_t space);

/* ===== v2.0 Triad Identity API ===== */

/* Set the canonical triad_id for a pair */
bool ds_set_triad_id(ds_registry_t *reg, uint32_t pair_idx,
                     const char *triad_id);

/* Compute pair-level Merkle digests over all S+/S-/S0 artifacts */
bool ds_compute_pair_digests(ds_registry_t *reg, uint32_t pair_idx);

/* Set release signature on a triad */
bool ds_set_release_signature(ds_registry_t *reg, uint32_t pair_idx,
                              const uint8_t *signature, uint32_t sig_len);

/* Set triad_id on an event pair */
bool ds_set_event_triad_id(ds_registry_t *reg, uint32_t event_idx,
                           const char *triad_id);

#endif /* DUAL_SPACE_H */
