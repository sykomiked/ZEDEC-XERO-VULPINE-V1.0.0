/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* homestead_rules.h — Homestead's rules in C, the reference its Eve, KRON,
 * Forth (Cairndrake), Trebago and MYRA blocks are checked against
 * (blocks/homestead). Pure integer functions, no state: the model
 * (homestead_model.c) and the language test (tests/homestead_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs.
 *
 * Units: a setpoint is an integer in its device's own unit (a light's or
 * a blind's level in percent, a plug's or a lock's 0 or 1, a heater's or
 * an oven's temperature in tenths of a degree), -99999..99999.
 *
 *   HR1 GUARDED KINDS. A lock, a heater and an oven (kind HOMESTEAD_LOCK
 *       and up) are guarded: a command to one needs the owner's approval,
 *       warren.approval_ok (an owner session and the owner's yes, both
 *       exactly 1). owed = guarded - approved is 1 exactly when a guarded
 *       command lacks that approval, else 0 or -1.
 *   HR2 A COMMAND IS ALLOWED (device_cmd_ok) when the setpoint is
 *       inside its device's range, given as two margins, above = setpoint
 *       - lo and below = hi - setpoint, each 0..HOMESTEAD_MARGIN_MAX; no
 *       approval is owed (owed -1..0); and it is not an AI's suggestion
 *       (ai = 0). An AI's suggestion is held for the owner, never run. A
 *       margin past HOMESTEAD_MARGIN_MAX is no setpoint any device has.
 *   HR3 AS A NUMBER (device_cmd_n): 1 when HR2 allows it, else 0;
 *       refused (HOMESTEAD_REFUSED) when a value is no measurement: a
 *       margin past HOMESTEAD_MARGIN_MAX either way, owed outside -1..1,
 *       ai outside 0..1.
 *   HR4 A DEVICE'S ENERGY (device_energy): the exact sum of the readings
 *       in watt-hours whose device is `want`, added from the first;
 *       refused when a partial sum passes 64 bits, never wrapped.
 *   HR5 A DEVICE RECORD of ^ZXHOME (home_device_ok): device
 *       1..HOMESTEAD_DEVICE_MAX, a known kind (HOMESTEAD_LIGHT..
 *       HOMESTEAD_OVEN), lo, hi and setpoint each inside
 *       -HOMESTEAD_POINT_MAX..HOMESTEAD_POINT_MAX.
 *   HR6 A SCENE STEP of ^ZXSCENE (home_scene_ok): a scene of at most 3
 *       digits and a device of at most 4 (MYRA's digits, sign aside) and a
 *       setpoint inside -HOMESTEAD_POINT_MAX..HOMESTEAD_POINT_MAX. The
 *       model keeps only scenes HOMESTEAD_SCENE_FIRST..HOMESTEAD_SCENE_MAX
 *       of kept devices (homestead_model.h HM3). */
#ifndef HOMESTEAD_RULES_H
#define HOMESTEAD_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "warren_rules.h"

#define HOMESTEAD_REFUSED     (-1)
#define HOMESTEAD_POINT_MAX   99999  /* MYRA's i5 */
#define HOMESTEAD_MARGIN_MAX  199998 /* the widest range: -99999..99999 */
#define HOMESTEAD_DEVICE_MAX  9999   /* MYRA's i4 */
#define HOMESTEAD_SCENE_MAX   999    /* MYRA's i3 */
#define HOMESTEAD_SCENE_FIRST 1      /* scene 0 fits the record, the model keeps none */

enum {
    HOMESTEAD_LIGHT = 1,  /* level, percent */
    HOMESTEAD_PLUG = 2,   /* 0 off, 1 on */
    HOMESTEAD_BLIND = 3,  /* open, percent */
    HOMESTEAD_LOCK = 4,   /* 0 unlocked, 1 locked: guarded */
    HOMESTEAD_HEATER = 5, /* tenths of a degree: guarded */
    HOMESTEAD_OVEN = 6,   /* tenths of a degree: guarded */
};
#define HOMESTEAD_KINDS 6

/* HR1 */
static inline bool homestead_guarded(int64_t kind)
{
    return kind >= HOMESTEAD_LOCK && kind <= HOMESTEAD_OVEN;
}

static inline int64_t homestead_owed(int64_t kind, int64_t owner, int64_t approved)
{
    return (homestead_guarded(kind) ? 1 : 0) - (warren_approval_ok(owner, approved) ? 1 : 0);
}

/* HR2 */
static inline bool homestead_command_ok(int64_t above, int64_t below, int64_t owed, int64_t ai)
{
    return above >= 0 && above <= HOMESTEAD_MARGIN_MAX && below >= 0 &&
           below <= HOMESTEAD_MARGIN_MAX && owed >= -1 && owed <= 0 && ai == 0;
}

/* HR3 */
static inline int64_t homestead_command_n(int64_t above, int64_t below, int64_t owed, int64_t ai)
{
    if (above < -HOMESTEAD_MARGIN_MAX || above > HOMESTEAD_MARGIN_MAX ||
        below < -HOMESTEAD_MARGIN_MAX || below > HOMESTEAD_MARGIN_MAX || owed < -1 || owed > 1 ||
        ai < 0 || ai > 1)
        return HOMESTEAD_REFUSED;
    return homestead_command_ok(above, below, owed, ai) ? 1 : 0;
}

/* HR4: 0 and the total in *total, or HOMESTEAD_REFUSED past 64 bits. */
static inline int homestead_energy(const int64_t *device, const int64_t *wh, uint32_t n,
                                   int64_t want, int64_t *total)
{
    int64_t s = 0;
    if ((!device || !wh) && n) return HOMESTEAD_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (device[i] != want) continue;
        if (wh[i] > 0 ? s > INT64_MAX - wh[i] : s < INT64_MIN - wh[i]) return HOMESTEAD_REFUSED;
        s += wh[i];
    }
    *total = s;
    return 0;
}

static inline bool homestead_point(int64_t v)
{
    return v >= -HOMESTEAD_POINT_MAX && v <= HOMESTEAD_POINT_MAX;
}

/* HR5 */
static inline bool homestead_device_ok(int64_t device, int64_t kind, int64_t lo, int64_t hi,
                                       int64_t setpoint)
{
    return device >= 1 && device <= HOMESTEAD_DEVICE_MAX && kind >= HOMESTEAD_LIGHT &&
           kind <= HOMESTEAD_OVEN && homestead_point(lo) && homestead_point(hi) &&
           homestead_point(setpoint);
}

/* HR6 */
static inline bool homestead_scene_ok(int64_t scene, int64_t device, int64_t setpoint)
{
    return scene >= -HOMESTEAD_SCENE_MAX && scene <= HOMESTEAD_SCENE_MAX &&
           device >= -HOMESTEAD_DEVICE_MAX && device <= HOMESTEAD_DEVICE_MAX &&
           homestead_point(setpoint);
}

#endif
