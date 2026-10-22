/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tally_model.h — Tally, the calculator, as a model (zxm.h): exact rational
 * arithmetic on a running value with a bounded tape, unit conversion by
 * exact factors, and currency conversion in integer minor units between
 * the currencies the owner enabled. No allocation, integers only
 * (tally_rules.h, kernel/src/rational), so 1/3 stays 1/3.
 *
 *   TM1 THE RUNNING VALUE. A value is typed as an integer, an exact
 *       decimal ("0.1" is 1/10) or a ratio ("3/4"), and an operation
 *       (TALLY_ADD .. TALLY_DIV) applies the next one to it by TL1. A
 *       value that does not parse, or a result TL1 refuses, leaves the
 *       running value and the tape as they were (ZXM_E_ARG).
 *   TM2 THE TAPE. Every value entered and every result is a line on a tape
 *       of TALLY_TAPE lines, pre-allocated; a full tape refuses the next
 *       line (ZXM_E_TOOBIG) before anything changes, until it is cleared.
 *   TM3 SHOWN EXACTLY. A value is shown as a terminating decimal when it
 *       has one, else as the reduced n/d (rat_to_string), never rounded;
 *       tally_reduced_by is the divisor a fraction typed as num / den was
 *       reduced by: |den| over the reduced denominator (rat_make).
 *   TM4 CURRENCIES. The currencies offered are the distinct ones of the
 *       owner's accounts (ledger.accounts, ledger.read only), each an ISO
 *       4217 code of three capitals; anything else in the answer is
 *       ZXM_E_PROTOCOL and the list is kept as it was. More than
 *       TALLY_CCY_MAX is ZXM_E_TOOBIG. zxvd has no method that reads a
 *       currency's minor units, so they come from the ISO 4217 table in
 *       tally_model.c; a code not in it is shown in minor units only.
 *   TM5 CONVERSION, SHOWN, NEVER POSTED. An amount in minor units of one
 *       enabled currency at a rate rnum / rden typed by the owner (zxvd
 *       has no exchange-rate method) is TL3 whole minor units of the other
 *       with TL4 left over, both shown in one line of text. Both codes
 *       must be enabled (ZXM_E_STATE before tally_load, ZXM_E_ARG when
 *       not), and the TL3 domain must hold (ZXM_E_ARG). The model calls no
 *       posting method: moving money is the owner's, through Coffer.
 *   TM6 UNITS. A quantity from one unit to another by name (tally_units),
 *       by TL2: the same dimension only (ZXM_E_ARG otherwise).
 *   TM7 NET PRESENT VALUE. Cash flows cf[t] at the end of period t (1 ..
 *       TALLY_FLOWS of them, cf[0] now) at a rate rnum / rden per period,
 *       through the shared pce_fin block (its npv entry, the manifest's
 *       use), exact; a rate of -1 or below, or a refusal there, is
 *       ZXM_E_ARG. Tally writes no npv of its own. */
#ifndef TALLY_MODEL_H
#define TALLY_MODEL_H

#include "tally_rules.h"
#include "zxm.h"

#define TALLY_CCY_MAX  32u
#define TALLY_TAPE     64u
#define TALLY_TEXT_MAX 160u
#define TALLY_NUM_MAX  64u /* a value as text: two int64 and a slash fit */
#define TALLY_FLOWS    32u /* pce_fin's PCE_FIN_CF */

typedef struct {
    char code[4];
    int32_t exponent; /* ISO 4217 minor units; -1 when not in the table */
} tally_ccy_t;

typedef struct {
    int64_t op; /* 0 for a value entered, else TALLY_ADD .. TALLY_DIV */
    rat_t value;
} tally_line_t;

typedef struct {
    zxm_link_t *link;
    tally_ccy_t ccy[TALLY_CCY_MAX];
    uint32_t nccy;
    bool loaded;
    rat_t acc; /* TM1: the running value */
    tally_line_t tape[TALLY_TAPE];
    uint32_t ntape;
} tally_t;

typedef struct {
    int64_t amount, rnum, rden;
    int64_t whole; /* TL3 */
    int64_t rest;  /* TL4, in rden-ths of a minor unit */
    char text[TALLY_TEXT_MAX];
} tally_fx_t;

void tally_init(tally_t *t, zxm_link_t *link);
/* TM1 and TM2 */
int tally_enter(tally_t *t, const char *text);
int tally_apply(tally_t *t, int64_t op, const char *text);
void tally_clear(tally_t *t);
/* TM3 */
uint32_t tally_show(rat_t r, char *out, uint32_t cap);
int64_t tally_reduced_by(int64_t num, int64_t den);
/* TM4 */
int tally_load(tally_t *t);
const tally_ccy_t *tally_currency(const tally_t *t, const char *code);
int32_t tally_minor_exponent(const char *code);
uint32_t tally_minor_text(int64_t minor, int32_t exponent, const char *code, char *out,
                          uint32_t cap);
/* TM5 */
int tally_convert(const tally_t *t, const char *from, const char *to, int64_t amount, int64_t rnum,
                  int64_t rden, tally_fx_t *fx);
/* TM6 */
int64_t tally_unit_index(const char *name);
int tally_unit(int64_t qty, const char *src, const char *dst, rat_t *r);
/* TM7 */
int tally_npv(const int64_t *cf, uint32_t n, int64_t rnum, int64_t rden, rat_t *out);

#endif
