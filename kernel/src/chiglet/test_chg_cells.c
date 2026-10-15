/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_chg_cells.c — the model-cell table and its resolver (chg_cells.h C1-C3). */
#include <stdio.h>
#include <string.h>

#include "chg_cells.h"

static int g_pass, g_fail;

static void check(int ok, const char *what)
{
    if (ok)
        g_pass++;
    else {
        g_fail++;
        printf("FAIL: %s\n", what);
    }
}

static bool yes(void *ctx, const chg_cell_t *c)
{
    (void) c;
    (*(int *) ctx)++;
    return true;
}

#ifndef CHG_CELLS_FIXTURE
static bool only_spinewyrm(void *ctx, const chg_cell_t *c)
{
    (void) ctx;
    return strcmp(c->name, "Spinewyrm") == 0;
}

static const chg_cell_t *named(const char *n)
{
    for (uint32_t i = 0; i < chg_cells_n; i++)
        if (strcmp(chg_cells[i].name, n) == 0) return &chg_cells[i];
    return NULL;
}

static void table(void)
{
    check(chg_cells_n == 80u, "C1: 80 cells");
    check(chg_cell_organs_n == 21u, "C2: 21 organs");
    bool sorted = true, words = true, found = true, organs = true, texts = true;
    uint32_t runnable = 0;
    for (uint32_t i = 0; i < chg_cells_n; i++) {
        const chg_cell_t *c = &chg_cells[i];
        if (i && chg_cells[i - 1].coord >= c->coord) sorted = false;
        if ((c->coord >> 56) != CHG_CELL_DOMAIN || (c->coord & 0xFFFFFFFFu) != 0 ||
            ((c->coord >> 32) & 0xFFu) == 0)
            words = false;
        if (chg_cell_find(c->coord) != c) found = false;
        if (!chg_cell_organ(c->coord)) organs = false;
        if (!c->name[0] || !c->engine[0] || c->placement > CHG_CELL_PACK_WORKSTATION) texts = false;
        bool want = strncmp(c->engine, "runs now", 8) == 0 ||
                    strncmp(c->engine, "zt_vision", 9) == 0 ||
                    strncmp(c->engine, "zt_whisper", 10) == 0;
        if (want != c->runnable) texts = false;
        runnable += c->runnable;
    }
    check(sorted, "C1: sorted by coordinate, no coordinate twice");
    check(words, "C2: every word is domain 0x60 with an item and the low 32 bits zero");
    check(found, "C2: every row is found by its own coordinate");
    check(organs, "C2: every row's place names an organ");
    check(texts, "C3: names, engines and placements are set; runnable follows the engine text");
    check(runnable == 21u, "C3: 21 cells run on this build's engines");
}

static void words(void)
{
    const chg_cell_t *s = chg_cell_find(0x60000b0100000000ull);
    check(s && strcmp(s->name, "Spinewyrm") == 0, "C2: Spinewyrm is trunk place 11 item 1");
    check(s && s->placement == CHG_CELL_DISC && s->runnable && !s->retired,
          "Spinewyrm is on the disc");
    check(strcmp(chg_cell_organ(0x60000b0100000000ull), "trunk") == 0, "C2: place 11 is the trunk");
    check(strcmp(chg_cell_organ(0x6000010000000000ull), "arbiter") == 0,
          "C2: place 1 is the arbiter");
    check(strcmp(chg_cell_organ(0x6000150000000000ull), "council") == 0,
          "C2: place 21 is the council");
    check(chg_cell_organ(0x6000000100000000ull) == NULL, "C2: place 0 is no organ");
    check(chg_cell_organ(0x6000160100000000ull) == NULL, "C2: place 22 is no organ");
    check(chg_cell_organ(0x61000b0100000000ull) == NULL, "C2: another domain has no organs");
    /* only an exact word names a cell */
    check(chg_cell_find(0x60000b0100000001ull) == NULL, "C2: a nonzero lang byte is no cell");
    check(chg_cell_find(0x60000b0100010000ull) == NULL, "C2: a nonzero ext is no cell");
    check(chg_cell_find(0x61000b0100000000ull) == NULL, "C2: another domain is no cell");
    check(chg_cell_find(0x60000b0000000000ull) == NULL, "C2: item 0 is no cell");
    check(chg_cell_find(0x6002580400000000ull) == NULL, "C2: place 600 is no cell");
    check(chg_cell_find(0) == NULL && chg_cell_find(~0ull) == NULL, "C2: the ends are no cell");
    check(chg_cell_find(chg_cells[0].coord - 1u) == NULL, "C2: below the first row is no cell");
    check(chg_cell_find(chg_cells[chg_cells_n - 1].coord + 1u) == NULL,
          "C2: above the last row is no cell");
}

static void resolve(void)
{
    const chg_cell_t *c = (const chg_cell_t *) 1;
    int asked = 0;
    check(chg_cell_resolve(0x6002580400000000ull, yes, &asked, &c) == CHG_CELL_NO_MODEL && !c,
          "C3: no cell: no model at that coordinate");
    check(asked == 0, "C3: nothing is asked about a coordinate with no cell");
    check(chg_cell_resolve(0x60000b0100000000ull, NULL, NULL, &c) == CHG_CELL_NOT_INSTALLED && c &&
              strcmp(c->name, "Spinewyrm") == 0,
          "C3: a runnable cell with no weights: not installed");
    check(chg_cell_resolve(0x60000b0100000000ull, yes, &asked, &c) == CHG_CELL_RUNS && asked == 1,
          "C3: a runnable, installed cell runs");
    const chg_cell_t *g = named("Greetwyvern");
    check(g && !g->runnable && strcmp(g->engine, "needs smollm3 arch") == 0,
          "Greetwyvern needs the smollm3 arch");
    asked = 0;
    check(g && chg_cell_resolve(g->coord, yes, &asked, &c) == CHG_CELL_NO_ENGINE && c == g &&
              asked == 0,
          "C3: an engine this build lacks refuses before installation is asked");
    const chg_cell_t *b = named("Bastiondrake");
    check(b && chg_cell_resolve(b->coord, yes, &asked, NULL) == CHG_CELL_NO_ENGINE,
          "C3: a server-runtime cell does not run here");
    const chg_cell_t *v = NULL;
    for (uint32_t i = 0; i < chg_cells_n; i++)
        if (strncmp(chg_cells[i].engine, "zt_whisper", 10) == 0) v = &chg_cells[i];
    check(v && chg_cell_resolve(v->coord, yes, &asked, NULL) == CHG_CELL_RUNS,
          "C3: a zt_whisper cell runs when installed");
    uint32_t runs = 0;
    for (uint32_t i = 0; i < chg_cells_n; i++)
        runs += chg_cell_resolve(chg_cells[i].coord, only_spinewyrm, NULL, NULL) == CHG_CELL_RUNS;
    check(runs == 1, "C3: installed is asked per cell");
    check(strcmp(chg_cell_why_text(CHG_CELL_NO_MODEL), "no model at that coordinate") == 0 &&
              strcmp(chg_cell_why_text(CHG_CELL_NO_ENGINE),
                     "model needs an engine path this build lacks") == 0 &&
              strcmp(chg_cell_why_text(CHG_CELL_NOT_INSTALLED),
                     "model not installed on this machine") == 0 &&
              strcmp(chg_cell_why_text(CHG_CELL_RUNS), "runs") == 0,
          "C3: the reasons in words");
}

#endif

#ifdef CHG_CELLS_FIXTURE
/* Built with -DCHG_CELLS_FIXTURE and without chg_cells_table.c: a small
 * table holding a retired cell, which the generated table does not have yet. */
const char *const chg_cell_organs[] = {"arbiter"};
const uint32_t chg_cell_organs_n = 1u;
const chg_cell_t chg_cells[] = {
    {0x6000010100000000ull, CHG_CELL_DISC, true, false, "Live", "runs now (qwen2)"},
    {0x6000010200000000ull, CHG_CELL_DISC, true, true, "Gone", "runs now (qwen2)"},
    {0x6000010300000000ull, CHG_CELL_SERVER, false, false, "Far", "server runtime"},
};
const uint32_t chg_cells_n = 3u;

static void fixture(void)
{
    const chg_cell_t *c = (const chg_cell_t *) 1;
    int asked = 0;
    check(chg_cell_find(0x6000010200000000ull) == &chg_cells[1],
          "C1: a retired cell keeps its row");
    check(chg_cell_resolve(0x6000010200000000ull, yes, &asked, &c) == CHG_CELL_NO_MODEL && !c &&
              asked == 0,
          "C3: a retired cell is no model, even when installed");
    check(chg_cell_resolve(0x6000010100000000ull, yes, &asked, &c) == CHG_CELL_RUNS &&
              c == &chg_cells[0],
          "C3: its live neighbour runs");
    check(chg_cell_resolve(0x6000010300000000ull, yes, &asked, &c) == CHG_CELL_NO_ENGINE,
          "C3: the far neighbour needs an engine");
    check(chg_cell_find(0x6000010000000000ull) == NULL &&
              chg_cell_find(0x6000010400000000ull) == NULL,
          "C2: either side of a small table is no cell");
}
#endif

int main(void)
{
#ifdef CHG_CELLS_FIXTURE
    fixture();
#else
    table();
    words();
    resolve();
#endif
    printf("%d/%d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
