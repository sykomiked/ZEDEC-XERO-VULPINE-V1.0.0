/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zs_codec.c — record bytes <-> exact values.
 *
 *   DISPLAY  one ASCII digit per byte; the sign overpunched on the first
 *            or last digit (IBM or ASCII convention), or a separate '+'
 *            or '-' byte before or after the digits
 *   COMP     big-endian binary, 2 bytes for 1-4 digits, 4 for 5-9, 8 for
 *            10-18; two's complement when signed; the value must fit the
 *            picture's digits (TRUNC(STD))
 *   COMP-3   packed decimal, two digits per byte, the sign in the last
 *            nibble (C or F positive, D negative; A, E and B accepted on
 *            input as IBM does)
 *
 * Every value is an int64 of at most 18 digits with an implied scale, so
 * there is no rounding anywhere and no float (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zs_internal.h"

const int64_t zs_pow10[19] = {1LL,
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

static const char k_ibm_pos[] = "{ABCDEFGHI";
static const char k_ibm_neg[] = "}JKLMNOPQR";

size_t zs_decimal_text(int64_t unscaled, uint32_t scale, char *b) {
    char d[24];
    size_t nd = 0, k = 0;
    uint64_t u = unscaled < 0 ? (uint64_t) 0 - (uint64_t) unscaled : (uint64_t) unscaled;
    do {
        d[nd++] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    while (nd < (size_t) scale + 1u) d[nd++] = '0';
    if (unscaled < 0) b[k++] = '-';
    while (nd) {
        if (scale && nd == scale) b[k++] = '.';
        b[k++] = d[--nd];
    }
    b[k] = '\0';
    return k;
}

/* overpunch: one character -> digit and sign; 0 if not valid */
static int op_decode(int32_t style, uint8_t ch, int *digit, int *neg) {
    if (ch >= '0' && ch <= '9') {
        *digit = ch - '0';
        *neg = 0;
        return 1;
    }
    if (style == ZS_OVERPUNCH_ASCII) {
        if (ch >= 'p' && ch <= 'y') {
            *digit = ch - 'p';
            *neg = 1;
            return 1;
        }
        return 0;
    }
    for (int i = 0; i < 10; i++) {
        if (ch == (uint8_t) k_ibm_pos[i]) {
            *digit = i;
            *neg = 0;
            return 1;
        }
        if (ch == (uint8_t) k_ibm_neg[i]) {
            *digit = i;
            *neg = 1;
            return 1;
        }
    }
    return 0;
}

static uint8_t op_encode(int32_t style, int digit, int neg) {
    if (style == ZS_OVERPUNCH_ASCII) return (uint8_t) (neg ? 'p' + digit : '0' + digit);
    return (uint8_t) (neg ? k_ibm_neg[digit] : k_ibm_pos[digit]);
}

static int32_t bad(const zs_item *it, const char *fmt, unsigned a) {
    char m[160];
    snprintf(m, sizeof m, fmt, a);
    zs_set_errorf("%s: %s", it->f.name, m);
    return ZS_E_DATA;
}

int32_t zs_num_decode(const zs_layout *l, const zs_item *it, const uint8_t *p, int64_t *out) {
    const zs_field *f = &it->f;
    int64_t v = 0;
    int neg = 0;
    if (f->usage == ZS_U_DISPLAY) {
        uint32_t nd = f->digits;
        const uint8_t *s = p + (f->sign == ZS_SIGN_LEADING_SEP ? 1 : 0);
        if (f->sign == ZS_SIGN_LEADING_SEP || f->sign == ZS_SIGN_TRAILING_SEP) {
            uint8_t sc = f->sign == ZS_SIGN_LEADING_SEP ? p[0] : p[nd];
            if (sc != '+' && sc != '-')
                return bad(it, "separate sign byte 0x%02x is not '+' or '-'", sc);
            neg = sc == '-';
        }
        for (uint32_t k = 0; k < nd; k++) {
            int dg, ng = 0;
            int over = (f->sign == ZS_SIGN_TRAILING && k == nd - 1u) ||
                       (f->sign == ZS_SIGN_LEADING && k == 0);
            if (over) {
                if (!op_decode(l->overpunch, s[k], &dg, &ng))
                    return bad(it, "byte 0x%02x is not a digit with an overpunched sign "
                                   "(check zs_layout_set_overpunch)",
                               s[k]);
                neg = ng;
            } else {
                if (s[k] < '0' || s[k] > '9')
                    return bad(it, "byte 0x%02x is not a digit", s[k]);
                dg = s[k] - '0';
            }
            v = v * 10 + dg;
        }
    } else if (f->usage == ZS_U_COMP) {
        uint64_t u = 0;
        for (uint64_t k = 0; k < f->size; k++) u = (u << 8) | p[k];
        if (f->sign != ZS_SIGN_NONE) {
            /* sign-extend from size bytes */
            uint32_t bits = (uint32_t) (f->size * 8u);
            if (bits < 64u && (u >> (bits - 1u)) & 1u) u |= ~(uint64_t) 0 << bits;
            if (u >> 63) {
                neg = 1;
                u = (uint64_t) 0 - u;
            }
        }
        if (u > (uint64_t) (zs_pow10[f->digits] - 1))
            return bad(it, "binary value does not fit the %u digits of its picture", f->digits);
        v = (int64_t) u;
    } else {
        uint32_t nn = (uint32_t) (f->size * 2u), sg;
        for (uint32_t k = 0; k + 1u < nn; k++) {
            uint32_t nib = (k & 1u) ? (p[k / 2u] & 0x0Fu) : (uint32_t) (p[k / 2u] >> 4);
            if (nib > 9u) return bad(it, "packed digit nibble 0x%x is not 0-9", nib);
            if (k == 0 && nn - 1u > f->digits && nib != 0)
                return bad(it, "the pad nibble of an even-digit packed field is %u, not 0", nib);
            v = v * 10 + (int64_t) nib;
        }
        sg = p[f->size - 1u] & 0x0Fu;
        if (sg == 0xBu || sg == 0xDu) neg = 1;
        else if (sg < 0xAu) return bad(it, "packed sign nibble 0x%x is not A-F", sg);
        if (neg && f->sign == ZS_SIGN_NONE)
            return bad(it, "negative sign nibble 0x%x in an unsigned packed field", sg);
    }
    *out = neg ? -v : v;
    return ZXV_OK;
}

int32_t zs_num_encode(const zs_layout *l, const zs_item *it, uint8_t *p, int64_t v) {
    const zs_field *f = &it->f;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    if (f->usage == ZS_U_DISPLAY) {
        uint32_t nd = f->digits;
        uint8_t *s = p + (f->sign == ZS_SIGN_LEADING_SEP ? 1 : 0);
        for (uint32_t k = nd; k-- > 0;) {
            s[k] = (uint8_t) ('0' + (int) (u % 10u));
            u /= 10u;
        }
        if (f->sign == ZS_SIGN_TRAILING) s[nd - 1u] = op_encode(l->overpunch, s[nd - 1u] - '0', neg);
        if (f->sign == ZS_SIGN_LEADING) s[0] = op_encode(l->overpunch, s[0] - '0', neg);
        if (f->sign == ZS_SIGN_LEADING_SEP) p[0] = neg ? '-' : '+';
        if (f->sign == ZS_SIGN_TRAILING_SEP) p[nd] = neg ? '-' : '+';
    } else if (f->usage == ZS_U_COMP) {
        uint64_t w = (uint64_t) v; /* two's complement */
        for (uint64_t k = f->size; k-- > 0;) {
            p[k] = (uint8_t) (w & 0xFFu);
            w >>= 8;
        }
    } else {
        uint32_t nn = (uint32_t) (f->size * 2u);
        memset(p, 0, (size_t) f->size);
        p[f->size - 1u] = (uint8_t) (f->sign == ZS_SIGN_NONE ? 0x0Fu : neg ? 0x0Du : 0x0Cu);
        for (uint32_t k = nn - 1u; k-- > 0 && u;) {
            uint8_t nib = (uint8_t) (u % 10u);
            u /= 10u;
            if (k & 1u) p[k / 2u] |= nib;
            else p[k / 2u] |= (uint8_t) (nib << 4);
        }
    }
    return ZXV_OK;
}

/* ===== the item a reference names ===== */

static int32_t resolve(const zs_layout *l, const uint8_t *rec, size_t rec_len, const char *ref,
                       const zs_item **out_it, uint64_t *out_off) {
    uint32_t idx;
    int32_t st;
    if (!l || !rec || !ref) return ZXV_E_ARG;
    if (rec_len < l->size) {
        zs_set_errorf("record is %zu bytes; the layout needs %llu", rec_len,
                      (unsigned long long) l->size);
        return ZXV_E_ARG;
    }
    if ((st = zs_find(l, ref, &idx, out_off)) != ZXV_OK) return st;
    *out_it = &l->items[idx];
    return ZXV_OK;
}

static int32_t need_num(const zs_item *it) {
    if (it->f.kind == ZS_K_NUM) return ZXV_OK;
    zs_set_errorf("%s is not a numeric item", it->f.name);
    return ZXV_E_ARG;
}

int32_t zs_get_decimal(const zs_layout *l, const uint8_t *rec, size_t rec_len, const char *ref,
                       int64_t *out_unscaled, uint32_t *out_scale) {
    const zs_item *it;
    uint64_t off;
    int64_t v;
    int32_t st;
    if (!out_unscaled || !out_scale) return ZXV_E_ARG;
    if ((st = resolve(l, rec, rec_len, ref, &it, &off)) != ZXV_OK) return st;
    if ((st = need_num(it)) != ZXV_OK) return st;
    if ((st = zs_num_decode(l, it, rec + off, &v)) != ZXV_OK) return st;
    *out_unscaled = v;
    *out_scale = it->f.scale;
    return ZXV_OK;
}

int32_t zs_get_rat(const zs_layout *l, const uint8_t *rec, size_t rec_len, const char *ref,
                   int64_t *out_num, int64_t *out_den) {
    int64_t v;
    uint32_t sc;
    int32_t st;
    if (!out_num || !out_den) return ZXV_E_ARG;
    if ((st = zs_get_decimal(l, rec, rec_len, ref, &v, &sc)) != ZXV_OK) return st;
    return zxv_rat_normalize(v, zs_pow10[sc], out_num, out_den);
}

int32_t zs_get_value(const zs_layout *l, const uint8_t *rec, size_t rec_len, const char *ref,
                     char *buf, size_t cap, size_t *out_len) {
    const zs_item *it;
    uint64_t off;
    int32_t st;
    if (!out_len) return ZXV_E_ARG;
    if ((st = resolve(l, rec, rec_len, ref, &it, &off)) != ZXV_OK) return st;
    if (it->f.kind == ZS_K_NUM) {
        char t[32];
        int64_t v;
        size_t n;
        if ((st = zs_num_decode(l, it, rec + off, &v)) != ZXV_OK) return st;
        n = zs_decimal_text(v, it->f.scale, t);
        return zs_out_text(t, n, buf, cap, out_len);
    }
    return zs_out_text((const char *) rec + off, (size_t) it->f.size, buf, cap, out_len);
}

int32_t zs_put_decimal(const zs_layout *l, uint8_t *rec, size_t rec_len, const char *ref,
                       int64_t v, uint32_t scale) {
    const zs_item *it;
    uint64_t off;
    int32_t st;
    char t[32];
    uint8_t tmp[24]; /* the largest item: 18 digits + a separate sign */
    if ((st = resolve(l, rec, rec_len, ref, &it, &off)) != ZXV_OK) return st;
    if ((st = need_num(it)) != ZXV_OK) return st;
    if (scale > ZS_MAX_DIGITS || v == INT64_MIN) {
        zs_set_errorf("%s: scale above 18 or INT64_MIN", it->f.name);
        return ZXV_E_ARG;
    }
    zs_decimal_text(v, scale, t);
    if (scale > it->f.scale) {
        int64_t div = zs_pow10[scale - it->f.scale];
        if (v % div != 0) {
            zs_set_errorf("%s: %s has more than the %u decimals of the field", it->f.name, t,
                          (unsigned) it->f.scale);
            return ZS_E_FIT;
        }
        v /= div;
    } else if (scale < it->f.scale) {
        int64_t mul = zs_pow10[it->f.scale - scale];
        if (v > INT64_MAX / mul || v < -(INT64_MAX / mul)) {
            zs_set_errorf("%s: %s has more digits than the field holds", it->f.name, t);
            return ZS_E_FIT;
        }
        v *= mul;
    }
    if (v > zs_pow10[it->f.digits] - 1 || v < -(zs_pow10[it->f.digits] - 1)) {
        zs_set_errorf("%s: %s has more digits than the field's %u", it->f.name, t,
                      (unsigned) it->f.digits);
        return ZS_E_FIT;
    }
    if (v < 0 && it->f.sign == ZS_SIGN_NONE) {
        zs_set_errorf("%s: %s is negative and the field is unsigned (no S)", it->f.name, t);
        return ZS_E_FIT;
    }
    zs_num_encode(l, it, tmp, v);
    memcpy(rec + off, tmp, (size_t) it->f.size);
    return ZXV_OK;
}

int32_t zs_put_rat(const zs_layout *l, uint8_t *rec, size_t rec_len, const char *ref, int64_t num,
                   int64_t den) {
    const zs_item *it;
    uint64_t off;
    int64_t n2, d2;
    int32_t st;
    if ((st = resolve(l, rec, rec_len, ref, &it, &off)) != ZXV_OK) return st;
    if ((st = need_num(it)) != ZXV_OK) return st;
    if ((st = zxv_rat_mul(num, den, zs_pow10[it->f.scale], 1, &n2, &d2)) != ZXV_OK) {
        if (st == ZXV_E_RANGE) {
            zs_set_errorf("%s: %lld/%lld does not fit the field", it->f.name, (long long) num,
                          (long long) den);
            return ZS_E_FIT;
        }
        return st;
    }
    if (d2 != 1) {
        zs_set_errorf("%s: %lld/%lld is not a whole number of units of 10^-%u", it->f.name,
                      (long long) num, (long long) den, (unsigned) it->f.scale);
        return ZS_E_FIT;
    }
    return zs_put_decimal(l, rec, rec_len, ref, n2, it->f.scale);
}

int32_t zs_put_value(const zs_layout *l, uint8_t *rec, size_t rec_len, const char *ref,
                     const char *text, size_t text_len) {
    const zs_item *it;
    uint64_t off;
    int32_t st;
    if (!text && text_len) return ZXV_E_ARG;
    if ((st = resolve(l, rec, rec_len, ref, &it, &off)) != ZXV_OK) return st;
    if (it->f.kind == ZS_K_NUM) {
        char t[64];
        int64_t n, d;
        if (text_len == 0 || text_len >= sizeof t || memchr(text, 0, text_len)) {
            zs_set_errorf("%s: expected exact decimal text such as -123.45 or 3/4", it->f.name);
            return ZXV_E_FORMAT;
        }
        memcpy(t, text, text_len);
        t[text_len] = '\0';
        if ((st = zxv_rat_parse(t, &n, &d)) != ZXV_OK) {
            zs_set_errorf("%s: '%s' is not an exact number (%s)", it->f.name, t,
                          zxv_status_name(st));
            return st == ZXV_E_RANGE ? ZS_E_FIT : st;
        }
        return zs_put_rat(l, rec, rec_len, ref, n, d);
    }
    if (text_len > it->f.size) {
        zs_set_errorf("%s: %zu bytes do not fit %llu (text is never truncated)", it->f.name,
                      text_len, (unsigned long long) it->f.size);
        return ZS_E_FIT;
    }
    if (text_len) memcpy(rec + off, text, text_len);
    memset(rec + off + text_len, ' ', (size_t) (it->f.size - text_len));
    return ZXV_OK;
}

/* ===== whole records ===== */

/* Call fn for every occurrence of item idx; stops at the first error. */
static int32_t each_occurrence(const zs_layout *l, uint32_t idx,
                               int32_t (*fn)(const zs_layout *, const zs_item *, uint64_t, void *),
                               void *ctx) {
    uint32_t chain[ZS_MAX_DIMS], nchain = 0, sub[ZS_MAX_DIMS] = {0};
    for (uint32_t i = idx;; i = l->items[i].f.parent) {
        if (l->items[i].f.occurs) chain[nchain++] = i;
        if (i == 0) break;
    }
    for (;;) {
        uint64_t off = l->items[idx].f.offset;
        int32_t st;
        uint32_t k;
        for (k = 0; k < nchain; k++) off += (uint64_t) sub[k] * l->items[chain[k]].f.size;
        if ((st = fn(l, &l->items[idx], off, ctx)) != ZXV_OK) return st;
        for (k = 0; k < nchain; k++) {
            if (++sub[k] < l->items[chain[k]].f.occurs) break;
            sub[k] = 0;
        }
        if (k == nchain) return ZXV_OK;
    }
}

static int32_t init_one(const zs_layout *l, const zs_item *it, uint64_t off, void *ctx) {
    zs_num_encode(l, it, (uint8_t *) ctx + off, 0);
    return ZXV_OK;
}

static int32_t check_one(const zs_layout *l, const zs_item *it, uint64_t off, void *ctx) {
    int64_t v;
    int32_t st = zs_num_decode(l, it, (const uint8_t *) ctx + off, &v);
    if (st != ZXV_OK) {
        char m[400];
        size_t n = 0;
        zs_last_error(m, sizeof m, &n);
        zs_set_errorf("at byte offset %llu: %s", (unsigned long long) off, m);
    }
    return st;
}

int32_t zs_record_init(const zs_layout *l, uint8_t *rec, size_t rec_len) {
    if (!l || !rec || rec_len < l->size) return ZXV_E_ARG;
    memset(rec, ' ', (size_t) l->size);
    for (uint32_t i = 0; i < l->n; i++)
        if (l->items[i].f.kind == ZS_K_NUM) each_occurrence(l, i, init_one, rec);
    return ZXV_OK;
}

int32_t zs_record_check(const zs_layout *l, const uint8_t *rec, size_t rec_len) {
    if (!l || !rec || rec_len < l->size) return ZXV_E_ARG;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->items[i].f.kind == ZS_K_NUM) {
            int32_t st = each_occurrence(l, i, check_one, (void *) (uintptr_t) rec);
            if (st != ZXV_OK) return st;
        }
    return ZXV_OK;
}
