/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pawprint_model.h — Pawprint, the debugger, as a model (zxm.h): step,
 * break and inspect a Playground program, and read zxvd's events beside
 * it. No drawing, no allocation, integers only; lines and columns count
 * from 1. The rules it runs are pawprint_rules.h (PR1-PR5).
 *
 * zxvd methods used: play.run, log.tail and journal.query. zxvd has no
 * attach, breakpoint, step, stack or process-list method: a run is made
 * by play.run, and the model replays it on its own side; the flags of an
 * attach (whose program it is, whether it is a system process) come from
 * the view, never from zxvd.
 *
 *   PM1 THE PROGRAM. pawprint_open names a Playground template (its id);
 *       pawprint_file opens one of its files with the owner's text (at
 *       most PAWPRINT_FILES, each at most PAWPRINT_TEXT bytes, the name
 *       with no '/' or '\\'), replacing a file of the same name. Opening a
 *       template, or changing a file, ends the run and the attach.
 *   PM2 ATTACH, advisory and owner-confirmed. pawprint_attach asks to
 *       attach with the view's two flags (own, system); nothing is
 *       attached until the owner's word (pawprint_approve). The attach
 *       holds when pawprint_may_attach (PR1: attach.eve and attach.kron,
 *       witnessed) says so with the owner's approval; a program that is
 *       not the owner's, or a system process, is refused (ZXM_E_STATE)
 *       before the owner is asked. pawprint_detach ends it.
 *   PM3 RUN. pawprint_run (attached only) calls play.run with the template
 *       and every opened file, and keeps its verdict (TRUE, FALSE, BOTH or
 *       NEITHER), its headline and each step (name, language, file, ran,
 *       verdict, and the line and column of a refusal). Each step reaches
 *       a number of lines, its instructions: every line of its file when
 *       it ran and was not refused, up to its refusal's line when refused,
 *       none when it never ran or its file is not opened (its lines are not
 *       known). A reply out of bounds leaves the last run as it was.
 *   PM4 STEP AND CONTINUE replay the run from its first step: a step moves
 *       one line, continue moves until a breakpoint stops it or the run
 *       ends. Each line passed in an opened file asks pawprint_breakpoint_hit
 *       (PR2: hit.kron and hit.tb, witnessed) of that file's breakpoints;
 *       a hit counts on the breakpoint, and it stops when its hits pass
 *       its condition. Each move's lines are kept (at most PAWPRINT_MOVES
 *       moves, then ZXM_E_TOOBIG) and the instructions run are their exact
 *       sum (PR3, instructions.tb). At the end, the run's verdict stands;
 *       a refused step ends the replay at its line and column.
 *   PM5 BREAKPOINTS are kept sorted by file, then line, each once, inside
 *       the file's lines; a new one must keep the session inside its
 *       limits (PR4, limits.eve: ZXM_E_TOOBIG past them) and fit its
 *       record (PR5, breakpoint.myra). The stack view is the steps entered
 *       so far, newest first, at most PAW_FRAMES_MAX of them.
 *   PM6 EVENTS. pawprint_follow reads zxvd's live events (log.tail) on
 *       from the last one read, keeping the newest PAWPRINT_EVENTS;
 *       pawprint_history reads a page of the journal kept on disk
 *       (journal.query), newest first, optionally with a text to find.
 *       With no journal, history is empty and says so (journal_on). */
#ifndef PAWPRINT_MODEL_H
#define PAWPRINT_MODEL_H

#include "lpres.h"
#include "pawprint_rules.h"
#include "zxm.h"

#define PAWPRINT_FILES    4u
#define PAWPRINT_TEXT     4096u /* bytes of one opened file */
#define PAWPRINT_NAME     31u   /* a file's or a step's name */
#define PAWPRINT_LANG     15u
#define PAWPRINT_TEMPLATE 63u   /* a template's id (play.run takes 63) */
#define PAWPRINT_STEPS    12u   /* the Playground's most steps */
#define PAWPRINT_MOVES    1024u /* step and continue moves in one replay */
#define PAWPRINT_EVENTS   32u
#define PAWPRINT_PAGE     32u /* journal entries on one page */
#define PAWPRINT_SOURCE   15u
#define PAWPRINT_EVTEXT   160u /* zxvd's own most (ZXVD_LOG_TEXT) */
#define PAWPRINT_HEADLINE 159u
#define PAWPRINT_NOTICE   239u

typedef struct {
    char name[PAWPRINT_NAME + 1];
    char text[PAWPRINT_TEXT + 1];
    uint32_t len;
    int64_t lines;
} pawprint_file_t;

typedef struct {
    char name[PAWPRINT_NAME + 1], lang[PAWPRINT_LANG + 1], file[PAWPRINT_NAME + 1];
    bool ran;
    int32_t verdict;   /* LPRES_STATE_* */
    int64_t line, col; /* of a refusal; 0 when none */
    int64_t reach;     /* PM3: the lines it runs */
    int32_t opened;    /* its file among the opened ones, -1 when not */
} pawprint_step_t;

typedef struct {
    int64_t file, line, condition, hits;
} pawprint_break_t;

typedef struct {
    uint64_t seq, time_ms;
    uint8_t level; /* 0 info, 1 warn, 2 error */
    char source[PAWPRINT_SOURCE + 1];
    char text[PAWPRINT_EVTEXT + 1];
} pawprint_event_t;

typedef struct {
    char step[PAWPRINT_NAME + 1], file[PAWPRINT_NAME + 1];
    int64_t line;
} pawprint_frame_t;

typedef struct {
    zxm_link_t *link;
    char template_id[PAWPRINT_TEMPLATE + 1];
    uint32_t nfiles;
    pawprint_file_t file[PAWPRINT_FILES];
    /* PM2 */
    bool asked, own, system, attached;
    /* PM3: the last run */
    bool have_run;
    int32_t verdict;
    char headline[PAWPRINT_HEADLINE + 1];
    uint32_t nsteps;
    pawprint_step_t step[PAWPRINT_STEPS];
    /* PM4: where the replay is */
    uint32_t at_step;
    int64_t at_line; /* 0: the step not entered yet */
    bool ended, stopped;
    int32_t stopped_at; /* the breakpoint that stopped it, -1 none */
    uint32_t nmoves;
    int64_t moves[PAWPRINT_MOVES];
    int64_t instructions;
    /* PM5 */
    uint32_t nbreaks;
    pawprint_break_t brk[PAW_BREAK_MAX];
    /* PM6 */
    uint64_t next, missed;
    uint32_t nev, first;
    pawprint_event_t ev[PAWPRINT_EVENTS];
    bool journal_on, more;
    uint32_t nhist;
    pawprint_event_t hist[PAWPRINT_PAGE];
    char notice[PAWPRINT_NOTICE + 1];
} pawprint_t;

void pawprint_init(pawprint_t *p, zxm_link_t *link);
/* PM1 */
int pawprint_open(pawprint_t *p, const char *template_id);
int pawprint_file(pawprint_t *p, const char *name, const char *text);
/* PM2 */
int pawprint_attach(pawprint_t *p, bool own, bool system);
int pawprint_approve(pawprint_t *p, bool approved);
void pawprint_detach(pawprint_t *p);
/* PM3 */
int pawprint_run(pawprint_t *p);
/* PM4: ZXM_E_STATE with no run, or when the replay has ended */
int pawprint_step(pawprint_t *p);
int pawprint_continue(pawprint_t *p);
/* PM5: file is its place among the opened files */
int pawprint_break(pawprint_t *p, uint32_t file, int64_t line, int64_t condition);
int pawprint_unbreak(pawprint_t *p, uint32_t file, int64_t line);
/* The stack view, newest first: ZXM_E_TOOBIG when cap is short of it */
int pawprint_stack(const pawprint_t *p, pawprint_frame_t *out, uint32_t cap, uint32_t *n);
/* PM6 */
int pawprint_follow(pawprint_t *p, uint32_t *added);
int pawprint_history(pawprint_t *p, const char *find);
/* The i-th live event kept, oldest first */
const pawprint_event_t *pawprint_event(const pawprint_t *p, uint32_t i);

#endif
