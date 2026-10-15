/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_cells.c — resolve a model coordinate to its cell (chg_cells.h C1-C3). */
#include "chg_cells.h"

#include <stddef.h>

const chg_cell_t *chg_cell_find(uint64_t coord)
{
    uint32_t lo = 0, hi = chg_cells_n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (chg_cells[mid].coord == coord) return &chg_cells[mid];
        if (chg_cells[mid].coord < coord)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return NULL;
}

const char *chg_cell_organ(uint64_t coord)
{
    if ((coord >> 56) != CHG_CELL_DOMAIN) return NULL;
    uint32_t place = (uint32_t) (coord >> 40) & 0xFFFFu;
    if (place == 0 || place > chg_cell_organs_n) return NULL;
    return chg_cell_organs[place - 1u];
}

chg_cell_why_t chg_cell_resolve(uint64_t coord, bool (*installed)(void *ctx, const chg_cell_t *c),
                                void *ctx, const chg_cell_t **cell)
{
    const chg_cell_t *c = chg_cell_find(coord);
    if (c && c->retired) c = NULL;
    if (cell) *cell = c;
    if (!c) return CHG_CELL_NO_MODEL;
    if (!c->runnable) return CHG_CELL_NO_ENGINE;
    if (!installed || !installed(ctx, c)) return CHG_CELL_NOT_INSTALLED;
    return CHG_CELL_RUNS;
}

const char *chg_cell_why_text(chg_cell_why_t w)
{
    switch (w) {
    case CHG_CELL_RUNS:
        return "runs";
    case CHG_CELL_NO_MODEL:
        return "no model at that coordinate";
    case CHG_CELL_NO_ENGINE:
        return "model needs an engine path this build lacks";
    case CHG_CELL_NOT_INSTALLED:
        return "model not installed on this machine";
    }
    return "no model at that coordinate";
}
