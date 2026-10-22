/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ninefold_model.c — Ninefold, the nine forms of capital (see
 * ninefold_model.h). The accounts and balances are Coffer's (coffer_load)
 * and a form's sum is coffer_holdings, never written again. Integers only:
 * nothing that does not fit is ever rounded. */
#include <string.h>

#include "ninefold_model.h"

/* ---- the rules (NF1 - NF3) -------------------------------------------------- */

static bool is_form(int64_t form)
{
    return form >= 0 && form < (int64_t) NINEFOLD_FORMS;
}

bool ninefold_state_reserved(int64_t form)
{
    return form >= NINEFOLD_SOCIAL && form <= NINEFOLD_SPIRITUAL;
}

int64_t ninefold_reserved_n(int64_t form)
{
    if (!is_form(form)) return NINEFOLD_REFUSED;
    return ninefold_state_reserved(form) ? 1 : 0;
}

int64_t ninefold_form_holding(int64_t form, int64_t minor)
{
    if (!is_form(form) || minor < 0 || minor > NINEFOLD_MINOR_MAX) return NINEFOLD_REFUSED;
    return ninefold_state_reserved(form) ? 0 : minor;
}

int64_t ninefold_form_share(int64_t form, int64_t held, int64_t whole)
{
    if (!is_form(form) || whole < 1 || whole > NINEFOLD_WHOLE_MAX || held < 0 || held > whole)
        return NINEFOLD_REFUSED;
    if (ninefold_state_reserved(form)) return 0;
    return held * NINEFOLD_SHARE_ALL / whole; /* both 14 digits: no 64-bit overflow */
}

/* ---- the forms by name (NF4) --------------------------------------------- */

static const char *const FORM_NAME[NINEFOLD_FORMS] = {
    "financial", "manufactured", "intellectual", "human",  "social",
    "natural",   "cultural",     "spiritual",    "system",
};

const char *ninefold_form_name(int64_t form)
{
    return is_form(form) ? FORM_NAME[form] : "";
}

static char lower(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? (char) (ch - 'A' + 'a') : ch;
}

uint32_t ninefold_form_of(const char *name)
{
    if (!name) return NINEFOLD_FINANCIAL;
    for (uint32_t f = 0; f < NINEFOLD_FORMS; f++) {
        const char *w = FORM_NAME[f];
        uint32_t i = 0;
        while (w[i] && lower(name[i]) == w[i]) i++;
        if (!w[i] && name[i] == ':') return f;
    }
    return NINEFOLD_FINANCIAL;
}

/* ---- text ------------------------------------------------------------------- */

static void put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    while (*s && *at + 1u < cap) out[(*at)++] = *s++;
    out[*at] = 0;
}

static void put_num(char *out, uint32_t cap, uint32_t *at, uint64_t v)
{
    char t[24];
    int k = 23;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (v % 10u));
        v /= 10u;
    } while (v && k > 0);
    put(out, cap, at, t + k);
}

/* An ISO 4217-style code: three capital letters (as coffer_enable takes). */
static bool ccy_ok(const char *s)
{
    if (!s) return false;
    for (uint32_t i = 0; i < 3u; i++)
        if (s[i] < 'A' || s[i] > 'Z') return false;
    return !s[3];
}

/* ---- the books -------------------------------------------------------------- */

void ninefold_init(ninefold_t *n, zxm_link_t *link)
{
    memset(n, 0, sizeof *n);
    n->link = link;
    n->earnings = -1;
    coffer_init(&n->books, link, COFFER_DAY_CAP); /* read only: the cap is never used */
}

/* swarm.earnings: the owner's earnings account, or -1 before the first
 * settled cycle (owner_account null). */
static int read_earnings(ninefold_t *n, int64_t *acct)
{
    zxm_begin(n->link, "swarm.earnings");
    int rc = zxm_call(n->link);
    if (rc != ZXM_OK) return rc;
    int32_t t = zxm_get(n->link, n->link->result, "owner_account");
    if (t < 0) return ZXM_E_PROTOCOL;
    if (n->link->tok[t].type == W4_J_NULL) {
        *acct = -1;
        return ZXM_OK;
    }
    uint64_t u = 0;
    if (zxm_get_u64(n->link, n->link->result, "owner_account", &u) != ZXM_OK || u > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    *acct = (int64_t) u;
    return ZXM_OK;
}

int ninefold_load(ninefold_t *n, const char *ccy)
{
    static int64_t bal[NINEFOLD_FORMS][COFFER_ACCOUNTS];
    uint32_t count[NINEFOLD_FORMS] = {0};
    int64_t holding[NINEFOLD_FORMS], share[NINEFOLD_FORMS], earnings = -1;
    if (!ccy_ok(ccy)) return ZXM_E_ARG;
    int rc = read_earnings(n, &earnings);
    if (rc != ZXM_OK) return rc;
    rc = coffer_load(&n->books);
    if (rc != ZXM_OK) return rc;

    /* NF4: each holder account in ccy into its form */
    for (uint32_t i = 0; i < n->books.n; i++) {
        const coffer_account_t *a = &n->books.a[i];
        if (a->issuer || strcmp(a->ccy, ccy)) continue;
        uint32_t f = (int64_t) a->id == earnings ? NINEFOLD_FINANCIAL : ninefold_form_of(a->name);
        bal[f][count[f]++] = a->balance;
    }

    /* NF2: each form's sum (Coffer), then what it shows */
    for (uint32_t f = 0; f < NINEFOLD_FORMS; f++) {
        int64_t sum = coffer_holdings(bal[f], count[f]);
        if (sum == COFFER_REFUSED) return ZXM_E_TOOBIG;
        holding[f] = ninefold_form_holding(f, sum);
        if (holding[f] == NINEFOLD_REFUSED) return ZXM_E_TOOBIG;
    }
    /* the priced whole: a state-reserved form's holding is 0 (NF2) */
    int64_t whole = coffer_holdings(holding, NINEFOLD_FORMS);
    if (whole == COFFER_REFUSED || whole > NINEFOLD_MINOR_MAX) return ZXM_E_TOOBIG;

    /* NF3: shares of the priced whole, when it fits */
    bool shared = whole >= 1 && whole <= NINEFOLD_WHOLE_MAX;
    for (uint32_t f = 0; f < NINEFOLD_FORMS; f++)
        share[f] = shared ? ninefold_form_share(f, holding[f], whole) : 0;

    /* NF5: the whole answer fits; now the copy is replaced */
    memcpy(n->ccy, ccy, 4);
    n->earnings = earnings;
    memcpy(n->accounts, count, sizeof count);
    memcpy(n->holding, holding, sizeof holding);
    memcpy(n->share, share, sizeof share);
    n->whole = whole;
    n->shared = shared;
    n->loaded = true;

    uint32_t at = 0, held = 0, reserved = 0;
    for (uint32_t f = 0; f < NINEFOLD_FORMS; f++) {
        if (ninefold_state_reserved(f))
            reserved += count[f];
        else if (holding[f] > 0)
            held++;
    }
    n->notice[0] = 0;
    put(n->notice, sizeof n->notice, &at, "Held in ");
    put_num(n->notice, sizeof n->notice, &at, held);
    put(n->notice, sizeof n->notice, &at, held == 1 ? " priced form of " : " priced forms of ");
    put(n->notice, sizeof n->notice, &at, ccy);
    put(n->notice, sizeof n->notice, &at, "; ");
    put_num(n->notice, sizeof n->notice, &at, reserved);
    put(n->notice, sizeof n->notice, &at,
        reserved == 1 ? " account in a state-reserved form, counted and never priced."
                      : " accounts in state-reserved forms, counted and never priced.");
    if (whole > NINEFOLD_WHOLE_MAX)
        put(n->notice, sizeof n->notice, &at, " The whole is past 14 digits: no shares shown.");
    return ZXM_OK;
}
