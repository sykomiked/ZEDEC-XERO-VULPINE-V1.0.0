/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* escapement_model.h — Escapement, the shared clock, as a model (zxm.h):
 * one time across the owner's devices, and the shifts, timesheets and
 * timed jobs run from it. No drawing, no allocation, integers only. The
 * rules are escapement_rules.h (ER1-ER6) and its parts' own: Treadle's
 * next run, budget and task record, Solstice's weekday and overlap,
 * Tallystick's next document number and Sundial's local minute.
 *
 * zxvd has no method that asks another device or a peer for its clock,
 * and none is invented: this clock is sys.info's time_ms, and the other
 * devices' readings are given to the model, each only from a device on the
 * signed roster (devmesh.devices) or a peer zxvd has verified (net.peers).
 *
 *   EM1 TIME. escapement_refresh reads time_ms; a time before 1970 or past
 *       year 9999 is refused and a failed read keeps the last one. The
 *       roster and the peers are read on their own (escapement_read_roster,
 *       escapement_read_peers); a revoked device and this one are not
 *       sources.
 *   EM2 READINGS. Up to ESCAPEMENT_READINGS - 1 other devices' clocks, read
 *       at the instant this one was (EM1), one each, from a known source,
 *       within a day of this clock (ER1). Refreshing the time clears them.
 *   EM3 VOTE. The offsets (this clock 0, each reading less it) give the
 *       clock's state (ER6): TRUE within the tolerance (0..60000 ms, 500 to
 *       begin with), BOTH past it, NEITHER with no time. With three
 *       devices or more their median offset (ER1) is the vote.
 *   EM4 NOTHING OVERWRITTEN. A vote changes no clock and no reading; the
 *       shared time (escapement_shared_ms) is this clock plus the offset
 *       the owner last accepted (escapement_accept_vote), and accepting
 *       makes the clock TRUE until the next vote.
 *   EM5 SHIFTS. Up to ESCAPEMENT_SHIFTS, each a person's, in a week, from
 *       start to finish (minutes of the week) with an unpaid break. One is
 *       kept only when its node fits (ER5), the person's week stays inside
 *       10080 minutes (ER3) and it clashes with none of that person's
 *       shifts that week (ER4): nobody is booked twice at once.
 *   EM6 TIMESHEETS. A person's week is the sum of ER3 over their shifts. A
 *       timesheet is numbered from the register (tallystick.next_doc_number:
 *       last + 1, nine digits, never restarted) and written to the Hoard as
 *       a new file (never over one), then the register is written.
 *   EM7 JOBS. Up to ESCAPEMENT_JOBS timed jobs, each a Treadle task record
 *       (TR3) with a window and a deadline in ms; none runs until the owner
 *       approves it. A job's state is the clock's state conjoined
 *       (lpres_conjoin) with whether it may run now by the shared time:
 *       approved, inside its window before its deadline (ER2) and its cost
 *       within the owner's budget (TR2). Its next run is TR1 at the local
 *       minute of the owner's zone (SD1). Nothing is run here; the view
 *       shows the notice. */
#ifndef ESCAPEMENT_MODEL_H
#define ESCAPEMENT_MODEL_H

#include "escapement_rules.h"
#include "solstice_rules.h"
#include "sundial_rules.h"
#include "tallystick_rules.h"
#include "treadle_rules.h"
#include "zxm.h"

#define ESCAPEMENT_READINGS   16u  /* devices in one vote, this one included */
#define ESCAPEMENT_ROSTER     32u  /* devices on the roster kept */
#define ESCAPEMENT_DEVICE_HEX 32u  /* a device id: 16 bytes as hex */
#define ESCAPEMENT_NODE_HEX   64u  /* a NodeID */
#define ESCAPEMENT_SHIFTS     128u /* shifts kept */
#define ESCAPEMENT_JOBS       16u  /* timed jobs */
#define ESCAPEMENT_NOTICE_MAX 511u
#define ESCAPEMENT_TOL_MAX    60000 /* ms */
#define ESCAPEMENT_TOL_START  500   /* ms */
#define ESCAPEMENT_WEEK_MAX   9999  /* ER5: a week number, 4 digits */
#define ESCAPEMENT_MS_MAX     (SOLSTICE_SECONDS_MAX * 1000 + 999)
#define ESCAPEMENT_REGISTER   "escapement-timesheets.txt"

typedef struct {
    char id[ESCAPEMENT_NODE_HEX + 1]; /* a device id or a NodeID */
    int64_t read_ms;
} escapement_reading_t;

typedef struct {
    int64_t person, week, shift; /* shift: its number in the person's week, from 1 */
    int64_t start, finish, unpaid;
} escapement_shift_t;

typedef struct {
    int64_t task, start, period, cost; /* a Treadle task: start and period in minutes */
    int64_t open_ms, close_ms, deadline_ms;
    bool approved;
} escapement_job_t;

typedef struct {
    zxm_link_t *link;
    bool have;                                                 /* EM1: a time has been read */
    int64_t now_ms;                                            /* this clock, ms since 1970 */
    char device[ESCAPEMENT_ROSTER][ESCAPEMENT_DEVICE_HEX + 1]; /* active, not this one */
    uint32_t ndevices;
    char peer[ESCAPEMENT_PEERS_MAX][ESCAPEMENT_NODE_HEX + 1];
    uint32_t npeers;
    escapement_reading_t reading[ESCAPEMENT_READINGS - 1u];
    uint32_t nread;
    int64_t tol_ms;
    lpres_state_t clock; /* EM3 */
    bool voted;
    int64_t vote_ms;
    int64_t offset_ms; /* EM4: the offset the owner accepted */
    escapement_shift_t shift[ESCAPEMENT_SHIFTS];
    uint32_t nshift;
    bool register_loaded;
    int64_t last_sheet; /* EM6: the register's last number */
    escapement_job_t job[ESCAPEMENT_JOBS];
    uint32_t njob;
    char notice[ESCAPEMENT_NOTICE_MAX + 1];
} escapement_t;

void escapement_init(escapement_t *e, zxm_link_t *link);

/* EM1 */
int escapement_refresh(escapement_t *e);
int escapement_read_roster(escapement_t *e);
int escapement_read_peers(escapement_t *e);

/* EM2: device `id` read read_ms when this clock read now_ms. */
int escapement_reading(escapement_t *e, const char *id, int64_t read_ms);
int escapement_set_tolerance(escapement_t *e, int64_t tol_ms);

/* EM3, EM4 */
lpres_state_t escapement_vote(escapement_t *e);
int escapement_accept_vote(escapement_t *e);
int64_t escapement_shared_ms(const escapement_t *e); /* -1 before a time */
/* The shared time in a zone `offset` minutes from UTC (-720..840): its
 * weekday (1 Monday .. 7 Sunday) and minute of the week. */
int escapement_week_minute(const escapement_t *e, int64_t offset, int64_t *weekday,
                           int64_t *minute);

/* EM5 */
int escapement_shift_add(escapement_t *e, int64_t person, int64_t week, int64_t start,
                         int64_t finish, int64_t unpaid, uint32_t *index);

/* EM6 */
int escapement_week_total(const escapement_t *e, int64_t person, int64_t week, int64_t *minutes);
int escapement_load_register(escapement_t *e);
int escapement_issue_timesheet(escapement_t *e, int64_t person, int64_t week, int64_t *number);

/* EM7 */
int escapement_job_add(escapement_t *e, int64_t task, int64_t start, int64_t period, int64_t cost,
                       int64_t open_ms, int64_t close_ms, int64_t deadline_ms, uint32_t *index);
int escapement_job_approve(escapement_t *e, uint32_t index);
/* left: the owner's budget left after the job's cost. */
lpres_state_t escapement_job_state(escapement_t *e, uint32_t index, int64_t offset, int64_t left,
                                   int64_t *next_run);

#endif
