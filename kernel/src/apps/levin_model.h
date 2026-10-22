/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* levin_model.h — Levin, the home wiring planner, as a model (zxm.h): a
 * panel tree of circuits, the loads on each by room, each circuit's
 * current, voltage drop and breaker check, the plan held against the
 * person's readings, the test run after work, the panel's temperature and
 * a permit's fees. Composed of its parts: Tally's rational arithmetic gives
 * a load's exact current, Mockingbird's boot rule judges a test run's five
 * steps, Brazier's limits judge the panel's temperature, and Homestead's
 * rules say whether a command to a device would be allowed (shown, never
 * sent) and total a circuit's energy.
 *
 *   LM1 THE PLAN FILE (LEVIN_FILE, in the Hoard, files.read and
 *       files.write): "levin 1", then one line per circuit, "circuit <id>
 *       <parent> <breaker> <cable> <volts> <metres> <mohm> <limit>", ids
 *       rising (breaker and cable in mA, volts, the cable's one-way length
 *       in metres, milliohms a metre per conductor, the owner's drop limit
 *       in microvolts), then one line per load, "load <circuit> <room>
 *       <watts> <name>", in the order added. Every line ends with "\n";
 *       numbers are plain decimal digits with no leading zero. Anything
 *       else is not Levin's file: the load is refused (ZXM_E_PROTOCOL) and
 *       every change is refused (ZXM_E_STATE) until a load succeeds, so a
 *       file the model cannot read is never overwritten.
 *   LM2 THE PANEL TREE. A circuit is 1..LEVIN_CIRCUIT_MAX, fed from 0 (the
 *       main switch) or a circuit already kept with a lower id (so the
 *       file, ids rising, names a parent before its children), its node
 *       fitting LR6
 *       (panel_circuit_ok). Added on the owner's word (owner_confirmed):
 *       without it ZXM_E_STATE and nothing is written. Checked, written
 *       whole, then kept: a write zxvd refuses leaves the plan as it was.
 *   LM3 LOADS. A load names its circuit, its room (1..LEVIN_ROOM_MAX) and
 *       its watts; the owner's word, as LM2.
 *   LM4 THE CIRCUIT CHECK. A circuit's planned watts (the sum of its
 *       loads), its exact current (LR2, tally.ratio_div), its drop (LR3,
 *       the current rounded up) and its three margins; within its breaker
 *       only when LR1 says so. Advice only: Levin switches nothing.
 *   LM5 THE CIRCUIT MATRIX (LR4): each load's cell, the watts of its
 *       circuit in its room.
 *   LM6 PLAN AGAINST READING (LR7). zxvd has no meter or clamp method: a
 *       circuit's measured draw is typed in by the person. Each circuit
 *       read is lpres_kjoin of the plan and the reading; a disagreement is
 *       BOTH and the circuit is flagged miswired; the plan is lpres_conjoin
 *       of its circuits, never TRUE while one is BOTH.
 *   LM7 THE TEST RUN. Five steps in order (continuity, insulation,
 *       polarity, earth loop, residual-current trip), each a code, 0
 *       passed: passed only when mockingbird.boot_passed says every step
 *       ran and gave 0 in order; the first failed step is named.
 *   LM8 THE PANEL'S TEMPERATURE in tenths of a degree, typed in by the
 *       person: brazier.heat_ok and brazier.heat_safe give ok, a warning
 *       or a critical warning; advice, nothing is switched.
 *   LM9 THE MACHINE'S OWN DRAW (power.status total_mw), and a circuit's
 *       energy over the person's readings (homestead.device_energy). A
 *       permit's fee is LR5, a job's permits summed exactly.
 *
 * zxvd has no notification method: a notice is held in `notice` for the
 * view. No drawing, no allocation, integers only; the rules are
 * levin_rules.h. */
#ifndef LEVIN_MODEL_H
#define LEVIN_MODEL_H

#include "brazier_model.h"
#include "homestead_rules.h"
#include "levin_rules.h"
#include "mockingbird_rules.h"
#include "zxm.h"

#define LEVIN_FILE        "Levin.plan" /* in the Hoard */
#define LEVIN_CIRCUITS    32u
#define LEVIN_LOADS       64u
#define LEVIN_READINGS    128u
#define LEVIN_NAME_MAX    31u
#define LEVIN_TEXT_MAX    8192u
#define LEVIN_NOTICE_MAX  383u
#define LEVIN_PANEL       INT64_C(1) /* the home's panel in the tree */
#define LEVIN_CIRCUIT_MAX INT64_C(999)
#define LEVIN_ROOM_MAX    INT64_C(999)
#define LEVIN_WH_MAX      INT64_C(999999999) /* one energy reading */
#define LEVIN_STEPS       5u                 /* a test run: Mockingbird's five stages */
#define LEVIN_E_FILES     (-32014)           /* zxvd's files refusal (ZXVD_E_FILES) */

typedef struct {
    int64_t id, parent, breaker, cable, volts, metres, mohm, limit;
} levin_circuit_t;

typedef struct {
    int64_t circuit, room, watts;
    char name[LEVIN_NAME_MAX + 1];
} levin_load_t;

/* LM4: what the circuit check found */
typedef struct {
    int64_t watts; /* planned, the sum of its loads */
    rat_t load;    /* exact current, mA */
    int64_t ma;    /* the current rounded up */
    int64_t drop;  /* microvolts */
    int64_t spare, cable, slack;
    bool within;
} levin_report_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    levin_circuit_t c[LEVIN_CIRCUITS];
    uint32_t nc;
    levin_load_t ld[LEVIN_LOADS];
    uint32_t nld;
    /* the person's readings, not part of the plan file */
    bool has_reading[LEVIN_CIRCUITS];
    int64_t measured[LEVIN_CIRCUITS];
    lpres_state_t state[LEVIN_CIRCUITS]; /* LM6, the last verify */
    int64_t tested[LEVIN_CIRCUITS];      /* LM7: -1 none, 0 failed, 1 passed */
    int64_t en_circuit[LEVIN_READINGS], en_wh[LEVIN_READINGS];
    uint32_t nen;
    int64_t draw_mw; /* LM9: -1 until read */
    char notice[LEVIN_NOTICE_MAX + 1];
} levin_t;

void levin_init(levin_t *v, zxm_link_t *link);
/* LM1: read the plan; a plan not made yet is an empty one. */
int levin_load(levin_t *v);
/* The place of circuit id in the plan, or -1. */
int32_t levin_find(const levin_t *v, int64_t id);

/* LM2 */
int levin_add_circuit(levin_t *v, const levin_circuit_t *c, bool owner_confirmed);
/* LM3 */
int levin_add_load(levin_t *v, int64_t circuit, int64_t room, int64_t watts, const char *name,
                   bool owner_confirmed);

/* LM4 */
int levin_check(levin_t *v, int64_t circuit, levin_report_t *out);
/* LM5: one cell per load, in the order added, into cell (cap). */
int levin_matrix(const levin_t *v, int64_t *cell, uint32_t cap, uint32_t *n);

/* LM6: the person's reading of a circuit's draw, in watts. */
int levin_reading(levin_t *v, int64_t circuit, int64_t watts);
/* LM6: every circuit held against its reading; *plan the plan's state,
 * *miswired how many circuits are BOTH. */
int levin_verify(levin_t *v, lpres_state_t *plan, uint32_t *miswired);

/* LM7: code[LEVIN_STEPS] in order; *failed the first failed step, or
 * LEVIN_STEPS when it passed. */
int levin_test_run(levin_t *v, int64_t circuit, const int64_t *code, bool *passed,
                   uint32_t *failed);
/* LM7: the step's name for the person. */
const char *levin_step_name(uint32_t step);

/* LM8: BRAZIER_HEAT_OK, _WARN or _CRITICAL for the panel's temperature. */
int32_t levin_panel_heat(levin_t *v, int64_t temp_dc);

/* LM9 */
int levin_refresh_draw(levin_t *v);
int levin_energy(levin_t *v, int64_t circuit, int64_t wh);
int levin_circuit_energy(const levin_t *v, int64_t circuit, int64_t *wh);
/* Whether turning a Homestead device of `kind` (range lo..hi) to `setpoint`
 * would be allowed for the owner (owner, approved: warren.approval_ok's
 * flags): homestead.device_cmd_ok, shown, never sent. */
bool levin_device_cmd_shown(int64_t kind, int64_t lo, int64_t hi, int64_t setpoint, int64_t owner,
                            int64_t approved);
/* LR5 over n permits: *total their fees, exact; refused on overflow. */
int levin_permits(const int64_t *base, const int64_t *inspections, const int64_t *visit, uint32_t n,
                  int64_t *total);

#endif
