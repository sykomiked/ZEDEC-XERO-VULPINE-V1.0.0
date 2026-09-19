/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* zxv_cover.h — which emulation paths a REAL ROM actually reached.
 *
 * WHY THIS EXISTS. gamedrive.py's report ends with:
 *     coverage-instrumented runs: 0/5160
 *     distinct symbols reached  : 0
 *     NOTE: ... "symbols reached" is 0 because it was NOT MEASURED --
 *           not because nothing was reached. Instrument first.
 * A 5,160-ROM campaign that cannot say WHICH code the ROMs exercised proves the
 * kernel did not crash and nothing else. This closes that gap.
 *
 * THE CONSUMER DEFINES THE FORMAT. gamedrive.py:268 is:
 *     cover = {l.split('[COVER]',1)[1].strip() for l in lines if '[COVER]' in l}
 * i.e. any line containing "[COVER] <name>", collected into a SET. Duplicates
 * are free, order is irrelevant, and the name is everything after the tag. This
 * header is written to that contract, not to a guess about it.
 *
 * DESIGN: MARKING IS PURE, REPORTING IS NOT.
 * game_runner.c and the machine modules perform NO I/O -- they compute and
 * return game_run_t. That is good design and instrumenting must not break it.
 * So marking a point sets a BIT (no console, no formatting, no allocation, safe
 * on any arch and in any context) and a SEPARATE reporter prints the set from
 * the one place that owns a console: the arch main. Same split the modbind gate
 * uses -- collect everywhere, report once, at a place that can actually print.
 *
 * WHAT IS AND IS NOT MEASURED. A bit means "this path executed during THIS
 * boot". It does NOT mean the path was correct, and it says nothing about lines
 * or branches inside the function. This is REACHABILITY at named checkpoints --
 * deliberately coarse, because a per-instruction counter in a CPU core's step
 * function would dominate the very workload it is trying to measure.
 *
 * Freestanding: integer only, no libc, no allocation, no float. */
#ifndef ZXV_COVER_H
#define ZXV_COVER_H

#include <stdint.h>

/* Coverage point ids. NAMES MATCH REAL SYMBOLS so "distinct symbols reached"
 * means something a reader can grep for. Keep <= 64: the set is one uint64_t,
 * which makes marking a single OR with no memory beyond a static word. */
typedef enum {
    COV_GAME_RUNNER_RUN = 0,   /* game_runner_run entered                    */
    COV_ROM_READ,              /* ROM bytes actually read off the blockdev   */

    COV_NES_IS_INES,           /* format detectors: the ROM was RECOGNISED   */
    COV_NES_LOAD,              /* ...and the machine accepted it             */
    COV_NES_RUN,               /* ...and the machine was stepped             */
    COV_NES_RUNNING,           /* ...and reported a genuine running verdict  */

    COV_GB_IS_GB,   COV_GB_LOAD,   COV_GB_RUN,   COV_GB_RUNNING,
    COV_SNES_IS,    COV_SNES_LOAD, COV_SNES_RUN, COV_SNES_RUNNING,
    COV_GBA_IS,     COV_GBA_LOAD,  COV_GBA_RUN,  COV_GBA_RUNNING,
    COV_GEN_IS,     COV_GEN_LOAD,  COV_GEN_RUN,  COV_GEN_RUNNING,
    COV_PCE_IS,     COV_PCE_LOAD,  COV_PCE_RUN,  COV_PCE_RUNNING,

    COV_CPU6502_STEP,          /* CPU cores: the core executed at least once */
    COV_CPU_Z80_STEP,
    COV_CPU65816_STEP,
    COV_CPU_LR35902_STEP,
    COV_CPU_ARM7_STEP,
    COV_CPU_M68K_STEP,
    COV_CPU_HUC6280_STEP,

    COV_POINT_COUNT            /* must stay <= 64 */
} zxv_cover_id_t;

/* Mark a point as reached. PURE: one OR into a static word. No I/O, no locks,
 * no failure mode. Safe to call from a machine module that must not print. */
void zxv_cover_mark(zxv_cover_id_t id);

/* Clear the set. Call at the START of a ROM run so the report describes THIS
 * ROM and not the accumulated history of the boot. */
void zxv_cover_reset(void);

/* Emit one "[COVER] <name>" line per marked point through the caller's puts.
 * Returns how many points were marked -- so a caller can distinguish "nothing
 * was reached" from "the reporter never ran", which are very different facts. */
uint32_t zxv_cover_report(void (*puts_fn)(const char *));

/* The name of a point, for tests and for anyone printing their own format. */
const char *zxv_cover_name(zxv_cover_id_t id);

#endif /* ZXV_COVER_H */
