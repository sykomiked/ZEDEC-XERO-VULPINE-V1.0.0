/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_util.c — see cb_util.h. */
#include "cb_util.h"
#include "zt.h"

cb_u128 cb_mul64(uint64_t a, uint64_t b)
{
    uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    cb_u128 r;
    r.lo = (p00 & 0xffffffffu) | (mid << 32);
    r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return r;
}

bool cb_u128_mul64(cb_u128 a, uint64_t b, cb_u128 *out)
{
    cb_u128 lo = cb_mul64(a.lo, b);
    cb_u128 hi = cb_mul64(a.hi, b);
    if (hi.hi != 0) return false;
    uint64_t h = lo.hi + hi.lo;
    if (h < lo.hi) return false;
    out->hi = h;
    out->lo = lo.lo;
    return true;
}

bool cb_udiv128(cb_u128 n, uint64_t d, uint64_t *q, uint64_t *rem)
{
    if (d == 0 || n.hi >= d) return false; /* quotient would exceed 64 bits */
    uint64_t r = n.hi, qq = 0;
    for (int i = 63; i >= 0; i--) {
        uint64_t carry = r >> 63;
        r = (r << 1) | ((n.lo >> i) & 1u);
        if (carry || r >= d) {
            r -= d;
            qq |= (uint64_t) 1 << i;
        }
    }
    *q = qq;
    if (rem) *rem = r;
    return true;
}

uint64_t cb_pow10(uint32_t e)
{
    uint64_t v = 1;
    if (e > 19) return 0;
    while (e--) v *= 10u;
    return v;
}

bool cb_muldiv_p10(uint64_t a, uint64_t b, uint32_t e, uint64_t d, uint64_t *q, uint64_t *rem)
{
    uint64_t p = cb_pow10(e);
    if (p == 0) return false;
    cb_u128 n = cb_mul64(a, b);
    if (!cb_u128_mul64(n, p, &n)) return false;
    return cb_udiv128(n, d, q, rem);
}

uint64_t cb_udiv64(uint64_t n, uint64_t d, uint64_t *rem)
{
    return zt_udiv64(n, d, rem);
}

/* Checked arithmetic on the compiler builtins: *out untouched on overflow. */
bool cb_add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    uint64_t s;
    if (__builtin_add_overflow(a, b, &s)) return false;
    *out = s;
    return true;
}

bool cb_sadd_ok(int64_t a, int64_t b, int64_t *out)
{
    int64_t s;
    if (__builtin_add_overflow(a, b, &s)) return false;
    *out = s;
    return true;
}

void cb_memset(void *p, uint8_t v, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) p;
    while (n--) *d++ = v;
}

void cb_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--) *d++ = *s++;
}

bool cb_memeq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return false;
    return true;
}

size_t cb_strnlen(const char *s, size_t max)
{
    size_t n = 0;
    if (!s) return 0;
    while (n < max && s[n]) n++;
    return n;
}

bool cb_streq(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

bool cb_strlcpy(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    if (!dst || cap == 0) return false;
    if (!src) {
        dst[0] = 0;
        return true;
    }
    for (; src[i]; i++) {
        if (i + 1 >= cap) {
            dst[0] = 0;
            return false;
        }
        dst[i] = src[i];
    }
    dst[i] = 0;
    return true;
}

bool cb_is_upper(char c)
{
    return c >= 'A' && c <= 'Z';
}

bool cb_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool cb_is_upper_n(const char *s, size_t n)
{
    if (!s) return false;
    for (size_t i = 0; i < n; i++)
        if (!cb_is_upper(s[i])) return false;
    return s[n] == 0;
}

static bool alnum_up(char c)
{
    return cb_is_upper(c) || cb_is_digit(c);
}

bool cb_bic_valid(const char *s)
{
    size_t n = cb_strnlen(s, 12);
    if (n != 8 && n != 11) return false;
    for (size_t i = 0; i < 4; i++)
        if (!alnum_up(s[i])) return false;
    if (!cb_is_upper(s[4]) || !cb_is_upper(s[5])) return false;
    for (size_t i = 6; i < n; i++)
        if (!alnum_up(s[i])) return false;
    return true;
}

/* ===== writer ===== */

void cb_w_init(cb_w *w, char *buf, uint32_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->trunc = (buf == 0 || cap == 0);
    if (!w->trunc) buf[0] = 0;
}

void cb_w_c(cb_w *w, char c)
{
    if (w->trunc) return;
    if (w->len + 1 >= w->cap) {
        w->trunc = true;
        return;
    }
    w->buf[w->len++] = c;
}

void cb_w_s(cb_w *w, const char *s)
{
    if (!s) return;
    while (*s) cb_w_c(w, *s++);
}

void cb_w_esc(cb_w *w, const char *s)
{
    if (!s) return;
    for (; *s; s++) {
        switch (*s) {
        case '&':
            cb_w_s(w, "&amp;");
            break;
        case '<':
            cb_w_s(w, "&lt;");
            break;
        case '>':
            cb_w_s(w, "&gt;");
            break;
        case '"':
            cb_w_s(w, "&quot;");
            break;
        case '\'':
            cb_w_s(w, "&apos;");
            break;
        default:
            cb_w_c(w, *s);
            break;
        }
    }
}

void cb_w_u64_pad(cb_w *w, uint64_t v, uint32_t width)
{
    char t[21];
    uint32_t n = 0;
    do {
        uint64_t r;
        v = cb_udiv64(v, 10u, &r);
        t[n++] = (char) ('0' + r);
    } while (v);
    while (n < width && n < 20) t[n++] = '0';
    while (n) cb_w_c(w, t[--n]);
}

void cb_w_u64(cb_w *w, uint64_t v)
{
    cb_w_u64_pad(w, v, 0);
}

void cb_w_amount(cb_w *w, uint64_t units, uint8_t frac)
{
    uint64_t p = cb_pow10(frac);
    if (frac == 0 || p == 0) {
        cb_w_u64(w, units);
        return;
    }
    uint64_t r;
    uint64_t q = cb_udiv64(units, p, &r);
    cb_w_u64(w, q);
    cb_w_c(w, '.');
    cb_w_u64_pad(w, r, frac);
}

void cb_w_samount(cb_w *w, int64_t units, uint8_t frac)
{
    if (units < 0) {
        cb_w_c(w, '-');
        cb_w_amount(w, (uint64_t) 0 - (uint64_t) units, frac);
    } else {
        cb_w_amount(w, (uint64_t) units, frac);
    }
}

void cb_civil_from_days(uint32_t days, uint32_t *y, uint32_t *m, uint32_t *d)
{
    /* All quantities stay non-negative for days >= 0 with the era shift. */
    uint32_t z = days + 719468u;
    uint32_t era = z / 146097u;
    uint32_t doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t yy = yoe + era * 400u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    uint32_t dd = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t mm = mp < 10u ? mp + 3u : mp - 9u;
    *y = yy + (mm <= 2u ? 1u : 0u);
    *m = mm;
    *d = dd;
}

uint32_t cb_weekday(uint32_t days)
{
    return (days + 3u) % 7u; /* 1970-01-01 was a Thursday (3) */
}

void cb_w_date(cb_w *w, uint64_t unix_s)
{
    uint32_t y, m, d;
    cb_civil_from_days((uint32_t) cb_udiv64(unix_s, 86400u, 0), &y, &m, &d);
    cb_w_u64_pad(w, y, 4);
    cb_w_c(w, '-');
    cb_w_u64_pad(w, m, 2);
    cb_w_c(w, '-');
    cb_w_u64_pad(w, d, 2);
}

void cb_w_datetime(cb_w *w, uint64_t unix_s)
{
    uint64_t sod;
    cb_udiv64(unix_s, 86400u, &sod);
    uint32_t s = (uint32_t) sod;
    cb_w_date(w, unix_s);
    cb_w_c(w, 'T');
    cb_w_u64_pad(w, s / 3600u, 2);
    cb_w_c(w, ':');
    cb_w_u64_pad(w, (s / 60u) % 60u, 2);
    cb_w_c(w, ':');
    cb_w_u64_pad(w, s % 60u, 2);
    cb_w_c(w, 'Z');
}

int32_t cb_w_finish(cb_w *w)
{
    if (!w->buf || w->cap == 0) return -1;
    if (w->trunc) {
        w->buf[w->len < w->cap ? w->len : w->cap - 1] = 0;
        return -1;
    }
    w->buf[w->len] = 0;
    return (int32_t) w->len;
}
