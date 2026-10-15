/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_cells.h — the Tiamat model cells by Grid Chain coordinate: what a
 * chg_abi.h A7 MODEL_EXEC resolves its coordinate to.
 *
 *   C1 THE TABLE. chg_cells_table.c is generated from
 *      tools/tiamat/cell_coords.json by tools/tiamat/cell_table.py (never by
 *      hand; --check fails when the two differ). One row per cell, sorted
 *      by coordinate, retired cells included so their numbers stay taken.
 *   C2 THE WORD. domain 0x60 | place 16 | item 8 | ext 16 | format 8 |
 *      lang 8, high byte first: place is the organ number (chg_cell_organ),
 *      item the cell's ordinal within it, the low 32 bits zero. Only an
 *      exact match names a cell; nothing is rounded or guessed.
 *   C3 RESOLVE. chg_cell_resolve answers one of four, in this order:
 *      NO_MODEL     no live cell at that coordinate (none, or retired):
 *                   "no model at that coordinate";
 *      NO_ENGINE    the cell's engine path is not in this build (its engine
 *                   text says which): "model needs an engine path this build
 *                   lacks";
 *      NOT_INSTALLED the build can run it but the caller has no weights for
 *                   it here: "model not installed on this machine";
 *      RUNS         run it.
 *      A cell's engine is runnable when its text starts "runs now", or
 *      "zt_vision" or "zt_whisper" (the generator sets the flag; the kernel
 *      never parses the text). Weights are never in the image, so whether a
 *      cell is installed is the caller's answer.
 *
 * Integer only, no allocation, no libc.
 */
#ifndef ZXV_CHG_CELLS_H
#define ZXV_CHG_CELLS_H

#include <stdbool.h>
#include <stdint.h>

#define CHG_CELL_DOMAIN 0x60u

enum { /* where the cell's weights ship (cell_coords.json "placement") */
       CHG_CELL_DISC = 0,
       CHG_CELL_DISC_GATED,
       CHG_CELL_SERVER,
       CHG_CELL_PACK_LAPTOP,
       CHG_CELL_PACK_WORKSTATION
};

typedef enum {
    CHG_CELL_RUNS = 0,
    CHG_CELL_NO_MODEL,
    CHG_CELL_NO_ENGINE,
    CHG_CELL_NOT_INSTALLED
} chg_cell_why_t;

typedef struct {
    uint64_t coord;
    uint8_t placement;
    bool runnable; /* C3 */
    bool retired;  /* C1 */
    const char *name;
    const char *engine;
} chg_cell_t;

extern const chg_cell_t chg_cells[];
extern const uint32_t chg_cells_n;
extern const char *const chg_cell_organs[];
extern const uint32_t chg_cell_organs_n;

/* C2: the row with exactly this coordinate (retired rows too), or NULL. */
const chg_cell_t *chg_cell_find(uint64_t coord);
/* C2: the organ a coordinate's place names ("trunk"), or NULL. */
const char *chg_cell_organ(uint64_t coord);
/* C3: installed(ctx, cell) answers whether this machine holds the cell's
 * weights; NULL means none are. *cell (may be NULL) gets the live row, or
 * NULL on NO_MODEL. */
chg_cell_why_t chg_cell_resolve(uint64_t coord, bool (*installed)(void *ctx, const chg_cell_t *c),
                                void *ctx, const chg_cell_t **cell);
/* C3: the reason in words ("no model at that coordinate"). */
const char *chg_cell_why_text(chg_cell_why_t w);

#endif /* ZXV_CHG_CELLS_H */
