/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_record.c — one node of a global as typed values: decoding and
 * encoding "^"-delimited value strings exactly, keys from references,
 * and the field getters and setters. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm_internal.h"

typedef struct {
    int32_t set;
    int64_t i;  /* INT, NUM (unscaled), DATE ($H day) */
    char *t;    /* TEXT: f.len + 1 bytes */
    uint32_t tn;
} zm_val;

struct zm_record {
    const zm_schema *s;
    uint32_t node;
    const zm_node *nd;
    zm_val *v;
};

int32_t zm_record_create(const zm_schema *s, uint32_t node, zm_record **out) {
    zm_record *r;
    const zm_node *nd;
    if (!s || !out) return ZXV_E_ARG;
    if (node >= s->n) return ZXV_E_NOT_FOUND;
    nd = &s->nodes[node];
    r = (zm_record *) calloc(1, sizeof *r);
    if (!r) return ZXV_E_NOMEM;
    r->s = s;
    r->node = node;
    r->nd = nd;
    r->v = (zm_val *) calloc(nd->nf ? nd->nf : 1u, sizeof *r->v);
    if (!r->v) {
        free(r);
        return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < nd->nf; i++)
        if (nd->f[i].f.type == ZM_T_TEXT) {
            r->v[i].t = (char *) malloc(nd->f[i].f.len + 1u);
            if (!r->v[i].t) {
                zm_record_destroy(r);
                return ZXV_E_NOMEM;
            }
            r->v[i].t[0] = '\0';
        }
    *out = r;
    return ZXV_OK;
}

void zm_record_destroy(zm_record *r) {
    if (!r) return;
    if (r->v)
        for (uint32_t i = 0; i < r->nd->nf; i++) free(r->v[i].t);
    free(r->v);
    free(r);
}

int32_t zm_record_clear(zm_record *r) {
    if (!r) return ZXV_E_ARG;
    for (uint32_t i = 0; i < r->nd->nf; i++) {
        r->v[i].set = 0;
        r->v[i].i = 0;
        r->v[i].tn = 0;
        if (r->v[i].t) r->v[i].t[0] = '\0';
    }
    return ZXV_OK;
}

int32_t zm_record_node(const zm_record *r, uint32_t *out_node) {
    if (!r || !out_node) return ZXV_E_ARG;
    *out_node = r->node;
    return ZXV_OK;
}

static void set_text(zm_val *v, const char *s, size_t n) {
    if (n) memcpy(v->t, s, n);
    v->t[n] = '\0';
    v->tn = (uint32_t) n;
    v->set = 1;
}

/* ---- keys ---- */

int32_t zm_record_set_ref(zm_record *r, const char *ref, size_t ref_len) {
    zm_ref x;
    char msg[400], sh[64];
    int64_t vals[ZM_MAX_SUBS];
    char *t;
    int32_t st;
    if (!r || !ref) return ZXV_E_ARG;
    if ((st = zm_ref_parse(ref, ref_len, &x)) != ZXV_OK) return st;
    if ((st = zm_ref_match_node(r->s, r->nd, &x, msg, sizeof msg)) != ZXV_OK) {
        if (st == ZM_E_KEY)
            zm_set_errorf("%s does not match %s node %u: %s", zm_show(ref, ref_len, sh, sizeof sh),
                          r->s->gname, (unsigned) r->node + 1u, msg);
        return st;
    }
    t = (char *) malloc(ZM_MAX_REF + 1u);
    if (!t) return ZXV_E_NOMEM;
    /* first pass: values (the match already checked them) */
    for (uint32_t i = 0; i < x.n; i++) {
        vals[i] = 0;
        if (r->nd->subs[i].kind == ZM_SUB_KEY && x.sub[i].isnum) {
            size_t tn = zm_rsub_text(&x.sub[i], t, ZM_MAX_REF + 1u);
            zm_key_check(&r->nd->f[r->nd->subs[i].field].f, t, tn, 1, &vals[i], msg, sizeof msg);
        }
    }
    for (uint32_t i = 0; i < x.n; i++) {
        zm_val *v;
        if (r->nd->subs[i].kind != ZM_SUB_KEY) continue;
        v = &r->v[r->nd->subs[i].field];
        if (r->nd->f[r->nd->subs[i].field].f.type == ZM_T_TEXT) {
            size_t tn = zm_rsub_text(&x.sub[i], t, ZM_MAX_REF + 1u);
            set_text(v, t, tn);
        } else {
            v->i = vals[i];
            v->set = 1;
        }
    }
    free(t);
    return ZXV_OK;
}

static void sb_mtext(zm_sb *sb, const zm_field *f, const zm_val *v) {
    char b[24];
    if (!v->set) return;
    if (f->type == ZM_T_TEXT) zm_sb_put(sb, v->t, v->tn);
    else zm_sb_put(sb, b, zm_dec_mtext(v->i, f->type == ZM_T_NUM ? f->scale : 0u, b));
}

int32_t zm_record_ref(const zm_record *r, char *buf, size_t cap, size_t *out_len) {
    zm_sb sb;
    if (!r || !out_len) return ZXV_E_ARG;
    for (uint32_t i = 0; i < r->nd->nkeys; i++)
        if (!r->v[i].set) {
            zm_set_errorf("key %s is not set", r->nd->f[i].f.name);
            return ZXV_E_FIELD;
        }
    zm_sb_init(&sb, SIZE_MAX / 2u);
    zm_sb_chr(&sb, '^');
    zm_sb_str(&sb, r->s->gname);
    if (r->nd->nsubs) {
        zm_sb_chr(&sb, '(');
        for (uint32_t i = 0; i < r->nd->nsubs; i++) {
            const zm_sub *x = &r->nd->subs[i];
            if (i) zm_sb_chr(&sb, ',');
            if (x->kind == ZM_SUB_LIT) {
                zm_sb_sub(&sb, x->lit, x->litlen, x->isnum);
            } else {
                const zm_field *f = &r->nd->f[x->field].f;
                const zm_val *v = &r->v[x->field];
                if (f->type == ZM_T_TEXT) zm_sb_sub(&sb, v->t, v->tn, 0);
                else sb_mtext(&sb, f, v);
            }
        }
        zm_sb_chr(&sb, ')');
    }
    return zm_sb_out(&sb, buf, cap, out_len);
}

/* ---- value strings ---- */

int32_t zm_record_decode(zm_record *r, const char *value, size_t len) {
    const zm_node *nd;
    size_t *off, *ln;
    int64_t *vals;
    int32_t st = ZXV_OK;
    uint32_t p = 1, k;
    size_t b = 0;
    char msg[300];
    if (!r || (!value && len)) return ZXV_E_ARG;
    if (len > ZM_MAX_VALUE) {
        zm_set_errorf("the value is longer than %u bytes", (unsigned) ZM_MAX_VALUE);
        return ZXV_E_TOO_LONG;
    }
    nd = r->nd;
    off = (size_t *) calloc(nd->nf + 1u, sizeof *off);
    ln = (size_t *) calloc(nd->nf + 1u, sizeof *ln);
    vals = (int64_t *) calloc(nd->nf + 1u, sizeof *vals);
    if (!off || !ln || !vals) {
        st = ZXV_E_NOMEM;
        goto done;
    }
    /* pass 1: check every piece; nothing is written yet */
    k = nd->nkeys;
    for (size_t i = 0; i <= len; i++) {
        size_t e;
        if (i < len && value[i] != '^') continue;
        e = i;
        while (k < nd->nf && nd->f[k].f.piece < p) k++;
        if (k < nd->nf && nd->f[k].f.piece == p) {
            const zm_field *f = &nd->f[k].f;
            off[k] = b;
            ln[k] = e - b;
            if (e > b) {
                st = zm_value_check(f, value + b, e - b, &vals[k], msg, sizeof msg);
                if (st != ZXV_OK) {
                    zm_set_errorf("piece %u (%s): %s", (unsigned) p, f->name, msg);
                    goto done;
                }
            }
        } else if (e > b) {
            char sh[64];
            zm_set_errorf("piece %u (\"%s\") is not declared in node %u of ^%s", (unsigned) p,
                          zm_show(value + b, e - b, sh, sizeof sh), (unsigned) r->node + 1u,
                          r->s->gname);
            st = ZM_E_DATA;
            goto done;
        }
        b = i + 1u;
        p++;
    }
    for (k = nd->nkeys; k < nd->nf; k++) {
        if (nd->f[k].f.piece >= p) ln[k] = 0; /* past the last "^": empty */
        if (ln[k] == 0 && nd->f[k].f.required) {
            zm_set_errorf("piece %u (%s) is required and empty", (unsigned) nd->f[k].f.piece,
                          nd->f[k].f.name);
            st = ZM_E_DATA;
            goto done;
        }
    }
    /* pass 2: commit */
    for (k = nd->nkeys; k < nd->nf; k++) {
        zm_val *v = &r->v[k];
        if (ln[k] == 0) {
            v->set = 0;
            v->i = 0;
            v->tn = 0;
            if (v->t) v->t[0] = '\0';
        } else if (nd->f[k].f.type == ZM_T_TEXT) {
            set_text(v, value + off[k], ln[k]);
        } else {
            v->i = vals[k];
            v->set = 1;
        }
    }
done:
    free(off);
    free(ln);
    free(vals);
    return st;
}

int32_t zm_record_encode(const zm_record *r, char *buf, size_t cap, size_t *out_len) {
    zm_sb sb;
    uint32_t k;
    if (!r || !out_len) return ZXV_E_ARG;
    for (k = r->nd->nkeys; k < r->nd->nf; k++)
        if (r->nd->f[k].f.required && !r->v[k].set) {
            zm_set_errorf("piece %u (%s) is required and empty", (unsigned) r->nd->f[k].f.piece,
                          r->nd->f[k].f.name);
            return ZM_E_DATA;
        }
    zm_sb_init(&sb, ZM_MAX_VALUE);
    k = r->nd->nkeys;
    for (uint32_t p = 1; p <= r->nd->width; p++) {
        if (p > 1) zm_sb_chr(&sb, '^');
        if (k < r->nd->nf && r->nd->f[k].f.piece == p) {
            sb_mtext(&sb, &r->nd->f[k].f, &r->v[k]);
            k++;
        }
    }
    while (sb.err == ZXV_OK && sb.n > 0 && sb.b[sb.n - 1u] == '^') sb.b[--sb.n] = '\0';
    return zm_sb_out(&sb, buf, cap, out_len);
}

int32_t zm_node_decode(const zm_schema *s, const char *ref, size_t ref_len, const char *value,
                       size_t value_len, zm_record **out) {
    uint32_t node;
    zm_record *r = NULL;
    int32_t st;
    if (!s || !ref || !out || (!value && value_len)) return ZXV_E_ARG;
    if ((st = zm_node_match(s, ref, ref_len, &node)) != ZXV_OK) return st;
    if ((st = zm_record_create(s, node, &r)) != ZXV_OK) return st;
    if ((st = zm_record_set_ref(r, ref, ref_len)) == ZXV_OK)
        st = zm_record_decode(r, value, value_len);
    if (st != ZXV_OK) {
        zm_record_destroy(r);
        return st;
    }
    *out = r;
    return ZXV_OK;
}

/* ---- fields ---- */

static int32_t field(const zm_record *r, const char *name, uint32_t *out) {
    if (!r || !name) return ZXV_E_ARG;
    for (uint32_t i = 0; i < r->nd->nf; i++)
        if (zm_ieq(r->nd->f[i].f.name, name)) {
            *out = i;
            return ZXV_OK;
        }
    zm_set_errorf("node %u of ^%s has no field %s", (unsigned) r->node + 1u, r->s->gname, name);
    return ZXV_E_NOT_FOUND;
}

int32_t zm_is_null(const zm_record *r, const char *name, int32_t *out_null) {
    uint32_t i;
    int32_t st;
    if (!out_null) return ZXV_E_ARG;
    if ((st = field(r, name, &i)) != ZXV_OK) return st;
    *out_null = !r->v[i].set;
    return ZXV_OK;
}

static int32_t numeric_field(const zm_record *r, const char *name, uint32_t *i) {
    int32_t st = field(r, name, i);
    if (st != ZXV_OK) return st;
    if (r->nd->f[*i].f.type == ZM_T_TEXT) {
        zm_set_errorf("%s is TEXT, not a number", r->nd->f[*i].f.name);
        return ZXV_E_ARG;
    }
    return ZXV_OK;
}

int32_t zm_get_decimal(const zm_record *r, const char *name, int64_t *out_unscaled,
                       uint32_t *out_scale) {
    uint32_t i;
    int32_t st;
    if (!out_unscaled || !out_scale) return ZXV_E_ARG;
    if ((st = numeric_field(r, name, &i)) != ZXV_OK) return st;
    if (!r->v[i].set) {
        zm_set_errorf("%s is empty", r->nd->f[i].f.name);
        return ZXV_E_FIELD;
    }
    *out_unscaled = r->v[i].i;
    *out_scale = r->nd->f[i].f.type == ZM_T_NUM ? r->nd->f[i].f.scale : 0u;
    return ZXV_OK;
}

int32_t zm_get_rat(const zm_record *r, const char *name, int64_t *out_num, int64_t *out_den) {
    int64_t v;
    uint32_t sc;
    int32_t st;
    if (!out_num || !out_den) return ZXV_E_ARG;
    if ((st = zm_get_decimal(r, name, &v, &sc)) != ZXV_OK) return st;
    return zxv_rat_normalize(v, zm_pow10[sc], out_num, out_den);
}

int32_t zm_get_value(const zm_record *r, const char *name, char *buf, size_t cap,
                     size_t *out_len) {
    uint32_t i;
    int32_t st;
    char b[32];
    const zm_field *f;
    const zm_val *v;
    if (!out_len) return ZXV_E_ARG;
    if ((st = field(r, name, &i)) != ZXV_OK) return st;
    f = &r->nd->f[i].f;
    v = &r->v[i];
    if (!v->set) return zm_out_text("", 0, buf, cap, out_len);
    switch (f->type) {
    case ZM_T_TEXT: return zm_out_text(v->t, v->tn, buf, cap, out_len);
    case ZM_T_DATE: {
        int32_t y, m, d;
        if ((st = zm_date_from_h(v->i, &y, &m, &d)) != ZXV_OK) return st;
        snprintf(b, sizeof b, "%04d-%02d-%02d", (int) y, (int) m, (int) d);
        return zm_out_text(b, strlen(b), buf, cap, out_len);
    }
    default: {
        size_t n = zm_dec_text(v->i, f->type == ZM_T_NUM ? f->scale : 0u, b);
        return zm_out_text(b, n, buf, cap, out_len);
    }
    }
}

int32_t zm_get_mtext(const zm_record *r, const char *name, char *buf, size_t cap,
                     size_t *out_len) {
    uint32_t i;
    int32_t st;
    zm_sb sb;
    if (!out_len) return ZXV_E_ARG;
    if ((st = field(r, name, &i)) != ZXV_OK) return st;
    zm_sb_init(&sb, ZM_MAX_VALUE);
    sb_mtext(&sb, &r->nd->f[i].f, &r->v[i]);
    return zm_sb_out(&sb, buf, cap, out_len);
}

/* Store an unscaled value already at the field's scale, after the range
 * check. */
static int32_t store_num(zm_record *r, uint32_t i, int64_t v) {
    const zm_field *f = &r->nd->f[i].f;
    char b[32];
    if (f->type == ZM_T_DATE) {
        if (v < 0 || v > ZM_MAX_DATE) {
            zm_set_errorf("%s: $H day %lld is outside 0..%d", f->name, (long long) v, ZM_MAX_DATE);
            return ZM_E_FIT;
        }
    } else if (zm_digits(v) > f->len) {
        zm_dec_text(v, f->type == ZM_T_NUM ? f->scale : 0u, b);
        zm_set_errorf("%s: %s has more than %u digits", f->name, b, (unsigned) f->len);
        return ZM_E_FIT;
    }
    r->v[i].i = v;
    r->v[i].set = 1;
    return ZXV_OK;
}

static uint32_t field_scale(const zm_field *f) { return f->type == ZM_T_NUM ? f->scale : 0u; }

int32_t zm_put_decimal(zm_record *r, const char *name, int64_t unscaled, uint32_t scale) {
    uint32_t i, fs;
    int32_t st;
    int64_t v;
    if ((st = numeric_field(r, name, &i)) != ZXV_OK) return st;
    if (scale > 18u) return ZXV_E_ARG;
    fs = field_scale(&r->nd->f[i].f);
    if (scale > fs) {
        int64_t d = zm_pow10[scale - fs];
        if (unscaled % d != 0) {
            char b[32];
            zm_dec_text(unscaled, scale, b);
            zm_set_errorf("%s: %s has more than %u decimals (never rounded)", r->nd->f[i].f.name,
                          b, (unsigned) fs);
            return ZM_E_FIT;
        }
        v = unscaled / d;
    } else {
        int64_t m = zm_pow10[fs - scale];
        if (unscaled > INT64_MAX / m || unscaled < -(INT64_MAX / m)) {
            zm_set_errorf("%s: the value does not fit", r->nd->f[i].f.name);
            return ZM_E_FIT;
        }
        v = unscaled * m;
    }
    return store_num(r, i, v);
}

int32_t zm_put_rat(zm_record *r, const char *name, int64_t num, int64_t den) {
    uint32_t i, fs;
    int64_t n, d, q;
    int32_t st;
    if ((st = numeric_field(r, name, &i)) != ZXV_OK) return st;
    if ((st = zxv_rat_normalize(num, den, &n, &d)) != ZXV_OK) return st;
    fs = field_scale(&r->nd->f[i].f);
    if (zm_pow10[fs] % d != 0) {
        zm_set_errorf("%s: %lld/%lld has no exact decimal with %u places (never rounded)",
                      r->nd->f[i].f.name, (long long) n, (long long) d, (unsigned) fs);
        return ZM_E_FIT;
    }
    q = zm_pow10[fs] / d;
    if (n > INT64_MAX / q || n < -(INT64_MAX / q)) {
        zm_set_errorf("%s: the value does not fit", r->nd->f[i].f.name);
        return ZM_E_FIT;
    }
    return store_num(r, i, n * q);
}

static int two_digits(const char *s, int *out) {
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') return 0;
    *out = (s[0] - '0') * 10 + (s[1] - '0');
    return 1;
}

int32_t zm_put_value(zm_record *r, const char *name, const char *text, size_t n) {
    uint32_t i;
    int32_t st;
    const zm_field *f;
    char msg[300];
    if ((st = field(r, name, &i)) != ZXV_OK) return st;
    if (!text && n) return ZXV_E_ARG;
    f = &r->nd->f[i].f;
    if (n == 0) {
        if (f->sub) {
            zm_set_errorf("key %s cannot be empty", f->name);
            return ZXV_E_ARG;
        }
        return zm_put_null(r, name);
    }
    if (f->type == ZM_T_TEXT) {
        if (f->sub) st = zm_key_check(f, text, n, zm_is_canonic(text, n), NULL, msg, sizeof msg);
        else st = zm_value_check(f, text, n, NULL, msg, sizeof msg);
        if (st != ZXV_OK) {
            zm_set_errorf("%s: %s", f->name, msg);
            return st;
        }
        set_text(&r->v[i], text, n);
        return ZXV_OK;
    }
    if (f->type == ZM_T_DATE) {
        int hi = 0, lo = 0, m = 0, d = 0;
        int64_t h;
        if (n != 10 || text[4] != '-' || text[7] != '-' || !two_digits(text, &hi) ||
            !two_digits(text + 2, &lo) || !two_digits(text + 5, &m) || !two_digits(text + 8, &d)) {
            zm_set_errorf("%s: a DATE is written YYYY-MM-DD", f->name);
            return ZXV_E_FORMAT;
        }
        if ((st = zm_date_to_h(hi * 100 + lo, m, d, &h)) != ZXV_OK) return st;
        return store_num(r, i, h);
    }
    if (memchr(text, '/', n)) {
        char t[64];
        int64_t a, b;
        if (n >= sizeof t) return ZXV_E_FORMAT;
        memcpy(t, text, n);
        t[n] = '\0';
        if ((st = zxv_rat_parse(t, &a, &b)) != ZXV_OK) return st;
        return zm_put_rat(r, name, a, b);
    }
    {
        int64_t v;
        st = zm_dec_parse(text, n, field_scale(f), &v);
        if (st == ZXV_E_FORMAT) {
            char sh[64];
            zm_set_errorf("%s: \"%s\" is not decimal text", f->name, zm_show(text, n, sh, sizeof sh));
            return st;
        }
        if (st == ZM_E_FIT) {
            char sh[64];
            zm_set_errorf("%s: %s does not fit %u decimals (never rounded)", f->name,
                          zm_show(text, n, sh, sizeof sh), (unsigned) field_scale(f));
            return st;
        }
        return store_num(r, i, v);
    }
}

int32_t zm_put_null(zm_record *r, const char *name) {
    uint32_t i;
    int32_t st;
    if ((st = field(r, name, &i)) != ZXV_OK) return st;
    if (r->nd->f[i].f.sub) {
        zm_set_errorf("key %s cannot be empty", r->nd->f[i].f.name);
        return ZXV_E_ARG;
    }
    r->v[i].set = 0;
    r->v[i].i = 0;
    r->v[i].tn = 0;
    if (r->v[i].t) r->v[i].t[0] = '\0';
    return ZXV_OK;
}
