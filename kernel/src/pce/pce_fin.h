/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_fin.h — the financial analysis engine: advisory analytics only.
 *
 * Findings, never instructions. Every input is read-only; no output is a
 * balance, an order or a trade, and every verdict is advisory (pce.h E2):
 * a finding that would PROCEED is ESCALATE, for a person to review. Nothing
 * here is investment advice.
 *
 *   F1 RECONCILE. The same figure from several statements (sources): all
 *      equal is TRUE; different is BOTH, kept as an exact [low, high] with
 *      the number of sources; reported by none is NEITHER. A difference
 *      between two different accounting standards (GAAP against IFRS) is an
 *      allowed disagreement: settled by a reconciliation rule, so the
 *      report reads as converging (WAIT). A difference under the same
 *      standard is not: it reads as diverging and goes to a person.
 *   F2 THE IDENTITY IS A WALL. A statement that reports assets, liabilities
 *      and equity with assets != liabilities + equity breaks an invariant:
 *      it is quarantined (left out of every figure and ratio) and logged
 *      on its own with a forbidden tag.
 *   F3 RATIOS AS EXACT RANGES: current ratio (current assets / current
 *      liabilities), debt to equity (liabilities / equity), net margin (net
 *      income / revenue) and interest cover (EBIT / interest). Built from
 *      the reconciled ranges by their four corners, so the range covers
 *      every combination of the sources' figures (it can be wider than the
 *      range of the per-source ratios). A denominator range
 *      that touches zero is NEITHER, never infinity; one exact value is
 *      TRUE, a range is BOTH.
 *   F4 PRESENT VALUE UNDER DISPUTED RATES. Exact NPV of a cash-flow series
 *      at each rate given (one per analyst or source). Positive at every
 *      rate: TRUE; negative at every rate: FALSE; zero at every rate:
 *      NEITHER (break-even, no finding); anything else: BOTH, whose rate
 *      decides. A value too large to keep exactly is NEITHER for the whole
 *      run (PCE_E_OVERFLOW in that rate's slot), never a rounded guess.
 *   F5 DISCRETE RISK. Expected loss and value at risk (the smallest listed
 *      loss whose cumulative probability reaches the level) from an exact
 *      outcome distribution. Two models of the same outcomes are witnesses
 *      (as the app model's witnessed call): equal figures TRUE, different
 *      BOTH with both kept, one model NEITHER (unwitnessed, still shown).
 */
#ifndef PCE_FIN_H
#define PCE_FIN_H

#include "pce.h"

#define PCE_FIN_SRC       8u
#define PCE_FIN_CF        32u
#define PCE_FIN_RATES     8u
#define PCE_FIN_OUTCOMES  64u
#define PCE_FIN_MONEY_MAX ((int64_t) 1 << 61) /* minor units, either sign */

enum {
    PCE_FIN_ASSETS = 0,
    PCE_FIN_LIABILITIES,
    PCE_FIN_EQUITY,
    PCE_FIN_CUR_ASSETS,
    PCE_FIN_CUR_LIAB,
    PCE_FIN_REVENUE,
    PCE_FIN_NET_INCOME,
    PCE_FIN_EBIT,
    PCE_FIN_INTEREST,
    PCE_FIN_FIELDS
};
enum { PCE_STD_OTHER = 0, PCE_STD_GAAP = 1, PCE_STD_IFRS = 2 };
enum {
    PCE_RATIO_CURRENT = 0,
    PCE_RATIO_DEBT_EQUITY,
    PCE_RATIO_NET_MARGIN,
    PCE_RATIO_INTEREST_COVER,
    PCE_FIN_RATIOS
};

typedef struct {
    int64_t v[PCE_FIN_FIELDS]; /* minor units, |v| <= PCE_FIN_MONEY_MAX */
    uint32_t present;          /* bit f set when field f is reported   */
    uint32_t standard;         /* PCE_STD_*                            */
} pce_stmt_t;

typedef struct {
    uint8_t base; /* F1: LPX_B_* */
    int64_t lo, hi;
    uint32_t n;    /* sources reporting it               */
    bool same_std; /* a difference under one standard   */
} pce_field_t;

typedef struct {
    uint8_t base; /* F3 */
    rat_t lo, hi;
} pce_ratio_t;

typedef struct {
    pce_field_t field[PCE_FIN_FIELDS];
    pce_ratio_t ratio[PCE_FIN_RATIOS];
    uint32_t quarantined; /* F2: bit i for source i */
    uint32_t used;        /* sources the figures come from */
    pce_verdict_t v;      /* TRUE all figures agree, BOTH any differ, NEITHER no source */
    uint8_t in[PDX_HASH], out[PDX_HASH];
} pce_fin_report_t;

/* F1-F3. n is 1..PCE_FIN_SRC. A field out of range, an unknown standard or a
 * bad phase is PCE_E_ARG. The report is one log record (PCE_OP_FIN, aux =
 * used mask), after one forbidden record per quarantined source. */
int pce_fin_analyse(const pce_stmt_t *s, uint32_t n, uint8_t phase, pdx_log_t *log,
                    pce_fin_report_t *out);

typedef struct {
    uint32_t nrates;
    rat_t npv[PCE_FIN_RATES]; /* invalid where it would not fit exactly */
    rat_t lo, hi;             /* over the rates (valid when all are)    */
    pce_verdict_t v;          /* F4 */
    uint8_t in[PDX_HASH], out[PDX_HASH];
} pce_npv_t;

/* F4. cf[t] at the end of period t (cf[0] now), n in 1..PCE_FIN_CF, each
 * |cf| <= PCE_FIN_MONEY_MAX; each rate per period, exact, > -1. */
int pce_fin_npv(const int64_t *cf, uint32_t n, const rat_t *rates, uint32_t nr, uint8_t phase,
                pdx_log_t *log, pce_npv_t *out);

typedef struct {
    uint32_t nmodels;
    rat_t expected[2]; /* expected loss per model */
    int64_t var[2];    /* value at risk per model */
    pce_verdict_t v;   /* F5 */
    uint8_t in[PDX_HASH], out[PDX_HASH];
} pce_risk_t;

/* F5. loss[i] with probability p[i] (and q[i] in a second model, or q NULL);
 * level in (0, 1]. */
int pce_fin_risk(const int64_t *loss, const rat_t *p, const rat_t *q, uint32_t n, rat_t level,
                 uint8_t phase, pdx_log_t *log, pce_risk_t *out);

#endif /* PCE_FIN_H */
