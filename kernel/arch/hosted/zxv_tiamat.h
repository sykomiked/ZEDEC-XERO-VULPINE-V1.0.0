/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_tiamat.h — harness H1: talk to several Tiamat cells at once
 * (tools/tiamat/TEST_PLAN.md, "First conversation").
 *
 *   H1 CONFIG. A text file, one directive per line ('#' starts a comment):
 *        slot N PATH [SHA256]            a GGUF model for slot N (0..11), and
 *                                        optionally its pinned SHA-256 (64 hex),
 *                                        which idle time re-checks (zxv_idle.h)
 *        cell NAME SLOT LINEAGE ADAPTER KEYWORD...
 *                                        a cell: its dragon name, the slot it
 *                                        runs on, its lineage (cells of one
 *                                        lineage count as one witness), a
 *                                        LoRA adapter file or "-", and the
 *                                        words that route a question to it
 *        log PATH                        append one JSON line per question
 *        delegate                        answer through H6 (below)
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
 *      When the reckoner found the lead's sum wrong, the exact result is
 *      the answer the user sees, first, marked as exact arithmetic; the
 *      lead's draft is still held (exact_shown 1 in the trace).
 *   H5 LOG. One JSON line per question: the question, the cells, their
 *      replies, each verdict, the joined verdict and the time in ms.
 *   H6 DELEGATE (swarm_deleg.h). Orchestration is a position, held for a
 *      question by the cell whose keywords match it best (its interaction
 *      circle: every cell with its H2 score and lineage). That cell is
 *      asked to split the question into at most 4 questions that can be
 *      answered on their own, one per line, or to say NONE. Each part opens
 *      its own circle: the position passes to the part's best match (the
 *      holder keeps it on a tie it is in; a tie across lineages is BOTH,
 *      logged), and the part runs H2-H4 with that cell as its lead. The
 *      parts are leaves of one delegation tree under the holder: the
 *      whole's verdict is the conjoin of theirs (every part must hold; a
 *      part past the fourth is not run and counts NEITHER), and every BOTH
 *      stays in the tree. The tree is sealed once every part is closed.
 *      No split (NONE, one line, or the holder could not answer): the
 *      question runs H2-H4 as one part, exactly as without H6. The host's
 *      budget gate prices every ask, so the tree reserves no tokens of its
 *      own (estimates 0). Each hand-off is returned for the host's
 *      receipts.
 */
#ifndef ZXV_TIAMAT_H
#define ZXV_TIAMAT_H

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include "swarm_deleg.h"

#define ZXV_TIAMAT_SLOTS    12u
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
    char slot_sha[ZXV_TIAMAT_SLOTS][65];               /* "" = not pinned */
    zxv_tiamat_cell_t cell[ZXV_TIAMAT_CELLS];
    uint32_t n_cells;
    char log_path[ZXV_TIAMAT_PATH]; /* "" = no log */
    int delegate;                   /* 1: answer through H6 */
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
    int exact_shown;               /* 1: the lead's sum was wrong; the exact result was shown */
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

/* H3-H4 on a given answer (as if the routed lead had written it): the
 * test harness plants answers this way (tools/tiamat/TEST_PLAN.md T2). */
int zxv_tiamat_check(const zxv_tiamat_conf_t *c, zxv_tiamat_ask_fn ask, void *ctx, const char *q,
                     const char *answer, zxv_tiamat_trace_t *t, char *out, size_t cap, int *shown);

/* H5. Append the trace as one JSON line. 0, or -1 when the file can't be written. */
int zxv_tiamat_log(const char *path, const char *q, const zxv_tiamat_conf_t *c,
                   const zxv_tiamat_trace_t *t);
/* The same JSON line to f, with what the user saw ("seen", omitted when null). */
void zxv_tiamat_write(FILE *f, const char *q, const zxv_tiamat_conf_t *c,
                      const zxv_tiamat_trace_t *t, const char *seen);

/* ---- H6 ---- */
#define ZXV_TIAMAT_PARTS  SWD_FANOUT
#define ZXV_TIAMAT_PART_Q 512u

typedef struct {
    swd_handoff_t take; /* the position for the whole question (from SWD_NONE) */
    uint32_t holder;    /* the cell that holds it */
    int split_rc;       /* the holder's ask result for the split */
    char split_reply[ZXV_TIAMAT_REPLY];
    uint32_t n_parts; /* 0: not split, part[0] is the whole question */
    uint32_t n_lost;  /* parts past the fourth, not run */
    char part_q[ZXV_TIAMAT_PARTS][ZXV_TIAMAT_PART_Q];
    swd_handoff_t hand[ZXV_TIAMAT_PARTS];
    uint32_t node[ZXV_TIAMAT_PARTS];
    zxv_tiamat_trace_t part[ZXV_TIAMAT_PARTS];
    int part_shown[ZXV_TIAMAT_PARTS];
    swd_tree_t tree; /* sealed: tree.root_tv is tv */
    uint32_t n_conflicts;
    int tv;
} zxv_tiamat_deleg_t;

/* H6: the position, the split, the parts, the tree, and what the user sees
 * into out. Returns the whole's verdict; 1 in *shown when it is TRUE or
 * NEITHER (held parts show only their reasons, never their drafts). */
int zxv_tiamat_delegate(const zxv_tiamat_conf_t *c, zxv_tiamat_ask_fn ask, void *ctx, const char *q,
                        zxv_tiamat_deleg_t *d, char *out, size_t cap, int *shown);
/* H5 for H6: one JSON line with the position, the split and every part. */
int zxv_tiamat_log_deleg(const char *path, const char *q, const zxv_tiamat_conf_t *c,
                         const zxv_tiamat_deleg_t *d);
void zxv_tiamat_write_deleg(FILE *f, const char *q, const zxv_tiamat_conf_t *c,
                            const zxv_tiamat_deleg_t *d, const char *seen);

#endif
