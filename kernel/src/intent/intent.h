/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* intent.h — the AI intent layer: a model proposes, a person approves.
 *
 * A local model can suggest a payment, an invoice, a route or a report.
 * This layer turns its text into a typed proposal, runs the guard, and
 * releases a draft only after the owner approves exactly that draft. It
 * holds no ledger handle: it never writes a balance. The caller posts a
 * released draft through its normal, checked ledger path.
 *
 *   I1 GRAMMAR. Model output is untrusted. intent_parse accepts one line:
 *        pay <amount> <CCY> to <account> [memo "<text>"]
 *        invoice <amount> <CCY> from <account> [memo "<text>"]
 *        route <amount> <CCY> to <account>
 *        report balance_sheet | report tax
 *      Words are lower case and separated by single spaces. <amount> is
 *      decimal with at most the currency's minor digits; <CCY> must be one
 *      the owner configured; <account> is a decimal account number. A memo
 *      is printable ASCII without quotes or backslashes. Anything else is
 *      refused (INTENT_E_PARSE): no partial reads, no guessing.
 *   I2 NEVER WRITES. Proposals and drafts are data. Reports and routes are
 *      read-only and need no approval; pay and invoice drafts do.
 *   I3 GUARD. A pay proposal must pass every invariant; the tags combine
 *      with lpx_and (lpres_hex.h), so the worst one decides:
 *        G1 the payer is always the owner's first account (cfg.own[0]):
 *           the grammar has no payer, so a model cannot choose one;
 *        G2 the amount is at most the per-proposal cap (FALSE above it);
 *        G3 approved amounts this epoch stay within the epoch budget;
 *        G4 the recipient is not the payer and not on the deny list;
 *        G5 at most max_per_epoch proposals per epoch (NEITHER: wait);
 *        G6 the jurisdiction host, when one is attached, allows the
 *           transfer (juris.h J5).
 *      An invoice asks the counterparty to pay into cfg.own[0], so it
 *      skips G2, G3 and G6 but keeps G4 and G5. Only PROCEED is proposed:
 *      a wait returns INTENT_E_WAIT, anything else INTENT_E_GUARD, and both
 *      are kept (state REFUSED) for audit.
 *   I4 APPROVAL. A draft is released only with the owner's signature over
 *        "zxv/intent-approve/v1" || digest[32] || seq u64 (little-endian)
 *      checked by the verify hook (a passkey or ML-DSA-87 key the model
 *      never holds). An approval is used once, the draft expires ttl
 *      epochs after it was proposed, and G3 is checked again at approval.
 *   I5 DIGEST. digest = SHA-256 of the canonical draft (kind, amount,
 *      currency, accounts, memo, seq). The person approves what they saw;
 *      a draft changed after that no longer matches its approval.
 *   I6 ROUTES. intent_rank_routes orders route options the caller found
 *      (vhub quotes and the like): allowed ones first, then the most
 *      received, the lowest fee, the fewest hops. It ranks; it never
 *      executes.
 *
 * HONEST LIMITS. The guard checks what it is told: it cannot tell whether
 * the person meant to approve, only that their key signed. Keeping the
 * approval key away from the model (a separate device or a passkey prompt)
 * is the deployment's job. Freestanding C11: no allocation, no float.
 */
#ifndef ZXV_INTENT_H
#define ZXV_INTENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../lpres/lpres_hex.h"
#include "../juris/juris.h"

#define INTENT_MAX_TEXT 256u
#define INTENT_MEMO_MAX 64u
#define INTENT_MAX_CCY  8u
#define INTENT_MAX_OWN  8u
#define INTENT_MAX_DENY 8u
#define INTENT_SLOTS    16u

typedef enum {
    INTENT_PAY = 1,
    INTENT_INVOICE = 2,
    INTENT_ROUTE = 3,
    INTENT_REPORT_BALANCE_SHEET = 4,
    INTENT_REPORT_TAX = 5
} intent_kind_t;

typedef enum {
    INTENT_OK = 0,
    INTENT_E_ARG = -1,
    INTENT_E_PARSE = -2,   /* I1 */
    INTENT_E_CCY = -3,     /* I1: currency not configured */
    INTENT_E_GUARD = -4,   /* I3: refused */
    INTENT_E_WAIT = -5,    /* I3: not now */
    INTENT_E_SIG = -6,     /* I4 */
    INTENT_E_USED = -7,    /* I4: already released or refused */
    INTENT_E_EXPIRED = -8, /* I4 */
    INTENT_E_UNKNOWN = -9, /* no such proposal */
    INTENT_E_FULL = -10
} intent_status_t;

typedef enum {
    INTENT_ST_FREE = 0,
    INTENT_ST_PROPOSED = 1,
    INTENT_ST_RELEASED = 2,
    INTENT_ST_REFUSED = 3,
    INTENT_ST_EXPIRED = 4
} intent_state_t;

typedef struct {
    uint8_t kind;    /* intent_kind_t */
    uint64_t amount; /* minor units */
    char ccy[4];
    uint32_t from, to; /* accounts; for an invoice `from` is who is asked */
    char memo[INTENT_MEMO_MAX + 1];
} intent_req_t;

typedef struct {
    intent_req_t req;
    uint64_t seq;
    uint64_t epoch; /* when proposed */
    uint8_t digest[32];
    lpx_t tag;     /* the guard's verdict */
    uint8_t state; /* intent_state_t */
} intent_draft_t;

typedef struct {
    char code[4];
    uint8_t minor;
} intent_ccy_t;

/* verify(ctx, msg, len, sig, sig_len): true when sig is the owner's */
typedef bool (*intent_verify_fn)(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig,
                                 uint32_t sig_len);

typedef struct {
    intent_ccy_t ccy[INTENT_MAX_CCY];
    uint32_t n_ccy;
    uint32_t own[INTENT_MAX_OWN];
    uint32_t n_own;
    uint32_t deny[INTENT_MAX_DENY];
    uint32_t n_deny;
    uint64_t cap;           /* G2 */
    uint64_t epoch_budget;  /* G3 */
    uint32_t max_per_epoch; /* G5 */
    uint64_t ttl;           /* I4, in epochs */
    intent_verify_fn verify;
    void *vctx;
    juris_host_t *juris; /* G6, optional */
} intent_cfg_t;

typedef struct {
    intent_cfg_t cfg;
    intent_draft_t slot[INTENT_SLOTS];
    uint64_t seq;
    uint64_t epoch;
    uint64_t spent;    /* approved this epoch */
    uint32_t proposed; /* this epoch */
    uint64_t released, refused;
} intent_t;

/* I1: parse one line. minor digits come from cfg's currency list. */
intent_status_t intent_parse(const intent_cfg_t *cfg, const char *text, size_t len,
                             intent_req_t *out);

void intent_init(intent_t *x, const intent_cfg_t *cfg);
/* A new epoch: G3 and G5 counters restart. Epochs only move forward. */
void intent_set_epoch(intent_t *x, uint64_t epoch);

/* I1 + I3: parse and guard; on INTENT_OK *seq names the stored draft. A
 * refused or waiting proposal is still recorded (state REFUSED) for audit,
 * and *seq is set. */
intent_status_t intent_propose(intent_t *x, const char *text, size_t len, uint64_t *seq);
const intent_draft_t *intent_get(const intent_t *x, uint64_t seq);

/* I4: the message the owner signs */
#define INTENT_APPROVE_MSG (21u + 32u + 8u)
void intent_approve_msg(const intent_draft_t *d, uint8_t out[INTENT_APPROVE_MSG]);
/* I4: on INTENT_OK *out is the released draft, for the caller to post. */
intent_status_t intent_release(intent_t *x, uint64_t seq, const uint8_t *sig, uint32_t sig_len,
                               intent_draft_t *out);

/* I6 */
typedef struct {
    uint64_t received; /* minor units the recipient gets */
    uint64_t fee;
    uint32_t hops;
    lpx_t tag; /* e.g. the jurisdiction verdict for this route */
} intent_route_t;
/* order[0..n) gets the option indexes best first; returns how many are
 * allowed (action PROCEED), which come first. */
uint32_t intent_rank_routes(const intent_route_t *opt, uint32_t n, uint32_t *order);

#endif /* ZXV_INTENT_H */
