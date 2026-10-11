/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vhub.h — cross-currency clearing through V-Bills, on pay_ledger.
 *
 * A demonstration of the hub design the owner described: each currency has
 * one exact peg against the V-Bill, and a swap from currency A to currency B
 * goes A -> V-Bill -> B in ONE atomic ledger posting. It is designed to
 * support PAPSS-style settlement; it is not connected to PAPSS or to any
 * central bank.
 *
 *   H1 PEGS. A currency's peg is num/den: num minor units of the currency
 *      per den V-Bill units, both nonzero. Only the currency's issuer can set
 *      it: a peg update carries an epoch above the last one and the issuer's
 *      signature over (ccy, num, den, epoch), checked by the caller's verify
 *      hook (ML-DSA-87 in production). The cross rate A->B is derived from
 *      the two pegs, so the hub never quotes two prices for one swap.
 *   H2 EXACT. A swap moves a minor units of A, v = a*denA/numA V-Bills and
 *      b = v*numB/denB minor units of B; both divisions must be exact, else
 *      the swap is refused (nothing is rounded away). vhub_quote gives the
 *      largest exact amount not above a requested one.
 *   H3 ONE POSTING. Six lines, three assets, each balanced on its own:
 *      sender -a, A pool +a; A's V-Bill float -v, B's V-Bill float +v;
 *      B pool -b, receiver +b. The ledger applies all or nothing.
 *   H4 BACKING. Each currency's V-Bill float is what its issuer deposited
 *      against the peg, and the B pool is the B its issuer provided. A swap
 *      that would take either below zero is refused by the ledger (funds),
 *      so a peg pays out only while its issuer backs it. An exact peg removes
 *      rounding; it does not remove exchange-rate risk.
 *   H5 RULES. Each currency has signed jurisdiction rules (capital controls
 *      and the like) run as plug-ins. Every rule of the sending AND the
 *      receiving currency returns a 64-state tag (lpres_hex.h); the swap
 *      posts only when every tag's action is PROCEED. Both (a dispute)
 *      escalates to a person, false refuses, neither waits, and forbidden
 *      refuses and is written to the quarantine log. A currency with no rule
 *      registered waits: missing rules fail closed.
 *   H6 LIMIT RULE. vhub_rule_limit is a ready-made capital control: at most
 *      `limit` minor units out of a currency per sender per epoch.
 *
 * HONEST LIMITS. Rules run in software, here and in userland; a tag reports
 * what a rule decided and is not itself a legal check. V-Bills are likely
 * securities in many jurisdictions; real use needs legal review and a
 * participating issuer. Freestanding C11: no allocation, no float.
 */
#ifndef ZXV_VHUB_H
#define ZXV_VHUB_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"
#include "../lpres/lpres_hex.h"

#define VHUB_MAX_CCY   8u
#define VHUB_MAX_RULES 4u
#define VHUB_NONE      0xFFFFFFFFu

typedef enum {
    VHUB_OK = 0,
    VHUB_ERR_ARG = -1,
    VHUB_ERR_FULL = -2,
    VHUB_ERR_SIG = -3,     /* H1: bad signature */
    VHUB_ERR_EPOCH = -4,   /* H1: epoch not above the last */
    VHUB_ERR_INEXACT = -5, /* H2 */
    VHUB_ERR_LEDGER = -6,  /* H3/H4: the ledger refused (see last_ledger) */
    VHUB_REFUSED = -7,     /* H5: a rule said false */
    VHUB_WAIT = -8,        /* H5: a rule said neither, or no rule */
    VHUB_ESCALATE = -9,    /* H5: a rule said both */
    VHUB_FORBIDDEN = -10,  /* H5: quarantined */
    VHUB_HALTED = -11      /* the quarantine log is full: fail closed */
} vhub_status_t;

typedef struct {
    uint8_t from_ccy, to_ccy;
    uint32_t sender, receiver; /* ledger accounts */
    uint64_t amount;           /* minor units of from_ccy */
    uint64_t vbills, out;      /* filled by the hub before the rules run */
    uint64_t epoch;
} vhub_swap_t;

/* A rule: the tag for one swap. `side` is 0 when the rule belongs to the
 * sending currency, 1 for the receiving one. */
typedef lpx_t (*vhub_rule_fn)(void *ctx, const vhub_swap_t *sw, uint8_t side);
/* Optional: told about each swap once it has posted (to keep tallies). */
typedef void (*vhub_commit_fn)(void *ctx, const vhub_swap_t *sw, uint8_t side);

typedef struct {
    vhub_rule_fn fn;
    vhub_commit_fn commit;
    void *ctx;
} vhub_rule_t;

typedef struct {
    bool used;
    uint16_t asset;
    uint32_t issuer; /* the issuer's ledger account (signs pegs) */
    uint32_t pool;   /* the hub's holding of this currency */
    uint32_t vfloat; /* this currency's V-Bill float (its backing) */
    uint64_t num, den;
    uint64_t peg_epoch;
    vhub_rule_t rule[VHUB_MAX_RULES];
    uint32_t n_rules;
} vhub_ccy_t;

/* verify(ctx, issuer, msg, len, sig, sig_len): true when sig is issuer's */
typedef bool (*vhub_verify_fn)(void *ctx, uint32_t issuer, const uint8_t *msg, uint32_t len,
                               const uint8_t *sig, uint32_t sig_len);

typedef struct {
    pay_ledger_t *L;
    uint16_t vbill; /* the V-Bill asset */
    vhub_ccy_t ccy[VHUB_MAX_CCY];
    vhub_verify_fn verify;
    void *vctx;
    uint8_t seed[32];
    uint64_t seq, posted;
    uint64_t epoch; /* the current rule epoch (H6) */
    pay_status_t last_ledger;
    lpx_t last_tag;
    lpx_qlog_t qlog;
} vhub_t;

void vhub_init(vhub_t *h, pay_ledger_t *L, uint16_t vbill_asset, vhub_verify_fn verify, void *vctx,
               const uint8_t seed[32], lpx_qrec_t *qstore, uint32_t qcap);
/* Register currency slot c (0..VHUB_MAX_CCY-1) with its accounts. The peg
 * starts unset (no swaps) until the first signed vhub_set_peg. */
vhub_status_t vhub_add_ccy(vhub_t *h, uint8_t c, uint16_t asset, uint32_t issuer, uint32_t pool,
                           uint32_t vfloat);
/* H1: the message an issuer signs: "zxv/vhub-peg/v1" || c || num || den ||
 * epoch, little-endian; written to out (VHUB_PEG_MSG bytes). */
#define VHUB_PEG_MSG (15u + 1u + 8u + 8u + 8u)
void vhub_peg_msg(uint8_t c, uint64_t num, uint64_t den, uint64_t epoch, uint8_t out[VHUB_PEG_MSG]);
vhub_status_t vhub_set_peg(vhub_t *h, uint8_t c, uint64_t num, uint64_t den, uint64_t epoch,
                           const uint8_t *sig, uint32_t sig_len);
vhub_status_t vhub_add_rule(vhub_t *h, uint8_t c, vhub_rule_fn fn, vhub_commit_fn commit,
                            void *ctx);
/* H2: the largest exact amount <= want for a -> b (0 if none). */
uint64_t vhub_quote(const vhub_t *h, uint8_t a, uint8_t b, uint64_t want);
/* Run the swap. sw->vbills and sw->out are filled. */
vhub_status_t vhub_swap(vhub_t *h, vhub_swap_t *sw, uint64_t tick);

/* H6: the limit rule's state. */
#define VHUB_LIMIT_SENDERS 16u
typedef struct {
    uint64_t limit;
    uint64_t epoch;
    uint32_t sender[VHUB_LIMIT_SENDERS];
    uint64_t used[VHUB_LIMIT_SENDERS];
    uint32_t n;
} vhub_limit_t;
/* Register with vhub_add_rule(h, c, vhub_rule_limit, vhub_limit_commit, &l).
 * Outflows only (side 0); a new epoch starts every sender at zero; a sender
 * past VHUB_LIMIT_SENDERS distinct ones is refused (fail closed). */
lpx_t vhub_rule_limit(void *ctx, const vhub_swap_t *sw, uint8_t side);
void vhub_limit_commit(void *ctx, const vhub_swap_t *sw, uint8_t side);

#endif /* ZXV_VHUB_H */
