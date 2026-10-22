/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* colonnade_model.h — Colonnade, the language pillars, as a model (zxm.h):
 * write, compile and run code in the pillars (Sutra, Trebago, Eve, KRON,
 * MYRA) through the Playground, and compare runs of one rule. The rules
 * are colonnade_rules.h.
 *
 * zxvd methods used: play.catalogue, play.run, play.draft and files.read.
 * Nothing else is called and none is invented. No drawing, no allocation,
 * integers only; the time a run takes comes from the caller's clock
 * (milliseconds), since a model makes no system calls.
 *
 *   CM1 THE CATALOGUE: play.catalogue's templates, up to
 *       COLONNADE_TEMPLATES, each its id, title, language, safety, step
 *       count and draft file, and the pillar its language runs in (the
 *       index, colonnade_pillar_of). A template without its id, lang or
 *       steps is not zxvd's (ZXM_E_PROTOCOL), and nothing is kept.
 *   CM2 THE EDITOR holds one file of one template: typed by the person
 *       (colonnade_edit) or opened from the Hoard (colonnade_open, one
 *       files.read of at most COLONNADE_SOURCE_MAX bytes; a longer file is
 *       ZXM_E_TOOBIG and the editor keeps what it had). A name is 1..63
 *       bytes with no '/', as play.run takes it.
 *   CM3 A RUN (colonnade_run): play.run of a template, on scratch state,
 *       never the books, with the editor's file when it is that
 *       template's. Checked first: the catalogue read, room for the run
 *       (COLONNADE_RUNS), a clock, and fuel for the most steps a template
 *       may hold (COLONNADE_STEPS): fuel_left (CL2) at least that, else
 *       ZXM_E_STATE and nothing is sent. Then timed, its verdict and steps
 *       read, one fuel unit spent for each step that ran, and kept. A run
 *       past its limits (CL4: output bytes, ms) is kept as over, its
 *       verdict NEITHER: not used.
 *   CM4 THE FUEL: the owner's budget for the session (fuel_max), set only
 *       on the owner's word (owner_confirmed, checked after the argument
 *       and before anything changes) and never below what is spent.
 *   CM5 A DRAFT (colonnade_draft): play.draft asks the local swarm for the
 *       template's draft file from the person's request (at most 1023
 *       bytes). The draft is untrusted text: kept apart and shown, never
 *       put in the editor and never run by itself.
 *       colonnade_take_draft moves it into the editor only on the owner's
 *       word; from there it is run, and checked by play.run, like anything
 *       the person types.
 *   CM6 WITNESSED RUNS (colonnade_witness): the verdicts of chosen runs
 *       joined with lpres_kjoin (CL1): all agree, that verdict; a TRUE run
 *       and a FALSE run, BOTH, held, each side kept (which runs said
 *       which); an over run counts as NEITHER.
 *   CM7 THE TALLY (colonnade_tally): runs and time per pillar (CL3) over
 *       the kept runs, an over run counted at the limit, and the share of
 *       the output limit a run used (CL6, used_share.used_x100). */
#ifndef COLONNADE_MODEL_H
#define COLONNADE_MODEL_H

#include "colonnade_rules.h"
#include "zxm.h"

#define COLONNADE_NAME         63u   /* an id or a file name (zxvplay.h ZP_MAX_NAME) */
#define COLONNADE_TITLE        127u  /* a template's title, cut on a character */
#define COLONNADE_WORD         15u   /* a language or a safety word */
#define COLONNADE_TEMPLATES    16u   /* the Playground ships 11 */
#define COLONNADE_STEPS        12u   /* zxvplay.h ZP_MAX_STEPS */
#define COLONNADE_RUNS         32u   /* runs kept in a session */
#define COLONNADE_SOURCE_MAX   8191u /* the editor: a request line is at most 16384 bytes */
#define COLONNADE_DRAFT_MAX    8191u /* a draft kept apart */
#define COLONNADE_REQUEST      1023u /* play.draft's request maxLength */
#define COLONNADE_NOTICE       383u
#define COLONNADE_DEFAULT_FUEL INT64_C(1000)

typedef int64_t (*colonnade_clock_fn)(void *ctx); /* milliseconds, any origin */

typedef struct {
    char id[COLONNADE_NAME + 1];
    char title[COLONNADE_TITLE + 1];
    char lang[COLONNADE_WORD + 1];
    char safety[COLONNADE_WORD + 1];
    char draft_file[COLONNADE_NAME + 1]; /* "" when it has no draft */
    int64_t pillar;                      /* CL5; COLONNADE_REFUSED when not in the index */
    uint32_t nsteps;
} colonnade_template_t;

typedef struct {
    uint32_t tmpl;
    int64_t pillar;
    int64_t verdict; /* lpres_state_t; NEITHER when over */
    int64_t steps_ran;
    int64_t ms;       /* as timed, at most COLONNADE_MS_MAX + 1 */
    int64_t outbytes; /* the result's bytes */
    bool over;        /* past CL4 */
    bool edited;      /* ran with the editor's file */
} colonnade_run_t;

typedef struct {
    int64_t verdict; /* lpres_state_t */
    bool held;       /* BOTH: each side kept below */
    uint32_t ntrue, nfalse;
    uint32_t true_side[COLONNADE_RUNS], false_side[COLONNADE_RUNS];
} colonnade_witness_t;

typedef struct {
    zxm_link_t *link;
    colonnade_clock_fn clock;
    void *clock_ctx;
    /* CM1 */
    bool known;
    uint32_t ntemplates;
    colonnade_template_t tmpl[COLONNADE_TEMPLATES];
    /* CM2 */
    int64_t edit_tmpl; /* the template the editor's file is for; -1 none */
    char file[COLONNADE_NAME + 1];
    char text[COLONNADE_SOURCE_MAX + 1];
    uint32_t len;
    /* CM4 */
    int64_t fuel_max, spent;
    /* CM5 */
    bool has_draft, draft_model;
    uint32_t draft_tmpl;
    char draft[COLONNADE_DRAFT_MAX + 1];
    uint32_t draft_len;
    /* CM3 */
    uint32_t nruns;
    colonnade_run_t run[COLONNADE_RUNS];
    char notice[COLONNADE_NOTICE + 1];
} colonnade_t;

void colonnade_init(colonnade_t *c, zxm_link_t *link, colonnade_clock_fn clock, void *clock_ctx);
/* CL5: the pillar a language runs in (a catalogue name such as "COBOL", or a
 * runtime such as "sutra" or the Playground's "treaty"), its place in
 * LANGUAGE_FIT.tsv into *place; COLONNADE_REFUSED when it is not indexed. */
int64_t colonnade_pillar_of(const char *lang, int64_t *place);
/* CM1 */
int colonnade_catalogue(colonnade_t *c);
/* CM2 */
int colonnade_edit(colonnade_t *c, uint32_t tmpl, const char *file, const char *text);
int colonnade_open(colonnade_t *c, uint32_t tmpl, const char *space, const char *path);
/* CM3 */
int colonnade_run(colonnade_t *c, uint32_t tmpl);
/* CM4: ZXM_E_ARG when fuel_max is no budget or below what is spent;
 * ZXM_E_STATE without the owner's word. */
int colonnade_set_fuel(colonnade_t *c, int64_t fuel_max, bool owner_confirmed);
int64_t colonnade_fuel(const colonnade_t *c);
/* CM5 */
int colonnade_draft(colonnade_t *c, uint32_t tmpl, const char *request);
int colonnade_take_draft(colonnade_t *c, bool owner_confirmed);
/* CM6: runs[0..n), each a kept run */
int colonnade_witness(colonnade_t *c, const uint32_t *runs, uint32_t n, colonnade_witness_t *out);
/* CM7 */
int colonnade_tally(colonnade_t *c, int64_t pillar, int64_t *runs, int64_t *ms);
int64_t colonnade_output_x100(const colonnade_t *c, uint32_t run);

#endif
