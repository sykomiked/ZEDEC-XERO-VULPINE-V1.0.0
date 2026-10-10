/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* i18n_format.c — integer-exact number, currency and date formatting from the
 * CLDR patterns in i18n_tables.c. See i18n.h. */
#include "i18n_internal.h"

#define POOL(off) (i18n_pool + (off))

static const i18n_locale_t *loc_or_en(const i18n_locale_t *l)
{
    return l ? l : i18n_locale_default();
}

/* ===== digits ===== */

static const uint64_t P10[20] = {1ull,
                                 10ull,
                                 100ull,
                                 1000ull,
                                 10000ull,
                                 100000ull,
                                 1000000ull,
                                 10000000ull,
                                 100000000ull,
                                 1000000000ull,
                                 10000000000ull,
                                 100000000000ull,
                                 1000000000000ull,
                                 10000000000000ull,
                                 100000000000000ull,
                                 1000000000000000ull,
                                 10000000000000000ull,
                                 100000000000000000ull,
                                 1000000000000000000ull,
                                 10000000000000000000ull};

/* Decimal digits of x, most significant first, by repeated subtraction. */
static uint32_t to_digits(uint64_t x, uint8_t dig[20])
{
    uint32_t nd = 0;
    for (int k = 19; k >= 0; k--) {
        uint8_t d = 0;
        while (x >= P10[k]) {
            x -= P10[k];
            d++;
        }
        if (d || nd || k == 0) dig[nd++] = d;
    }
    return nd;
}

/* Expand a pattern affix: control bytes become the locale's symbols. */
static void affix(i18n_w_t *w, const i18n_numfmt_t *nf, const char *a, const char *cur)
{
    for (; *a; a++) {
        switch ((uint8_t) *a) {
        case I18N_AFX_CURRENCY:
            i18n__w_str(w, cur ? cur : "");
            break;
        case I18N_AFX_MINUS:
            i18n__w_str(w, POOL(nf->minus));
            break;
        case I18N_AFX_PLUS:
            i18n__w_str(w, POOL(nf->plus));
            break;
        case I18N_AFX_PERCENT:
            i18n__w_str(w, POOL(nf->percent));
            break;
        case I18N_AFX_PERMILLE:
            i18n__w_str(w, "\xE2\x80\xB0");
            break;
        default:
            i18n__w_bytes(w, a, 1);
        }
    }
}

/* The number body: grouped integer digits, decimal separator, fraction. */
static void body(i18n_w_t *w, const i18n_locale_t *l, const i18n_numfmt_t *nf, uint64_t mag,
                 uint32_t frac, uint8_t groups)
{
    uint8_t dig[24];
    uint32_t nd = to_digits(mag, dig);
    const uint32_t *ds = i18n_digitsets[nf->digitset];
    uint32_t g1 = groups & 0x0Fu, g2 = (uint32_t) (groups >> 4);
    (void) l;
    if (frac > 19) frac = 19;
    while (nd <= frac) {
        for (uint32_t k = nd; k > 0; k--) dig[k] = dig[k - 1];
        dig[0] = 0;
        nd++;
    }
    uint32_t ni = nd - frac;
    bool grouping = g1 > 0 && ni >= g1 + nf->min_group;
    for (uint32_t k = 0; k < ni; k++) {
        i18n__w_cp(w, ds[dig[k]]);
        uint32_t left = ni - 1 - k; /* digits still to write */
        if (grouping && left > 0) {
            bool sep;
            if (left == g1)
                sep = true;
            else if (left > g1 && g2 > 0) {
                /* left - g1 must be a multiple of g2; 32-bit modulo */
                uint32_t r = left - g1;
                sep = (r - (r / g2) * g2) == 0;
            } else
                sep = false;
            if (sep) i18n__w_str(w, POOL(nf->group));
        }
    }
    if (frac) {
        i18n__w_str(w, POOL(nf->decimal));
        for (uint32_t k = ni; k < nd; k++) i18n__w_cp(w, ds[dig[k]]);
    }
}

static uint64_t magnitude(int64_t v, bool *neg)
{
    *neg = v < 0;
    return *neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
}

static int32_t fmt_plain(const i18n_locale_t *l, int64_t v, uint32_t frac, int which, char *out,
                         uint32_t cap)
{
    i18n_w_t w;
    bool neg;
    l = loc_or_en(l);
    const i18n_numfmt_t *nf = &i18n_numfmts[l->numfmt];
    uint64_t mag = magnitude(v, &neg);
    const uint32_t *a = &nf->affix[which * 4];
    i18n__w_init(&w, out, cap);
    affix(&w, nf, POOL(a[neg ? 2 : 0]), 0);
    body(&w, l, nf, mag, frac, nf->groups[which]);
    affix(&w, nf, POOL(a[neg ? 3 : 1]), 0);
    return i18n__w_end(&w);
}

int32_t i18n_fmt_int(const i18n_locale_t *l, int64_t v, char *out, uint32_t cap)
{
    return fmt_plain(l, v, 0, 0, out, cap);
}

int32_t i18n_fmt_decimal(const i18n_locale_t *l, int64_t scaled, uint32_t frac_digits, char *out,
                         uint32_t cap)
{
    return fmt_plain(l, scaled, frac_digits, 0, out, cap);
}

int32_t i18n_fmt_percent(const i18n_locale_t *l, int64_t scaled, uint32_t frac_digits, char *out,
                         uint32_t cap)
{
    return fmt_plain(l, scaled, frac_digits, 1, out, cap);
}

/* ===== currency ===== */

static bool code_eq(const char *a, const char *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && b[3] == 0;
}

static const i18n_sym_t *sym_in(uint16_t set, const char *iso)
{
    const i18n_symset_t *s = &i18n_symsets[set];
    for (uint32_t k = 0; k < s->count; k++)
        if (code_eq(i18n_syms[s->start + k].code, iso)) return &i18n_syms[s->start + k];
    return 0;
}

static const i18n_sym_t *sym_find(const i18n_locale_t *l, const char *iso)
{
    if (!iso || i18n__strlen(iso) != 3) return 0;
    for (const i18n_locale_t *c = l; c; c = i18n_locale_parent(c)) {
        const i18n_sym_t *s = sym_in(c->symset, iso);
        if (s) return s;
    }
    return sym_in(i18n_root_symset, iso);
}

const char *i18n_currency_symbol(const i18n_locale_t *l, const char *iso, bool narrow)
{
    const i18n_sym_t *s = sym_find(loc_or_en(l), iso);
    if (!s) return iso ? iso : "";
    return POOL(narrow ? s->narrow : s->symbol);
}

uint32_t i18n_currency_digits(const char *iso)
{
    if (!iso) return 2;
    for (uint32_t i = 0; i < i18n_curdigits_count; i++)
        if (code_eq(i18n_curdigits[i].code, iso)) return i18n_curdigits[i].digits;
    return 2;
}

/* Is cp a symbol (S*) or separator (Z*)? Enough of each for currency
 * symbols a caller may pass that are not in the CLDR tables. */
static bool sym_or_space(uint32_t cp)
{
    if (cp == 0x20 || cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F ||
        cp == 0x205F || cp == 0x3000)
        return true;
    if (cp == '$' || cp == '+' || cp == '<' || cp == '=' || cp == '>' || cp == '^' || cp == '`' ||
        cp == '|' || cp == '~')
        return true;
    if ((cp >= 0xA2 && cp <= 0xA9) || cp == 0xAC || cp == 0xAE || cp == 0xB0 || cp == 0xB1 ||
        cp == 0xB4 || cp == 0xD7 || cp == 0xF7)
        return true;
    if ((cp >= 0x20A0 && cp <= 0x20C0) || cp == 0x058F || cp == 0x060B || cp == 0x09F2 ||
        cp == 0x09F3 || cp == 0x0AF1 || cp == 0x0BF9 || cp == 0x0E3F || cp == 0x17DB ||
        cp == 0xFDFC || (cp >= 0x2100 && cp <= 0x2BFF))
        return true;
    return false;
}

static uint32_t first_cp(const char *s)
{
    uint32_t cp = 0;
    (void) i18n_utf8_decode((const uint8_t *) s, i18n__strlen(s), &cp);
    return cp;
}

static uint32_t last_cp(const char *s)
{
    uint32_t n = i18n__strlen(s), k = n, cp = 0;
    if (!n) return 0;
    k--;
    while (k > 0 && ((uint8_t) s[k] & 0xC0) == 0x80) k--;
    (void) i18n_utf8_decode((const uint8_t *) s + k, n - k, &cp);
    return cp;
}

static bool ends_with_cur(const char *a)
{
    uint32_t n = i18n__strlen(a);
    return n > 0 && (uint8_t) a[n - 1] == I18N_AFX_CURRENCY;
}

int32_t i18n_fmt_currency(const i18n_locale_t *l, int64_t minor, const char *iso, uint32_t exponent,
                          uint32_t flags, const char *symbol, char *out, uint32_t cap)
{
    i18n_w_t w;
    bool neg, alpha_before, alpha_after;
    l = loc_or_en(l);
    const i18n_numfmt_t *nf = &i18n_numfmts[l->numfmt];
    const char *sym;
    const i18n_sym_t *s = 0;
    if (symbol) {
        sym = symbol;
    } else if (flags & I18N_CUR_CODE) {
        sym = iso ? iso : "";
    } else {
        s = sym_find(l, iso);
        sym = s ? POOL((flags & I18N_CUR_NARROW) ? s->narrow : s->symbol) : (iso ? iso : "");
    }
    if (s) {
        bool nar = (flags & I18N_CUR_NARROW) != 0;
        alpha_before = !(s->flags & (nar ? 8u : 2u)); /* symbol's last char faces the number */
        alpha_after = !(s->flags & (nar ? 4u : 1u));
    } else {
        alpha_before = !sym_or_space(last_cp(sym));
        alpha_after = !sym_or_space(first_cp(sym));
    }
    uint64_t mag = magnitude(minor, &neg);
    /* choose the pattern: the alphaNextToNumber variant when the symbol is
     * alphabetic and sits against the number */
    const uint32_t *std = &nf->affix[8], *alt = &nf->affix[12];
    const char *pre = POOL(std[neg ? 2 : 0]), *suf = POOL(std[neg ? 3 : 1]);
    uint8_t groups = nf->groups[2];
    bool touch_before = ends_with_cur(pre);
    bool touch_after = (uint8_t) suf[0] == I18N_AFX_CURRENCY;
    if ((touch_before && alpha_before) || (touch_after && alpha_after)) {
        pre = POOL(alt[neg ? 2 : 0]);
        suf = POOL(alt[neg ? 3 : 1]);
        groups = nf->groups[3];
        touch_before = ends_with_cur(pre);
        touch_after = (uint8_t) suf[0] == I18N_AFX_CURRENCY;
    }
    i18n__w_init(&w, out, cap);
    affix(&w, nf, pre, sym);
    if (touch_before && alpha_before) i18n__w_str(&w, POOL(nf->ins_before));
    body(&w, l, nf, mag, exponent, groups);
    if (touch_after && alpha_after) i18n__w_str(&w, POOL(nf->ins_after));
    affix(&w, nf, suf, sym);
    return i18n__w_end(&w);
}

/* ===== dates ===== */

/* Howard Hinnant's civil-from-days, in 32-bit arithmetic (floor division by
 * positive constants only). Valid for the whole int32_t day range used here
 * (years -5,000,000 .. +5,000,000). */
static int32_t floordiv(int32_t a, int32_t b)
{
    int32_t q = a / b;
    if ((a % b) != 0 && ((a < 0) != (b < 0))) q--;
    return q;
}

void i18n_civil_from_days(int32_t days, int32_t *y, uint32_t *m, uint32_t *d)
{
    int32_t z = days + 719468;
    int32_t era = floordiv(z, 146097);
    uint32_t doe = (uint32_t) (z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yy = (int32_t) yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t dd = doy - (153 * mp + 2) / 5 + 1;
    uint32_t mm = mp < 10 ? mp + 3 : mp - 9;
    if (y) *y = yy + (mm <= 2 ? 1 : 0);
    if (m) *m = mm;
    if (d) *d = dd;
}

int32_t i18n_days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2 ? 1 : 0;
    int32_t era = floordiv(y, 400);
    uint32_t yoe = (uint32_t) (y - era * 400);
    uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t) doe - 719468;
}

uint32_t i18n_weekday(int32_t days)
{
    /* 1970-01-01 was a Thursday (4) */
    int32_t r = (days + 4) % 7;
    return (uint32_t) (r < 0 ? r + 7 : r);
}

const char *i18n_month_name(const i18n_locale_t *l, uint32_t month, bool wide, bool standalone)
{
    l = loc_or_en(l);
    if (month < 1 || month > 12) return "";
    const i18n_datefmt_t *df = &i18n_datefmts[l->datefmt];
    uint16_t set = standalone ? (wide ? df->mon_sa_wide : df->mon_sa_abbr)
                              : (wide ? df->mon_wide : df->mon_abbr);
    return POOL(i18n_monthsets[set][month - 1]);
}

const char *i18n_day_name(const i18n_locale_t *l, uint32_t weekday, bool wide)
{
    l = loc_or_en(l);
    if (weekday > 6) return "";
    const i18n_datefmt_t *df = &i18n_datefmts[l->datefmt];
    return POOL(i18n_daysets[wide ? df->day_wide : df->day_abbr][weekday]);
}

static void num_pad(i18n_w_t *w, const i18n_numfmt_t *nf, uint32_t v, uint32_t width)
{
    uint8_t dig[24];
    uint32_t nd = to_digits(v, dig);
    const uint32_t *ds = i18n_digitsets[nf->digitset];
    for (uint32_t k = nd; k < width; k++) i18n__w_cp(w, ds[0]);
    for (uint32_t k = 0; k < nd; k++) i18n__w_cp(w, ds[dig[k]]);
}

int32_t i18n_fmt_date(const i18n_locale_t *l, int32_t days, i18n_date_style_t style, char *out,
                      uint32_t cap)
{
    i18n_w_t w;
    int32_t y;
    uint32_t m, d;
    l = loc_or_en(l);
    const i18n_numfmt_t *nf = &i18n_numfmts[l->numfmt];
    const i18n_datefmt_t *df = &i18n_datefmts[l->datefmt];
    i18n__w_init(&w, out, cap);
    if ((uint32_t) style > 3u) style = I18N_DATE_MEDIUM;
    i18n_civil_from_days(days, &y, &m, &d);
    if (y < 1) { /* BCE dates are not supported */
        (void) i18n__w_end(&w);
        return -1;
    }
    uint32_t wd = i18n_weekday(days);
    const char *p = POOL(df->pattern[style]);
    for (; *p; p++) {
        switch ((uint8_t) *p) {
        case I18N_DF_D:
            num_pad(&w, nf, d, 1);
            break;
        case I18N_DF_DD:
            num_pad(&w, nf, d, 2);
            break;
        case I18N_DF_M:
            num_pad(&w, nf, m, 1);
            break;
        case I18N_DF_MM:
            num_pad(&w, nf, m, 2);
            break;
        case I18N_DF_MMM:
            i18n__w_str(&w, i18n_month_name(l, m, false, false));
            break;
        case I18N_DF_MMMM:
            i18n__w_str(&w, i18n_month_name(l, m, true, false));
            break;
        case I18N_DF_LLL:
            i18n__w_str(&w, i18n_month_name(l, m, false, true));
            break;
        case I18N_DF_LLLL:
            i18n__w_str(&w, i18n_month_name(l, m, true, true));
            break;
        case I18N_DF_Y:
            num_pad(&w, nf, (uint32_t) y, 1);
            break;
        case I18N_DF_YY: {
            uint32_t yy = (uint32_t) y;
            num_pad(&w, nf, yy - (yy / 100u) * 100u, 2);
            break;
        }
        case I18N_DF_YYYY:
            num_pad(&w, nf, (uint32_t) y, 4);
            break;
        case I18N_DF_E:
            i18n__w_str(&w, i18n_day_name(l, wd, false));
            break;
        case I18N_DF_EEEE:
            i18n__w_str(&w, i18n_day_name(l, wd, true));
            break;
        case I18N_DF_ERA:
            i18n__w_str(&w, POOL(df->era));
            break;
        default:
            i18n__w_bytes(&w, p, 1);
        }
    }
    return i18n__w_end(&w);
}
