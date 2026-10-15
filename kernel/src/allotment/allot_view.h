/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* allot_view.h — what app #075 Crown shows for the Sicilian Crown
 * Allotment. Pure: it reads the allotment and a caller-kept history and
 * fills a view record; the zui screen draws it.
 *
 *   V1  ENTRY VIEW. While jurisdiction_permits is true the screen shows the
 *       header, the subtext and the "Register Sortition Ballot" action
 *       (owner wording 2026-10-15). The action is offered while the epoch
 *       is open and the person has room for a ballot (in FREE mode: holds
 *       none yet); registering is the free civic ballot of allot.h L2
 *       and costs nothing.
 *   V2  READ-ONLY HISTORY (owner rule 2026-10-15). Where the jurisdiction
 *       switch is off (local rules restrict private selection by lot), the
 *       screen never shows an entry prompt: `mode` is ALLOT_VIEW_HISTORY,
 *       `action` is NULL, `can_register` is false, and the past
 *       stewardships in the history are listed read-only.
 *   V3  HISTORY. allot_history_note() records each settlement (epoch,
 *       steward, endowment, link) in a caller-supplied ring, newest last,
 *       oldest dropped when full. Records are copies of settlements anyone
 *       can replay (allot.h R1-R4), so the list is evidence, not a claim.
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_ALLOT_VIEW_H
#define ZXV_ALLOT_VIEW_H

#include "allot.h"

#define ALLOT_UI_HEADER "The Sicilian Crown Allotment"
#define ALLOT_UI_SUBTEXT                                                                           \
    "A mutual discretionary sortition conducted in accordance with Section 6.3 covenants. "        \
    "Selection is non-commercial, interest-free, and confers custodial endowment of available "    \
    "system surplus to the designated steward."
#define ALLOT_UI_ACTION  "Register Sortition Ballot"
#define ALLOT_UI_NO_COST "No consideration required"
#define ALLOT_UI_HISTORY_NOTE                                                                      \
    "Selection by lot is switched off where you are. Past stewardships are shown read-only."
#define ALLOT_UI_CATALOG "The Crown Allotment: Mutual endowment and distribution by sortition."

typedef enum {
    ALLOT_VIEW_ENTRY = 0,  /* V1 */
    ALLOT_VIEW_HISTORY = 1 /* V2: read-only */
} allot_view_mode_t;

/* V3: one past stewardship. */
typedef struct {
    uint64_t epoch;
    uint8_t steward[ALLOT_ID_LEN];
    uint64_t endowment; /* crown_endowment of the settlement */
    uint8_t link[ALLOT_HASH_LEN];
} allot_stewardship_t;

typedef struct {
    allot_stewardship_t *rec; /* caller storage */
    uint32_t cap, len, head;  /* ring: head = index of the oldest */
} allot_history_t;

typedef struct {
    allot_view_mode_t mode;
    const char *header, *subtext;
    const char *action; /* V1: the button label; NULL in V2 */
    const char *note;   /* V1: ALLOT_UI_NO_COST; V2: ALLOT_UI_HISTORY_NOTE */
    bool can_register;  /* V1: open epoch and no ballot held yet */
    uint64_t epoch, ballots, threshold, endowment; /* the current epoch (V1) */
    uint32_t history_len;
} allot_view_t;

/* V2: ALLOT_VIEW_HISTORY unless the jurisdiction switch is on. */
allot_view_mode_t allot_view_mode(const allot_config_t *c);

/* V3 */
allot_status_t allot_history_init(allot_history_t *h, allot_stewardship_t *rec, uint32_t cap);
allot_status_t allot_history_note(allot_history_t *h, const allot_settlement_t *s);
/* The i-th record, oldest first (i < len); NULL outside. */
const allot_stewardship_t *allot_history_at(const allot_history_t *h, uint32_t i);

/* V1/V2: fill `out` for `who` (the person's ballot owner id; NULL = not
 * signed in, so nothing to register). `r` may be NULL in V2. */
allot_status_t allot_view_build(const allot_config_t *c, const allot_t *r, const allot_history_t *h,
                                const uint8_t *who, allot_view_t *out);

#endif /* ZXV_ALLOT_VIEW_H */
