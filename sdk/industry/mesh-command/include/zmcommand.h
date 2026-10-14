/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zmcommand.h — mesh-command: an incident picture merged from many units'
 * field reports, with contradictions held for the incident commander.
 *
 *   reports    a unit signs (ML-DSA-87) a numbered report that a fact is
 *              TRUE or FALSE ("road:R12/open" FALSE), with its clock, an
 *              optional position and a short note; the report is bound to
 *              one incident, so it cannot be replayed into another
 *   picture    every fact's state comes from zxv_lp_from_evidence over
 *              the latest claim of each unit in the current epoch (plus
 *              the standing decision, if any): TRUE, FALSE, NEITHER (no
 *              evidence) or BOTH (units contradict each other)
 *   holds      a fact that becomes BOTH is held and logged; nothing in the
 *              kit picks a side. The commander resolves it with a signed
 *              decision that names the evidence they saw and says why; the
 *              decision is written to the quarantine hexlog with who
 *              (commander) and why (the digest of the signed message,
 *              whose full text is in the journal), and opens a new epoch
 *   advice     tasks and resources: zmc_recommend proposes assignments
 *              (never relying on a held or unknown fact); a commander
 *              approves each one with a signature. The kit dispatches
 *              nothing
 *   journal    every accepted report, decision, task, resource and
 *              approval is appended to a libzind chain signed by the
 *              operator; zmc_verify_journal replays it offline from
 *              nothing but public keys
 *
 * Advisory only (README "Limits"). Integer code only (R11). Kit API 0.1
 * (preview). */
#ifndef ZMCOMMAND_H
#define ZMCOMMAND_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZMC_VERSION_MAJOR 0
#define ZMC_VERSION_MINOR 1

/* Status codes (-3700..-3799). */
#define ZMC_E_UNIT         (-3701) /* unknown unit, or not that unit's key          */
#define ZMC_E_COMMANDER    (-3702) /* unknown commander, or not that commander's key */
#define ZMC_E_FACT         (-3703) /* unknown fact                                  */
#define ZMC_E_TASK         (-3704) /* unknown task or resource                      */
#define ZMC_E_EQUIVOCATION (-3705) /* one unit signed two different reports with one
                                      number: refused and logged                   */
#define ZMC_E_BLOCKED      (-3706) /* the assignment's conditions no longer hold    */
#define ZMC_E_INCIDENT     (-3707) /* a report or record for another incident        */

const char *zmc_status_name(int32_t status);

#define ZMC_ID_MAX     24u  /* incident, unit, commander, task, resource, kind IDs */
#define ZMC_FACT_MAX   31u  /* fact names: [A-Za-z0-9._:/-]                         */
#define ZMC_NOTE_MAX   120u /* printable ASCII                                     */
#define ZMC_WHY_MAX    160u /* a decision's reason: 1..160 printable ASCII          */
#define ZMC_NO_POS     INT32_MIN
#define ZMC_MAX_UNITS      256u
#define ZMC_MAX_COMMANDERS 16u
#define ZMC_MAX_FACTS      1024u
#define ZMC_MAX_REPORTS    65536u
#define ZMC_MAX_TASKS      256u
#define ZMC_MAX_RESOURCES  256u

#define ZMC_REPORT_CTX   "mesh-command/report"
#define ZMC_DECISION_CTX "mesh-command/decision"
#define ZMC_APPROVE_CTX  "mesh-command/approve"
#define ZMC_JOURNAL      "mesh-command/journal"

/* quarantine reason for a resolution (libzind kit range) */
#define ZMC_REASON_RESOLVED (ZIND_REASON_KIT_BASE + 1)

/* ===== Reports (signed by the unit) ===== */
typedef struct {
    char fact[ZMC_FACT_MAX + 1];
    int32_t claim;          /* ZXV_LP_TRUE or ZXV_LP_FALSE                    */
    uint64_t observed;      /* the unit's clock, unix seconds                 */
    int32_t lat_e6, lon_e6; /* microdegrees, or both ZMC_NO_POS               */
    char note[ZMC_NOTE_MAX + 1];
} zmc_report;

int32_t zmc_report_check(const zmc_report *r);

/* Sign report number `report_no` of `unit` for `incident` (a unit numbers
 * its reports itself, upward). Layout: "ZMCR" u8 1 str incident str unit
 * u64 report_no str fact u8 claim u64 observed u32 lat u32 lon str note,
 * then the signature of zind_subject("mesh-command/report", body) under
 * ZMC_REPORT_CTX. buf may be NULL to ask the size (ZXV_E_BUFFER). */
int32_t zmc_report_sign(const zxv_cap *cap, const zind_signer *unit_key, const char *incident,
                        const char *unit, uint64_t report_no, const zmc_report *r, uint8_t *buf,
                        size_t cap_len, size_t *out_len);

/* ===== The incident picture ===== */
typedef struct zmc_incident zmc_incident;

/* The cap needs CRYPTO_SIGN (and ATTEST with a quarantine log, which may be
 * NULL; resolutions need ATTEST). The operator key signs the journal. */
int32_t zmc_incident_create(const zxv_cap *cap, const char *incident,
                            const zind_signer *operator_key, zxv_hexlog *qlog,
                            zmc_incident **out);
void zmc_incident_destroy(zmc_incident *inc);
int32_t zmc_unit_add(zmc_incident *inc, const char *unit, const uint8_t pk[ZIND_PK_BYTES]);
int32_t zmc_commander_add(zmc_incident *inc, const char *commander,
                          const uint8_t pk[ZIND_PK_BYTES]);

typedef struct {
    char fact[ZMC_FACT_MAX + 1];
    int32_t lp_before, lp_after;
    int stale;    /* older than this unit's latest report on the fact: kept, not counted */
    int new_hold; /* the fact became BOTH                                              */
} zmc_ingest_info;

/* Ingest one signed report. ZXV_OK (recorded; see info), ZIND_E_REPLAY
 * (already held), ZMC_E_EQUIVOCATION, ZIND_E_CHAIN (bad signature),
 * ZMC_E_UNIT, ZMC_E_INCIDENT, ZXV_E_FORMAT, ZXV_E_CAPACITY. info may be
 * NULL. */
int32_t zmc_ingest(zmc_incident *inc, const uint8_t *buf, size_t len, zmc_ingest_info *info);

typedef struct {
    int32_t lp;       /* TRUE, FALSE, NEITHER or BOTH (held)                  */
    uint8_t tag;      /* base = lp; polarity: how the evidence last moved;
                         phase Potential (one unit), Emerging (several),
                         Manifest (a decision stands)                         */
    uint32_t pos, neg; /* units whose latest claim in this epoch is TRUE / FALSE */
    uint32_t epoch;   /* decisions taken on this fact                         */
    int32_t decision; /* the standing decision (TRUE/FALSE), or NEITHER       */
    uint32_t reports; /* every report recorded on the fact                    */
} zmc_fact_view;

int32_t zmc_fact_state(const zmc_incident *inc, const char *fact, zmc_fact_view *out);
int32_t zmc_fact_count(const zmc_incident *inc, uint32_t *out_count);
int32_t zmc_fact_name(const zmc_incident *inc, uint32_t i, char *buf, size_t cap);

/* The bytes a commander signs (ZMC_DECISION_CTX) to resolve a held fact:
 * "zmc/decision/v1" incident fact u32 epoch evidence[32] u8 decision
 * commander why, where evidence is SHA3-256 over the claims counted now.
 * ZXV_E_STATE unless the fact is BOTH. */
int32_t zmc_decision_message(const zmc_incident *inc, const char *fact, int32_t decision,
                             const char *commander, const char *why, uint8_t *buf, size_t cap,
                             size_t *out_len);
/* Apply it (the cap needs ATTEST). Logs the resolution to the quarantine
 * hexlog (who: the commander's index; subject: SHA3-256 of the message,
 * which carries the reason) and the journal (the full message and the
 * commander's signature). A forged or stale decision is ZXV_E_VERIFY. */
int32_t zmc_resolve(zmc_incident *inc, const char *fact, int32_t decision, const char *commander,
                    const char *why, const uint8_t sig[ZIND_SIG_BYTES]);

/* ===== Advice: tasks, resources, recommendations ===== */
#define ZMC_REC_ASSIGN      1 /* recommend this resource                         */
#define ZMC_REC_HELD        2 /* needs a fact that is held as BOTH: resolve first */
#define ZMC_REC_UNKNOWN     3 /* needs a fact nobody has reported (NEITHER)       */
#define ZMC_REC_BLOCKED     4 /* the fact says the opposite of what it needs      */
#define ZMC_REC_NO_RESOURCE 5 /* no free resource of the kind                     */

/* A resource of a kind ("engine", "ambulance"). */
int32_t zmc_resource_add(zmc_incident *inc, const char *resource, const char *kind);
/* A task with priority 1..9 (9 first) that needs one resource of
 * need_kind and, optionally, a fact to be `required` (TRUE or FALSE). */
int32_t zmc_task_add(zmc_incident *inc, const char *task, uint8_t priority, const char *need_kind,
                     const char *requires_fact, int32_t required);

typedef struct {
    char task[ZMC_ID_MAX + 1];
    char resource[ZMC_ID_MAX + 1]; /* "" unless ASSIGN */
    int32_t status;                /* ZMC_REC_*        */
} zmc_rec;

/* Open tasks by priority (high first), then task ID; each recommended
 * resource is the first free one of its kind by ID. Deterministic. */
int32_t zmc_recommend(const zmc_incident *inc, zmc_rec *out, size_t cap, size_t *out_n);

/* "zmc/approve/v1" incident task resource u32 fact_epoch commander. */
int32_t zmc_approve_message(const zmc_incident *inc, const char *task, const char *resource,
                            const char *commander, uint8_t *buf, size_t cap, size_t *out_len);
/* A commander approves an assignment (signed, ZMC_APPROVE_CTX). The
 * conditions are checked again now: ZMC_E_BLOCKED if the task is taken,
 * the resource is busy or of another kind, or the fact is not as required. */
int32_t zmc_approve(zmc_incident *inc, const char *task, const char *resource,
                    const char *commander, const uint8_t sig[ZIND_SIG_BYTES]);
/* 1 assigned (resource filled in), 0 open. */
int32_t zmc_task_state(const zmc_incident *inc, const char *task, int32_t *out_assigned,
                       char *resource_buf, size_t cap);

/* ===== Journal and offline verification ===== */
#define ZMC_J_INCIDENT 1
#define ZMC_J_REPORT   2
#define ZMC_J_DECISION 3
#define ZMC_J_RESOURCE 4
#define ZMC_J_TASK     5
#define ZMC_J_APPROVAL 6

const zind_chain *zmc_journal(const zmc_incident *inc);

typedef struct {
    const char *const *unit_ids;
    const uint8_t *const *unit_pks;
    size_t n_units;
    const char *const *commander_ids;
    const uint8_t *const *commander_pks;
    size_t n_commanders;
    const uint8_t *operator_pk;
} zmc_trust;

typedef struct {
    char incident[ZMC_ID_MAX + 1];
    uint32_t records, reports, facts, held, decisions, approvals;
    uint32_t bad_index; /* the first bad record on failure, else UINT32_MAX */
} zmc_verdict;

/* Replay an exported journal from scratch with nothing but the trust set:
 * every link and operator signature, every unit's report signature, every
 * decision (signature, and that it names the evidence and epoch the
 * replay reaches) and every approval (signature and conditions).
 * ZXV_OK with a verdict, or ZIND_E_CHAIN, ZIND_E_UNTRUSTED, ZXV_E_FORMAT.
 * Needs CRYPTO_SIGN. */
int32_t zmc_verify_journal(const zxv_cap *cap, const uint8_t *buf, size_t len,
                           const zmc_trust *trust, zmc_verdict *out);

#ifdef __cplusplus
}
#endif

#endif /* ZMCOMMAND_H */
