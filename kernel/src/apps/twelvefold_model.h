/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* twelvefold_model.h — Twelvefold, the paraconsistent logic workbench, as a
 * model (zxm.h): a set of formulas over TRUE, FALSE, BOTH and NEITHER, their
 * tables, the count of assignments giving each value, the 64-state
 * hexagram tag, whether a conclusion follows, the Playground's templates
 * and the set kept in the Hoard. No drawing, no allocation, integers only.
 *
 *   TF1 FORMULAS. Text of at most TWELVEFOLD_TEXT_MAX bytes:
 *         formula := either [ "->" formula ]       (to the right)
 *         either  := both { "|" both }
 *         both    := unary { "&" unary }
 *         unary   := "~" unary | atom
 *         atom    := a..z | T | F | B | N | "(" formula ")"
 *       Spaces between tokens are ignored. A variable is one lower-case
 *       letter, T F B N the four constants. "a -> b" is ~a | b, built from
 *       lpres's own negation and disjunction. Anything else is refused
 *       (ZXM_E_ARG) with the byte where it went wrong.
 *   TF2 LIMITS (lp_limits_ok, Eve and KRON): at most TWELVEFOLD_VARS_MAX
 *       distinct variables and at most TWELVEFOLD_DEPTH_MAX connectives
 *       deep (an atom is 0 deep; ~a is 1). A formula past them is refused
 *       before it is evaluated; so is a consequence whose formulas share
 *       more than TWELVEFOLD_VARS_MAX variables between them.
 *   TF3 EVALUATION is lpres_negate, lpres_conjoin and lpres_disjoin, the
 *       kernel's tables on the chain F < N < B < T, never re-derived. An
 *       assignment gives each variable, in letter order, one of the four
 *       codes (NEITHER 0, TRUE 1, FALSE 2, BOTH 3); assignment k gives the
 *       last variable the code k mod 4, the one before it (k / 4) mod 4,
 *       and so on: 4^v assignments in all.
 *   TF4 COUNTS (witness.kind_count): how many assignments give each value.
 *       The formula's own tag is the hexagram (lpres_hex.h) of the meet of
 *       every row (lpres_conjoin over the table: TRUE only when every
 *       assignment gives TRUE), neutral and manifest, with its action
 *       (lpx_action).
 *   TF5 CONSEQUENCE (lp_entail_kept, Sutra and KRON): the premises'
 *       conjunction (TRUE when there are none) and the conclusion on every
 *       assignment of their shared variables; the conclusion follows when
 *       no assignment breaks it (TR4). The first that breaks it is kept as
 *       the counterexample.
 *   TF6 THE PLAYGROUND: play.catalogue lists the engines' templates;
 *       play.run runs one, with at most one edited file, on scratch state.
 *       Its four-valued verdict is read back as a value code, never
 *       widened: BOTH is held for a person, as zxvd says.
 *   TF7 THE HOARD: a set is a text file, the line "# twelvefold 1" then one
 *       formula a line, written whole (files.write) and read back
 *       (files.read). A file with a line that is not a formula, or past the
 *       limits, leaves the open set as it was. */
#ifndef TWELVEFOLD_MODEL_H
#define TWELVEFOLD_MODEL_H

#include "lpres_hex.h" /* the tag's polarities, phases and actions */
#include "twelvefold_rules.h"
#include "zxm.h"

#define TWELVEFOLD_TEXT_MAX     255u
#define TWELVEFOLD_NODES        128u
#define TWELVEFOLD_FORMULAS     16u
#define TWELVEFOLD_ROWS         4096u /* 4^TWELVEFOLD_VARS_MAX */
#define TWELVEFOLD_PATH_MAX     1024u
#define TWELVEFOLD_FILE_MAX     (16u + TWELVEFOLD_FORMULAS * (TWELVEFOLD_TEXT_MAX + 1u))
#define TWELVEFOLD_TEMPLATES    32u
#define TWELVEFOLD_ID_MAX       63u
#define TWELVEFOLD_TITLE_MAX    95u
#define TWELVEFOLD_HEADLINE_MAX 239u
#define TWELVEFOLD_PLAYFILE_MAX 2048u /* bytes of the one edited file */

enum {
    TWELVEFOLD_OP_VAR,
    TWELVEFOLD_OP_CONST,
    TWELVEFOLD_OP_NOT,
    TWELVEFOLD_OP_AND,
    TWELVEFOLD_OP_OR,
    TWELVEFOLD_OP_IMP
};

typedef struct {
    uint8_t op;
    uint8_t val; /* VAR: the letter 0..25; CONST: the value code */
    uint8_t a, b;
} twelvefold_node_t;

typedef struct {
    char text[TWELVEFOLD_TEXT_MAX + 1];
    twelvefold_node_t node[TWELVEFOLD_NODES];
    uint8_t n, root;
    uint8_t depth;
    uint32_t vars; /* bit i: the letter 'a' + i appears */
} twelvefold_formula_t;

typedef struct {
    uint8_t nvars;
    char var[TWELVEFOLD_VARS_MAX]; /* letter order */
    uint32_t rows;                 /* 4^nvars */
    uint8_t value[TWELVEFOLD_ROWS];
    int64_t count[4]; /* by value code (TF4) */
    uint8_t meet;     /* value code */
    uint8_t tag;      /* lpx_t */
    uint8_t action;   /* lpx_action_t */
} twelvefold_table_t;

typedef struct {
    uint8_t nvars;
    char var[TWELVEFOLD_VARS_MAX];
    uint32_t rows;
    int64_t breaks;  /* assignments that break it (TR4) */
    bool follows;    /* breaks == 0 */
    int64_t counter; /* the first that breaks it, or -1 */
    uint8_t premise; /* at the counterexample: the premises' value code */
    uint8_t conclusion;
} twelvefold_verdict_t;

typedef struct {
    char id[TWELVEFOLD_ID_MAX + 1];
    char title[TWELVEFOLD_TITLE_MAX + 1];
    char lang[24];
} twelvefold_template_t;

typedef struct {
    char template_id[TWELVEFOLD_ID_MAX + 1];
    uint8_t verdict; /* value code */
    char headline[TWELVEFOLD_HEADLINE_MAX + 1];
    bool approval; /* a person must approve */
    uint32_t steps, ran;
} twelvefold_play_t;

typedef struct {
    zxm_link_t *link;
    twelvefold_formula_t f[TWELVEFOLD_FORMULAS];
    uint32_t n;
    char space[ZXM_SPACE_MAX + 1];
    char path[TWELVEFOLD_PATH_MAX + 1]; /* "" = not saved yet */
    bool dirty;
    twelvefold_template_t tpl[TWELVEFOLD_TEMPLATES];
    uint32_t ntpl;
    twelvefold_play_t play;
} twelvefold_t;

/* TF1, TF2: parse text into *f; ZXM_E_ARG with *at the byte where it went
 * wrong (or the length, for a formula past the limits). */
int twelvefold_parse(twelvefold_formula_t *f, const char *text, uint32_t *at);
/* TF3: the value code of f with each letter's code in code[0..25]. */
uint8_t twelvefold_eval(const twelvefold_formula_t *f, const uint8_t *code);
/* The connectives' tables (lpres's): op NOT, AND, OR or IMP; a and b
 * value codes (b unused for NOT). 255 for any other op or code. */
uint8_t twelvefold_connective(uint8_t op, uint8_t a, uint8_t b);
/* The hexagram tag of a value code with a polarity and phase (lpres_hex.h
 * LPX_P_*, LPX_PH_*), and its action in *action (when not NULL). */
uint8_t twelvefold_tag(uint8_t code, uint8_t pol, uint8_t phase, uint8_t *action);

void twelvefold_init(twelvefold_t *t, zxm_link_t *link);
int twelvefold_add(twelvefold_t *t, const char *text, uint32_t *at);
int twelvefold_remove(twelvefold_t *t, uint32_t i);
/* TF3, TF4 for formula i. */
int twelvefold_table(const twelvefold_t *t, uint32_t i, twelvefold_table_t *out);
/* TF5: premises prem[0..np) and the conclusion, all indexes into t->f. */
int twelvefold_follows(const twelvefold_t *t, const uint32_t *prem, uint32_t np, uint32_t concl,
                       twelvefold_verdict_t *out);

/* TF6 */
int twelvefold_catalogue(twelvefold_t *t);
/* file and text may be NULL (the template as it is). */
int twelvefold_play(twelvefold_t *t, const char *template_id, const char *file, const char *text);

/* TF7. space: a space id from spaces.list, "" for home. */
int twelvefold_save(twelvefold_t *t); /* to t->path; ZXM_E_STATE when there is none */
int twelvefold_save_as(twelvefold_t *t, const char *space, const char *path);
int twelvefold_open(twelvefold_t *t, const char *space, const char *path);

#endif
