/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ninefold_model.h — Ninefold, the nine forms of capital, as a model
 * (zxm.h): what the owner holds in each form, in one currency, read from
 * the books (ledger.accounts and ledger.balance, through coffer_load) and
 * the swarm's earnings account (swarm.earnings). Read only: it posts
 * nothing and keeps no book of its own.
 *
 *   NF1 THE FORMS are zcap_forms.h's canonical indexes: 0 financial,
 *       1 manufactured, 2 intellectual, 3 human, 4 social, 5 natural,
 *       6 cultural, 7 spiritual, 8 system. 4 .. 7 are state-reserved
 *       (finance/capital_forms.h capital_is_state_reserved): inalienable,
 *       never priced. Anything outside 0 .. 8 is not a form: refused.
 *   NF2 A FORM'S HOLDING is the exact sum of its holder accounts'
 *       balances (coffer_holdings), in minor units, 0 .. 18 digits
 *       (NINEFOLD_MINOR_MAX); a state-reserved form shows 0, never a
 *       price, and its accounts are only counted.
 *   NF3 A FORM'S SHARE of the priced whole (the sum of the priceable
 *       forms' holdings), in hundredths of a percent, floored:
 *       floor(10000 * held / whole), held 0 .. whole and whole 1 ..
 *       NINEFOLD_WHOLE_MAX; 0 for a state-reserved form. A whole of 0 or
 *       past 14 digits gives no shares (ninefold_t.shared false).
 *   NF4 AN ACCOUNT'S FORM. zxvd has no form for an account, so the
 *       default taken is: a name that starts with a form's canonical name
 *       and a colon ("manufactured: lathe", any case) is in that form; the
 *       swarm earnings account and every other holder account are
 *       financial. Issuer accounts (an outside party's side) are left out.
 *   NF5 What is read is checked (coffer_model.h CF5) and the copy is
 *       replaced only after a whole answer has been read and every figure
 *       fits; a refusal keeps what the model had.
 *
 * No drawing, no allocation, integers only. */
#ifndef NINEFOLD_MODEL_H
#define NINEFOLD_MODEL_H

#include "coffer_model.h"

#define NINEFOLD_FORMS      9u
#define NINEFOLD_MINOR_MAX  INT64_C(999999999999999999) /* 18 digits, Sutra's 9(18) */
#define NINEFOLD_WHOLE_MAX  INT64_C(99999999999999)     /* 14 digits, Sutra's 9(14) */
#define NINEFOLD_SHARE_ALL  INT64_C(10000)
#define NINEFOLD_REFUSED    INT64_MIN
#define NINEFOLD_NOTICE_MAX 255u

enum {
    NINEFOLD_FINANCIAL = 0,
    NINEFOLD_MANUFACTURED = 1,
    NINEFOLD_INTELLECTUAL = 2,
    NINEFOLD_HUMAN = 3,
    NINEFOLD_SOCIAL = 4,
    NINEFOLD_NATURAL = 5,
    NINEFOLD_CULTURAL = 6,
    NINEFOLD_SPIRITUAL = 7,
    NINEFOLD_SYSTEM = 8,
};

typedef struct {
    zxm_link_t *link;
    coffer_t books;                       /* the accounts and balances (Coffer) */
    bool loaded;                          /* a whole load has been read */
    char ccy[COFFER_CCY_MAX + 1];         /* the currency the figures are in */
    int64_t earnings;                     /* swarm.earnings owner_account; -1 none */
    uint32_t accounts[NINEFOLD_FORMS];    /* holder accounts in each form */
    int64_t holding[NINEFOLD_FORMS];      /* NF2, minor units */
    int64_t whole;                        /* the priced whole */
    bool shared;                          /* NF3: share holds figures */
    int64_t share[NINEFOLD_FORMS];        /* NF3, x100 */
    char notice[NINEFOLD_NOTICE_MAX + 1]; /* for the view */
} ninefold_t;

/* The rules, as the app's logic says them (ninefold.zxapp). */

/* NF1: true for 4 .. 7. */
bool ninefold_state_reserved(int64_t form);
/* NF1 as a number: 1 state-reserved, 0 priceable, NINEFOLD_REFUSED when
 * form is not 0 .. 8. */
int64_t ninefold_reserved_n(int64_t form);
/* NF2: minor for a priceable form, 0 for a state-reserved one;
 * NINEFOLD_REFUSED when form is not 0 .. 8 or minor not 0 ..
 * NINEFOLD_MINOR_MAX. */
int64_t ninefold_form_holding(int64_t form, int64_t minor);
/* NF3: floor(10000 * held / whole), 0 for a state-reserved form;
 * NINEFOLD_REFUSED when form is not 0 .. 8, whole not 1 ..
 * NINEFOLD_WHOLE_MAX, or held not 0 .. whole. */
int64_t ninefold_form_share(int64_t form, int64_t held, int64_t whole);

/* NF4: the form an account's name puts it in (financial when untagged). */
uint32_t ninefold_form_of(const char *name);
/* The canonical lower-case name of a form; "" when out of range. */
const char *ninefold_form_name(int64_t form);

/* The books. */
void ninefold_init(ninefold_t *n, zxm_link_t *link);
/* swarm.earnings, then ledger.accounts and ledger.balance (coffer_load);
 * each form's holding and share in ccy (an ISO 4217 code, or any code
 * zxvd's books hold, 3 capital letters). */
int ninefold_load(ninefold_t *n, const char *ccy);

#endif
