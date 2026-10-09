/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_cover.c — see zxv_cover.h. Marking is a bit set; reporting is a print. */
#include "zxv_cover.h"

/* One word. COV_POINT_COUNT is asserted <= 64 at compile time below, so this
 * cannot silently overflow into a truncated set -- a coverage system that
 * quietly stops recording past point 64 would report LESS coverage than
 * reality and be worse than none. */
static uint64_t g_marked;

_Static_assert((int)COV_POINT_COUNT <= 64,
               "zxv_cover: more than 64 points needs a wider set; a truncated "
               "coverage set under-reports and must never happen silently");

/* Names are the SYMBOLS a reader will grep for, not prose. gamedrive collects
 * everything after the [COVER] tag verbatim, so a space here becomes part of
 * the symbol name -- keep them tight. Order MUST match zxv_cover_id_t. */
static const char *const NAMES[COV_POINT_COUNT] = {
    "game_runner_run", "rom_read",
    "nes_is_ines",  "nes_load",   "nes_run",   "nes_running",
    "gb_is_gb",     "gb_load",    "gb_run",    "gb_running",
    "snes_is_lorom","snes_load",  "snes_run",  "snes_running",
    "gba_is_gba",   "gba_load",   "gba_run",   "gba_running",
    "genesis_is_md","genesis_load","genesis_run","genesis_running",
    "pce_is_hucard","pce_load",   "pce_run",   "pce_running",
    "cpu6502_step", "cpu_z80_step", "cpu65816_step", "cpu_lr35902_step",
    "cpu_arm7_step","cpu_m68k_step","cpu_huc6280_step"
};

void zxv_cover_mark(zxv_cover_id_t id)
{
    /* Bounds-check rather than trust the caller: an out-of-range id shifting by
     * >= 64 is undefined behaviour, and this file exists because of a bug that
     * was exactly that (keccak's rotl64 shift-by-64). Refuse, do not wrap. */
    if ((unsigned)id >= (unsigned)COV_POINT_COUNT) return;
    g_marked |= ((uint64_t)1u << (unsigned)id);
}

void zxv_cover_reset(void) { g_marked = 0u; }

const char *zxv_cover_name(zxv_cover_id_t id)
{
    if ((unsigned)id >= (unsigned)COV_POINT_COUNT) return "?";
    return NAMES[id];
}

uint32_t zxv_cover_report(void (*puts_fn)(const char *))
{
    uint32_t n = 0u, i;
    if (!puts_fn) return 0u;            /* a report that cannot print is not a report */
    for (i = 0u; i < (uint32_t)COV_POINT_COUNT; i++) {
        if ((g_marked >> i) & 1u) {
            /* Exactly the shape gamedrive.py:268 parses: "[COVER] " then the
             * name, one per line. Emitted in three calls to avoid needing any
             * formatting function in a freestanding build. */
            puts_fn("[COVER] ");
            puts_fn(NAMES[i]);
            puts_fn("\n");
            n++;
        }
    }
    return n;
}
