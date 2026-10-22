/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pawprint_rules.h — Pawprint's rules in C, the reference its Eve, KRON,
 * Trebago and MYRA blocks are checked against (blocks/pawprint). Pure
 * integer functions, no state: the model (pawprint_model.c) and the
 * language test (tests/pawprint_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs. Lines and
 * columns count from 1.
 *
 *   PR1 MAY ATTACH. A debugger may attach to a program when it is the
 *       owner's own, the owner approved this attach, and it is not a
 *       system process. Each is a flag, 0 or 1; any other value is no flag
 *       at all, and the answer is no.
 *   PR2 BREAKPOINT HIT. A file's breakpoints are its lines, sorted, each
 *       once (strictly rising), 1 .. PAW_LINE_MAX, at most PAW_BREAK_MAX
 *       of them. A line is hit when it is one of them: 1, else 0. Unsorted
 *       or repeated lines, a line outside 1 .. PAW_LINE_MAX, or more than
 *       PAW_BREAK_MAX breakpoints are refused. No breakpoints hit nothing.
 *   PR3 STEPPED INSTRUCTIONS. The instructions a debugging session ran,
 *       one count for each step taken, summed exactly from the first step:
 *       a negative count, or a sum past int64, is refused, never wrapped.
 *   PR4 LIMITS. At most PAW_BREAK_MAX breakpoints and a stack view of at
 *       most PAW_FRAMES_MAX frames, each held to its own range from 0.
 *   PR5 BREAKPOINT RECORD. A breakpoint fits ^ZXBREAK: file (its place
 *       among the files opened) at most 2 digits, line 7, condition (the
 *       hits to pass before it stops, 0 every time) 9 and hits 9, sign
 *       aside. */
#ifndef PAWPRINT_RULES_H
#define PAWPRINT_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define PAW_BREAK_MAX  INT64_C(256)
#define PAW_FRAMES_MAX INT64_C(64)
#define PAW_LINE_MAX   INT64_C(9999999)
#define PAW_FILE_MAX   INT64_C(99)
#define PAW_COUNT_MAX  INT64_C(999999999)

static inline bool pawprint_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* PR1 */
static inline bool pawprint_may_attach(int64_t own, int64_t approved, int64_t system)
{
    return own == 1 && approved == 1 && system == 0;
}

/* PR2: false when refused; *hit 1 when line is a breakpoint, else 0 */
static inline bool pawprint_breakpoint_hit(const int64_t *stops, uint32_t n, int64_t line,
                                           int64_t *hit)
{
    if (!hit || (n && !stops) || (int64_t) n > PAW_BREAK_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!pawprint_in(stops[i], 1, PAW_LINE_MAX) || (i && stops[i - 1] >= stops[i]))
            return false;
    if (!pawprint_in(line, 1, PAW_LINE_MAX)) return false;
    uint32_t lo = 0, hi = n; /* sorted: a binary search */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (stops[mid] < line)
            lo = mid + 1u;
        else
            hi = mid;
    }
    *hit = lo < n && stops[lo] == line ? 1 : 0;
    return true;
}

/* PR3: false when refused */
static inline bool pawprint_stepped_insns(const int64_t *count, uint32_t n, int64_t *total)
{
    int64_t s = 0;
    if (!total || (n && !count)) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (count[i] < 0 || s > INT64_MAX - count[i]) return false;
        s += count[i];
    }
    *total = s;
    return true;
}

/* PR4 */
static inline bool pawprint_limits_ok(int64_t breakpoints, int64_t frames)
{
    return pawprint_in(breakpoints, 0, PAW_BREAK_MAX) && pawprint_in(frames, 0, PAW_FRAMES_MAX);
}

/* PR5: |v| fits the digits */
static inline bool pawprint_digits(int64_t v, int64_t most)
{
    return v >= -most && v <= most;
}

static inline bool pawprint_record_ok(int64_t file, int64_t line, int64_t condition, int64_t hits)
{
    return pawprint_digits(file, PAW_FILE_MAX) && pawprint_digits(line, PAW_LINE_MAX) &&
           pawprint_digits(condition, PAW_COUNT_MAX) && pawprint_digits(hits, PAW_COUNT_MAX);
}

#endif
