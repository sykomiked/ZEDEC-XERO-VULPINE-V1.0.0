/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_organ.c — ask an organ with a witness (chg_organ.h O1-O6). */
#include "chg_organ.h"
#include "chg_vocab.h"

void chgo_init(chgo_table_t *t)
{
    if (t) t->n = 0;
}

int chgo_add(chgo_table_t *t, uint8_t organ, uint32_t agent, uint32_t lineage)
{
    if (!t || organ >= CHGV_ORGANS) return CHGO_E_ARG;
    if (t->n >= CHGO_CELLS) return CHGO_E_FULL;
    chgo_cell_t *c = &t->cell[t->n++];
    c->organ = organ;
    c->agent = agent;
    c->lineage = lineage;
    return CHGO_OK;
}

int chgo_lead(const chgo_table_t *t, uint8_t organ)
{
    if (!t) return -1;
    for (uint32_t i = 0; i < t->n && i < CHGO_CELLS; i++)
        if (t->cell[i].organ == organ) return (int) i;
    return -1;
}

/* the first cell of `organ` whose lineage is not `lineage` */
static int other_lineage(const chgo_table_t *t, uint8_t organ, uint32_t lineage)
{
    for (uint32_t i = 0; i < t->n && i < CHGO_CELLS; i++)
        if (t->cell[i].organ == organ && t->cell[i].lineage != lineage) return (int) i;
    return -1;
}

int chgo_witness(const chgo_table_t *t, int lead_cell)
{
    if (!t || lead_cell < 0 || (uint32_t) lead_cell >= t->n) return -1;
    const chgo_cell_t *l = &t->cell[lead_cell];
    int w = chgv_organ_witness(l->organ);
    if (w < 0) return -1;
    int k = other_lineage(t, (uint8_t) w, l->lineage);
    if (k < 0) k = other_lineage(t, l->organ, l->lineage);
    return k;
}

static bool certain(lpres_state_t s)
{
    return s == LPRES_STATE_TRUE || s == LPRES_STATE_FALSE;
}

static bool valid(lpres_state_t s)
{
    return s == LPRES_STATE_NEITHER || s == LPRES_STATE_TRUE || s == LPRES_STATE_FALSE ||
           s == LPRES_STATE_BOTH;
}

static size_t text_len(const char *s, size_t cap)
{
    size_t n = 0;
    while (n < cap && s[n]) n++;
    return n;
}

int chgo_ask(const chgo_table_t *t, uint8_t organ, const char *prompt, size_t plen, uint64_t budget,
             chgo_answer_fn answer, chgo_check_fn check, void *ctx, char *out, size_t cap,
             chgo_result_t *res)
{
    if (!res) return CHGO_E_ARG;
    res->lead = res->witness = res->verdict = LPRES_STATE_NEITHER;
    res->witnessed = false;
    res->lead_cell = res->witness_cell = -1;
    res->tokens = 0;
    res->tag = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    if (!t || !prompt || !answer || !check || !out || cap == 0 || organ >= CHGV_ORGANS)
        return CHGO_E_ARG;
    out[0] = '\0';
    int lc = chgo_lead(t, organ);
    if (lc < 0) return CHGO_E_NOCELL; /* O2 */
    if (budget == 0) return CHGO_E_BUDGET;
    res->lead_cell = lc;

    lpres_state_t lv = LPRES_STATE_NEITHER;
    uint64_t used = 0;
    if (answer(ctx, &t->cell[lc], prompt, plen, out, cap, &lv, &used) != 0 || !valid(lv)) {
        out[0] = '\0';
        return CHGO_E_CELL;
    }
    out[cap - 1] = '\0';
    if (used > budget) { /* O5: the caller's cell overran what it was given */
        res->tokens = budget;
        out[0] = '\0';
        return CHGO_E_BUDGET;
    }
    res->tokens = used;
    res->lead = lv;

    lpres_state_t verdict = lv == LPRES_STATE_BOTH ? LPRES_STATE_BOTH : LPRES_STATE_NEITHER;
    if (certain(lv) && used < budget) { /* O3 */
        int wc = chgo_witness(t, lc);
        if (wc >= 0) {
            res->witness_cell = wc;
            lpres_state_t wv = LPRES_STATE_NEITHER;
            uint64_t wu = 0;
            if (check(ctx, &t->cell[wc], prompt, plen, out, text_len(out, cap), &wv, &wu) != 0 ||
                !valid(wv))
                return CHGO_E_CELL;
            uint64_t sum;
            if (__builtin_add_overflow(res->tokens, wu, &sum) || sum > budget) {
                res->tokens = budget; /* O5: over budget, so it does not count */
            } else {
                res->tokens = sum;
                res->witness = wv;
                if (wv != LPRES_STATE_NEITHER) { /* O4 */
                    res->witnessed = true;
                    verdict = lpres_kjoin(lv, wv);
                }
            }
        }
    }
    res->verdict = verdict;
    uint8_t pol = res->witnessed && verdict == lv ? LPX_P_NEGENTROPIC : LPX_P_NEUTRAL;
    res->tag = lpx_make(lpx_base_from_lpres(verdict), pol, LPX_PH_POTENTIAL); /* O6 */
    return CHGO_OK;
}
