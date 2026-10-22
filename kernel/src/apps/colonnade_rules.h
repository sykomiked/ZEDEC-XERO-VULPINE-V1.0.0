/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* colonnade_rules.h — Colonnade's rules in C, the reference its Eve, KRON,
 * Sutra, Trebago and MYRA blocks are checked against (blocks/colonnade).
 * Pure integer functions, no state: the model (colonnade_model.c) and the
 * language test (tests/colonnade_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   CL1 WITNESSED RUNS. Each run of one rule (in one pillar or another) is
 *       a source about it: its verdict is a four-valued state (lpres.h:
 *       NEITHER 0, TRUE 1, FALSE 2, BOTH 3). The runs are joined with the
 *       knowledge join (lpres_kjoin): all agree, that verdict; a TRUE side
 *       and a FALSE side, BOTH, held for the person with each side kept;
 *       none decided, NEITHER. witness_split is the BOTH the blocks
 *       witness: sawtrue and sawfalse each a flag exactly 1.
 *   CL2 FUEL LEFT: fuelmax - spent, each 0..COLONNADE_FUEL_MAX (18
 *       digits); spending past fuelmax is refused, never below zero.
 *   CL3 RUNS AND TIME PER PILLAR, exact: how many kept runs ran in pillar
 *       want, and the sum of their milliseconds. A pillar outside 0..5 or
 *       a run outside 0..COLONNADE_MS_MAX refuses the whole total.
 *   CL4 A RUN'S LIMITS: output 0..COLONNADE_OUT_MAX bytes (zxvd's
 *       files.read read_max) and 0..COLONNADE_MS_MAX ms (a minute: zxvd
 *       sets no wall-clock limit on play.run, the default taken here).
 *   CL5 THE LANGUAGE INDEX RECORD of ^ZXCOLONNADE: a language's place in
 *       LANGUAGE_FIT.tsv at most 3 digits and its pillar 1, sign aside
 *       (blocks/colonnade/pillar.myra). Which pillar a language has is
 *       colonnade_model.c's table, from LANGUAGE_FIT.tsv's runtime column.
 *   CL6 THE OUTPUT SHARE: the share of the output limit a run used,
 *       used_share.used_x100 (floor(10000 * (total - left) / total)). */
#ifndef COLONNADE_RULES_H
#define COLONNADE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"

#define COLONNADE_REFUSED  INT64_C(-1)
#define COLONNADE_FUEL_MAX INT64_C(999999999999999999) /* 9(18) */
#define COLONNADE_OUT_MAX  INT64_C(49152)              /* files.read read_max */
#define COLONNADE_MS_MAX   INT64_C(60000)              /* a minute */

/* the pillars (CL3, CL5): 0 is a Lingua language, run by no pillar */
enum {
    COLONNADE_LINGUA = 0,
    COLONNADE_SUTRA = 1,
    COLONNADE_TREBAGO = 2,
    COLONNADE_EVE = 3,
    COLONNADE_KRON = 4,
    COLONNADE_MYRA = 5,
    COLONNADE_PILLARS = 6
};

/* CL1: the BOTH the blocks witness (flags 0 or 1 only). */
static inline bool colonnade_witness_split(int64_t sawtrue, int64_t sawfalse)
{
    return sawtrue == 1 && sawfalse == 1;
}

/* CL1: the knowledge join of n verdicts, each 0..3; NEITHER for none. */
static inline bool colonnade_verdict_join(const int64_t *v, uint32_t n, lpres_state_t *out)
{
    lpres_state_t acc = LPRES_STATE_NEITHER;
    if (!out || (n && !v)) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (v[i] < LPRES_STATE_NEITHER || v[i] > LPRES_STATE_BOTH) return false;
        acc = lpres_kjoin(acc, (lpres_state_t) v[i]);
    }
    *out = acc;
    return true;
}

/* CL2 */
static inline int64_t colonnade_fuel_left(int64_t fuelmax, int64_t spent)
{
    if (fuelmax < 0 || fuelmax > COLONNADE_FUEL_MAX || spent < 0 || spent > fuelmax)
        return COLONNADE_REFUSED;
    return fuelmax - spent;
}

static inline bool colonnade_pillar_ok(int64_t p)
{
    return p >= COLONNADE_LINGUA && p < COLONNADE_PILLARS;
}

/* CL3: runs in pillar want */
static inline int64_t colonnade_pillar_runs(const int64_t *pillar, int64_t want, uint32_t n)
{
    int64_t runs = 0;
    if (n && !pillar) return COLONNADE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (!colonnade_pillar_ok(pillar[i])) return COLONNADE_REFUSED;
        runs += pillar[i] == want;
    }
    return runs;
}

/* CL3: their time, ms */
static inline int64_t colonnade_pillar_time_ms(const int64_t *pillar, int64_t want,
                                               const int64_t *ms, uint32_t n)
{
    int64_t total = 0;
    if (n && (!pillar || !ms)) return COLONNADE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (!colonnade_pillar_ok(pillar[i]) || ms[i] < 0 || ms[i] > COLONNADE_MS_MAX)
            return COLONNADE_REFUSED;
        if (pillar[i] == want) total += ms[i]; /* at most 2^32 rows of a minute: no overflow */
    }
    return total;
}

/* CL4 */
static inline bool colonnade_play_limits_ok(int64_t outbytes, int64_t ms)
{
    return outbytes >= 0 && outbytes <= COLONNADE_OUT_MAX && ms >= 0 && ms <= COLONNADE_MS_MAX;
}

/* CL5: |v| has at most `digits` digits */
static inline bool colonnade_digits_fit(int64_t v, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits; i++) lim *= 10;
    return v > -lim && v < lim;
}

static inline bool colonnade_pillar_index_fits(int64_t lang, int64_t pillar)
{
    return colonnade_digits_fit(lang, 3) && colonnade_digits_fit(pillar, 1);
}

/* CL6: used_share.used_x100's meaning. */
static inline int64_t colonnade_used_x100(int64_t total, int64_t left)
{
    if (total <= 0 || left < 0 || left > total || total > INT64_MAX / 10000)
        return COLONNADE_REFUSED;
    return (total - left) * 10000 / total;
}

#endif
