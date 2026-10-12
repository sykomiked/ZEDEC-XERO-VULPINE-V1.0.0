/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_num.c — M canonical numbers, M collation, exact decimals and $H
 * dates. Numbers are compared and converted as decimal text and int64,
 * never through a float (R11). */
#include <string.h>

#include "zm_internal.h"

const int64_t zm_pow10[19] = {1LL,
                              10LL,
                              100LL,
                              1000LL,
                              10000LL,
                              100000LL,
                              1000000LL,
                              10000000LL,
                              100000000LL,
                              1000000000LL,
                              10000000000LL,
                              100000000000LL,
                              1000000000000LL,
                              10000000000000LL,
                              100000000000000LL,
                              1000000000000000LL,
                              10000000000000000LL,
                              100000000000000000LL,
                              1000000000000000000LL};

static int is_digit(char c) { return c >= '0' && c <= '9'; }

int zm_cnum_split(const char *s, size_t n, zm_cnum *out) {
    size_t i = 0;
    zm_cnum c;
    memset(&c, 0, sizeof c);
    if (!s || n == 0) return 0;
    if (s[0] == '-') {
        c.neg = 1;
        i = 1;
    }
    c.ip = s + i;
    while (i < n && is_digit(s[i])) i++;
    c.in = (size_t) (s + i - c.ip);
    if (i < n) {
        if (s[i] != '.') return 0;
        i++;
        c.fp = s + i;
        while (i < n && is_digit(s[i])) i++;
        c.fn = (size_t) (s + i - c.fp);
        if (i != n || c.fn == 0 || c.fp[c.fn - 1u] == '0') return 0;
    } else {
        c.fp = s + i;
    }
    if (c.in == 0 && c.fn == 0) return 0;
    if (c.in > 0 && c.ip[0] == '0') {
        /* only "0" itself: not "-0", "00", "0.5" */
        if (c.in != 1 || c.fn != 0 || c.neg) return 0;
    }
    *out = c;
    return 1;
}

int zm_is_canonic(const char *s, size_t n) {
    zm_cnum c;
    size_t lead = 0, trail = 0, total;
    if (!zm_cnum_split(s, n, &c)) return 0;
    if (c.in == 1 && c.ip[0] == '0') return 1; /* zero */
    if (c.in > 47u) return 0;                  /* 1E47 and above overflow */
    total = c.in + c.fn;
    if (c.in == 0)
        while (lead < c.fn && c.fp[lead] == '0') lead++;
    if (c.in == 0 && lead + 1u > 43u) return 0; /* below 1E-43 */
    if (c.fn == 0)
        while (trail < c.in && c.ip[c.in - 1u - trail] == '0') trail++;
    return total - lead - trail <= 18u;
}

/* |a| vs |b| for split canonical numbers. */
static int mag_cmp(const zm_cnum *a, const zm_cnum *b) {
    size_t ain = (a->in == 1 && a->ip[0] == '0') ? 0 : a->in;
    size_t bin = (b->in == 1 && b->ip[0] == '0') ? 0 : b->in;
    size_t m;
    int r;
    if (ain != bin) return ain < bin ? -1 : 1;
    if (ain) {
        r = memcmp(a->ip, b->ip, ain);
        if (r) return r < 0 ? -1 : 1;
    }
    m = a->fn < b->fn ? a->fn : b->fn;
    if (m) {
        r = memcmp(a->fp, b->fp, m);
        if (r) return r < 0 ? -1 : 1;
    }
    if (a->fn != b->fn) return a->fn < b->fn ? -1 : 1;
    return 0;
}

static int sub_class(const char *s, size_t n) {
    if (n == 0) return 0;
    return zm_is_canonic(s, n) ? 1 : 2;
}

int32_t zm_collate(const char *a, size_t an, const char *b, size_t bn, int32_t *out_cmp) {
    int ca, cb, r;
    if (!out_cmp || (an && !a) || (bn && !b)) return ZXV_E_ARG;
    ca = sub_class(a, an);
    cb = sub_class(b, bn);
    if (ca != cb) {
        *out_cmp = ca < cb ? -1 : 1;
        return ZXV_OK;
    }
    if (ca == 0) {
        *out_cmp = 0;
        return ZXV_OK;
    }
    if (ca == 1) {
        zm_cnum x, y;
        zm_cnum_split(a, an, &x);
        zm_cnum_split(b, bn, &y);
        if (x.neg != y.neg) {
            *out_cmp = x.neg ? -1 : 1;
            return ZXV_OK;
        }
        r = mag_cmp(&x, &y);
        *out_cmp = x.neg ? -r : r;
        return ZXV_OK;
    }
    {
        size_t m = an < bn ? an : bn;
        r = m ? memcmp(a, b, m) : 0;
        if (r) *out_cmp = r < 0 ? -1 : 1;
        else *out_cmp = an == bn ? 0 : (an < bn ? -1 : 1);
    }
    return ZXV_OK;
}

int32_t zm_cnum_to_dec(const char *s, size_t n, uint32_t digits, uint32_t scale,
                       int64_t *out_unscaled) {
    zm_cnum c;
    size_t in;
    int64_t v = 0;
    if (!zm_is_canonic(s, n) || !zm_cnum_split(s, n, &c)) return ZM_E_DATA;
    in = (c.in == 1 && c.ip[0] == '0') ? 0 : c.in;
    if (c.fn > scale) return ZM_E_FIT;
    if (in > (size_t) (digits - scale)) return ZM_E_FIT;
    for (size_t i = 0; i < in; i++) v = v * 10 + (c.ip[i] - '0');
    for (uint32_t k = 0; k < scale; k++) v = v * 10 + (k < c.fn ? c.fp[k] - '0' : 0);
    *out_unscaled = c.neg ? -v : v;
    return ZXV_OK;
}

int32_t zm_dec_parse(const char *s, size_t n, uint32_t scale, int64_t *out_unscaled) {
    size_t i = 0, ib, ie, fb, fe;
    int neg = 0;
    int64_t v = 0;
    if (!s || n == 0 || scale > 18u) return ZXV_E_FORMAT;
    if (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    ib = i;
    while (i < n && is_digit(s[i])) i++;
    ie = i;
    fb = fe = i;
    if (i < n && s[i] == '.') {
        fb = ++i;
        while (i < n && is_digit(s[i])) i++;
        fe = i;
    }
    if (i != n || (ie == ib && fe == fb)) return ZXV_E_FORMAT;
    for (size_t k = ib; k < ie; k++) {
        int d = s[k] - '0';
        if (v > (INT64_MAX - d) / 10) return ZM_E_FIT;
        v = v * 10 + d;
    }
    for (uint32_t k = 0; k < scale; k++) {
        int d = (fb + k < fe) ? s[fb + k] - '0' : 0;
        if (v > (INT64_MAX - d) / 10) return ZM_E_FIT;
        v = v * 10 + d;
    }
    for (size_t k = fb + scale; k < fe; k++)
        if (s[k] != '0') return ZM_E_FIT;
    *out_unscaled = neg ? -v : v;
    return ZXV_OK;
}

static size_t u64_digits(uint64_t v, char *t /* 20 */) {
    char r[20];
    size_t k = 0, n = 0;
    do {
        r[k++] = (char) ('0' + (int) (v % 10u));
        v /= 10u;
    } while (v);
    while (k) t[n++] = r[--k];
    return n;
}

static size_t dec_fmt(int64_t unscaled, uint32_t scale, char *b, int canonic) {
    char d[24];
    uint64_t a = unscaled < 0 ? (uint64_t) 0 - (uint64_t) unscaled : (uint64_t) unscaled;
    size_t nd = u64_digits(a, d), k = 0, ilen, flen;
    char frac[24];
    if (unscaled < 0) b[k++] = '-';
    if (scale > 18u) scale = 18u;
    /* integer digits and fraction digits of the padded number */
    if (nd > scale) {
        ilen = nd - scale;
        memcpy(b + k, d, ilen);
        k += ilen;
        memcpy(frac, d + ilen, scale);
    } else {
        ilen = 0;
        memset(frac, '0', scale - nd);
        memcpy(frac + (scale - nd), d, nd);
    }
    flen = scale;
    if (canonic) {
        while (flen && frac[flen - 1u] == '0') flen--;
        if (a == 0) {
            b[0] = '0';
            b[1] = '\0';
            return 1;
        }
    } else if (ilen == 0) {
        b[k++] = '0';
    }
    if (flen) {
        b[k++] = '.';
        memcpy(b + k, frac, flen);
        k += flen;
    }
    b[k] = '\0';
    return k;
}

size_t zm_dec_mtext(int64_t unscaled, uint32_t scale, char *b) {
    return dec_fmt(unscaled, scale, b, 1);
}

size_t zm_dec_text(int64_t unscaled, uint32_t scale, char *b) {
    if (unscaled == 0) {
        /* "0", "0.00": no "-0" */
        size_t k = 0;
        b[k++] = '0';
        if (scale) {
            b[k++] = '.';
            for (uint32_t i = 0; i < scale && i < 18u; i++) b[k++] = '0';
        }
        b[k] = '\0';
        return k;
    }
    return dec_fmt(unscaled, scale, b, 0);
}

uint32_t zm_digits(int64_t v) {
    uint64_t a = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    uint32_t n = 1;
    while (a >= 10u) {
        a /= 10u;
        n++;
    }
    return n;
}

/* ---- dates: days from 1970-01-01 (H. Hinnant's civil algorithms) ---- */

static int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
    int64_t era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, int64_t *m, int64_t *d) {
    int64_t era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}

#define ZM_H_EPOCH (-47117) /* days_from_civil(1840, 12, 31) */

int32_t zm_date_from_h(int64_t h, int32_t *out_year, int32_t *out_month, int32_t *out_day) {
    int64_t y, m, d;
    if (!out_year || !out_month || !out_day) return ZXV_E_ARG;
    if (h < 0 || h > ZM_MAX_DATE) {
        zm_set_errorf("$H day %lld is outside 0..%d (1840-12-31..9999-12-31)", (long long) h,
                      ZM_MAX_DATE);
        return ZM_E_FIT;
    }
    civil_from_days(h + ZM_H_EPOCH, &y, &m, &d);
    *out_year = (int32_t) y;
    *out_month = (int32_t) m;
    *out_day = (int32_t) d;
    return ZXV_OK;
}

int32_t zm_date_to_h(int32_t year, int32_t month, int32_t day, int64_t *out_h) {
    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim;
    int64_t h;
    if (!out_h) return ZXV_E_ARG;
    if (month < 1 || month > 12 || day < 1) {
        zm_set_errorf("%04d-%02d-%02d is not a calendar date", (int) year, (int) month, (int) day);
        return ZXV_E_FORMAT;
    }
    dim = mdays[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) dim = 29;
    if (day > dim) {
        zm_set_errorf("%04d-%02d-%02d is not a calendar date", (int) year, (int) month, (int) day);
        return ZXV_E_FORMAT;
    }
    if (year < 1840 || year > 9999) {
        zm_set_errorf("year %d is outside 1840..9999", (int) year);
        return ZM_E_FIT;
    }
    h = days_from_civil(year, month, day) - ZM_H_EPOCH;
    if (h < 0) {
        zm_set_errorf("%04d-%02d-%02d is before $H day 0 (1840-12-31)", (int) year, (int) month,
                      (int) day);
        return ZM_E_FIT;
    }
    *out_h = h;
    return ZXV_OK;
}
