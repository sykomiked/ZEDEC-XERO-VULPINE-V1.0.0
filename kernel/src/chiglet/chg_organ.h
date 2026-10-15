/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_organ.h — ask a Tiamat organ, with a witness from another lineage.
 *
 * The chiglet answers its person through organs (chg_vocab.h V1): cells on
 * one job. This router turns "ask the smith" into one lead answer and one
 * check of it, under the roster's evidence rule (tools/tiamat/
 * tiamat_swarm.json "evidence"): a TRUE or FALSE verdict needs a witness
 * from a different lineage, and a contradiction is never averaged away.
 * Running a cell is the caller's job (the hosted agents, zxv_model_host.h
 * M6, or a test double): this file only picks, charges and joins.
 *
 *   O1 CELLS. The caller lists its cells: organ, an agent number and a
 *      lineage (a different trunk or developer is a different lineage).
 *      For each organ the first listed cell is its lead.
 *   O2 LEAD. The organ's lead answers the prompt, with its evidence head's
 *      verdict (NEITHER when it has no head). No cell for the organ:
 *      CHGO_E_NOCELL, and nothing runs.
 *   O3 WITNESS. When the lead says TRUE or FALSE, the first cell of the
 *      organ's witness organ (chg_vocab V1) whose lineage differs from the
 *      lead's checks the answer; failing that, the first such cell of the
 *      lead's own organ. Same-lineage cells are never asked: their
 *      agreement counts as one voice.
 *   O4 JOIN. The lead and a witness are two sources about one answer, so
 *      they join with lpres_kjoin (the information order, lpres.h): agree
 *      keeps the value, TRUE against FALSE is BOTH. A witness that says
 *      NEITHER adds nothing. Without a witness that spoke, a TRUE or FALSE
 *      lead is reported NEITHER (one voice is not enough), a BOTH lead
 *      stays BOTH, and a NEITHER lead stays NEITHER and asks no witness.
 *   O5 BUDGET. Every call charges the tokens the caller reports, checked
 *      for overflow. A zero budget runs nothing; a lead that ran past the
 *      budget is refused and its answer dropped (CHGO_E_BUDGET); when the lead used the budget up,
 * the witness is not asked, and a witness that ran past it is not counted. Either way O4's "no
 * witness" applies and the charge is the whole budget. O6 TAG. The result carries an lpres_hex tag:
 * the joined base, negentropic when a witness agreed, else neutral, phase potential. Its lpx_action
 * is a hint for the screen (PROCEED: offer it; WAIT: offer it as "not sure"; ESCALATE: show both
 * sides). Nothing here acts: an answer that would change the machine is a held act (intent.h I7)
 * the person accepts.
 *
 * Freestanding C11: no libc, no allocation, no float.
 */
#ifndef ZXV_CHG_ORGAN_H
#define ZXV_CHG_ORGAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../lpres/lpres.h"
#include "../lpres/lpres_hex.h"

#define CHGO_CELLS 128u

enum {
    CHGO_OK = 0,
    CHGO_E_ARG = -1,
    CHGO_E_NOCELL = -2, /* O2 */
    CHGO_E_BUDGET = -3, /* O5 */
    CHGO_E_CELL = -4,   /* a cell failed to run */
    CHGO_E_FULL = -5
};

typedef struct {
    uint8_t organ;    /* CHGV_O_* */
    uint32_t agent;   /* the caller's handle for running it */
    uint32_t lineage; /* O1 */
} chgo_cell_t;

typedef struct {
    chgo_cell_t cell[CHGO_CELLS];
    uint32_t n;
} chgo_table_t;

/* O2: run the lead on the prompt; write a NUL-terminated answer, its
 * verdict and the tokens it used. 0 on success. */
typedef int (*chgo_answer_fn)(void *ctx, const chgo_cell_t *cell, const char *prompt, size_t plen,
                              char *out, size_t cap, lpres_state_t *verdict, uint64_t *tokens);
/* O3: the witness reads the prompt and the lead's answer and gives its
 * verdict on that answer and the tokens it used. 0 on success. */
typedef int (*chgo_check_fn)(void *ctx, const chgo_cell_t *cell, const char *prompt, size_t plen,
                             const char *answer, size_t alen, lpres_state_t *verdict,
                             uint64_t *tokens);

typedef struct {
    lpres_state_t lead;    /* O2 */
    lpres_state_t witness; /* O3: NEITHER when none spoke */
    lpres_state_t verdict; /* O4 */
    bool witnessed;        /* a witness from another lineage spoke */
    int32_t lead_cell;     /* index into the table */
    int32_t witness_cell;  /* -1 when none was asked */
    uint64_t tokens;       /* O5: charged in all */
    lpx_t tag;             /* O6 */
} chgo_result_t;

void chgo_init(chgo_table_t *t);
/* O1: append a cell; CHGO_E_FULL past CHGO_CELLS, CHGO_E_ARG for an
 * unknown organ */
int chgo_add(chgo_table_t *t, uint8_t organ, uint32_t agent, uint32_t lineage);
/* O1: the organ's lead (table index) or -1 */
int chgo_lead(const chgo_table_t *t, uint8_t organ);
/* O3: the witness for that lead (table index) or -1 */
int chgo_witness(const chgo_table_t *t, int lead_cell);

/* O2-O6: ask the organ. out gets the lead's answer. */
int chgo_ask(const chgo_table_t *t, uint8_t organ, const char *prompt, size_t plen, uint64_t budget,
             chgo_answer_fn answer, chgo_check_fn check, void *ctx, char *out, size_t cap,
             chgo_result_t *res);

#endif /* ZXV_CHG_ORGAN_H */
