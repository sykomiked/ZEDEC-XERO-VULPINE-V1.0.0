/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* poultice_model.h — Poultice, the care plan, as a model (zxm.h): one
 * person's symptoms, medicines, vitals, carers, medical ID and care costs,
 * kept as one text file in the Hoard, with the carers' notices. No
 * drawing, no allocation, integers only. The rules it runs are
 * poultice_rules.h (PR1-PR6).
 *
 * Clinical: advisory and human-approved. Not a medical device; nothing
 * here diagnoses, and nothing decides or changes a dose. The limits and
 * the safe ranges come from the person's clinician and a carer enters
 * them; every change to the plan or the log takes the owner's confirmed
 * word (checked after the arguments, before anything is sent).
 *
 * zxvd methods used: files.read, files.write, notes.list and
 * notes.mark_read. zxvd has no method to post a notice and none to take a
 * reading from a vitals device: the model composes each carer's notice
 * for the view (notify), and a carer types each reading in.
 *
 *   PM1 THE PLAN is Poultice.plan in the Hoard's home, one line each,
 *       tab-separated: a header "poultice 1 <patient>", then M (a
 *       medicine: id, per-dose maximum, daily maximum, minimum interval in
 *       minutes, start day, every (days, 0 as needed), skipped weekdays,
 *       review day (0 none), name), V (a vital's declared range: kind, lo,
 *       hi), C (a carer: seq, peer, stamp, name), I (a care item: id,
 *       price in minor units, units a day, name), D (a medical ID field:
 *       field, stamp, text as typed) and L (a log entry: kind, day,
 *       minute, item, value, carer). A missing plan is an empty one; a
 *       line that does not fit is refused on load, and the plan is never
 *       written over then. The whole plan is written (files.write) on each
 *       change; a write that fails leaves the model as it was.
 *   PM2 THE PLAN'S LIMITS. A medicine's three limits (PR1) and each
 *       vital's range are set by a carer from the clinician's word.
 *   PM3 A DOSE PROPOSED is measured against its medicine's limits:
 *       poultice_dose_check gives the margins (PR1) and TRUE within, FALSE
 *       not (PR2), NEITHER for a medicine the plan does not have. Advice:
 *       a dose is logged as given whatever it says, and one outside the
 *       limits is escalated to every carer, never changed.
 *   PM4 THE LOG (kind 1 a symptom and its severity 0..10, 2 a dose given,
 *       3 a vital reading 0..1000 in its unit). Every entry holds PR5
 *       (care_node_ok); a heart-rate entry also fits Wellspring's log
 *       (wellspring.log_ok, kind pulse). One carer's entry for one item
 *       and minute is one node: a second is refused.
 *   PM5 TWO CARERS, ONE MINUTE. Readings of one vital at one minute are
 *       joined with lpres_kjoin (PR6): two values are BOTH, both kept, and
 *       an escalation notice is composed for every carer; neither is
 *       dropped. A reading outside its declared range (wellspring.band) is
 *       escalated too.
 *   PM6 TIMES. A repeating dose's next day is solstice.next_occurrence
 *       moved past its skipped weekdays (bowline.unskipped_day); a review
 *       past its day is bowline.overdue; two dose windows (each a dose's
 *       minimum interval from its minute) clash by solstice.events_overlap.
 *   PM7 MEASURES. A vital's week (lowest, highest, floored mean) is
 *       weatherglass.week_temps; the symptom grid, symptom by day, is
 *       cartulary.class_year_cell; a medicine's doses in a week are
 *       wellspring.week's exact total, and so is a month's care cost: each
 *       item's care_month_cost (PR4) over solstice.month_days.
 *   PM8 NOTICES. notes.list counts the unread notices and Poultice's;
 *       notes.mark_read marks every notice read, on the owner's word. */
#ifndef POULTICE_MODEL_H
#define POULTICE_MODEL_H

#include "poultice_rules.h"
#include "weatherglass_model.h"
#include "wellspring_model.h"
#include "zxm.h"

#define POULTICE_PLAN     "Poultice.plan"
#define POULTICE_SOURCE   "poultice"
#define POULTICE_MEDS     16u
#define POULTICE_VITALS   8u /* kinds 1..8, below */
#define POULTICE_CARERS   8u
#define POULTICE_ITEMS    16u
#define POULTICE_FIELDS   6u /* medical ID fields 1..6 (Watchfire's) */
#define POULTICE_LOG      256u
#define POULTICE_NAME_MAX 47u  /* bytes of a name */
#define POULTICE_TEXT_MAX 255u /* bytes of a medical ID field */
#define POULTICE_NOTICE   240u
#define POULTICE_NOTES    64u /* notices notes.list may give */
#define POULTICE_ID_MAX   9999
#define POULTICE_GAP_MAX  99999 /* minutes of a minimum interval */
#define POULTICE_EVERY    9999  /* days between repeating doses */
#define POULTICE_SEVERITY 10
#define POULTICE_MINUTES  1440
#define POULTICE_DAY_LAST INT64_C(999999)

/* a log entry's kind (PM4) */
enum { POULTICE_SYMPTOM = 1, POULTICE_DOSE = 2, POULTICE_VITAL = 3 };
/* a vital's kind and unit */
enum {
    POULTICE_HEART = 1,     /* beats a minute */
    POULTICE_SPO2 = 2,      /* % saturation */
    POULTICE_TEMP = 3,      /* tenths of a degree Celsius */
    POULTICE_BREATH = 4,    /* breaths a minute */
    POULTICE_SYSTOLIC = 5,  /* mmHg */
    POULTICE_DIASTOLIC = 6, /* mmHg */
    POULTICE_GLUCOSE = 7,   /* mg/dL */
    POULTICE_WEIGHT = 8     /* whole kg */
};

typedef struct {
    int64_t id, perdose, daymax, gap, start, every, skip, review;
    char name[POULTICE_NAME_MAX + 1];
} poultice_med_t;

typedef struct {
    int64_t seq, peer, stamp;
    char name[POULTICE_NAME_MAX + 1];
} poultice_carer_t;

typedef struct {
    int64_t id, price, perday;
    char name[POULTICE_NAME_MAX + 1];
} poultice_item_t;

typedef struct {
    int64_t kind, day, minute, item, value, carer;
} poultice_entry_t;

typedef struct {
    bool set;
    int64_t lo, hi;
} poultice_range_t;

typedef struct {
    bool set;
    int64_t stamp;
    char text[POULTICE_TEXT_MAX + 1];
} poultice_field_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    int64_t patient;
    poultice_med_t med[POULTICE_MEDS];
    uint32_t nmed;
    poultice_range_t range[POULTICE_VITALS + 1u]; /* by vital kind */
    poultice_carer_t carer[POULTICE_CARERS];
    uint32_t ncarer;
    poultice_item_t item[POULTICE_ITEMS];
    uint32_t nitem;
    poultice_field_t field[POULTICE_FIELDS + 1u]; /* by field */
    poultice_entry_t log[POULTICE_LOG];
    uint32_t nlog;
    uint32_t unread, ours; /* PM8 */
    char notice[POULTICE_NOTICE];
    char escalate[POULTICE_CARERS][POULTICE_NOTICE]; /* PM5: one for each carer */
    uint32_t nescalate;
} poultice_t;

void poultice_init(poultice_t *p, zxm_link_t *link);
/* PM1 */
int poultice_load(poultice_t *p);
int poultice_set_patient(poultice_t *p, int64_t patient, bool owner_confirmed);
/* PM2: a medicine, added or replaced by its id; a vital's range */
int poultice_set_med(poultice_t *p, const poultice_med_t *m, bool owner_confirmed);
int poultice_set_range(poultice_t *p, int64_t vital, int64_t lo, int64_t hi, bool owner_confirmed);
int poultice_add_carer(poultice_t *p, int64_t seq, int64_t peer, int64_t stamp, const char *name,
                       bool owner_confirmed);
int poultice_set_item(poultice_t *p, int64_t id, int64_t price, int64_t perday, const char *name,
                      bool owner_confirmed);
int poultice_set_field(poultice_t *p, int64_t field, int64_t stamp, const char *text,
                       bool owner_confirmed);
int32_t poultice_find_med(const poultice_t *p, int64_t id);
/* PM3: margin[3] (PR1) */
lpres_state_t poultice_dose_check(const poultice_t *p, int64_t med, int64_t dose, int64_t day,
                                  int64_t minute, int64_t margin[3]);
/* PM4, PM5 */
int poultice_log_entry(poultice_t *p, int64_t kind, int64_t day, int64_t minute, int64_t item,
                       int64_t value, int64_t carer, bool owner_confirmed);
lpres_state_t poultice_reading(const poultice_t *p, int64_t vital, int64_t day, int64_t minute);
/* PM6 */
int64_t poultice_next_dose_day(const poultice_t *p, int64_t med, int64_t today);
bool poultice_review_overdue(const poultice_t *p, int64_t med, int64_t today);
bool poultice_windows_clash(const poultice_t *p, int64_t meda, int64_t at_a, int64_t medb,
                            int64_t at_b);
/* PM7 */
int poultice_vital_week(const poultice_t *p, int64_t vital, int64_t first, int64_t out[3]);
int64_t poultice_symptom_cell(const poultice_t *p, uint32_t i);
int poultice_dose_week(const poultice_t *p, int64_t med, int64_t first, int64_t *total);
int poultice_month_total(const poultice_t *p, int64_t year, int64_t month, int64_t *total);
/* PM8 */
int poultice_notices(poultice_t *p);
int poultice_mark_read(poultice_t *p, uint32_t *marked, bool owner_confirmed);

#endif
