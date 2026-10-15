/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_tiamat.h — harness H1: talk to several Tiamat cells at once
 * (tools/tiamat/TEST_PLAN.md, "First conversation").
 *
 *   H1 CONFIG. A text file, one directive per line ('#' starts a comment):
 *        slot N PATH                     a GGUF model for slot N (0..3)
 *        cell NAME SLOT LINEAGE ADAPTER KEYWORD...
 *                                        a cell: its dragon name, the slot it
 *                                        runs on, its lineage (cells of one
 *                                        lineage count as one witness), a
 *                                        LoRA adapter file or "-", and the
 *                                        words that route a question to it
 *        log PATH                        append one JSON line per question
 *      tools/tiamat/make_h1_config.py writes it from the model manifest.
 *   H2 ROUTE. The lead is the cell whose keywords occur most often in the
 *      question (case-insensitive), the first cell on a tie. The witness is
 *      the best-scoring cell of another lineage; with none, the answer is
 *      unwitnessed. A stand-in for the fitted router head (server task S7).
 *   H3 WITNESS. When the question is "A times/plus/minus/divided by B"
 *      with whole numbers, the reckoner checks the answer's result exactly
 *      (int64, overflow refused): TRUE or FALSE. That is a proof, so no
 *      model witness is asked. Otherwise the witness is shown one statement,
 *      "The answer to the question '...' is: ...", and asked whether it is
 *      true or false; the earliest of TRUE, FALSE, BOTH or NEITHER in its
 *      reply (any case, whole words) is its verdict, NEITHER when there is
 *      none.
 *   H4 JOIN. The verdicts are joined in the knowledge order (lpres states,
 *      bitwise OR: TRUE with FALSE is BOTH), starting from NEITHER, so the
 *      result does not depend on the order. TRUE: the answer is shown as
 *      checked. NEITHER: it is shown, marked "not checked". FALSE or BOTH:
 *      it is held; the user sees the reasons, and the draft goes only to the
 *      log. A failed integrity check on the lead holds the answer too.
 *   H5 LOG. One JSON line per question: the question, the cells, their
 *      replies, each verdict, the joined verdict and the time in ms.
 */
#ifndef ZXV_TIAMAT_H
#define ZXV_TIAMAT_H

#include <stddef.h>
#include <stdint.h>

#define ZXV_TIAMAT_SLOTS    4u
#define ZXV_TIAMAT_CELLS    16u
#define ZXV_TIAMAT_KEYWORDS 24u
#define ZXV_TIAMAT_NAME     33u
#define ZXV_TIAMAT_WORD     24u
#define ZXV_TIAMAT_PATH     1024u
#define ZXV_TIAMAT_REPLY    4096u

/* lpres states (src/lpres/lpres.h) */
enum { ZXV_TV_NEITHER = 0, ZXV_TV_TRUE = 1, ZXV_TV_FALSE = 2, ZXV_TV_BOTH = 3 };

typedef struct {
    char name[ZXV_TIAMAT_NAME];
    uint32_t slot;
    char lineage[ZXV_TIAMAT_WORD];
    char adapter[ZXV_TIAMAT_PATH]; /* "" = none */
    char kw[ZXV_TIAMAT_KEYWORDS][ZXV_TIAMAT_WORD];
    uint32_t n_kw;
} zxv_tiamat_cell_t;

typedef struct {
    char slot_path[ZXV_TIAMAT_SLOTS][ZXV_TIAMAT_PATH]; /* "" = unused */
    zxv_tiamat_cell_t cell[ZXV_TIAMAT_CELLS];
    uint32_t n_cells;
    char log_path[ZXV_TIAMAT_PATH]; /* "" = no log */
} zxv_tiamat_conf_t;

/* H1. Parse a config text. 0, or -1 with "line N: why" in err. */
int zxv_tiamat_parse(const char *text, zxv_tiamat_conf_t *c, char *err, size_t err_cap);
/* The same, from a file (at most 64 KB). */
int zxv_tiamat_load(const char *path, zxv_tiamat_conf_t *c, char *err, size_t err_cap);

/* H2. The lead's cell index; *witness gets another lineage's cell, or -1. */
uint32_t zxv_tiamat_route(const zxv_tiamat_conf_t *c, const char *q, int *witness);

/* H3. A witness reply's verdict. */
int zxv_tiamat_verdict(const char *reply);
/* H3. The reckoner: ZXV_TV_TRUE or ZXV_TV_FALSE, or -1 when the question is
 * not a two-number sum it can check (or the answer has no whole number).
 * expr (may be NULL) gets "A x B = R". */
int zxv_tiamat_reckon(const char *q, const char *answer, char *expr, size_t expr_cap);

/* H4. The knowledge join. */
static inline int zxv_tiamat_join(int a, int b)
{
    return (a | b) & 3;
}
const char *zxv_tiamat_tv_name(int v);

/* Ask one cell (its agent number is its cell index). Returns 1 with the
 * reply in out; 0 with a note in out (the cell could not answer, e.g. its
 * budget); ZXV_TIAMAT_INTEGRITY when the engine's integrity check failed
 * (out holds the reason). */
#define ZXV_TIAMAT_INTEGRITY (-2)
typedef int (*zxv_tiamat_ask_fn)(void *ctx, uint32_t cell, const char *prompt, char *out,
                                 size_t cap);

typedef struct {
    uint32_t lead;
    int witness;                   /* -1: none */
    int witness_skipped;           /* 1: the reckoner decided, the witness was not asked */
    int lead_rc, witness_rc;       /* the ask_fn results; witness_rc 0 when none */
    int witness_tv, reckon_tv, tv; /* reckon_tv -1: not applicable */
    char lead_reply[ZXV_TIAMAT_REPLY];
    char witness_reply[ZXV_TIAMAT_REPLY];
    char reckon_expr[96];
    uint64_t ms_lead, ms_witness;
} zxv_tiamat_trace_t;

/* H2-H4: route, ask, witness, join, and write what the user sees into out.
 * Returns the joined verdict; 1 in *shown when the answer was shown. */
int zxv_tiamat_answer(const zxv_tiamat_conf_t *c, zxv_tiamat_ask_fn ask, void *ctx, const char *q,
                      zxv_tiamat_trace_t *t, char *out, size_t cap, int *shown);

/* H5. Append the trace as one JSON line. 0, or -1 when the file can't be written. */
int zxv_tiamat_log(const char *path, const char *q, const zxv_tiamat_conf_t *c,
                   const zxv_tiamat_trace_t *t);

#endif
