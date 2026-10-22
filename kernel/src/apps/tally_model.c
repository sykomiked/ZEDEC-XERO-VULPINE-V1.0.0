/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tally_model.c — Tally, the calculator (tally_model.h). */
#include <string.h>

#include "tally_model.h"
#include "../pce/pce_entry.h"

/* TM4: ISO 4217 minor units of the currencies in common use (zxvd has no
 * method that reads them; libzxv keeps them behind zxv_ccy_get). */
static const struct {
    char code[4];
    int32_t exponent;
} ISO4217[] = {
    {"AED", 2}, {"AUD", 2}, {"BHD", 3}, {"BRL", 2}, {"CAD", 2}, {"CHF", 2}, {"CLP", 0}, {"CNY", 2},
    {"CZK", 2}, {"DKK", 2}, {"EUR", 2}, {"GBP", 2}, {"HKD", 2}, {"HUF", 2}, {"IDR", 2}, {"ILS", 2},
    {"INR", 2}, {"ISK", 0}, {"JOD", 3}, {"JPY", 0}, {"KRW", 0}, {"KWD", 3}, {"MXN", 2}, {"NOK", 2},
    {"NZD", 2}, {"OMR", 3}, {"PLN", 2}, {"SAR", 2}, {"SEK", 2}, {"SGD", 2}, {"THB", 2}, {"TND", 3},
    {"TRY", 2}, {"USD", 2}, {"VND", 0}, {"ZAR", 2},
};

/* ---- bounded text ------------------------------------------------------- */

static void put(char *out, uint32_t cap, uint32_t *k, const char *s)
{
    for (; *s; s++) {
        if (*k + 1u < cap) out[*k] = *s;
        (*k)++;
    }
    if (cap) out[*k < cap ? *k : cap - 1u] = 0;
}

static void put_u64(char *out, uint32_t cap, uint32_t *k, uint64_t v, uint32_t width)
{
    char d[24];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && n < sizeof d);
    while (n < width && n < sizeof d) d[n++] = '0';
    char s[2] = {0, 0};
    while (n) {
        s[0] = d[--n];
        put(out, cap, k, s);
    }
}

static uint64_t mag(int64_t v)
{
    return v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
}

/* ---- TM1, TM2: the running value and its tape -------------------------- */

void tally_init(tally_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->acc = rat_zero();
}

static bool parse(const char *text, rat_t *v)
{
    if (!text || !text[0] || strlen(text) > TALLY_NUM_MAX) return false;
    rat_t r = rat_from_string(text);
    if (!r.valid || !tally_part_ok(r.num) || !tally_part_ok(r.den)) return false;
    *v = r;
    return true;
}

int tally_enter(tally_t *t, const char *text)
{
    rat_t v;
    if (!parse(text, &v)) return ZXM_E_ARG;
    if (t->ntape >= TALLY_TAPE) return ZXM_E_TOOBIG;
    t->tape[t->ntape++] = (tally_line_t){0, v};
    t->acc = v;
    return ZXM_OK;
}

int tally_apply(tally_t *t, int64_t op, const char *text)
{
    rat_t v, r;
    if (op < TALLY_ADD || op > TALLY_DIV || !parse(text, &v)) return ZXM_E_ARG;
    if (!tally_ratio(op, t->acc.num, t->acc.den, v.num, v.den, &r)) return ZXM_E_ARG;
    if (t->ntape + 2u > TALLY_TAPE) return ZXM_E_TOOBIG;
    t->tape[t->ntape++] = (tally_line_t){op, v};
    t->tape[t->ntape++] = (tally_line_t){0, r};
    t->acc = r;
    return ZXM_OK;
}

void tally_clear(tally_t *t)
{
    memset(t->tape, 0, sizeof t->tape);
    t->ntape = 0;
    t->acc = rat_zero();
}

/* ---- TM3: shown exactly -------------------------------------------------- */

uint32_t tally_show(rat_t r, char *out, uint32_t cap)
{
    if (!out || !cap) return 0;
    if (!r.valid) {
        uint32_t k = 0;
        put(out, cap, &k, "refused");
        return k;
    }
    return rat_to_string(r, out, cap);
}

int64_t tally_reduced_by(int64_t num, int64_t den)
{
    if (den == 0 || num == INT64_MIN || den == INT64_MIN) return -1;
    rat_t r = rat_make(num, den);
    if (!r.valid) return -1;
    return (int64_t) (mag(den) / (uint64_t) r.den);
}

/* ---- TM7: net present value, through the pce_fin block ------------------ */

int tally_npv(const int64_t *cf, uint32_t n, int64_t rnum, int64_t rden, rat_t *out)
{
    if (!cf || !out || n == 0 || n > TALLY_FLOWS || rden < 1 || rnum <= -rden) return ZXM_E_ARG;
    const pce_arg_t in[3] = {{"cf", cf, n}, {"rate_num", &rnum, 1u}, {"rate_den", &rden, 1u}};
    pce_num_t v;
    uint32_t got = 0;
    if (pce_entry_call("fin.npv", in, 3u, &v, 1u, &got) != PCE_OK || got != 1u || v.den < 1)
        return ZXM_E_ARG;
    rat_t r = rat_make(v.num, v.den);
    if (!r.valid) return ZXM_E_ARG;
    *out = r;
    return ZXM_OK;
}

/* ---- TM4: the enabled currencies --------------------------------------- */

static bool code_ok(const char *c)
{
    for (uint32_t i = 0; i < 3u; i++)
        if (c[i] < 'A' || c[i] > 'Z') return false;
    return c[3] == 0;
}

int32_t tally_minor_exponent(const char *code)
{
    if (!code) return -1;
    for (uint32_t i = 0; i < sizeof ISO4217 / sizeof ISO4217[0]; i++)
        if (!strcmp(ISO4217[i].code, code)) return ISO4217[i].exponent;
    return -1;
}

int tally_load(tally_t *t)
{
    static tally_ccy_t got[TALLY_CCY_MAX];
    uint32_t n = 0;
    zxm_begin(t->link, "ledger.accounts");
    int rc = zxm_call(t->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(t->link, t->link->result, "accounts");
    if (arr < 0 || t->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t na = zxm_len(t->link, arr);
    for (uint32_t i = 0; i < na; i++) {
        int32_t o = zxm_at(t->link, arr, i);
        char code[8];
        if (o < 0 || zxm_get_str(t->link, o, "currency", code, sizeof code) != ZXM_OK ||
            !code_ok(code))
            return ZXM_E_PROTOCOL;
        bool seen = false;
        for (uint32_t k = 0; k < n && !seen; k++) seen = !strcmp(got[k].code, code);
        if (seen) continue;
        if (n >= TALLY_CCY_MAX) return ZXM_E_TOOBIG;
        memcpy(got[n].code, code, 4);
        got[n].exponent = tally_minor_exponent(code);
        n++;
    }
    memcpy(t->ccy, got, n * sizeof got[0]);
    t->nccy = n;
    t->loaded = true;
    return ZXM_OK;
}

const tally_ccy_t *tally_currency(const tally_t *t, const char *code)
{
    if (!code) return 0;
    for (uint32_t i = 0; i < t->nccy; i++)
        if (!strcmp(t->ccy[i].code, code)) return &t->ccy[i];
    return 0;
}

uint32_t tally_minor_text(int64_t minor, int32_t exponent, const char *code, char *out,
                          uint32_t cap)
{
    uint32_t k = 0;
    if (!out || !cap) return 0;
    out[0] = 0;
    if (minor < 0) put(out, cap, &k, "-");
    uint64_t m = mag(minor);
    if (exponent > 0 && exponent <= 18) {
        uint64_t scale = 1;
        for (int32_t i = 0; i < exponent; i++) scale *= 10u;
        put_u64(out, cap, &k, m / scale, 1);
        put(out, cap, &k, ".");
        put_u64(out, cap, &k, m % scale, (uint32_t) exponent);
    } else {
        put_u64(out, cap, &k, m, 1);
        if (exponent < 0) put(out, cap, &k, " minor units of");
    }
    put(out, cap, &k, " ");
    put(out, cap, &k, code ? code : "");
    return k;
}

/* ---- TM5: a conversion, shown ------------------------------------------- */

int tally_convert(const tally_t *t, const char *from, const char *to, int64_t amount, int64_t rnum,
                  int64_t rden, tally_fx_t *fx)
{
    if (!t->loaded) return ZXM_E_STATE;
    const tally_ccy_t *a = tally_currency(t, from), *b = tally_currency(t, to);
    if (!a || !b || !fx || !tally_fx_domain(amount, rnum, rden)) return ZXM_E_ARG;
    tally_fx_t r;
    memset(&r, 0, sizeof r);
    r.amount = amount;
    r.rnum = rnum;
    r.rden = rden;
    r.whole = tally_fx_amount(amount, rnum, rden);
    r.rest = tally_fx_remainder(amount, rnum, rden);
    char part[48];
    uint32_t k = 0;
    tally_minor_text(amount, a->exponent, a->code, part, sizeof part);
    put(r.text, sizeof r.text, &k, part);
    put(r.text, sizeof r.text, &k, " at ");
    put_u64(r.text, sizeof r.text, &k, (uint64_t) rnum, 1);
    put(r.text, sizeof r.text, &k, "/");
    put_u64(r.text, sizeof r.text, &k, (uint64_t) rden, 1);
    put(r.text, sizeof r.text, &k, " is ");
    tally_minor_text(r.whole, b->exponent, b->code, part, sizeof part);
    put(r.text, sizeof r.text, &k, part);
    if (r.rest) {
        put(r.text, sizeof r.text, &k, " and ");
        put_u64(r.text, sizeof r.text, &k, (uint64_t) r.rest, 1);
        put(r.text, sizeof r.text, &k, "/");
        put_u64(r.text, sizeof r.text, &k, (uint64_t) rden, 1);
        put(r.text, sizeof r.text, &k, " of a minor unit");
    }
    put(r.text, sizeof r.text, &k, " (shown, not posted)");
    if (k >= sizeof r.text) return ZXM_E_TOOBIG;
    *fx = r;
    return ZXM_OK;
}

/* ---- TM6: units ------------------------------------------------------------ */

int64_t tally_unit_index(const char *name)
{
    if (!name) return -1;
    for (int64_t i = 0; i < TALLY_UNITS; i++)
        if (!strcmp(tally_units[i].name, name)) return i;
    return -1;
}

int tally_unit(int64_t qty, const char *src, const char *dst, rat_t *r)
{
    rat_t x;
    if (!r || !tally_unit_convert(qty, tally_unit_index(src), tally_unit_index(dst), &x))
        return ZXM_E_ARG;
    *r = x;
    return ZXM_OK;
}
