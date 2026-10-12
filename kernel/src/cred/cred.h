/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cred.h — EigenTrust credibility over fulfilled trades, its bounded unlocks
 * (shorter escrow hold, zero-interest credit line) and the Player Card.
 *
 * OWNER DECISION (implemented here): "EigenTrust credibility (fulfilment-only,
 * log gain / exponential loss, decay) unlocking bounded credit lines /
 * shorter escrow". No usury: nothing here accrues with time or balance; the
 * credit line has no rate field. Player Card tiers come from
 * sicily.h sic_tier_eval and give NO auction priority. This module is not
 * the Pig Badge (kernel/src/reputation) and does not touch it.
 *
 * SUBJECTS. Up to CRED_MAX_SUBJECTS ids, added by the operator
 * (cred_subject_add), each optionally PRE-TRUSTED. Index order is the order
 * of addition; every tie and remainder rule below uses ascending index.
 *
 *   C1 FULFILMENT-ONLY INPUT. cred_fulfil(rater i, ratee j, value, receipt)
 *      records that j fulfilled an order for i worth `value` minor units
 *      (value >= 1). cred_fail(i, j, ruling) records a failure established
 *      by a dispute RULING (kernel/src/dispute returns the loser); an
 *      unsupported complaint has no entry point. Both subjects must be
 *      known and distinct (self-rating: CRED_ERR_SELF). Every receipt id and
 *      every ruling id is remembered in a ring of the last CRED_SEEN ids
 *      (receipts and rulings are separate namespaces); an id still in the
 *      ring is refused (CRED_ERR_DUP). A refused call changes nothing.
 *   C2 LOG GAIN. A fulfilment adds g(v) = floor(log2(1 + v)) + 1 units to
 *      the local score s_ij (the bit length of 1 + v; g(2^64 - 1) = 65), so
 *      trust grows with the number of honest trades, not with their size:
 *      one order of 2^20 earns 21 units, twenty orders of 1 earn 40. s_ij
 *      saturates at CRED_LOCAL_MAX.
 *   C3 EXPONENTIAL LOSS. The k-th failure of ratee j (k counts j's
 *      failures, over all raters, since the counts were last halved by C4)
 *      subtracts fail_base << min(k - 1, fail_cap) from s_ij, saturating at
 *      0. The count saturates at UINT32_MAX.
 *   C4 DECAY. cred_decay_epoch sets every s_ij to
 *          floor(s_ij * (decay_den - decay_num) / decay_den)
 *      (default 1/16 per epoch; with decay_num > 0 every positive score
 *      falls by at least 1 per epoch, so it reaches 0) and halves every
 *      failure count
 *      (floor(k / 2)), so old failures weigh less and are forgotten.
 *   C5 GLOBAL TRUST (EigenTrust). Fixed point, CRED_SCALE = 2^30 is all
 *      the trust there is. Pre-trusted distribution p: SCALE split equally
 *      over the pre-trusted subjects, or over all subjects when none is
 *      pre-trusted; the remainder SCALE mod P goes one unit each to the
 *      first members in ascending index. With a = alpha_num / alpha_den:
 *        t_0 = p;
 *        give_i = floor(t_i * (alpha_den - alpha_num) / alpha_den);
 *        flow_ij = floor(give_i * s_ij / S_i), S_i = sum_k s_ik > 0,
 *                = floor(give_i * p_j / SCALE)  when S_i == 0;
 *        T = SCALE - sum_ij flow_ij      (the a-share plus all rounding dust)
 *        t'_j = sum_i flow_ij + floor(T * p_j / SCALE), and the units the
 *               last floor leaves (< P) go one each to p's support in
 *               ascending index.
 *      So sum_j t_j == SCALE EXACTLY after every iteration (checked by
 *      cred_check and the test). The dust goes to the lowest indices, so
 *      t is a fixed point only to within a few units of 2^30 (with no
 *      ratings and uniform p, t differs from p by the dust). Iterate until the L1 change sum|t' -
 * t| <= eps or `iters` iterations. Every product is 128-bit (pay_muldiv). Deterministic: no
 * floating point, fixed order. C6 SYBIL LIMITS. Trust only flows along fulfilments a rater
 * recorded, weighted by the rater's own trust, and the a-share returns to p. A ring of fake
 * subjects rating each other gets only what flows into it from trusted raters plus its p share:
 * zero when the operator has pre-trusted subjects and none of them is in the ring. With NO
 *      pre-trusted subject p is uniform and every sybil gets about a/n of
 *      the trust: operators should pre-trust. A trusted rater that trades
 *      with one member of a ring hands it part of the rater's own trust,
 *      which then circulates inside the ring losing only the a-share per
 *      iteration: the leak is bounded by what that rater had, not zero, and
 *      decay (C4) plus failures (C3) are the remedy. Collusion by a trusted rater
 *      (fake receipts) is bounded only by C2's log gain and by the cost of
 *      the trades; a receipt is only as real as the market behind it.
 *   C7 CREDIBILITY FOR THE CARD. credibility(j) = min(CRED_CARD_CRED_MAX,
 *      floor(t_j * n * 100 / SCALE)), n = number of subjects: 100 is exactly
 *      the average trust, 800 is eight times it. That is the number handed
 *      to sic_tier_eval (whose defaults are Bronze 100 .. Platinum 800).
 *   C8 ESCROW HOLD (off by default). cred_escrow_hold(cfg, score) with
 *      score = t_j: hold_base when !hold_enabled; otherwise
 *          hold_base - floor((hold_base - hold_floor) * min(score, hold_sat)
 *                             / hold_sat)
 *      ticks: never below hold_floor, never above hold_base, non-increasing
 *      in score.
 *   C9 CREDIT LINE (off by default; a jurisdiction switch). 0 unless
 *      credit_lines_enabled; else min(credit_cap, floor(score * credit_k /
 *      SCALE)). ZERO INTEREST: there is no rate, no accrual, no fee field.
 *      It is a POLICY NUMBER ONLY: pay_ledger forbids CREDIT balances on
 *      holder accounts, and posting a credit line is out of scope here.
 *   C10 PLAYER CARD. cred_player_card fills credibility (C7), the global
 *      trust, the local mass received (sum_i s_ij), tier = sic_tier_eval(tab,
 *      credibility, bond, uptime_bps, giving), sicily's perk bits, the next
 *      tier's thresholds and which inputs are below them (credibility is
 *      compared as sicily does, effective = with giving), escrow hold (C8)
 *      and credit line (C9). It needs a computed trust vector (C5) since the
 *      last change to the subject set (CRED_ERR_STATE otherwise).
 *      cred_card_json writes it in the shape zxvd's `player.card` RPC and
 *      the Studio app already decode (all integers except "tier" are decimal
 *      strings):
 *        {"tier":T,"metal":"..","stone":"..",
 *         "inputs":{"credibility":{"value":"..","source":"EigenTrust over
 *            fulfilled trades"},"bond":{..},"uptime_bps":{..},"giving":{..}},
 *         "next":null | {"tier":T+1,"metal":"..","stone":"..",
 *            "credibility":"..","bond":"..","uptime_bps":"..",
 *            "missing":["credibility","bond","uptime_bps"]},
 *         "perks":["free registration","storefronts","forwards","IP notes",
 *            "cross-border licensing"],
 *         "score":"<t_j fixed point>","local":"..","escrow_hold_bps":"..",
 *         "credit_line":".."}
 *      "next" is null at Platinum. escrow_hold_bps = floor(hold * 10000 /
 *      hold_base). The bond / uptime / giving sources are the caller's plain
 *      strings (src_* in the card; NULL is written as "unspecified");
 *      strings are JSON-escaped; unknown perk or missing bits are
 *      ignored. Returns the length written (NUL excluded),
 *      or -1 when the buffer is too small or an argument is NULL (the buffer
 *      then holds a NUL-terminated prefix, or nothing when cap is 0).
 *   C11 AUDIT. cred_check: subject ids distinct, s_ii == 0, every s_ij <=
 *      CRED_LOCAL_MAX, s_total == sum s_ij, p sums to SCALE over its support
 *      and is 0 elsewhere, and, when computed, t sums to SCALE; the seen
 *      ring within bounds.
 *
 * HONEST LIMITS. Credibility measures recorded fulfilment, not character;
 * EigenTrust resists sybils only as far as C6 says. The thresholds, the
 * mapping C7 and every default here are choices of this module, not anyone's
 * standard. Credit lines are regulated lending in most places: they are off
 * by default, need legal review per jurisdiction, and this module only
 * computes a number. Nothing here is a credit score in any legal sense.
 * Freestanding C11: no allocation, no floating point, no libc.
 */
#ifndef ZXV_CRED_H
#define ZXV_CRED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"
#include "../sicily/sicily.h"

#define CRED_MAX_SUBJECTS   64u
#define CRED_SCALE          ((uint64_t) 1 << 30) /* C5 */
#define CRED_LOCAL_MAX      ((uint64_t) 1 << 40) /* C2 */
#define CRED_SEEN           256u                 /* C1 replay ring */
#define CRED_CARD_CRED_MAX  1000u                /* C7 */
#define CRED_FAIL_SHIFT_MAX 20u                  /* C3: fail_cap <= this */
#define CRED_FAIL_BASE_MAX  ((uint64_t) 1 << 20) /* C3: fail_base <= this */
#define CRED_ITERS_MAX      256u

typedef enum {
    CRED_OK = 0,
    CRED_ERR_ARG = -1,
    CRED_ERR_FULL = -2,
    CRED_ERR_UNKNOWN = -3, /* subject not added */
    CRED_ERR_DUP = -4,     /* C1: id already seen, or subject already added */
    CRED_ERR_SELF = -5,    /* C1: rater == ratee */
    CRED_ERR_STATE = -6    /* C10: trust not computed for this subject set */
} cred_status_t;

typedef struct {
    uint32_t decay_num, decay_den;            /* C4: 0 <= num <= den, den > 0          */
    uint64_t fail_base;                       /* C3: 1..CRED_FAIL_BASE_MAX             */
    uint32_t fail_cap;                        /* C3: 0..CRED_FAIL_SHIFT_MAX            */
    uint32_t alpha_num, alpha_den;            /* C5: 0 <= num <= den, den > 0          */
    uint32_t iters;                           /* C5: 1..CRED_ITERS_MAX                 */
    uint64_t eps;                             /* C5: L1 stop threshold                 */
    bool hold_enabled;                        /* C8 */
    uint64_t hold_base, hold_floor, hold_sat; /* C8: floor <= base, base, sat > 0 */
    bool credit_lines_enabled;                /* C9 */
    uint64_t credit_k, credit_cap;            /* C9 */
} cred_config_t;

typedef struct {
    uint32_t id;
    bool pretrusted;
    uint32_t fails; /* C3 k */
} cred_subject_t;

typedef struct {
    cred_config_t cfg;
    uint32_t n;
    cred_subject_t subj[CRED_MAX_SUBJECTS];
    uint64_t s[CRED_MAX_SUBJECTS][CRED_MAX_SUBJECTS]; /* s[i][j]: i's local score of j */
    uint64_t s_total;
    uint64_t p[CRED_MAX_SUBJECTS], t[CRED_MAX_SUBJECTS];
    bool computed;
    uint32_t iters_used;
    uint64_t last_delta;
    uint8_t seen[CRED_SEEN][32];
    uint8_t seen_kind[CRED_SEEN];
    uint32_t n_seen, seen_head;
    uint64_t epoch, n_fulfil, n_fail;
} cred_t;

/* C10 */
typedef struct {
    uint32_t subject;
    uint64_t credibility; /* C7 */
    uint64_t trust;       /* t_j, fixed point of CRED_SCALE */
    uint64_t local;       /* sum_i s_ij */
    uint64_t bond, giving;
    uint32_t uptime_bps;
    uint8_t tier; /* sic_tier_t, 0..4 */
    uint32_t perks;
    bool has_next;
    sic_threshold_t next; /* thresholds of tier + 1 */
    uint8_t missing;      /* CRED_MISS_* bits below `next` */
    uint64_t escrow_hold;
    uint64_t escrow_hold_bps;
    uint64_t credit_line;
    const char *src_bond, *src_uptime, *src_giving; /* caller's; NULL = "unspecified" */
} cred_card_t;

#define CRED_MISS_CRED   0x01u
#define CRED_MISS_BOND   0x02u
#define CRED_MISS_UPTIME 0x04u
#define CRED_SOURCE_CRED "EigenTrust over fulfilled trades"

void cred_config_default(cred_config_t *cfg);
bool cred_config_ok(const cred_config_t *cfg);
/* cfg NULL = defaults. */
cred_status_t cred_init(cred_t *c, const cred_config_t *cfg);
cred_status_t cred_set_config(cred_t *c, const cred_config_t *cfg);

cred_status_t cred_subject_add(cred_t *c, uint32_t id, bool pretrusted);
cred_status_t cred_set_pretrusted(cred_t *c, uint32_t id, bool pretrusted);
/* Index of a subject, or -1. */
int32_t cred_index(const cred_t *c, uint32_t id);

/* C2 / C3 (pure). cred_loss: fail_base << min(k - 1, fail_cap); 0 for k 0. */
uint64_t cred_gain(uint64_t value);
uint64_t cred_loss(const cred_config_t *cfg, uint32_t k);

cred_status_t cred_fulfil(cred_t *c, uint32_t rater, uint32_t ratee, uint64_t value,
                          const uint8_t receipt[32]);
cred_status_t cred_fail(cred_t *c, uint32_t rater, uint32_t ratee, const uint8_t ruling[32]);
/* C4 */
cred_status_t cred_decay_epoch(cred_t *c);
/* C5. CRED_ERR_STATE with no subjects. */
cred_status_t cred_trust_compute(cred_t *c);

/* Readers: 0 for unknown subjects. */
uint64_t cred_local(const cred_t *c, uint32_t rater, uint32_t ratee);
uint64_t cred_trust(const cred_t *c, uint32_t id);
uint64_t cred_credibility(const cred_t *c, uint32_t id); /* C7 */

/* C8 / C9 (pure). cfg NULL uses the defaults. */
uint64_t cred_escrow_hold(const cred_config_t *cfg, uint64_t score);
uint64_t cred_credit_line(const cred_config_t *cfg, uint64_t score);

/* C10. tab NULL = sicily defaults. */
cred_status_t cred_player_card(const cred_t *c, uint32_t subject, uint64_t bond,
                               uint32_t uptime_bps, uint64_t giving, const sic_tier_table_t *tab,
                               cred_card_t *out);
int32_t cred_card_json(const cred_card_t *card, char *buf, size_t cap);

/* C11 */
bool cred_check(const cred_t *c);

#endif /* ZXV_CRED_H */
