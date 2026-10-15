/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* brazier_model.h — Brazier, power, battery and heat, as a model (zxm.h):
 * reads zxvd's energy ledger (power.status, docs/POWER.md) and the machine's
 * facts (sys.info) and keeps what a view shows: the battery's level and
 * charge state, the device's draw, minutes left at that draw, and the
 * battery's temperature against a warning and a critical limit. Read only.
 * No drawing, no allocation, integers only (levels x100: 7140 = 71.40%;
 * temperatures in tenths of a degree C: 452 = 45.2 C).
 *
 *   BZ1 Every reading comes from zxvd. One zxvd does not give is unknown
 *       (BRAZIER_UNKNOWN, or has_temp false), never guessed: zxvd has no
 *       thermal zones, so the battery's own temperature is the only heat.
 *   BZ2 Warnings are advice only. brazier_refresh composes the notice
 *       (brazier_t.notice) for the view; zxvd has no notification method,
 *       and Brazier stops nothing.
 *   BZ3 A refresh is all or nothing: a reply that is not what the method
 *       promises (ZXM_E_PROTOCOL) or a refusal (ZXM_E_REMOTE) leaves the
 *       last good reading as it was. */
#ifndef BRAZIER_MODEL_H
#define BRAZIER_MODEL_H

#include "zxm.h"

/* The temperature limits, tenths of a degree C, both ends inside: the
 * usual lithium-ion ranges, charging 0.0..45.0 C (no warning inside it)
 * and discharging -20.0..60.0 C (critical outside it). Defaults taken;
 * blocks/brazier/heat_ok.eve and heat_safe.eve say the same. */
#define BRAZIER_WARN_LO 0
#define BRAZIER_WARN_HI 450
#define BRAZIER_CRIT_LO (-200)
#define BRAZIER_CRIT_HI 600

#define BRAZIER_CHARGE_MAX 999999999 /* mWh or mW, nine digits (Sutra's 9(9)) */
#define BRAZIER_UNKNOWN    (-1)
#define BRAZIER_NOTICE_MAX 383u
#define BRAZIER_WORD_MAX   15u

enum {
    BRAZIER_HEAT_UNKNOWN = 0, /* no temperature reported */
    BRAZIER_HEAT_OK = 1,
    BRAZIER_HEAT_WARN = 2,     /* outside BRAZIER_WARN_LO..HI */
    BRAZIER_HEAT_CRITICAL = 3, /* outside BRAZIER_CRIT_LO..HI */
};

typedef struct {
    bool present; /* false: no battery (a desktop, or not reported) */
    bool on_mains;
    bool charging;
    uint64_t now_mwh, full_mwh, design_mwh;
    int64_t rate_mw; /* > 0 discharging, < 0 charging, 0 idle */
    uint32_t cycles;
    int64_t level_x100;      /* of full_mwh; BRAZIER_UNKNOWN when not known */
    int64_t minutes_left;    /* to empty at rate_mw; BRAZIER_UNKNOWN when not draining */
    int64_t minutes_to_full; /* zxvd's own figure; BRAZIER_UNKNOWN when not charging */
    bool has_temp;
    int32_t temp_dc; /* tenths of a degree C, when has_temp */
    int32_t heat;    /* BRAZIER_HEAT_* */
} brazier_battery_t;

typedef struct {
    zxm_link_t *link;
    bool have;        /* a refresh succeeded */
    uint64_t time_ms; /* zxvd's clock at the last refresh (sys.info) */
    uint32_t cpus;
    uint64_t draw_mw;                   /* the device's power over the last interval */
    char device[BRAZIER_WORD_MAX + 1];  /* desktop, laptop, mobile, server */
    char source[BRAZIER_WORD_MAX + 1];  /* none, metered, attributed, conflict, fault */
    char posture[BRAZIER_WORD_MAX + 1]; /* perform, balanced, conserve, critical */
    brazier_battery_t bat;
    char notice[BRAZIER_NOTICE_MAX + 1]; /* advice for the view (BZ2) */
} brazier_t;

/* The rules, as the app's logic says them (brazier.zxapp). */

/* floor(10000 * now / full), as used_share.used_x100 with total = full and
 * left = full - now; BRAZIER_UNKNOWN when full <= 0, now < 0, now > full
 * or 10000 * now passes INT64_MAX. */
int64_t brazier_level_x100(int64_t now, int64_t full);
/* floor(60 * charge / draw); BRAZIER_UNKNOWN when draw <= 0, charge < 0,
 * or either passes BRAZIER_CHARGE_MAX. */
int64_t brazier_minutes_left(int64_t charge, int64_t draw);
/* temp inside BRAZIER_WARN_LO..HI: no warning. */
bool brazier_heat_ok(int64_t temp);
/* temp inside BRAZIER_CRIT_LO..HI: short of critical. */
bool brazier_heat_safe(int64_t temp);
/* BRAZIER_HEAT_OK, _WARN or _CRITICAL for one temperature. */
int32_t brazier_heat(int64_t temp);

/* The readings. */
void brazier_init(brazier_t *b, zxm_link_t *link);
/* Read sys.info, then power.status, and compose the notice (BZ1-BZ3). */
int brazier_refresh(brazier_t *b);

#endif
