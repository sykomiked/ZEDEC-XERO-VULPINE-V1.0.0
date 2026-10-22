/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* talewind_model.h — Talewind, interactive stories, as a model (zxm.h): a
 * story graph of passages and choices kept in the Banker, read through by
 * a reader whose flags and age rating ceiling decide which choices are
 * shown, and passages drafted by the swarm and held for the author's
 * approval. The rules are talewind_rules.h. No drawing, no allocation,
 * integers only.
 *
 * zxvd methods used: files.read, files.write and swarm.ask.
 *
 *   TM1 A STORY IS A FILE. One UTF-8 text file in the Banker, read with
 *       files.read and written whole with files.write:
 *         "talewind 1 <story>\n"                     the head, story 1..999999
 *         "p <n> <rating> <text>\n"                  a passage, n 1..64
 *         "c <n> <k> <target> <need> <sets> <label>\n"  choice k of passage n
 *       Passages come first, in rising n; then choices, by passage and in
 *       k order 1..4 with no gap. A choice leads to a passage the file
 *       holds. Text is 1..1023 bytes and a label 1..127, with no byte below
 *       0x20; numbers are plain decimal. Each record fits its MYRA record
 *       (TR4, TR5). Anything else is not a story: the open is refused
 *       (ZXM_E_PROTOCOL) and every change is refused (ZXM_E_STATE) until an
 *       open succeeds, so a file the model cannot read is never overwritten.
 *   TM2 THE AUTHOR'S WORD. Setting a passage, adding a choice, accepting
 *       a draft and rejecting one change kept state, so each takes
 *       owner_confirmed, checked after the argument checks and before
 *       anything is sent: without it ZXM_E_STATE and nothing changes. A
 *       change is written whole and only then kept: a write zxvd refuses
 *       leaves the model as it was. Each kept change raises the story's
 *       version by one.
 *   TM3 DRAFTS ARE HELD (zxm.h M3). talewind_draft asks the swarm for one
 *       passage; the answer is held, never written, with the second it was
 *       made, how long it is held (1..86400 seconds) and the story's
 *       version then. One draft is held at a time. It is written only when
 *       Sieve's rule says a held suggestion may be applied (sieve_rules.h
 *       SR1): the author's accept, time left on the hold, and the story
 *       unchanged since (drift 0). Line breaks in the answer become spaces,
 *       and an answer longer than a passage is cut on a character boundary
 *       (draft.cut says so).
 *   TM4 READING. A reader starts at passage 1 with no flags and the
 *       ceiling the owner allows. A choice is shown when TR1 says so, its
 *       margins from TR2 and TR6; taking a shown choice moves the reader to
 *       its target and sets its flags (TR3). Reading changes nothing kept,
 *       and asks zxvd nothing. */
#ifndef TALEWIND_MODEL_H
#define TALEWIND_MODEL_H

#include "talewind_rules.h"
#include "zxm.h"

#define TALEWIND_PASSAGES  64u   /* TM1: passages 1..64 */
#define TALEWIND_CHOICES   4u    /* TM1: choices leaving one passage */
#define TALEWIND_TEXT_MAX  1023u /* a passage's bytes */
#define TALEWIND_LABEL_MAX 127u  /* a choice's bytes */
#define TALEWIND_PATH_MAX  255u
#define TALEWIND_FILE_MAX  131072u  /* the whole story, every line full */
#define TALEWIND_E_FILES   (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

typedef struct {
    int64_t target; /* a passage, 1..64 */
    int64_t need;   /* flags it needs, 0..65535 */
    int64_t sets;   /* flags it sets, 0..65535 */
    char label[TALEWIND_LABEL_MAX + 1];
} talewind_choice_t;

typedef struct {
    bool present;
    int64_t rating; /* 0..4 */
    char text[TALEWIND_TEXT_MAX + 1];
    uint32_t nchoices;
    talewind_choice_t choice[TALEWIND_CHOICES];
} talewind_passage_t;

/* TM3: the one held draft */
typedef struct {
    bool pending;
    bool cut;        /* the answer was longer than a passage */
    int64_t passage; /* the number it will take, 1..64 */
    int64_t rating;
    int64_t made, ttl, version;
    zxm_cost_t cost; /* zxm.h M4: estimated */
    char text[TALEWIND_TEXT_MAX + 1];
} talewind_draft_t;

typedef struct {
    zxm_link_t *link;
    bool opened; /* an open succeeded: changes may be written (TM1) */
    char path[TALEWIND_PATH_MAX + 1];
    int64_t story;                                     /* 1..999999 */
    int64_t version;                                   /* kept changes since the open (TM2) */
    talewind_passage_t passage[TALEWIND_PASSAGES + 1]; /* [0] unused */
    talewind_draft_t draft;
    /* TM4: the reader */
    bool reading;
    int64_t at;      /* the passage being read */
    int64_t held;    /* the flags held */
    int64_t ceiling; /* the highest rating allowed, 0..4 */
} talewind_t;

void talewind_init(talewind_t *t, zxm_link_t *link);
/* TM1: open the story at path in the Banker (home); a file not made yet
 * is a new, empty story numbered story (1..999999). */
int talewind_open(talewind_t *t, const char *path, int64_t story);
/* TM1, TM2: passage n (1..64) gets rating and text, made or replaced. */
int talewind_set_passage(talewind_t *t, int64_t n, int64_t rating, const char *text,
                         bool owner_confirmed);
/* TM1, TM2: a choice leaving passage n, to target, needing and setting
 * flags; both passages must exist and n must have room. */
int talewind_add_choice(talewind_t *t, int64_t n, int64_t target, int64_t need, int64_t sets,
                        const char *label, bool owner_confirmed);
/* TM3: ask the swarm to draft passage n at rating, from the author's
 * prompt, held from now for ttl seconds. */
int talewind_draft(talewind_t *t, int64_t n, int64_t rating, const char *prompt, int64_t now,
                   int64_t ttl);
/* TM3: the author's accept at now writes the held draft as its passage;
 * ZXM_E_STATE when none is held, it lapsed or the story changed since. */
int talewind_draft_accept(talewind_t *t, int64_t now, bool owner_confirmed);
/* TM3: the author's reject drops it; nothing is sent. */
int talewind_draft_reject(talewind_t *t, bool owner_confirmed);

/* TM4: start reading at passage 1 under ceiling (0..4); ZXM_E_STATE when
 * passage 1 is missing or rated above the ceiling. */
int talewind_read_begin(talewind_t *t, int64_t ceiling);
/* TM4: whether choice k (0-based) of the passage being read is shown. */
bool talewind_shown(const talewind_t *t, uint32_t k);
/* TM4: the choices shown, as 0-based indexes into the passage's choices;
 * their count, or 0 when not reading. */
uint32_t talewind_shown_list(const talewind_t *t, uint32_t out[TALEWIND_CHOICES]);
/* TM4: take choice k; ZXM_E_STATE when it is not shown. */
int talewind_choose(talewind_t *t, uint32_t k);

#endif
