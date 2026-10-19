/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lpres_hex.h — the 64-state hexagram tag on top of lpres's four states.
 *
 * A tag is 6 bits, read like an I Ching hexagram from the top line down:
 *
 *   bits 5-4  BASE      the lpres state:      True 11, False 00,
 *                                             Both 10, Neither 01
 *   bits 3-2  POLARITY  how its evidence is   negentropic 11 (converging),
 *                       moving:               entropic 00 (diverging or
 *                                             stale), neutral 01 (steady),
 *                                             forbidden 10 (a wall)
 *   bits 1-0  PHASE     the work's lifecycle: potential 00, emerging 01,
 *                                             manifest 11, dissolving 10
 *
 *   X1 TAGS ON WORK. A tag belongs to a unit of work (an attestation, a
 *      ledger posting, an agent task, a model request), never to every byte
 *      of data and never to tensor weights.
 *   X2 ONE TABLE. What happens next is lpx_action(tag): one lookup in a
 *      64-entry const table. The table follows the rules below and the
 *      tests rebuild it from them entry by entry:
 *        - polarity FORBIDDEN: ROLLBACK (and quarantine, X6), whatever the
 *          other four bits say (16 entries);
 *        - TRUE: PROCEED unless entropic (a weakening truth), then WAIT;
 *        - FALSE: REFUSE (an ordinary refusal, nothing to roll back);
 *        - BOTH: WAIT while negentropic (a converging dispute stays in
 *          escrow), ESCALATE to a person when neutral or entropic;
 *        - NEITHER: WAIT (unknown never acts).
 *      The phase never changes the action; it is enforced by X3.
 *   X3 ONE STEP AT A TIME. The phase moves around a Gray cycle,
 *      potential -> emerging -> manifest -> dissolving -> potential, one bit
 *      flip per step. lpx_advance refuses a jump.
 *   X4 FORBIDDEN IS ABSORBING. No change takes a forbidden tag out of the
 *      forbidden polarity; only a fresh tag (new work) starts clean.
 *   X5 COMBINING. lpx_and / lpx_or / lpx_not use lpres's own tables for the
 *      base (lpres_conjoin, lpres_disjoin, lpres_negate: one source), the
 *      worst polarity (forbidden > entropic > neutral > negentropic) and the
 *      least advanced phase. Negation keeps polarity and phase.
 *   X6 QUARANTINE. lpx_qlog is an append-only, hash-chained (SHA-256) log
 *      of forbidden work: the tag, who submitted it, the subject digest and
 *      a reason code. It is read for audit and never replayed. A full log
 *      refuses the append, and the caller must then halt (fail closed).
 *      Signing the log head is the caller's job. The hash is SHA-256, an
 *      L0 pure function, so lpres (L1) stays below the crypto layer.
 *
 * HONEST LIMITS. A tag reports what a real check found; it is not the check.
 * FORBIDDEN on a memory access means a bounds check or a sanitizer caught
 * it; FORBIDDEN on a posting means the ledger's own A = L + E check refused
 * it. Policy refusals (no weapons targeting) are refusals by policy that a
 * tag can label, not something this logic detects. Polarity comes from
 * evidence counts the caller supplies (lpx_polarity). Freestanding C11: no
 * libc, no allocation, no float.
 */
#ifndef LPRES_HEX_H
#define LPRES_HEX_H

#include <stdbool.h>
#include <stdint.h>
#include "lpres.h"

typedef uint8_t lpx_t; /* 0..63 */

enum { LPX_B_FALSE = 0, LPX_B_NEITHER = 1, LPX_B_BOTH = 2, LPX_B_TRUE = 3 };
enum { LPX_P_ENTROPIC = 0, LPX_P_NEUTRAL = 1, LPX_P_FORBIDDEN = 2, LPX_P_NEGENTROPIC = 3 };
enum { LPX_PH_POTENTIAL = 0, LPX_PH_EMERGING = 1, LPX_PH_DISSOLVING = 2, LPX_PH_MANIFEST = 3 };

typedef enum {
    LPX_ACT_PROCEED = 0,
    LPX_ACT_WAIT = 1,
    LPX_ACT_REFUSE = 2,
    LPX_ACT_ESCALATE = 3,
    LPX_ACT_ROLLBACK = 4 /* and quarantine (X6) */
} lpx_action_t;

#define LPX_STATES 64u

static inline lpx_t lpx_make(uint8_t base, uint8_t pol, uint8_t phase)
{
    return (lpx_t) (((base & 3u) << 4) | ((pol & 3u) << 2) | (phase & 3u));
}
static inline uint8_t lpx_base(lpx_t t)
{
    return (uint8_t) ((t >> 4) & 3u);
}
static inline uint8_t lpx_pol(lpx_t t)
{
    return (uint8_t) ((t >> 2) & 3u);
}
static inline uint8_t lpx_phase(lpx_t t)
{
    return (uint8_t) (t & 3u);
}
static inline bool lpx_forbidden(lpx_t t)
{
    return lpx_pol(t) == LPX_P_FORBIDDEN;
}

/* lpres state <-> base code (lpres numbers them differently). An lpres value
 * out of range maps to NEITHER (unknown), never to TRUE. */
uint8_t lpx_base_from_lpres(lpres_state_t s);
lpres_state_t lpx_base_to_lpres(uint8_t base);

/* X2 */
lpx_action_t lpx_action(lpx_t t);

/* X3: the phase after `phase` on the Gray cycle. */
uint8_t lpx_phase_next(uint8_t phase);
/* X3/X4: move `t` to `phase`: the same phase or the next one only. Returns
 * false (and leaves *t) on a jump. */
bool lpx_advance(lpx_t *t, uint8_t phase);
/* X4: set the base and polarity, keeping the phase; a forbidden tag stays
 * forbidden (its base still updates, for the audit record). */
lpx_t lpx_set(lpx_t t, uint8_t base, uint8_t pol);
/* Mark forbidden (X4). */
static inline lpx_t lpx_forbid(lpx_t t)
{
    return lpx_make(lpx_base(t), LPX_P_FORBIDDEN, lpx_phase(t));
}

/* Polarity from evidence: counts before and after, and whether the newest
 * evidence is older than the caller's freshness limit. Stale -> entropic;
 * else the margin |pos - neg| growing -> negentropic, shrinking ->
 * entropic, unchanged -> neutral. Never returns FORBIDDEN. */
uint8_t lpx_polarity(uint32_t pos0, uint32_t neg0, uint32_t pos1, uint32_t neg1, bool stale);

/* X5 */
lpx_t lpx_and(lpx_t a, lpx_t b);
lpx_t lpx_or(lpx_t a, lpx_t b);
lpx_t lpx_not(lpx_t a);

/* Text form "base/polarity/phase", e.g. "true/negentropic/manifest". */
const char *lpx_base_name(uint8_t base);
const char *lpx_pol_name(uint8_t pol);
const char *lpx_phase_name(uint8_t phase);
const char *lpx_action_name(lpx_action_t a);

/* ---- X6: the quarantine log ---- */
#define LPX_QLOG_HASH 32u

typedef struct {
    uint64_t seq;
    lpx_t tag;
    uint8_t reason;      /* caller's code */
    uint32_t submitter;  /* who submitted the work */
    uint8_t subject[32]; /* digest of the refused work */
    uint8_t prev[LPX_QLOG_HASH];
    uint8_t hash[LPX_QLOG_HASH];
} lpx_qrec_t;

typedef struct {
    lpx_qrec_t *rec;
    uint32_t cap, n;
    uint8_t head[LPX_QLOG_HASH]; /* hash of the last record (zeros: empty) */
} lpx_qlog_t;

void lpx_qlog_init(lpx_qlog_t *q, lpx_qrec_t *storage, uint32_t cap);
/* Append one record. Only forbidden tags are quarantined: false for any
 * other tag, and false when full (X6: the caller halts). */
bool lpx_qlog_append(lpx_qlog_t *q, lpx_t tag, uint8_t reason, uint32_t submitter,
                     const uint8_t subject[32]);
/* Recompute every link; false if any record was altered. */
bool lpx_qlog_verify(const lpx_qlog_t *q);

#endif /* LPRES_HEX_H */
