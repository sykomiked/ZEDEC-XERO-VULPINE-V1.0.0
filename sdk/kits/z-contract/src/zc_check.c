/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zc_check.c — values and the runtime checker. Every comparison is exact:
 * rationals through zxv_rat_cmp, never through a float (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zc_internal.h"

/* ===== Values ===== */

int32_t zc_vals_create(zc_vals **out_vals) {
    zc_vals *v;
    if (!out_vals) return ZXV_E_ARG;
    v = (zc_vals *) calloc(1, sizeof *v);
    if (!v) return ZXV_E_NOMEM;
    *out_vals = v;
    return ZXV_OK;
}

int32_t zc_vals_clear(zc_vals *vals) {
    if (!vals) return ZXV_E_ARG;
    for (uint32_t i = 0; i < vals->n; i++) free(vals->v[i].bytes);
    memset(vals, 0, sizeof *vals);
    return ZXV_OK;
}

void zc_vals_destroy(zc_vals *vals) {
    if (!vals) return;
    (void) zc_vals_clear(vals);
    free(vals);
}

int32_t zc_vals_count(const zc_vals *vals, uint32_t *out_count) {
    if (!vals || !out_count) return ZXV_E_ARG;
    *out_count = vals->n;
    return ZXV_OK;
}

static int32_t push(zc_vals *vals, int32_t kind, int64_t num, int64_t den) {
    if (!vals) return ZXV_E_ARG;
    if (vals->n >= ZC_MAX_VALUES) {
        zc_set_errorf("zc_vals: more than %u values", ZC_MAX_VALUES);
        return ZXV_E_CAPACITY;
    }
    zc_val *v = &vals->v[vals->n++];
    memset(v, 0, sizeof *v);
    v->kind = kind;
    v->num = num;
    v->den = den;
    return ZXV_OK;
}

int32_t zc_vals_push_int(zc_vals *vals, int64_t value) { return push(vals, ZC_T_INT, value, 1); }
int32_t zc_vals_push_rat(zc_vals *vals, int64_t num, int64_t den) {
    return push(vals, ZC_T_RAT, num, den);
}
int32_t zc_vals_push_truth(zc_vals *vals, int32_t lp_state) {
    return push(vals, ZC_T_TRUTH, lp_state, 1);
}
int32_t zc_vals_push_tag(zc_vals *vals, int32_t tag) { return push(vals, ZC_T_TAG, tag, 1); }

int32_t zc_vals_push_bytes(zc_vals *vals, const uint8_t *data, size_t len) {
    uint8_t *copy;
    int32_t st;
    if (!vals || (len && !data)) return ZXV_E_ARG;
    if (len > ZC_MAX_BYTES) {
        zc_set_errorf("zc_vals_push_bytes: more than %u bytes", ZC_MAX_BYTES);
        return ZXV_E_TOO_LONG;
    }
    copy = (uint8_t *) malloc(len ? len : 1u);
    if (!copy) return ZXV_E_NOMEM;
    if (len) memcpy(copy, data, len);
    st = push(vals, ZC_T_BYTES, (int64_t) len, 1);
    if (st != ZXV_OK) {
        free(copy);
        return st;
    }
    vals->v[vals->n - 1u].bytes = copy;
    vals->v[vals->n - 1u].len = len;
    return ZXV_OK;
}

int32_t zc_vals_get(const zc_vals *vals, uint32_t index, int32_t *out_kind, int64_t *out_num,
                    int64_t *out_den) {
    if (!vals) return ZXV_E_ARG;
    if (index >= vals->n) return ZXV_E_NOT_FOUND;
    if (out_kind) *out_kind = vals->v[index].kind;
    if (out_num) *out_num = vals->v[index].num;
    if (out_den) *out_den = vals->v[index].den;
    return ZXV_OK;
}

/* ===== Checking ===== */

typedef struct {
    int32_t reason;
    char msg[320];
} verdict;

static const char *kind_name(int32_t k) {
    switch (k) {
    case ZC_T_INT: return "int";
    case ZC_T_RAT: return "rat";
    case ZC_T_TRUTH: return "truth";
    case ZC_T_TAG: return "tag";
    case ZC_T_BYTES: return "bytes";
    default: return "?";
    }
}

static int bound_text(const zc_type *t, int hi, char *out, size_t cap) {
    zc_buf b = {NULL, 0, 0, 0};
    if (t->kind == ZC_T_INT) zc_buf_i64(&b, hi ? t->hi_n : t->lo_n);
    else zc_buf_rat(&b, hi ? t->hi_n : t->lo_n, hi ? t->hi_d : t->lo_d);
    snprintf(out, cap, "%s", b.b ? b.b : "?");
    free(b.b);
    return 0;
}

/* Judge one value against one type. `where` names it in the message. */
static void judge(const zc_type *t, const zc_val *v, const char *where, verdict *out) {
    char vt[96], bt[64];
    out->reason = ZC_V_OK;
    out->msg[0] = '\0';
    zc_val_text(v, vt, sizeof vt);

    if (t->kind == ZC_T_INT || t->kind == ZC_T_RAT) {
        int64_t n, d;
        int32_t cmp = 0;
        if (v->kind != ZC_T_INT && v->kind != ZC_T_RAT) {
            out->reason = ZC_V_KIND;
            snprintf(out->msg, sizeof out->msg, "%s is of kind %s, expected %s", where,
                     kind_name(v->kind), kind_name(t->kind));
            return;
        }
        if (zxv_rat_normalize(v->num, v->den, &n, &d) != ZXV_OK) {
            out->reason = ZC_V_INVALID;
            snprintf(out->msg, sizeof out->msg, "%s = %s is not an exact rational "
                     "(zero denominator or INT64_MIN)", where, vt);
            return;
        }
        if (t->kind == ZC_T_INT && d != 1) {
            out->reason = ZC_V_NOT_INTEGER;
            snprintf(out->msg, sizeof out->msg, "%s = %s is not an integer", where, vt);
            return;
        }
        /* fail closed: a comparison that cannot be made is a violation */
        if ((t->has_lo && zxv_rat_cmp(n, d, t->lo_n, t->lo_d, &cmp) != ZXV_OK) ||
            (t->has_hi && zxv_rat_cmp(n, d, t->hi_n, t->hi_d, &cmp) != ZXV_OK)) {
            out->reason = ZC_V_INVALID;
            snprintf(out->msg, sizeof out->msg, "%s = %s cannot be compared exactly", where, vt);
            return;
        }
        if (t->has_lo && zxv_rat_cmp(n, d, t->lo_n, t->lo_d, &cmp) == ZXV_OK && cmp < 0) {
            bound_text(t, 0, bt, sizeof bt);
            out->reason = ZC_V_BELOW;
            snprintf(out->msg, sizeof out->msg, "%s = %s is below the minimum %s", where, vt, bt);
            return;
        }
        if (t->has_hi && zxv_rat_cmp(n, d, t->hi_n, t->hi_d, &cmp) == ZXV_OK && cmp > 0) {
            bound_text(t, 1, bt, sizeof bt);
            out->reason = ZC_V_ABOVE;
            snprintf(out->msg, sizeof out->msg, "%s = %s is above the maximum %s", where, vt, bt);
            return;
        }
        if (t->den_mode == ZC_DEN_LE && d > t->den_bound) {
            out->reason = ZC_V_DENOMINATOR;
            snprintf(out->msg, sizeof out->msg, "%s = %s: denominator %lld is above %lld", where,
                     vt, (long long) d, (long long) t->den_bound);
            return;
        }
        if (t->den_mode == ZC_DEN_DIVIDES && t->den_bound % d != 0) {
            out->reason = ZC_V_DENOMINATOR;
            snprintf(out->msg, sizeof out->msg, "%s = %s: denominator %lld does not divide %lld",
                     where, vt, (long long) d, (long long) t->den_bound);
            return;
        }
        return;
    }
    if (v->kind != t->kind) {
        out->reason = ZC_V_KIND;
        snprintf(out->msg, sizeof out->msg, "%s is of kind %s, expected %s", where, kind_name(v->kind),
                 kind_name(t->kind));
        return;
    }
    if (t->kind == ZC_T_TRUTH || t->kind == ZC_T_TAG) {
        int64_t top = t->kind == ZC_T_TRUTH ? 3 : 63;
        if (v->num < 0 || v->num > top) {
            out->reason = ZC_V_INVALID;
            snprintf(out->msg, sizeof out->msg, "%s = %lld is not a %s value", where,
                     (long long) v->num, kind_name(t->kind));
            return;
        }
        if (!(t->allowed & (UINT64_C(1) << v->num))) {
            zc_buf b = {NULL, 0, 0, 0};
            zc_type_format(&b, t);
            out->reason = ZC_V_NOT_IN_SET;
            snprintf(out->msg, sizeof out->msg, "%s = %s is not in %s", where, vt,
                     b.b ? b.b : "the set");
            free(b.b);
        }
        return;
    }
    if (t->kind == ZC_T_BYTES) {
        uint64_t len = (uint64_t) v->len;
        if (len < t->blo || (t->has_hi && len > t->bhi)) {
            zc_buf b = {NULL, 0, 0, 0};
            zc_type_format(&b, t);
            out->reason = ZC_V_LENGTH;
            snprintf(out->msg, sizeof out->msg, "%s has %llu bytes, outside %s", where,
                     (unsigned long long) len, b.b ? b.b : "the length range");
            free(b.b);
        }
    }
}

int32_t zc_check(const zc_contract *contract, const char *fn, int32_t part, const zc_vals *vals,
                 int32_t *out_reason, uint32_t *out_index, char *msg, size_t msg_cap,
                 size_t *out_msg_len) {
    const zc_fn *f;
    verdict vd;
    uint32_t idx = 0;
    char where[160];
    if (!contract || !fn || !vals) {
        zc_set_error("zc_check: NULL argument");
        return ZXV_E_ARG;
    }
    if (part != ZC_PART_PARAMS && part != ZC_PART_RESULT && part != ZC_PART_PROPOSALS) {
        zc_set_error("zc_check: unknown part");
        return ZXV_E_ARG;
    }
    f = zc_find_fn(contract, fn);
    if (!f) {
        zc_set_errorf("the contract declares no function '%.64s'", fn);
        return ZXV_E_NOT_FOUND;
    }
    vd.reason = ZC_V_OK;
    vd.msg[0] = '\0';
    if (part == ZC_PART_PARAMS) {
        if (vals->n != f->nparams) {
            vd.reason = ZC_V_ARITY;
            snprintf(vd.msg, sizeof vd.msg, "%s: %u values for %u parameters", f->name,
                     (unsigned) vals->n, (unsigned) f->nparams);
        }
        for (uint32_t i = 0; vd.reason == ZC_V_OK && i < f->nparams; i++) {
            snprintf(where, sizeof where, "%s: parameter %u '%s'", f->name, (unsigned) i,
                     f->params[i].name);
            judge(&f->params[i].type, &vals->v[i], where, &vd);
            idx = i;
        }
    } else if (part == ZC_PART_RESULT) {
        if (vals->n > 1u || (unsigned) vals->n != (unsigned) f->has_result) {
            vd.reason = ZC_V_ARITY;
            snprintf(vd.msg, sizeof vd.msg, "%s: %s", f->name,
                     f->has_result ? (vals->n ? "more than one result" : "the declared result is missing")
                                   : "a result was returned but none is declared");
        } else if (vals->n) {
            snprintf(where, sizeof where, "%s: result", f->name);
            judge(&f->result, &vals->v[0], where, &vd);
        }
    } else {
        uint32_t max = f->has_proposes ? f->max_proposals : 0u;
        if (vals->n > max) {
            vd.reason = ZC_V_COUNT;
            idx = max;
            if (f->has_proposes)
                snprintf(vd.msg, sizeof vd.msg, "%s: %u proposals, at most %u allowed", f->name,
                         (unsigned) vals->n, (unsigned) max);
            else
                snprintf(vd.msg, sizeof vd.msg, "%s: %u proposals, but the contract declares none",
                         f->name, (unsigned) vals->n);
        }
        for (uint32_t i = 0; vd.reason == ZC_V_OK && i < vals->n; i++) {
            snprintf(where, sizeof where, "%s: proposal %u", f->name, (unsigned) i);
            judge(&f->proposes, &vals->v[i], where, &vd);
            idx = i;
        }
    }
    if (vd.reason == ZC_V_OK) idx = 0;
    if (out_reason) *out_reason = vd.reason;
    if (out_index) *out_index = idx;
    zc_out_msg(vd.msg, msg, msg_cap, out_msg_len);
    if (vd.reason != ZC_V_OK) {
        zc_set_error(vd.msg);
        return ZC_E_VIOLATION;
    }
    return ZXV_OK;
}

/* ===== Subject digest =====
 * SHA3-256("zxv/zc-vals/v1" || part i32le || n u32le || for each value:
 * kind i32le || num i64le || den i64le || len u64le || bytes). Streams
 * through a buffer: the values may hold up to 64 MiB of bytes in total, so
 * the encoding is built once on the heap. */
static void le(uint8_t *p, uint64_t v, int n) {
    for (int i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8 * i));
}

int32_t zc_vals_digest(const zc_vals *vals, int32_t part, uint8_t out[32]) {
    static const char dom[] = "zxv/zc-vals/v1";
    size_t total = sizeof dom - 1u + 8u, k;
    uint8_t *buf;
    int32_t st;
    if (!vals || !out) return ZXV_E_ARG;
    for (uint32_t i = 0; i < vals->n; i++) total += 28u + vals->v[i].len;
    buf = (uint8_t *) malloc(total);
    if (!buf) return ZXV_E_NOMEM;
    memcpy(buf, dom, sizeof dom - 1u);
    k = sizeof dom - 1u;
    le(buf + k, (uint64_t) (uint32_t) part, 4);
    le(buf + k + 4, vals->n, 4);
    k += 8;
    for (uint32_t i = 0; i < vals->n; i++) {
        const zc_val *v = &vals->v[i];
        le(buf + k, (uint64_t) (uint32_t) v->kind, 4);
        le(buf + k + 4, (uint64_t) v->num, 8);
        le(buf + k + 12, (uint64_t) v->den, 8);
        le(buf + k + 20, (uint64_t) v->len, 8);
        k += 28;
        if (v->len) memcpy(buf + k, v->bytes, v->len);
        k += v->len;
    }
    st = zxv_sha3_256(buf, k, out);
    free(buf);
    return st;
}
