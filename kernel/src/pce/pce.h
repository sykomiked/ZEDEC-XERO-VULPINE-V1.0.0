/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce.h — paraconsistent decision engines: what the four engines share.
 *
 * Four engines sit on this header: probability (pce_prob.h), tactics
 * (pce_tac.h), strategy (pce_strat.h) and financial analysis (pce_fin.h).
 * None of them picks a winner when its evidence disagrees. Every answer is
 * one of lpres's four values and a 64-state hexagram tag on top of it, and
 * what happens next is lpx_action of that tag (lpres_hex.h X2):
 *
 *   TRUE     the finding stands (PROCEED), unless its support is weakening
 *   FALSE    refused, nothing to undo (REFUSE)
 *   BOTH     the disagreement is kept with both sides: converging WAIT,
 *            steady or diverging ESCALATE to a person
 *   NEITHER  not enough to say (WAIT): nothing acts on an unknown
 *   forbidden  a broken invariant (overflow, A != L + E): ROLLBACK
 *
 *   E1 EXACT. Integers and exact rationals (rational.h) only, never a
 *      float. An arithmetic overflow is detected (rat_t.valid) and refused
 *      as PCE_E_OVERFLOW, never wrapped or rounded.
 *   E2 ADVISORY. Work that touches life-safety, a device or money is
 *      advisory (PCE_ADV_*): a verdict that would PROCEED becomes ESCALATE,
 *      a person approves. Nothing here moves money, places an order or
 *      drives a device; the APIs take read-only inputs.
 *   E3 PURPOSE. Tactics and strategy runs name their purpose from
 *      pce_purpose_t: games, logistics, scheduling, business, negotiation,
 *      mapping (and finance analytics). There is no other purpose to name;
 *      weapons targeting is not one, and anything outside the list is
 *      refused (PCE_E_PURPOSE). This is a refusal by policy that the API
 *      makes explicit, not something the logic can detect.
 *   E4 REPLAYABLE. Each engine is deterministic: the same inputs give the
 *      same outputs, bit for bit, on every target. A run can append one
 *      record to the paradox engine's hash-chained log (pdx_log_t) with
 *      the SHA-256 of its canonical inputs and of its outputs; rerunning it
 *      must reproduce both digests.
 *   E5 FREESTANDING C11: no libc, no allocation, no float; every array is
 *      the caller's with a fixed capacity.
 */
#ifndef PCE_H
#define PCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../lpres/lpres.h"
#include "../lpres/lpres_hex.h"
#include "../paradox/paradox.h"
#include "../rational/rational.h"
#include "../robin_debanks/sha256.h"

enum {
    PCE_OK = 0,
    PCE_E_ARG = -1,      /* inputs out of range or not a distribution    */
    PCE_E_OVERFLOW = -2, /* E1: too large to compute exactly             */
    PCE_E_PURPOSE = -3,  /* E3: not a purpose these engines serve        */
    PCE_E_BUDGET = -4,   /* the search ran out of its node budget        */
    PCE_E_LOG = -5,      /* the log is full: halt (fail closed)          */
    PCE_E_CONFLICT = -6  /* the evidence contradicts the model (BOTH)    */
};

/* E3 */
typedef enum {
    PCE_PURPOSE_GAME = 1,
    PCE_PURPOSE_LOGISTICS = 2,
    PCE_PURPOSE_SCHEDULING = 3,
    PCE_PURPOSE_BUSINESS = 4,
    PCE_PURPOSE_NEGOTIATION = 5,
    PCE_PURPOSE_MAPPING = 6,
    PCE_PURPOSE_FINANCE = 7 /* analytics only (pce_fin.h) */
} pce_purpose_t;
bool pce_purpose_ok(uint32_t purpose);
const char *pce_purpose_name(uint32_t purpose); /* "?" when not a purpose */

/* E2 */
#define PCE_ADV_LIFE_SAFETY 1u
#define PCE_ADV_DEVICE      2u
#define PCE_ADV_MONEY       4u
#define PCE_ADV_ALL         7u

/* The paradox engine's log takes operator codes; the engines use these. */
enum { PCE_OP_PROB = 16, PCE_OP_TAC = 17, PCE_OP_STRAT = 18, PCE_OP_FIN = 19 };

typedef struct {
    lpx_t tag;        /* base, polarity, phase                         */
    lpx_action_t act; /* lpx_action(tag), raised to ESCALATE by E2     */
    uint8_t kind;     /* pdx_kind_of_tag(tag): the paradox kind, if any */
} pce_verdict_t;

/* The verdict for a base, a polarity and a phase. advisory is PCE_ADV_*
 * bits; any bit set turns PROCEED into ESCALATE (E2). */
pce_verdict_t pce_verdict(uint8_t base, uint8_t pol, uint8_t phase, uint32_t advisory);

/* Polarity from a margin that was m0 and is now m1 (both >= 0): growing
 * negentropic, shrinking entropic, unchanged neutral; stale (or an invalid
 * margin) entropic. The rational form of lpx_polarity. */
uint8_t pce_trend(rat_t m0, rat_t m1, bool stale);

/* ---- E4: canonical digests ---- */
typedef struct {
    sha256_ctx_t c;
} pce_hash_t;

/* Starts with "zxv/pce/v1/" and the engine's operator code. */
void pce_hash_begin(pce_hash_t *h, uint8_t op);
void pce_hash_u64(pce_hash_t *h, uint64_t v); /* 8 bytes, little-endian */
void pce_hash_i64(pce_hash_t *h, int64_t v);
void pce_hash_rat(pce_hash_t *h, rat_t r); /* num, den, valid */
void pce_hash_end(pce_hash_t *h, uint8_t out[PDX_HASH]);

/* Append one record (op, the verdict, aux, input and output digests) to
 * log. A NULL log is fine (nothing kept); a full one is PCE_E_LOG. */
int pce_log(pdx_log_t *log, uint8_t op, const pce_verdict_t *v, uint32_t aux,
            const uint8_t in[PDX_HASH], const uint8_t out[PDX_HASH]);

/* Copy n bytes. Results are built aside and copied out only once nothing
 * can fail; a plain struct assignment of that size would make the compiler
 * call memcpy, which a freestanding kernel object may not assume (E5). */
void pce_copy(void *dst, const void *src, size_t n);

/* Exact helpers every engine uses. */
bool pce_rat_nonneg(rat_t r); /* valid and >= 0 */
rat_t pce_rat_min(rat_t a, rat_t b);
rat_t pce_rat_max(rat_t a, rat_t b);

#endif /* PCE_H */
