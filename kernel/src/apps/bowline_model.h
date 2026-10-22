/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bowline_model.h — Bowline, tasks and reminders, as a model (zxm.h):
 * tasks as files in the Hoard, one file each, listed with files.list, read
 * with files.read and written with files.write; when a repeating task is
 * next due (every n days or weeks, moved past the weekdays it skips),
 * which tasks are overdue, how many each list holds, and the reminders
 * that are due. The notices already posted are read with notes.list.
 *
 * zxvd has no method to post a notice, and none is invented: the model
 * composes the reminder text (bowline_t.notice) for the view to show, and
 * counts the unread notices whose source is Bowline. Days are days since
 * 1970-01-01 in UTC, 0 .. SOLSTICE_DAY_MAX (9999-12-31): zxvd has no
 * time-zone method, so the view gives today's day in the owner's zone. No
 * drawing, no allocation, integers only. The rules are bowline_rules.h
 * (BW1-BW3).
 *
 *   BM1 FOLDER. The tasks are the files BOWLINE_DIR/<n>.task, n 1 ..
 *       BOWLINE_ID_MAX in plain decimal digits with no leading zero. Other
 *       names in the folder are not Bowline's and are left alone.
 *   BM2 FILE. "bowline 1\n", then "<list> <start> <due> <every> <unit>
 *       <skip> <done>\n", then "<title>\n", numbers in plain decimal
 *       digits. A task file that does not say this is not read as a task:
 *       it is counted in `skipped` and never overwritten.
 *   BM3 VALID. list 1 .. BOWLINE_LIST_MAX; start and due 0 ..
 *       SOLSTICE_DAY_MAX with start <= due; every 0 .. BOWLINE_EVERY_MAX
 *       (0: once) in units of 1 day or 7 days (a week); skip 0 ..
 *       BOWLINE_SKIP_MAX; done 0 or 1, and a repeating task is never done
 *       (completing it moves it on); a title of 1 .. BOWLINE_TITLE_MAX
 *       bytes, none of them a control byte. The fields fit their ^ZXTASK
 *       record (BW3).
 *   BM4 DUE. A new task is due on its start day moved past the skipped
 *       weekdays (BW1). Completing a repeating task on `today` makes it due
 *       on the first repeat after both today and its present due day
 *       (Solstice's next occurrence, SO3), moved past the skipped weekdays
 *       again: counted from the start, so a moved day never drifts. A task
 *       that repeats past 9999-12-31 has no next day and stays as it is.
 *   BM5 NEW NUMBERS. A new task takes one past the largest number in the
 *       folder, its file written with exclusive set, so no task is ever
 *       written over by another.
 *   BM6 CHECKED FIRST. Every change is checked before anything is asked,
 *       and kept only after zxvd has written it: a refused write leaves
 *       the model as it was. Nothing is written before a load succeeds. */
#ifndef BOWLINE_MODEL_H
#define BOWLINE_MODEL_H

#include "bowline_rules.h"
#include "zxm.h"

#define BOWLINE_DIR        "Bowline" /* in the Hoard's home */
#define BOWLINE_SOURCE     "bowline" /* the source of Bowline's notices */
#define BOWLINE_TASKS      256u
#define BOWLINE_ID_MAX     999999999 /* nine digits (^ZXTASK) */
#define BOWLINE_TITLE_MAX  120u
#define BOWLINE_TEXT_MAX   512u /* one task file */
#define BOWLINE_NOTICE_MAX 1023u
#define BOWLINE_LIST_NAMES 1024u    /* names in the folder one load reads */
#define BOWLINE_NOTES      64u      /* notices one notes.list read takes */
#define BOWLINE_E_FILES    (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

typedef struct {
    int64_t id;
    int64_t list;  /* 1 .. BOWLINE_LIST_MAX */
    int64_t start; /* the day its repeats count from */
    int64_t due;   /* the day it is due */
    int64_t every; /* 0: once */
    int64_t unit;  /* 1 days, 7 weeks */
    int64_t skip;  /* weekdays skipped, bit 0 Monday .. bit 6 Sunday */
    int64_t done;  /* 1 done, 0 open */
    char title[BOWLINE_TITLE_MAX + 1];
} bowline_task_t;

typedef struct {
    zxm_link_t *link;
    bool loaded; /* a load succeeded: tasks may be written (BM6) */
    bool folder; /* BOWLINE_DIR exists */
    bowline_task_t t[BOWLINE_TASKS];
    uint32_t n;
    uint32_t skipped; /* task files that are not tasks (BM2) */
    int64_t last_id;  /* the largest number in the folder (BM5) */
    uint32_t unread;  /* notes.list: unread notices, every source */
    uint32_t ours;    /* of them, Bowline's own */
    char notice[BOWLINE_NOTICE_MAX + 1];
} bowline_t;

/* BM3: may a task say this? */
bool bowline_task_valid(int64_t list, int64_t start, int64_t due, int64_t every, int64_t unit,
                        int64_t skip, int64_t done);
bool bowline_title_ok(const char *title);

/* BM4: the day t is due after it is completed on `today`; ZXM_E_STATE for
 * a task done once and for good, or one with no repeat before 9999-12-31. */
int bowline_next_due(const bowline_task_t *t, int64_t today, int64_t *day);
/* BW2: is t overdue on `today`? */
bool bowline_is_overdue(const bowline_task_t *t, int64_t today);

/* The tasks. */
void bowline_init(bowline_t *b, zxm_link_t *link);
/* Read every task in BOWLINE_DIR; no folder yet is no tasks (ZXM_OK). */
int bowline_load(bowline_t *b);
/* A new open task (BM4, BM5); its number in *id. */
int bowline_add(bowline_t *b, int64_t list, int64_t start, int64_t every, int64_t unit,
                int64_t skip, const char *title, int64_t *id);
/* Task `index` completed on `today`: a single task is done, a repeating
 * one is due again (BM4). */
int bowline_complete(bowline_t *b, uint32_t index, int64_t today);

/* How many open tasks list `list` holds (Witness's count of a kind). */
int bowline_list_count(const bowline_t *b, int64_t list, uint32_t *count);
/* How many open tasks are overdue on `today`. */
int bowline_overdue_count(const bowline_t *b, int64_t today, uint32_t *count);
/* The open tasks due on or before `today`: their number in *n and the
 * reminder text in b->notice, one line each (a line that does not fit is
 * counted, not written). */
int bowline_reminders(bowline_t *b, int64_t today, uint32_t *n);
/* Read the notices (notes.list): b->unread and b->ours. */
int bowline_notices(bowline_t *b);

#endif
