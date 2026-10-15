/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_comm.h — community-maintained updates: the OS is updated by its users,
 * not by central releases.
 *
 * evo.h already makes forks first-class (signed lineage records, a DAG, no
 * version numbers). This file adds the four things a user-maintained OS
 * needs on top, so that ANYONE may publish a change and every install still
 * stays compatible and safe:
 *
 *   U1  COMPATIBILITY CONTRACT. A set of pinned capability descriptors (the
 *       wire formats every install must speak) plus the safety-core
 *       conformance bits (evo_conform). A build passes iff, for every
 *       contract descriptor, the build advertises a descriptor of the same
 *       family that fully understands it (evo_desc_satisfies) and adds no
 *       new REQUIRED field, and its conformance covers the required bits.
 *       Forks may add anything optional; they may never drop the floor.
 *       The contract has no version number: its identity is its CID, and a
 *       new contract may replace an old one only if it EXTENDS it
 *       (evo_contract_extends), so old installs keep talking to new ones.
 *   U2  INDEPENDENT AUDITS. An auditor signs an attestation naming the
 *       lineage record, the contract CID it tested against, a PASS/FAIL
 *       verdict and the conformance bits it observed. An attestation may
 *       name a bounty claim, so the bounty system can pay the fixer in Vino
 *       on audited delivery (kernel bounty code reads it; nothing is paid
 *       here).
 *   U3  THE GATE. A community record is installable on THIS device only
 *       when: it is in the user's lineage DAG (so signed and conflict-free),
 *       its build passes the contract, k distinct auditors from the user's
 *       own auditor list passed it (none of them its author), no listed
 *       auditor failed it, the user has not blocked it or its author, and
 *       the user consented to that exact record before the consent expired.
 *   U4  ROLLBACK. Installs are journalled. A new install runs as a TRIAL
 *       until a health check confirms it; a failed check, or no
 *       confirmation within the trial window, rolls back to the last KEPT
 *       build automatically. The user can roll back at any time.
 *
 * HONEST LIMITS
 *  - Audits are only as independent as the auditor keys the user lists; two
 *    keys held by one person count as two. The default k = 2 is a floor.
 *  - The contract checks structure and the money-path behaviours evo_conform
 *    pins; it cannot prove a fork has no other bugs. That is what audits and
 *    the trial window are for.
 *  - Signatures are checked through the evo_verify_fn hook; no keys here.
 *  - Freestanding: integer only, no libc, no allocation.
 */
#ifndef ZXV_EVO_COMM_H
#define ZXV_EVO_COMM_H

#include "evo.h"

/* ===== U1: compatibility contract ===== */
#define EVO_CONTRACT_MAX 24u
#define EVO_CONTRACT_ENC_MAX (4u + 4u + 1u + EVO_CONTRACT_MAX * EVO_CID_LEN)

typedef struct {
    uint32_t conform_req; /* EVO_CHK_* bits every build must pass */
    uint8_t n;            /* kept sorted by CID */
    evo_cid_t desc[EVO_CONTRACT_MAX];
} evo_contract_t;

evo_status_t evo_contract_init(evo_contract_t *c, uint32_t conform_req);
/* Pin a descriptor by CID (EVO_ERR_DUP if already pinned). */
evo_status_t evo_contract_add(evo_contract_t *c, const evo_cid_t *desc_cid);
uint32_t evo_contract_encode(const evo_contract_t *c, uint8_t *out, uint32_t cap);
evo_status_t evo_contract_decode(const uint8_t *in, uint32_t len, evo_contract_t *c);
evo_status_t evo_contract_cid(const evo_contract_t *c, evo_cid_t *out);

/* Does a build with capability set `build` and conformance bitmap `conform`
 * keep the contract? Every contract descriptor and every build descriptor of
 * a contract family must be in `reg`. Errors: EVO_ERR_NOT_FOUND (a
 * descriptor is not in reg), EVO_ERR_UNSATISFIED (family missing or not
 * fully understood, or a new required field), EVO_ERR_NONCONFORMANT. On a
 * descriptor failure *bad is the contract index. */
evo_status_t evo_contract_check(const evo_contract_t *c, const evo_registry_t *reg,
                                const evo_capset_t *build, uint32_t conform, uint32_t *bad);

/* True iff every installation that keeps `nw` still talks to every one that
 * keeps `old`: nw requires at least old's conformance and every old
 * descriptor's family is pinned in nw by a descriptor that satisfies it. */
bool evo_contract_extends(const evo_contract_t *nw, const evo_contract_t *old,
                          const evo_registry_t *reg);

/* ===== U2: audit attestations ===== */
#define EVO_AUDIT_PASS 1u
#define EVO_AUDIT_FAIL 2u
#define EVO_AUDIT_BODY_MAX (4u + 2u * EVO_CID_LEN + 32u + 1u + 4u + 8u + 1u + EVO_NAME_MAX)

typedef struct {
    evo_cid_t record;   /* the lineage record audited */
    evo_cid_t contract; /* the contract CID it was tested against */
    uint8_t auditor[32];/* key id of the signer */
    uint8_t verdict;    /* EVO_AUDIT_PASS / EVO_AUDIT_FAIL */
    uint32_t conform;   /* conformance bits the auditor observed */
    uint64_t stamp;     /* auditor's choice */
    uint8_t bounty_len; /* 0: no bounty claim */
    char bounty[EVO_NAME_MAX];
} evo_audit_t;

uint32_t evo_audit_encode(const evo_audit_t *a, uint8_t *out, uint32_t cap);
evo_status_t evo_audit_decode(const uint8_t *in, uint32_t len, evo_audit_t *a);
/* Signed wire: the evo_fork_wire layout around the audit body. Verifies the
 * signature through vf, that auditor == evo_key_id(pk), and decodes. */
evo_status_t evo_audit_open(const uint8_t *wire, uint32_t len, evo_verify_fn vf, void *vctx,
                            evo_audit_t *out);

/* ===== U3: the install gate ===== */
#define EVO_AUDITORS_MAX 16u

typedef struct {
    uint32_t k;          /* passes needed; 0 is treated as 2 */
    uint32_t n;
    uint8_t auditor[EVO_AUDITORS_MAX][32]; /* the user's chosen auditors */
} evo_audit_policy_t;

evo_status_t evo_audit_policy_init(evo_audit_policy_t *p, uint32_t k);
evo_status_t evo_audit_policy_add(evo_audit_policy_t *p, const uint8_t key_id[32]);

typedef struct {
    evo_cid_t record;   /* consent is for this exact record */
    bool granted;
    uint64_t expires_at;
} evo_install_consent_t;

typedef enum {
    EVO_GATE_INSTALLABLE = 0,
    EVO_GATE_UNKNOWN_RECORD, /* not in the lineage DAG */
    EVO_GATE_BLOCKED,        /* the user rated it or its author below min_stars */
    EVO_GATE_INCOMPATIBLE,   /* breaks the contract */
    EVO_GATE_AUDIT_FAILED,   /* a listed auditor failed it */
    EVO_GATE_NEED_AUDITS,    /* fewer than k independent passes */
    EVO_GATE_NEED_CONSENT    /* the user has not said yes (or it expired) */
} evo_gate_t;

typedef struct {
    evo_gate_t verdict;
    uint32_t passes;    /* distinct listed auditors that passed it */
    uint32_t fails;     /* distinct listed auditors that failed it */
    uint32_t bad;       /* contract index on INCOMPATIBLE */
    bool bounty_ready;  /* passes >= k and a passing audit names a bounty */
    uint8_t bounty_len;
    char bounty[EVO_NAME_MAX];
} evo_gate_result_t;

/* `audits` must already be opened with evo_audit_open (signature checked).
 * `trust` may be NULL. Returns EVO_OK whenever the inputs were well formed;
 * the decision is res->verdict. Checks run in the order of evo_gate_t. */
evo_status_t evo_community_gate(const evo_dag_t *g, const evo_cid_t *record,
                                const evo_trust_t *trust, const evo_contract_t *contract,
                                const evo_registry_t *reg, const evo_capset_t *build,
                                uint32_t build_conform, const evo_audit_t *audits,
                                uint32_t n_audits, const evo_audit_policy_t *pol,
                                const evo_install_consent_t *consent, uint64_t now,
                                evo_gate_result_t *res);
const char *evo_gate_str(evo_gate_t g);

/* ===== U4: install journal with rollback ===== */
#define EVO_JOURNAL_MAX 16u
typedef enum { EVO_J_TRIAL = 1, EVO_J_KEPT = 2, EVO_J_ROLLED_BACK = 3 } evo_jstate_t;

typedef struct {
    evo_cid_t record;
    uint64_t at;
    uint8_t state; /* evo_jstate_t */
} evo_jent_t;

typedef struct {
    evo_cid_t base;   /* the build the device shipped or was restored to */
    uint32_t n;
    evo_jent_t e[EVO_JOURNAL_MAX];
    int32_t cur;      /* index of the running build, -1 = base */
    uint64_t trial_window;
} evo_journal_t;

void evo_journal_init(evo_journal_t *j, const evo_cid_t *base, uint64_t trial_window);
/* Start a TRIAL of record. Refused (EVO_ERR_CONFLICT) while another trial
 * runs. When full, the oldest entries that are not running are dropped. */
evo_status_t evo_journal_install(evo_journal_t *j, const evo_cid_t *record, uint64_t now);
/* The trial's health check: ok keeps it, !ok rolls back. */
evo_status_t evo_journal_confirm(evo_journal_t *j, bool health_ok);
/* Roll the running build back to the most recent earlier KEPT build (or the
 * base). EVO_ERR_NOT_FOUND when already on the base. */
evo_status_t evo_journal_rollback(evo_journal_t *j);
/* Dead-man switch: a trial not confirmed within the window rolls back.
 * Returns true if it rolled back. */
bool evo_journal_tick(evo_journal_t *j, uint64_t now);
const evo_cid_t *evo_journal_current(const evo_journal_t *j);
bool evo_journal_in_trial(const evo_journal_t *j);

#endif /* ZXV_EVO_COMM_H */
