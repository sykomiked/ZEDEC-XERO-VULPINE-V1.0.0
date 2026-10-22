/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* understory_model.h — Understory, nature budgets, as a model (zxm.h):
 * energy, water and land against budgets the owner sets. The rules are
 * understory_rules.h (UR1-UR5); a budget's limits are Keystone's
 * (keystone_rules.h KS2) and the share of a budget used is Wellspring's
 * progress (wellspring_progress_x100), both used, never written again.
 *
 *   UM1 READ, NEVER GUESSED. understory_refresh reads the power ledger
 *       (power.status: the last interval's energy in microjoules, its
 *       power, where it came from and the posture) and the apps ranked by
 *       energy (power.top, kind "app", at most UNDERSTORY_TOP). Any
 *       refusal, or an answer out of bounds, changes nothing
 *       (ZXM_E_PROTOCOL for a bad answer). Only a whole answer is kept,
 *       and its interval's energy, in whole millijoules, joins the last
 *       UNDERSTORY_SAMPLES readings (the oldest dropped when full).
 *   UM2 ENERGY AGAINST ITS BUDGET. understory_energy reports the rolling
 *       total of the last `width` readings (UR3), its cost at a tariff
 *       (UR1), whether it passed a budget in mJ (UR2) and the share of the
 *       budget used, in hundredths of a percent; it composes the notice
 *       and changes nothing else. Advice only.
 *   UM3 A BUDGET IS PROPOSED, THEN CONFIRMED. understory_propose holds an
 *       energy budget for the device (app NULL) or one app: window ms and
 *       mJ inside Keystone's limits (KS2), an app's no more than the
 *       device's whole. It sends nothing. understory_apply sends
 *       power.budget (max_uj = mJ * 1000, never strict) only with the
 *       owner's word, and only the held budget; the word is spent by one
 *       apply, and a refusal (the "power" feature is off by default) keeps
 *       it held. Understory never controls the grid.
 *   UM4 WATER AND LAND. zxvd has no water or land method, so the owner
 *       enters them: water readings (day, source, litres) and parcels
 *       (number, kind, square metres), each checked (understory_water_ok,
 *       understory_land_ok) and kept in a bounded table; a full table, a
 *       bad value or a repeated parcel refuses the entry. understory_water
 *       totals the litres of days from..to and understory_land the area of
 *       one kind (0 for every kind), exactly, each against a budget (UR2).
 *
 * zxvd has no notification method: the model composes the notice text
 * (understory_t.notice) for the view. No drawing, no allocation, integers
 * only. */
#ifndef UNDERSTORY_MODEL_H
#define UNDERSTORY_MODEL_H

#include "keystone_rules.h"
#include "understory_rules.h"
#include "wellspring_model.h"
#include "zxm.h"

#define UNDERSTORY_TOP        16u /* apps read from power.top */
#define UNDERSTORY_NAME       31u /* power.budget's executable name */
#define UNDERSTORY_WORD       15u /* a source or posture word */
#define UNDERSTORY_SAMPLES    64u /* interval readings kept */
#define UNDERSTORY_WATERS     256u
#define UNDERSTORY_PARCELS    64u
#define UNDERSTORY_NOTICE_MAX 255u
#define UNDERSTORY_UJ_PER_MJ  INT64_C(1000)

typedef struct {
    char name[UNDERSTORY_NAME + 1];
    int64_t uj; /* the last interval's energy */
    int64_t mw; /* its average power */
} understory_app_t;

typedef struct {
    int64_t day, source, litres;
} understory_water_t;

typedef struct {
    int64_t parcel, kind, area;
} understory_parcel_t;

typedef struct {
    bool pending;
    char app[UNDERSTORY_NAME + 1]; /* "" for the device */
    int64_t window;                /* ms */
    int64_t mj;                    /* the budget in a window */
} understory_budget_t;

typedef struct {
    int64_t total_mj; /* rolling total of the readings asked for */
    int64_t cost;     /* at the tariff, minor units */
    bool over;        /* past the budget (UR2) */
    int64_t used_x100;
} understory_report_t;

typedef struct {
    zxm_link_t *link;
    bool have; /* a refresh read everything */
    int64_t total_uj, total_mw;
    char source[UNDERSTORY_WORD + 1];
    char posture[UNDERSTORY_WORD + 1];
    uint32_t napps;
    understory_app_t app[UNDERSTORY_TOP];
    uint32_t nsamples;
    int64_t sample_mj[UNDERSTORY_SAMPLES]; /* oldest first */
    uint32_t nwater;
    understory_water_t water[UNDERSTORY_WATERS];
    uint32_t nland;
    understory_parcel_t land[UNDERSTORY_PARCELS];
    understory_budget_t held; /* UM3 */
    char notice[UNDERSTORY_NOTICE_MAX + 1];
} understory_t;

void understory_init(understory_t *u, zxm_link_t *link);
/* UM1 */
int understory_refresh(understory_t *u);
/* UM2: width 1..nsamples readings, tariff per kWh, budget_mj 1.. */
int understory_energy(understory_t *u, int64_t width, int64_t tariff, int64_t budget_mj,
                      understory_report_t *r);
/* UM3 */
int understory_propose(understory_t *u, const char *app, int64_t window, int64_t mj, int64_t whole);
int understory_apply(understory_t *u, bool owner_confirmed);
void understory_cancel(understory_t *u);
/* UM4 */
int understory_add_water(understory_t *u, int64_t day, int64_t source, int64_t litres);
int understory_add_parcel(understory_t *u, int64_t parcel, int64_t kind, int64_t area);
int understory_water(understory_t *u, int64_t from, int64_t to, int64_t budget, int64_t *litres,
                     bool *over);
int understory_land(understory_t *u, int64_t kind, int64_t budget, int64_t *area, bool *over);

#endif
