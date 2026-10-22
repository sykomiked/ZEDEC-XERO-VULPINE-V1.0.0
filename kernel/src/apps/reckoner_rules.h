/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* reckoner_rules.h — Reckoner's rules in C, the reference its Sutra,
 * Trebago and Eve blocks are checked against (blocks/reckoner). Pure
 * integer functions, no state: the model (reckoner_model.c) and the
 * language test (tests/reckoner_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   RR1 A STATEMENT'S CLOSING. One account over a period: closing =
 *       opening + credits - debits, in integer minor units, as a camt.053
 *       statement reconciles. credits and debits are the totals of one
 *       direction each, 0..RECKONER_MINOR_MAX; opening and the closing
 *       fit 18 digits either way (Sutra's s9(18)).
 *   RR2 THE TAX IN ONE BAND. A banded (marginal) table taxes the part of
 *       income inside each band lo..hi at that band's rate, in hundredths
 *       of a percent (RECKONER_RATE_ALL, 10000, is all of it):
 *       floor(max(0, min(income, hi) - lo) * rate / 10000). income, lo
 *       and hi are 0..RECKONER_TAX_MAX (14 digits, Sutra's 9(14)), lo at
 *       most hi, rate 0..10000. The estimate is the exact sum of the
 *       bands' taxes, the bands in order and never overlapping, so no
 *       part of income is taxed twice. It is advice, never a filing.
 *   RR3 A PERIOD MAY BE REPORTED when it is closed (closed = 1) and the
 *       books hold as ledger.check last answered (books = 1); each flag
 *       must be exactly 1.
 *
 * Anything else is refused (RECKONER_REFUSED), never rounded or wrapped. */
#ifndef RECKONER_RULES_H
#define RECKONER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define RECKONER_REFUSED   INT64_MIN
#define RECKONER_MINOR_MAX INT64_C(999999999999999999) /* 18 digits, Sutra's s9(18) */
#define RECKONER_TAX_MAX   INT64_C(99999999999999)     /* 14 digits, Sutra's 9(14) */
#define RECKONER_RATE_ALL  INT64_C(10000)              /* 100.00 percent */
#define RECKONER_BANDS     16u                         /* bands in one table */

static inline bool reckoner_fits18(int64_t v)
{
    return v >= -RECKONER_MINOR_MAX && v <= RECKONER_MINOR_MAX;
}

/* RR1: the closing balance, or RECKONER_REFUSED. */
static inline int64_t reckoner_statement_close(int64_t opening, int64_t credits, int64_t debits)
{
    if (!reckoner_fits18(opening) || credits < 0 || credits > RECKONER_MINOR_MAX || debits < 0 ||
        debits > RECKONER_MINOR_MAX)
        return RECKONER_REFUSED;
    int64_t c = opening + credits; /* each 18 digits: no 64-bit overflow */
    c -= debits;
    return reckoner_fits18(c) ? c : RECKONER_REFUSED;
}

/* RR2: the tax in one band, or RECKONER_REFUSED. */
static inline int64_t reckoner_band_tax(int64_t income, int64_t lo, int64_t hi, int64_t rate)
{
    if (income < 0 || income > RECKONER_TAX_MAX || lo < 0 || hi > RECKONER_TAX_MAX || lo > hi ||
        rate < 0 || rate > RECKONER_RATE_ALL)
        return RECKONER_REFUSED;
    int64_t taxed = income < hi ? income : hi;
    if (taxed <= lo) return 0;
    taxed -= lo;
    return taxed * rate / RECKONER_RATE_ALL; /* at most 14 digits * 10000: fits */
}

/* RR2: the estimate over n bands (1..RECKONER_BANDS), each band's tax in
 * tax[n]; the total, or RECKONER_REFUSED with nothing written when a band
 * is refused, the bands are out of order or overlap. */
static inline int64_t reckoner_tax_estimate(int64_t income, const int64_t *lo, const int64_t *hi,
                                            const int64_t *rate, uint32_t n, int64_t *tax)
{
    int64_t got[RECKONER_BANDS], total = 0;
    if (!lo || !hi || !rate || !tax || n == 0 || n > RECKONER_BANDS) return RECKONER_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (i > 0 && lo[i] < hi[i - 1u]) return RECKONER_REFUSED;
        got[i] = reckoner_band_tax(income, lo[i], hi[i], rate[i]);
        if (got[i] == RECKONER_REFUSED) return RECKONER_REFUSED;
        total += got[i]; /* the bands never overlap: at most income */
    }
    for (uint32_t i = 0; i < n; i++) tax[i] = got[i];
    return total;
}

/* RR3: closed and the books hold, each flag exactly 1. */
static inline bool reckoner_report_ok(int64_t closed, int64_t books)
{
    return closed == 1 && books == 1;
}

#endif
