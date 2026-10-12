/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
/* policy.h — zxv_policy: a deterministic policy evaluation engine that
 * enforces machine-verifiable transactional constraints, bilateral treaty
 * rules, and jurisdictional parameters prior to ledger commit.
 *
 * Input: a contract's rule layer (zxp.h), the operator's jurisdiction packs,
 * and either a proposed transfer or a set of signed attestations. Output:
 * POLICY_ALLOW, POLICY_DENY or POLICY_FAULT, the four-valued lpres value
 * behind it, and which rules decided. Nothing here moves money: a host
 * calls policy_commit before its own ledger commit, and policy_lower hands
 * the treaty runner (../treaty/zxt_exec.h) a batch to net or settle.
 *
 * FOUR VALUES (lpres_state_t, ../lpres/lpres.h). The encoding is a pair of
 * evidence bits: bit 0 "some evidence says true", bit 1 "some evidence says
 * false". So NEITHER = 00, TRUE = 01, FALSE = 10, BOTH = 11, and
 *   knowledge join (merge evidence)  = a | b    (N v T = T, T v F = B)
 *   knowledge meet (what both share) = a & b
 *   truth meet / join / negation     = lpres_conjoin / lpres_disjoin /
 *                                      lpres_negate, the kernel's own tables.
 * The kernel's truth tables keep B ^ N = N and B v N = B (an unknown never
 * becomes a definite answer), where textbook Belnap gives F and T. That is
 * deliberate and shared by every module; this engine does not fork it.
 *
 * TRANSFERS (policy_check). Every contract rule gives TRUE or FALSE. Each
 * side's jurisdiction tag is judged by merging (knowledge join) what the
 * contract's ALLOW/DENY lists and every applicable pack say about it: a
 * contract that ALLOWs a tag a pack DENYs (or the contract both allows and
 * denies it) is BOTH. NEITHER (nobody has a view) is permitted. The
 * decision is the truth meet of everything:
 *   TRUE -> ALLOW, FALSE -> DENY, BOTH -> DENY with escalate set: held for
 *   a person, never allowed by the engine.
 *   A malformed contract or transfer, or a transfer whose DOC digest is not
 *   the contract's, is FAULT.
 *
 * TERMS (policy_terms). A DUE term's evidence is the knowledge join of the
 * attestations about it (TRUE: performed, FALSE: not performed); two
 * attestors disagreeing make it BOTH (contested) without stopping anything
 * else. Its compliance is the knowledge join of what the applicable packs
 * say about the term. Its AFTER condition, if any, is the value of the
 * named clause. A GATE is the truth meet (ALL) or join (ANY) of its clauses.
 *
 * SETTLEMENT (policy_lower). For one asset at one date:
 *   TRUE  (performed)            nothing to settle;
 *   due today, undisputed        an OWE line payer -> payee;
 *   FALSE (breached or lapsed)   an OWE line for the amount plus, if the
 *                                contract states one, a PENALTY line;
 *   NEITHER (open or waiting)    deferred until its date;
 *   BOTH (contested evidence, condition or compliance)
 *                                an OWE line payer -> ESCROW, and a
 *                                mediation entry naming the clause, its byte
 *                                offset in the canonical Sutra form and the
 *                                attestations on each side.
 *   A clause a pack denies (FALSE compliance) is not lowered and is listed.
 * The rest of the contract settles normally.
 *
 * Freestanding C11 (no libc, no allocation, no floats); links lpres.c for
 * the truth tables and zxt.c for the treaty it lowers into.
 */
#ifndef ZXV_POLICY_POLICY_H
#define ZXV_POLICY_POLICY_H

#include "zxp.h"
#include "../lpres/lpres.h"
#include "../treaty/zxt.h"

typedef enum { POLICY_ALLOW = 0, POLICY_DENY = 1, POLICY_FAULT = 2 } policy_decision_t;

/* Rule bits in policy_result_t masks. */
#define POLICY_R_WINDOW   (1u << 0)  /* date outside WINDOW               */
#define POLICY_R_LOCK     (1u << 1)  /* date inside a LOCK                */
#define POLICY_R_SIGN     (1u << 2)  /* fewer than SIGN parties signed    */
#define POLICY_R_PAYER    (1u << 3)  /* the paying party did not sign     */
#define POLICY_R_ASSET    (1u << 4)  /* CAPs exist and none names it      */
#define POLICY_R_CAP      (1u << 5)  /* amount above the asset's CAP      */
#define POLICY_R_LIMIT    (1u << 6)  /* cumulative LIMIT would be passed  */
#define POLICY_R_JUR_FROM (1u << 7)  /* payer's jurisdiction tag          */
#define POLICY_R_JUR_TO   (1u << 8)  /* payee's jurisdiction tag          */
#define POLICY_R_PACK     (1u << 9)  /* a pack's own transfer hook        */

/* FAULT codes (policy_result_t.fault); contract faults are zxp_status_t. */
#define POLICY_E_TXN   (-40) /* transfer malformed: party, asset, amount, date, tag */
#define POLICY_E_DOC   (-41) /* transfer bound to a different document */
#define POLICY_E_STATE (-42) /* state counter would leave PIC S9(18)   */

typedef struct {
    uint8_t from, to; /* party indices, 0-based */
    char asset[4];
    int64_t amount; /* > 0 */
    uint32_t date;  /* YYYYMMDD */
    char from_jur[ZXP_JUR_MAX + 1u], to_jur[ZXP_JUR_MAX + 1u];
    uint16_t signers; /* bit i: party i's signature over this transfer verified */
    uint8_t doc[32];  /* digest of the document the transfer is under */
} policy_txn_t;

typedef struct {
    policy_decision_t decision;
    bool escalate;        /* a BOTH somewhere: hold for a person */
    lpres_state_t value;  /* the truth meet behind the decision  */
    uint32_t deny_mask;   /* rules that came out FALSE           */
    uint32_t both_mask;   /* rules that came out BOTH            */
    int32_t fault;        /* 0, a zxp_status_t or POLICY_E_*     */
} policy_result_t;

/* Running totals for LIMIT lines, one per limit index. */
typedef struct {
    int64_t spent[ZXP_MAX_LIMITS];
} policy_state_t;

/* ---- jurisdiction packs: the plug-in ABI ----
 * A pack is a parameter set the OPERATOR configures for a jurisdiction
 * scope ("KE", "US", "US-NY"). It applies to a contract when its scope
 * covers one of the contract's LAW tags or a party's tag. The family label
 * is informational. A pack never states what the law is; it states what
 * this deployment has been configured to allow. */
#define POLICY_MAX_PACKS        16u
#define POLICY_PACK_NO_PENALTY  0x01u /* a DUE term with a PENALTY is FALSE   */
#define POLICY_PACK_DEFER       0x02u /* every term is NEITHER: defers to a
                                         process outside the engine          */
#define POLICY_FAMILY_MAX       15u

typedef struct policy_pack {
    char scope[ZXP_JUR_MAX + 1u];
    char family[POLICY_FAMILY_MAX + 1u]; /* "civil", "common", "islamic",
                                            "customary", "mixed", "intl", ... */
    uint8_t flags;
    uint8_t n_allow, n_deny; /* counterparty tags, prefix match */
    char allow[ZXP_MAX_TAGS][ZXP_JUR_MAX + 1u];
    char deny[ZXP_MAX_TAGS][ZXP_JUR_MAX + 1u];
    /* Optional hooks. Out-of-range returns count as NEITHER. txn_fn is
     * where a host binds another module, e.g. juris_check_transfer. */
    lpres_state_t (*term_fn)(void *ctx, const zxp_contract_t *c, uint32_t clause);
    lpres_state_t (*txn_fn)(void *ctx, const zxp_contract_t *c, const policy_txn_t *t);
    void *ctx;
} policy_pack_t;

typedef struct {
    const policy_pack_t *pack[POLICY_MAX_PACKS];
    uint8_t n;
} policy_host_t;

void policy_host_init(policy_host_t *h);
/* Refuses (false) a NULL or malformed pack (bad scope or tag, unsorted
 * lists) or a full host. The pack must outlive the host. */
bool policy_host_register(policy_host_t *h, const policy_pack_t *p);
/* A clean pack: scope, family label and flags, no lists or hooks. */
bool policy_pack_init(policy_pack_t *p, const char *scope, const char *family, uint8_t flags);
/* Does pack p apply to contract c? */
bool policy_pack_applies(const policy_pack_t *p, const zxp_contract_t *c);

/* Knowledge lattice on lpres's encoding. */
static inline lpres_state_t policy_k_join(lpres_state_t a, lpres_state_t b)
{
    return (lpres_state_t) (((unsigned) a | (unsigned) b) & 3u);
}
static inline lpres_state_t policy_k_meet(lpres_state_t a, lpres_state_t b)
{
    return (lpres_state_t) (((unsigned) a & (unsigned) b) & 3u);
}

/* ---- transfers ---- */
void policy_state_init(policy_state_t *s);
/* Evaluate t against c (and the packs in h, which may be NULL) without
 * changing s (which may be NULL: limits are then judged from zero). */
policy_result_t policy_check(const zxp_contract_t *c, const policy_host_t *h,
                             const policy_state_t *s, const policy_txn_t *t);
/* policy_check, and on ALLOW add t to the LIMIT totals in s. The host
 * commits its ledger only on ALLOW. */
policy_result_t policy_commit(const zxp_contract_t *c, const policy_host_t *h, policy_state_t *s,
                              const policy_txn_t *t);
/* TRUE when at least SIGN parties signed (bit i of `signers` = party i)
 * and `date` is inside WINDOW and outside every LOCK; FALSE otherwise. */
lpres_state_t policy_in_force(const zxp_contract_t *c, uint16_t signers, uint32_t date);

/* ---- terms ---- */
#define POLICY_MAX_ATTS 64u

typedef struct {
    uint8_t clause;        /* index of a DUE clause */
    lpres_state_t verdict; /* LPRES_STATE_TRUE: performed; FALSE: not */
    uint32_t date;         /* when the attested event happened */
    uint8_t attestor;      /* host-defined attestor number */
} policy_att_t;

typedef enum {
    POLICY_T_WAITING = 0,   /* AFTER condition not yet TRUE              */
    POLICY_T_OPEN = 1,      /* before its date                           */
    POLICY_T_DUE = 2,       /* its date is today, nothing attested       */
    POLICY_T_PERFORMED = 3, /* attested performed on or before its date  */
    POLICY_T_LATE = 4,      /* attested performed after its date         */
    POLICY_T_BREACHED = 5,  /* attested not performed, or date passed    */
    POLICY_T_CONTESTED = 6, /* conflicting evidence or condition         */
    POLICY_T_EXCUSED = 7,   /* AFTER condition came out FALSE            */
    POLICY_T_GATE = 8       /* a GATE: see value                         */
} policy_term_status_t;

typedef struct {
    uint8_t status;           /* policy_term_status_t */
    lpres_state_t value;      /* the term's own four-valued state */
    lpres_state_t evidence;   /* knowledge join of its attestations */
    lpres_state_t condition;  /* AFTER clause's value; TRUE when none */
    lpres_state_t compliance; /* knowledge join over applicable packs */
    uint32_t performed_on;    /* earliest TRUE attestation date, or 0 */
    uint64_t att_true, att_false; /* which attestations said what */
} policy_term_t;

/* Evaluate every clause at date `now`. Attestations dated after `now`, or
 * about a clause that is not a DUE term, are ignored. Returns 0, or a
 * negative zxp_status_t / POLICY_E_TXN (n_att > POLICY_MAX_ATTS or a bad
 * verdict) with out[] zeroed. */
int32_t policy_terms(const zxp_contract_t *c, const policy_host_t *h, const policy_att_t *att,
                     uint32_t n_att, uint32_t now, policy_term_t out[ZXP_MAX_CLAUSES]);

/* Canonical text an attestor signs: "ATT <contract> <clause> T|F <date>
 * <doc hex>\n". Returns its length (or 0 on bad input / short buffer). */
uint32_t policy_att_text(const zxp_contract_t *c, const policy_att_t *a, char *out, uint32_t cap);

/* ---- settlement ---- */
typedef enum {
    POLICY_ESC_EVIDENCE = 1,   /* attestations disagree        */
    POLICY_ESC_CONDITION = 2,  /* its AFTER condition is BOTH  */
    POLICY_ESC_COMPLIANCE = 3  /* packs disagree               */
} policy_escrow_reason_t;

typedef struct {
    uint8_t clause;
    uint8_t reason;     /* policy_escrow_reason_t */
    int32_t offset;     /* clause line's byte offset in the canonical Sutra */
    int64_t amount;
    uint64_t att_true, att_false;
} policy_escrow_t;

typedef struct {
    uint8_t n_escrow, n_blocked, n_deferred, n_lines;
    policy_escrow_t escrow[ZXP_MAX_CLAUSES];
    uint8_t blocked[ZXP_MAX_CLAUSES]; /* clauses a pack denied */
} policy_settle_t;

/* Build the NET treaty for `asset` at `now` (see the table above): every
 * contract party in order with equal weights, then ESCROW last if any
 * line goes to it. Returns ZXT_OK or a negative status; on failure *out
 * and *rep are zeroed. Feed *out to zxt_exec; set op to ZXT_OP_SETL and
 * the deposits first to settle rather than net. */
int32_t policy_lower(const zxp_contract_t *c, const policy_host_t *h, const policy_att_t *att,
                     uint32_t n_att, uint32_t now, const char asset[4], zxt_treaty_t *out,
                     policy_settle_t *rep);

/* The mediation bundle for escrow entry `e`, as canonical text a host
 * hashes and signs:
 *   MEDIATION <contract> <clause> AT <offset> <EVIDENCE|CONDITION|COMPLIANCE>
 *   DOC <hex>
 *   AMOUNT <asset> <amount>
 *   FOR <attestor> <date>        (each TRUE attestation, in input order)
 *   AGAINST <attestor> <date>    (each FALSE attestation)
 *   END
 * Returns its length, or 0 on bad input or a short buffer. */
uint32_t policy_bundle_text(const zxp_contract_t *c, const policy_escrow_t *e,
                            const policy_att_t *att, uint32_t n_att, char *out, uint32_t cap);

const char *policy_decision_name(policy_decision_t d);
const char *policy_term_status_name(uint8_t s);

#endif /* ZXV_POLICY_POLICY_H */
