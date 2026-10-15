/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* talisman_model.h — Talisman, the owner's Player Card, as a model (zxm.h):
 * reads zxvd's player.card (sicily.h S2/S3, zxvd_rpc.h V3) and keeps what a
 * view shows: the tier, each input with its source, how far each input has
 * come toward the next tier's threshold (talisman_rules.h TL1), whether
 * that tier is earned (TL2), the perks, and the history of tiers. Read
 * only. No drawing, no allocation, integers only (x100: 5000 = 50.00%).
 *
 *   TM1 Every figure comes from zxvd. The tier is the one zxvd evaluated;
 *       the model never raises it. Credibility counts as zxvd counts it
 *       against a threshold: credibility + min(giving, floor(credibility /
 *       3)) (sicily.h S2, talisman_effective_cred).
 *   TM2 MISSING. An input zxvd reports as 0 is shown as missing, with
 *       zxvd's source as the reason, never as met: its reach is 0 and its
 *       margin -1, so the next tier is not earned on it. An uptime past
 *       10000 basis points is no measurement (sicily.h S2): shown as
 *       missing too. An input past its threshold is met: its reach is held
 *       at 10000 and its margin at TALISMAN_MARGIN_MAX.
 *   TM3 NO PRIORITY. Tiers give no priority, discount or credit; every
 *       notice says so, and nothing here computes any of them.
 *   TM4 HISTORY. Each refresh is given the day (days since 1970); the
 *       tier is marked when it differs from the last mark, or on the first
 *       refresh. TALISMAN_HISTORY marks are kept: when full, the oldest
 *       is dropped and counted (dropped). A day before the last mark's is
 *       refused before anything is asked.
 *   TM5 A refresh is all or nothing: a reply that is not what player.card
 *       promises (ZXM_E_PROTOCOL) or a refusal (ZXM_E_REMOTE) leaves the
 *       last card, its notice and the history as they were. */
#ifndef TALISMAN_MODEL_H
#define TALISMAN_MODEL_H

#include "talisman_rules.h"
#include "zxm.h"

#define TALISMAN_TIERS      5u /* Copper..Platinum: what player.card reports */
#define TALISMAN_WORD_MAX   15u
#define TALISMAN_SOURCE_MAX 95u
#define TALISMAN_PERKS_MAX  8u
#define TALISMAN_PERK_MAX   47u
#define TALISMAN_HISTORY    32u
#define TALISMAN_NOTICE_MAX 1023u

/* the inputs, in player.card's order */
enum {
    TALISMAN_CRED = 0,
    TALISMAN_BOND = 1,
    TALISMAN_UPTIME = 2,
    TALISMAN_GIVING = 3,
    TALISMAN_INPUTS = 4,
};
#define TALISMAN_NEEDS 3u /* a tier's thresholds: credibility, bond, uptime */

typedef struct {
    uint64_t value;
    char source[TALISMAN_SOURCE_MAX + 1];
    bool missing; /* TM2 */
} talisman_input_t;

typedef struct {
    bool have; /* false at the top tier */
    uint32_t tier;
    char metal[TALISMAN_WORD_MAX + 1];
    char stone[TALISMAN_WORD_MAX + 1];
    uint64_t need[TALISMAN_NEEDS];      /* credibility, bond, uptime bps */
    bool zxvd_missing[TALISMAN_NEEDS];  /* zxvd's own "missing" list */
    int64_t reach_x100[TALISMAN_NEEDS]; /* TL1; TALISMAN_REFUSED past 9(9) */
    int64_t margin[TALISMAN_NEEDS];     /* TL2's inputs (TM2) */
    bool met[TALISMAN_NEEDS];
    bool earned; /* TL2 over the three margins */
} talisman_next_t;

typedef struct {
    uint32_t day;
    uint32_t tier;
} talisman_mark_t;

typedef struct {
    zxm_link_t *link;
    bool have; /* a refresh succeeded */
    uint32_t tier;
    char metal[TALISMAN_WORD_MAX + 1];
    char stone[TALISMAN_WORD_MAX + 1];
    talisman_input_t in[TALISMAN_INPUTS];
    uint64_t effective_cred; /* TM1 */
    talisman_next_t next;
    char perks[TALISMAN_PERKS_MAX][TALISMAN_PERK_MAX + 1];
    uint32_t nperks;
    talisman_mark_t hist[TALISMAN_HISTORY]; /* a ring from hist_at, oldest first */
    uint32_t hist_at, nhist;
    uint32_t dropped;                     /* marks dropped when full (TM4) */
    char notice[TALISMAN_NOTICE_MAX + 1]; /* what the view says (TM3) */
} talisman_t;

/* TM1: credibility + min(giving, floor(credibility / 3)), held at UINT64_MAX. */
uint64_t talisman_effective_cred(uint64_t cred, uint64_t giving);
/* TM2: one input against its threshold: reach x100 (TL1) and margin (TL2). */
void talisman_measure(uint64_t have, bool missing, uint64_t need, bool uptime, int64_t *reach_x100,
                      int64_t *margin);

void talisman_init(talisman_t *t, zxm_link_t *link);
/* Read player.card, measure the next tier, compose the notice and mark the
 * history on `day` (TM1-TM5). */
int talisman_refresh(talisman_t *t, uint32_t day);
/* The i-th mark, oldest first; NULL past the last. */
const talisman_mark_t *talisman_mark(const talisman_t *t, uint32_t i);

#endif
