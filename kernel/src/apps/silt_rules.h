/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* silt_rules.h — Silt's rules in C, the reference its Eve, KRON, Trebago,
 * Sutra, ALGOL 60 and MYRA blocks are checked against (blocks/silt), with
 * the rules it composes from nearer the core. Pure integer functions, no
 * state: the model (silt_model.c) and the language test
 * (tests/silt_zxapp.inc) both include this.
 *
 *   SR1 REGISTER NUMBER (register_number): a record's register number is
 *       year * 1000000 + seq, seq its place in that year's register,
 *       1..999999, and year 0..9999 (Sutra's 9(4), 9(6), 9(10)). seq is
 *       tallystick.next_doc_number of the year's last seq (1 for the
 *       first); past 999999 the year's register is full: refused, never
 *       wrapped. Sutra's picture holds seq 0, the ALGOL 60 proof refuses
 *       it (no register number is 0): held.
 *   SR2 MAY DISPOSE (record_may_dispose): the retention period is over
 *       (left, cartulary.retention_left of the record's date and its
 *       series' schedule, 0 or fewer, down to -9999999, Sutra's s9(7)),
 *       no legal hold is on it (hold 0) and the records officer approved
 *       (approved 1). A hold always wins. Advice: the disposal itself is
 *       the owner's confirmed act and goes to the Trash.
 *   SR3 BACKLOG (backlog_clear_days): arrived[0..n) and cleared[0..n)
 *       records a day over the last n days (1..366, each 0..999999), and
 *       backlog (0..999999999) waiting now. The backlog falls by
 *       (sum cleared - sum arrived) / n a day, so it clears in
 *       backlog * n / (sum cleared - sum arrived) days, an exact reduced
 *       fraction (kernel/src/rational), never rounded. Refused when the
 *       backlog does not fall (cleared no more than arrived).
 *   SR4 CASE FILE NODE (case_file_node_ok) fits ^ZXSILT(series, caseno,
 *       doc) = regno ^ dated ^ hold: series 4 digits, caseno 6, doc 4,
 *       regno 10, dated (days since 1970) 6, hold 1, sign aside.
 *   SR5 ONE REFERENCE, ONE DATE (lpres_state_t): each record filed under
 *       a reference says what the record's date is. Every record agreeing
 *       with the first is TRUE; one that disagrees is FALSE evidence of
 *       that date, joined with lpres_kjoin, so two dates make BOTH: both
 *       records are kept and flagged, never merged. No record is NEITHER.
 *   Composed, as the parts' own rules: banker.share (floor(10000 * size
 *   / sum), a zero sum counted as 1), banker.total and banker.entry_ok
 *   (cartulary_rules.h), witness.chain_ok and witness.kind_count
 *   (witness_rules.h WR1, WR2), sealwax.sign_ready (sealwax_rules.h SR3),
 *   broadsheet.well_formed (broadsheet_rules.h BR1),
 *   cartulary.retention_left and cartulary.class_year_cell
 *   (cartulary_rules.h CR2, CR6) and tallystick.next_doc_number
 *   (tallystick_rules.h TR3). */
#ifndef SILT_RULES_H
#define SILT_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "../rational/rational.h"
#include "broadsheet_rules.h"
#include "cartulary_rules.h"
#include "lpres.h"
#include "sealwax_rules.h"
#include "tallystick_rules.h"
#include "witness_rules.h"

#define SILT_YEAR_MAX    9999                /* SR1: 9(4) */
#define SILT_SEQ_MAX     INT64_C(999999)     /* SR1: 9(6) a year */
#define SILT_REGNO_MAX   INT64_C(9999999999) /* SR1: 9(10) */
#define SILT_LEFT_MIN    INT64_C(-9999999)   /* SR2: s9(7) */
#define SILT_DAYS_MAX    366u                /* SR3: days of history */
#define SILT_PER_DAY_MAX INT64_C(999999)     /* SR3 */
#define SILT_BACKLOG_MAX INT64_C(999999999)  /* SR3 */
#define SILT_X100        INT64_C(10000)      /* banker.share: 100.00 % */

/* a register line's kind, counted by witness.kind_count */
enum { SILT_FILED = 1, SILT_DISPOSED = 2 };

/* SR1: the register number, or -1 */
static inline int64_t silt_register_number(int64_t year, int64_t seq)
{
    if (year < 0 || year > SILT_YEAR_MAX || seq < 1 || seq > SILT_SEQ_MAX) return -1;
    return year * 1000000 + seq;
}

/* SR1: the year's next seq after `last` (tallystick.next_doc_number), or -1 when the year's
 * register is full */
static inline int64_t silt_next_seq(int64_t last)
{
    int64_t next = tstk_next_number(last);
    return next == TSTK_REFUSED || next > SILT_SEQ_MAX ? -1 : next;
}

/* SR2 */
static inline bool silt_may_dispose(int64_t left, int64_t hold, int64_t approved)
{
    return left >= SILT_LEFT_MIN && left <= 0 && hold == 0 && approved == 1;
}

/* SR3: false when refused */
static inline bool silt_backlog_days(const int64_t *arrived, const int64_t *cleared, uint32_t n,
                                     int64_t backlog, rat_t *days)
{
    int64_t in = 0, out = 0;
    if (!arrived || !cleared || !days || n < 1u || n > SILT_DAYS_MAX || backlog < 0 ||
        backlog > SILT_BACKLOG_MAX)
        return false;
    for (uint32_t i = 0; i < n; i++) {
        if (arrived[i] < 0 || arrived[i] > SILT_PER_DAY_MAX || cleared[i] < 0 ||
            cleared[i] > SILT_PER_DAY_MAX)
            return false;
        in += arrived[i];
        out += cleared[i];
    }
    if (out <= in) return false;
    *days = rat_make(backlog * (int64_t) n, out - in); /* at most 10^9 * 366 */
    return days->valid;
}

/* SR4 */
static inline bool silt_case_node_ok(int64_t series, int64_t caseno, int64_t doc, int64_t regno,
                                     int64_t dated, int64_t hold)
{
    return witness_digits_fit(series, 4) && witness_digits_fit(caseno, 6) &&
           witness_digits_fit(doc, 4) && witness_digits_fit(regno, 10) &&
           witness_digits_fit(dated, 6) && witness_digits_fit(hold, 1);
}

/* SR5: the dates n records under one reference give */
static inline lpres_state_t silt_reference_state(const int64_t *dated, uint32_t n)
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    if (!dated) return s;
    for (uint32_t i = 0; i < n; i++)
        s = lpres_kjoin(s, dated[i] == dated[0] ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    return s;
}

/* banker.share: floor(10000 * size / sum), a zero sum counted as 1; -1 for a negative */
static inline int64_t silt_share(int64_t size, int64_t sum)
{
    if (size < 0 || sum < 0 || size > INT64_MAX / SILT_X100) return -1;
    return SILT_X100 * size / (sum ? sum : 1);
}

#endif
