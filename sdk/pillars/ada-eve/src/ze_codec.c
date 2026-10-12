/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_codec.c — the canonical encoding: bytes <-> exact values.
 *
 *   INT, DEC   the integer (DEC: in units of 10^-scale), big-endian, in
 *              the narrowest of 1, 2, 4 or 8 bytes that holds the declared
 *              range; unsigned when the range has no negative value, two's
 *              complement otherwise
 *   ENUM       the position, unsigned, the same width rule
 *   TEXT       n bytes (Ada's String is Latin-1; any byte is a Character)
 *   ARRAY      the elements, row-major (the last index varies fastest)
 *   RECORD     the components in declaration order, no padding
 *
 * A value outside its declared range is invalid data (ZE_E_DATA) when
 * read, and does not fit (ZE_E_FIT) when written. Integer code only
 * (R11). */
#include <stdlib.h>
#include <string.h>

#include "ze_internal.h"

const int64_t ze_pow10[19] = {1LL,
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

int32_t ze_scalar_read(const ze_titem *it, const uint8_t *p, int64_t *out) {
    uint64_t u = 0;
    int64_t v;
    for (uint32_t k = 0; k < it->width; k++) u = (u << 8) | p[k];
    if (it->is_signed) {
        uint32_t bits = it->width * 8u;
        if (bits < 64u && (u >> (bits - 1u)) & 1u) u |= ~(uint64_t) 0 << bits;
        memcpy(&v, &u, sizeof v);
    } else {
        if (u > (uint64_t) INT64_MAX) return ZE_E_DATA;
        v = (int64_t) u;
    }
    if (v < it->t.lo || v > it->t.hi) return ZE_E_DATA;
    *out = v;
    return ZXV_OK;
}

void ze_scalar_write(const ze_titem *it, uint8_t *p, int64_t v) {
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    for (uint32_t k = it->width; k > 0; k--) {
        p[k - 1u] = (uint8_t) (u & 0xFFu);
        u >>= 8;
    }
}

/* ---- paths ---- */

static int is_alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int is_alnum(char c) { return is_alpha(c) || (c >= '0' && c <= '9') || c == '_'; }

/* One subscript (an integer or a literal) of dimension d. */
static int32_t subscript(const ze_pkg *p, const ze_dim *d, const char *s, size_t n, int64_t *out_rel) {
    int64_t v = 0;
    if (n == 0) return ZXV_E_FORMAT;
    if (is_alpha(s[0])) {
        char low[ZE_MAX_NAME + 1];
        uint32_t pos;
        const ze_titem *ix;
        if (d->index == UINT32_MAX || p->types[d->index].t.kind != ZE_K_ENUM || n > ZE_MAX_NAME) {
            ze_set_errorf("subscript '%.*s' is not an integer", (int) (n > 64u ? 64u : n), s);
            return ZXV_E_FORMAT;
        }
        for (size_t k = 0; k < n; k++)
            if (!is_alnum(s[k])) return ZXV_E_FORMAT;
        ze_lower(low, s, n);
        ix = &p->types[d->index];
        if (ze_enum_pos(p, ix->t.base, low, &pos) != ZXV_OK && ze_enum_pos(p, d->index, low, &pos) != ZXV_OK) {
            ze_set_errorf("'%s' is not a literal of the index type", low);
            return ZXV_E_RANGE;
        }
        v = pos;
    } else {
        size_t k = 0;
        int neg = 0;
        uint64_t u = 0;
        if (d->index != UINT32_MAX && p->types[d->index].t.kind == ZE_K_ENUM) {
            ze_set_error("an enumeration index takes a literal");
            return ZXV_E_FORMAT;
        }
        if (s[0] == '-' || s[0] == '+') {
            neg = s[0] == '-';
            k = 1;
        }
        if (k == n) return ZXV_E_FORMAT;
        for (; k < n; k++) {
            if (s[k] < '0' || s[k] > '9') return ZXV_E_FORMAT;
            if (u > (uint64_t) INT64_MAX / 10u) return ZXV_E_RANGE;
            u = u * 10u + (uint64_t) (s[k] - '0');
        }
        if (u > (uint64_t) INT64_MAX) return ZXV_E_RANGE;
        v = neg ? -(int64_t) u : (int64_t) u;
    }
    if (v < d->lo || v > d->hi) {
        ze_set_errorf("subscript %.*s is outside %lld .. %lld", (int) (n > 64u ? 64u : n), s, (long long) d->lo,
                      (long long) d->hi);
        return ZXV_E_RANGE;
    }
    *out_rel = v - d->lo;
    return ZXV_OK;
}

static int32_t resolve(const ze_pkg *p, uint32_t type, const char *path, uint32_t *out_type,
                       uint64_t *out_off) {
    uint32_t t = type;
    uint64_t off = 0;
    size_t i = 0;
    int first = 1;
    for (;;) {
        const ze_titem *it = &p->types[t];
        while (path[i] == ' ') i++;
        if (!path[i]) break;
        if (path[i] == '(') {
            uint64_t lin = 0;
            uint32_t d = 0;
            if (it->t.kind != ZE_K_ARRAY) {
                ze_set_error("a subscript on something that is not an array");
                return ZXV_E_FORMAT;
            }
            i++;
            for (;;) {
                size_t a, b;
                int64_t rel;
                int32_t st;
                while (path[i] == ' ') i++;
                a = i;
                while (path[i] && path[i] != ',' && path[i] != ')' && path[i] != ' ') i++;
                b = i;
                while (path[i] == ' ') i++;
                if (d >= it->t.dims) {
                    ze_set_error("too many subscripts");
                    return ZXV_E_FORMAT;
                }
                if ((st = subscript(p, &it->dim[d], path + a, b - a, &rel)) != ZXV_OK) return st;
                lin = lin * ((uint64_t) (it->dim[d].hi - it->dim[d].lo) + 1u) + (uint64_t) rel;
                d++;
                if (path[i] == ',') {
                    i++;
                    continue;
                }
                if (path[i] == ')') {
                    i++;
                    break;
                }
                ze_set_error("a subscript list ends with ')'");
                return ZXV_E_FORMAT;
            }
            if (d != it->t.dims) {
                ze_set_errorf("the array has %u dimensions", (unsigned) it->t.dims);
                return ZXV_E_FORMAT;
            }
            off += lin * p->types[it->t.elem].t.size;
            t = it->t.elem;
        } else if (path[i] == '.' || (first && is_alpha(path[i]))) {
            size_t a, n;
            char low[ZE_MAX_NAME + 1];
            uint32_t c;
            if (path[i] == '.') i++;
            while (path[i] == ' ') i++;
            a = i;
            while (is_alnum(path[i])) i++;
            n = i - a;
            if (n == 0 || n > ZE_MAX_NAME || !is_alpha(path[a])) {
                ze_set_error("a component name is expected");
                return ZXV_E_FORMAT;
            }
            if (it->t.kind != ZE_K_RECORD) {
                ze_set_errorf("'%.*s' selects a component of something that is not a record", (int) n, path + a);
                return ZXV_E_FORMAT;
            }
            ze_lower(low, path + a, n);
            for (c = 0; c < it->t.count; c++)
                if (strcmp(p->comps[it->comp0 + c].name, low) == 0) break;
            if (c == it->t.count) {
                ze_set_errorf("%s has no component '%s'", it->t.name[0] ? it->t.name : "the record", low);
                return ZXV_E_NOT_FOUND;
            }
            off += p->comps[it->comp0 + c].offset;
            t = p->comps[it->comp0 + c].type;
        } else {
            ze_set_errorf("malformed path at '%c'", path[i]);
            return ZXV_E_FORMAT;
        }
        first = 0;
    }
    *out_type = t;
    *out_off = off;
    return ZXV_OK;
}

static int32_t args(const ze_pkg *p, uint32_t type, const void *val, size_t len, const char *path) {
    if (!p || !val || !path || type >= p->ntypes) return ZXV_E_ARG;
    if (len < p->types[type].t.size) {
        ze_set_errorf("the value is %zu bytes; %s needs %llu", len,
                      p->types[type].t.name[0] ? p->types[type].t.name : "the type",
                      (unsigned long long) p->types[type].t.size);
        return ZXV_E_ARG;
    }
    return ZXV_OK;
}

int32_t ze_find(const ze_pkg *p, uint32_t type, const char *path, uint32_t *out_type, uint64_t *out_offset) {
    uint32_t t;
    uint64_t off;
    int32_t st;
    if (!p || !path || type >= p->ntypes) return ZXV_E_ARG;
    if ((st = resolve(p, type, path, &t, &off)) != ZXV_OK) return st;
    if (out_type) *out_type = t;
    if (out_offset) *out_offset = off;
    return ZXV_OK;
}

/* Resolve a path to a scalar and read it. */
static int32_t read_scalar(const ze_pkg *p, uint32_t type, const uint8_t *val, size_t len, const char *path,
                           const ze_titem **out_it, int64_t *out_v) {
    uint32_t t;
    uint64_t off;
    int32_t st;
    const ze_titem *it;
    if ((st = args(p, type, val, len, path)) != ZXV_OK) return st;
    if ((st = resolve(p, type, path, &t, &off)) != ZXV_OK) return st;
    it = &p->types[t];
    *out_it = it;
    if (it->t.kind != ZE_K_INT && it->t.kind != ZE_K_DEC && it->t.kind != ZE_K_ENUM) return ZXV_OK;
    if (ze_scalar_read(it, val + off, out_v) != ZXV_OK) {
        ze_set_errorf("'%s': the bytes are not a value of %s (%lld .. %lld)", path,
                      it->t.name[0] ? it->t.name : "the type", (long long) it->t.lo, (long long) it->t.hi);
        return ZE_E_DATA;
    }
    return ZXV_OK;
}

int32_t ze_get_decimal(const ze_pkg *p, uint32_t type, const uint8_t *val, size_t len, const char *path,
                       int64_t *out_units, uint32_t *out_scale) {
    const ze_titem *it = NULL;
    int64_t v = 0;
    int32_t st = read_scalar(p, type, val, len, path, &it, &v);
    if (st != ZXV_OK) return st;
    if (it->t.kind != ZE_K_INT && it->t.kind != ZE_K_DEC) {
        ze_set_errorf("'%s' is not a number", path);
        return ZXV_E_ARG;
    }
    if (out_units) *out_units = v;
    if (out_scale) *out_scale = it->t.kind == ZE_K_DEC ? it->t.scale : 0u;
    return ZXV_OK;
}

int32_t ze_get_rat(const ze_pkg *p, uint32_t type, const uint8_t *val, size_t len, const char *path,
                   int64_t *out_num, int64_t *out_den) {
    int64_t u, n, d;
    uint32_t sc;
    int32_t st = ze_get_decimal(p, type, val, len, path, &u, &sc);
    if (st != ZXV_OK) return st;
    if ((st = zxv_rat_normalize(u, ze_pow10[sc], &n, &d)) != ZXV_OK) return st;
    if (out_num) *out_num = n;
    if (out_den) *out_den = d;
    return ZXV_OK;
}

int32_t ze_get_enum(const ze_pkg *p, uint32_t type, const uint8_t *val, size_t len, const char *path,
                    uint32_t *out_pos) {
    const ze_titem *it = NULL;
    int64_t v = 0;
    int32_t st = read_scalar(p, type, val, len, path, &it, &v);
    if (st != ZXV_OK) return st;
    if (it->t.kind != ZE_K_ENUM) {
        ze_set_errorf("'%s' is not an enumeration", path);
        return ZXV_E_ARG;
    }
    if (out_pos) *out_pos = (uint32_t) v;
    return ZXV_OK;
}

int32_t ze_get_value(const ze_pkg *p, uint32_t type, const uint8_t *val, size_t len, const char *path,
                     char *buf, size_t cap, size_t *out_len) {
    const ze_titem *it = NULL;
    int64_t v = 0;
    int32_t st = read_scalar(p, type, val, len, path, &it, &v);
    ze_sb s = {0};
    if (st != ZXV_OK) return st;
    if (!out_len) return ZXV_E_ARG;
    switch (it->t.kind) {
    case ZE_K_INT: ze_sb_i64(&s, v); break;
    case ZE_K_DEC: ze_sb_dec(&s, v, it->t.scale); break;
    case ZE_K_ENUM: ze_sb_str(&s, p->lits[it->lit0 + (uint32_t) v]); break;
    case ZE_K_TEXT: {
        uint32_t t;
        uint64_t off;
        resolve(p, type, path, &t, &off);
        return ze_out_text((const char *) val + off, it->t.count, buf, cap, out_len);
    }
    default: ze_set_errorf("'%s' is an array or a record; select a scalar", path); return ZXV_E_ARG;
    }
    if (s.failed) {
        free(s.p);
        return ZXV_E_NOMEM;
    }
    st = ze_out_text(s.p, s.n, buf, cap, out_len);
    free(s.p);
    return st;
}

/* ---- writing ---- */

static int32_t target(const ze_pkg *p, uint32_t type, uint8_t *val, size_t len, const char *path,
                      const ze_titem **out_it, uint8_t **out_at) {
    uint32_t t;
    uint64_t off;
    int32_t st;
    if ((st = args(p, type, val, len, path)) != ZXV_OK) return st;
    if ((st = resolve(p, type, path, &t, &off)) != ZXV_OK) return st;
    *out_it = &p->types[t];
    *out_at = val + off;
    return ZXV_OK;
}

static const char *tname(const ze_titem *it) { return it->t.name[0] ? it->t.name : "the subtype"; }

/* Store an exact integer v (in the type's units) after the range check. */
static int32_t store_units(const ze_titem *it, uint8_t *at, int64_t v, const char *path) {
    if (v < it->t.lo || v > it->t.hi) {
        ze_sb s = {0};
        ze_sb_str(&s, "");
        if (it->t.kind == ZE_K_DEC) {
            ze_sb_dec(&s, v, it->t.scale);
            ze_sb_str(&s, " is outside ");
            ze_sb_dec(&s, it->t.lo, it->t.scale);
            ze_sb_str(&s, " .. ");
            ze_sb_dec(&s, it->t.hi, it->t.scale);
        } else {
            ze_sb_i64(&s, v);
            ze_sb_str(&s, " is outside ");
            ze_sb_i64(&s, it->t.lo);
            ze_sb_str(&s, " .. ");
            ze_sb_i64(&s, it->t.hi);
        }
        ze_set_errorf("'%s': %s, the range of %s", path, s.failed ? "the value" : s.p, tname(it));
        free(s.p);
        return ZE_E_FIT;
    }
    ze_scalar_write(it, at, v);
    return ZXV_OK;
}

int32_t ze_put_decimal(const ze_pkg *p, uint32_t type, uint8_t *val, size_t len, const char *path, int64_t units,
                       uint32_t scale) {
    const ze_titem *it;
    uint8_t *at;
    uint32_t ts;
    int64_t v;
    int32_t st = target(p, type, val, len, path, &it, &at);
    if (st != ZXV_OK) return st;
    if (it->t.kind != ZE_K_INT && it->t.kind != ZE_K_DEC) {
        ze_set_errorf("'%s' is not a number", path);
        return ZXV_E_ARG;
    }
    if (scale > ZE_MAX_DIGITS) return ZXV_E_ARG;
    ts = it->t.kind == ZE_K_DEC ? it->t.scale : 0u;
    if (scale <= ts) {
        int64_t m = ze_pow10[ts - scale];
        if (m > 1 && (units > INT64_MAX / m || units < -(INT64_MAX / m))) {
            ze_set_errorf("'%s': the value does not fit 64 bits in units of the delta", path);
            return ZE_E_FIT;
        }
        v = units * m;
    } else {
        int64_t dv = ze_pow10[scale - ts];
        if (units % dv != 0) {
            ze_sb s = {0};
            ze_sb_dec(&s, units, scale);
            ze_set_errorf("'%s': %s has more decimals than %s's %u (never rounded)", path,
                          s.failed ? "the value" : s.p, tname(it), (unsigned) ts);
            free(s.p);
            return ZE_E_FIT;
        }
        v = units / dv;
    }
    return store_units(it, at, v, path);
}

int32_t ze_put_rat(const ze_pkg *p, uint32_t type, uint8_t *val, size_t len, const char *path, int64_t num,
                   int64_t den) {
    const ze_titem *it;
    uint8_t *at;
    int64_t n, d;
    ze_i128 x;
    uint32_t ts;
    int32_t st = target(p, type, val, len, path, &it, &at);
    if (st != ZXV_OK) return st;
    if (it->t.kind != ZE_K_INT && it->t.kind != ZE_K_DEC) {
        ze_set_errorf("'%s' is not a number", path);
        return ZXV_E_ARG;
    }
    if ((st = zxv_rat_normalize(num, den, &n, &d)) != ZXV_OK) return st;
    ts = it->t.kind == ZE_K_DEC ? it->t.scale : 0u;
    x = (ze_i128) n * ze_pow10[ts];
    if (x % d != 0) {
        ze_set_errorf("'%s': %lld/%lld has no exact value with %u decimals (never rounded)", path, (long long) n,
                      (long long) d, (unsigned) ts);
        return ZE_E_FIT;
    }
    x /= d;
    if (x > (ze_i128) INT64_MAX || x < (ze_i128) INT64_MIN) {
        ze_set_errorf("'%s': the value does not fit 64 bits in units of the delta", path);
        return ZE_E_FIT;
    }
    return store_units(it, at, (int64_t) x, path);
}

int32_t ze_put_enum(const ze_pkg *p, uint32_t type, uint8_t *val, size_t len, const char *path, uint32_t pos) {
    const ze_titem *it;
    uint8_t *at;
    int32_t st = target(p, type, val, len, path, &it, &at);
    if (st != ZXV_OK) return st;
    if (it->t.kind != ZE_K_ENUM) {
        ze_set_errorf("'%s' is not an enumeration", path);
        return ZXV_E_ARG;
    }
    if ((int64_t) pos < it->t.lo || (int64_t) pos > it->t.hi) {
        ze_set_errorf("'%s': position %u is outside %s", path, (unsigned) pos, tname(it));
        return ZE_E_FIT;
    }
    ze_scalar_write(it, at, pos);
    return ZXV_OK;
}

/* "[-]digits[.digits]" exactly into units of 10^-ts. 1: stored or refused
 * (*st set); 0: not of this form (the caller tries a fraction). Handles
 * the whole int64 range, -9223372036854775808 included, which a libzxv
 * rational cannot hold. */
static int put_plain_decimal(const ze_titem *it, uint8_t *at, const char *b, const char *path, int32_t *st) {
    const ze_i128 lim = (ze_i128) 1 << 100;
    ze_i128 m = 0;
    uint32_t ts = it->t.kind == ZE_K_DEC ? it->t.scale : 0u, sc = 0;
    const char *q = b;
    int neg = 0, frac = 0, digits = 0;
    if (*q == '-') {
        neg = 1;
        q++;
    }
    for (; *q; q++) {
        if (*q == '.' && !frac && digits) {
            frac = 1;
            if (!q[1]) return 0;
            continue;
        }
        if (*q < '0' || *q > '9') return 0;
        if (m > lim) {
            ze_set_errorf("'%s': %s does not fit exactly", path, b);
            *st = ZE_E_FIT;
            return 1;
        }
        m = m * 10 + (*q - '0');
        digits++;
        if (frac) sc++;
    }
    if (!digits) return 0;
    while (sc > ts && m % 10 == 0) {
        m /= 10;
        sc--;
    }
    if (sc > ts) {
        ze_set_errorf("'%s': %s has more decimals than %s's %u (never rounded)", path, b, tname(it), (unsigned) ts);
        *st = ZE_E_FIT;
        return 1;
    }
    for (; sc < ts; sc++) {
        if (m > lim) break;
        m *= 10;
    }
    if (neg) m = -m;
    if (sc < ts || m > (ze_i128) INT64_MAX || m < (ze_i128) INT64_MIN) {
        ze_set_errorf("'%s': %s does not fit 64 bits in units of the delta", path, b);
        *st = ZE_E_FIT;
        return 1;
    }
    *st = store_units(it, at, (int64_t) m, path);
    return 1;
}

int32_t ze_put_value(const ze_pkg *p, uint32_t type, uint8_t *val, size_t len, const char *path, const char *text,
                     size_t text_len) {
    const ze_titem *it;
    uint8_t *at;
    int32_t st = target(p, type, val, len, path, &it, &at);
    if (st != ZXV_OK) return st;
    if (!text && text_len) return ZXV_E_ARG;
    if (!text) text = "";
    switch (it->t.kind) {
    case ZE_K_TEXT:
        if (text_len > it->t.count) {
            ze_set_errorf("'%s': %zu characters do not fit String (1 .. %u) (never truncated)", path, text_len,
                          (unsigned) it->t.count);
            return ZE_E_FIT;
        }
        memcpy(at, text, text_len);
        memset(at + text_len, ' ', it->t.count - text_len);
        return ZXV_OK;
    case ZE_K_INT:
    case ZE_K_DEC: {
        char b[80];
        int64_t n, d;
        if (text_len >= sizeof b || memchr(text, '\0', text_len)) {
            ze_set_errorf("'%s': not a number", path);
            return ZXV_E_FORMAT;
        }
        memcpy(b, text, text_len);
        b[text_len] = '\0';
        if (put_plain_decimal(it, at, b, path, &st)) return st;
        st = zxv_rat_parse(b, &n, &d);
        if (st == ZXV_E_RANGE) {
            ze_set_errorf("'%s': %s does not fit exactly", path, b);
            return ZE_E_FIT;
        }
        if (st != ZXV_OK) {
            ze_set_errorf("'%s': '%s' is not an exact number (12.75, -0.1, 3/4)", path, b);
            return st;
        }
        return ze_put_rat(p, type, val, len, path, n, d);
    }
    case ZE_K_ENUM: {
        char low[ZE_MAX_NAME + 1];
        uint32_t pos;
        if (text_len == 0 || text_len > ZE_MAX_NAME || memchr(text, '\0', text_len)) {
            ze_set_errorf("'%s': not a literal", path);
            return ZE_E_FIT;
        }
        ze_lower(low, text, text_len);
        if (ze_enum_pos(p, it->t.base, low, &pos) != ZXV_OK && ze_enum_pos(p, (uint32_t) (it - p->types), low, &pos) != ZXV_OK) {
            ze_set_errorf("'%s': '%s' is not a literal of %s", path, low, tname(it));
            return ZE_E_FIT;
        }
        return ze_put_enum(p, type, val, len, path, pos);
    }
    default: ze_set_errorf("'%s' is an array or a record; select a scalar", path); return ZXV_E_ARG;
    }
}

/* ---- whole values ---- */

static void init_at(const ze_pkg *p, uint32_t t, uint8_t *at) {
    const ze_titem *it = &p->types[t];
    switch (it->t.kind) {
    case ZE_K_INT:
    case ZE_K_DEC:
    case ZE_K_ENUM: {
        int64_t v = it->t.lo > 0 ? it->t.lo : it->t.hi < 0 ? it->t.hi : 0;
        ze_scalar_write(it, at, v);
        break;
    }
    case ZE_K_TEXT: memset(at, ' ', it->t.count); break;
    case ZE_K_ARRAY: {
        uint64_t es = p->types[it->t.elem].t.size;
        for (uint32_t k = 0; k < it->t.count; k++) init_at(p, it->t.elem, at + (uint64_t) k * es);
        break;
    }
    case ZE_K_RECORD:
        for (uint32_t c = 0; c < it->t.count; c++) {
            const ze_component *cp = &p->comps[it->comp0 + c];
            init_at(p, cp->type, at + cp->offset);
        }
        break;
    default: break;
    }
}

int32_t ze_value_init(const ze_pkg *p, uint32_t type, uint8_t *val, size_t len) {
    int32_t st = args(p, type, val, len, "");
    if (st != ZXV_OK) return st;
    init_at(p, type, val);
    return ZXV_OK;
}

/* A path being built for error messages; truncated, never overflowing. */
typedef struct {
    char b[200];
    size_t n;
} vpath;

static size_t vp_put(vpath *v, const char *s, size_t n) {
    size_t was = v->n;
    if (n > sizeof v->b - 1u - v->n) n = sizeof v->b - 1u - v->n;
    memcpy(v->b + v->n, s, n);
    v->n += n;
    v->b[v->n] = '\0';
    return was;
}

static void vp_int(vpath *v, int64_t x) {
    ze_sb s = {0};
    ze_sb_i64(&s, x);
    if (!s.failed) vp_put(v, s.p, s.n);
    free(s.p);
}

static int32_t check_at(const ze_pkg *p, uint32_t t, const uint8_t *at, vpath *vp) {
    const ze_titem *it = &p->types[t];
    int32_t st;
    switch (it->t.kind) {
    case ZE_K_INT:
    case ZE_K_DEC:
    case ZE_K_ENUM: {
        int64_t v;
        if (ze_scalar_read(it, at, &v) != ZXV_OK) {
            ze_set_errorf("'%s': the bytes are not a value of %s", vp->n ? vp->b : "the value", tname(it));
            return ZE_E_DATA;
        }
        return ZXV_OK;
    }
    case ZE_K_ARRAY: {
        uint64_t es = p->types[it->t.elem].t.size;
        for (uint32_t k = 0; k < it->t.count; k++) {
            size_t mark = vp->n;
            uint32_t rest = k;
            int64_t sub[ZE_MAX_DIMS];
            for (uint32_t d = it->t.dims; d > 0; d--) {
                uint64_t ext = (uint64_t) (it->dim[d - 1u].hi - it->dim[d - 1u].lo) + 1u;
                sub[d - 1u] = it->dim[d - 1u].lo + (int64_t) (rest % ext);
                rest = (uint32_t) (rest / ext);
            }
            vp_put(vp, "(", 1);
            for (uint32_t d = 0; d < it->t.dims; d++) {
                if (d) vp_put(vp, ", ", 2);
                if (it->dim[d].index != UINT32_MAX && p->types[it->dim[d].index].t.kind == ZE_K_ENUM) {
                    const ze_titem *ix = &p->types[it->dim[d].index];
                    const char *lit = p->lits[ix->lit0 + (uint32_t) sub[d]];
                    vp_put(vp, lit, strlen(lit));
                } else {
                    vp_int(vp, sub[d]);
                }
            }
            vp_put(vp, ")", 1);
            st = check_at(p, it->t.elem, at + (uint64_t) k * es, vp);
            vp->n = mark;
            vp->b[mark] = '\0';
            if (st != ZXV_OK) return st;
        }
        return ZXV_OK;
    }
    case ZE_K_RECORD:
        for (uint32_t c = 0; c < it->t.count; c++) {
            const ze_component *cp = &p->comps[it->comp0 + c];
            size_t mark = vp->n;
            if (vp->n) vp_put(vp, ".", 1);
            vp_put(vp, cp->name, strlen(cp->name));
            st = check_at(p, cp->type, at + cp->offset, vp);
            vp->n = mark;
            vp->b[mark] = '\0';
            if (st != ZXV_OK) return st;
        }
        return ZXV_OK;
    default: return ZXV_OK;
    }
}

int32_t ze_value_check(const ze_pkg *p, uint32_t type, const uint8_t *val, size_t len) {
    vpath vp;
    int32_t st = args(p, type, val, len, "");
    if (st != ZXV_OK) return st;
    vp.n = 0;
    vp.b[0] = '\0';
    return check_at(p, type, val, &vp);
}
