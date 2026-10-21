/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* keystone_rules.h — Keystone's rules in C, the reference its Eve and KRON
 * blocks are checked against (blocks/keystone). Pure integer functions, no
 * state: the model (keystone_model.c) and the language test
 * (tests/keystone_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs.
 *
 *   KS1 SETTING LIMITS. A screen's brightness and the sound's volume are
 *       percents, 0..100 each; the text size is a percent of the normal
 *       size, 50..400 (half to four times). Each is held to its own range;
 *       nothing relates them.
 *   KS2 AN ENERGY BUDGET. A budget for one app over a rolling window:
 *       window 1..86400000 ms (a day at most; 0 is power.budget's clear,
 *       asked for on its own, never a budget), spend 1..999999999 mJ (what
 *       the app may use in a window; 0 would hold it from the start) and
 *       whole 1..999999999 mJ (the device's budget in the same window), and
 *       no app's spend past the whole. The relation across fields is the
 *       point of the rule: an app may never be given more than the device.
 *   KS3 BUDGET SHARES. The device's budget split across apps by the
 *       owner's weights is Crown's largest-remainder split (crown_allot,
 *       CW2), never written again: the shares sum to the whole exactly, and
 *       each share must then pass KS2.
 *
 * Anything else is refused, never rounded or wrapped. */
#ifndef KEYSTONE_RULES_H
#define KEYSTONE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define KEYSTONE_LEVEL_MAX  INT64_C(100) /* brightness and volume, percent */
#define KEYSTONE_TEXT_MIN   INT64_C(50)  /* text size, percent of normal */
#define KEYSTONE_TEXT_MAX   INT64_C(400)
#define KEYSTONE_WINDOW_MAX INT64_C(86400000)  /* a day, in ms */
#define KEYSTONE_MJ_MAX     INT64_C(999999999) /* 9(9) millijoules, crown_allot's units */
#define KEYSTONE_UJ_PER_MJ  INT64_C(1000)      /* power.budget counts microjoules */

/* KS1: each setting inside its own range. */
static inline bool keystone_setting_ok(int64_t brightness, int64_t volume, int64_t textsize)
{
    return brightness >= 0 && brightness <= KEYSTONE_LEVEL_MAX && volume >= 0 &&
           volume <= KEYSTONE_LEVEL_MAX && textsize >= KEYSTONE_TEXT_MIN &&
           textsize <= KEYSTONE_TEXT_MAX;
}

/* KS2: one app's energy budget inside its limits and inside the device's. */
static inline bool keystone_energy_budget_ok(int64_t window, int64_t spend, int64_t whole)
{
    return window >= 1 && window <= KEYSTONE_WINDOW_MAX && spend >= 1 && spend <= KEYSTONE_MJ_MAX &&
           whole >= 1 && whole <= KEYSTONE_MJ_MAX && spend <= whole;
}

#endif
