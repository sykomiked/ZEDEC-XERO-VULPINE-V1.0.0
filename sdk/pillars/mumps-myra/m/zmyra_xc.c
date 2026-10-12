/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zmyra_xc.c — the GT.M / YottaDB external-call (call-out) glue for
 * libzmyra. M code calls these through the table m/zmyra.xc.in:
 *
 *   set h=$&zmyra.load("examples/payment.myra")
 *   set st=$&zmyra.check(h,$name(^ZXPAY(7,1)),^ZXPAY(7,1))
 *
 * Every function returns a status (0 or a negative ZXV_E_* / ZM_E_*), or a
 * non-negative handle, id or flag. Strings cross as gtm_string_t (length +
 * bytes), so values are passed exactly, without NUL termination. Text
 * results go to preallocated output strings; a result that does not fit
 * is ZXV_E_BUFFER, never truncated.
 *
 * The glue keeps one libzxv context for the ledger calls, a root
 * capability and a narrowed poster capability (LEDGER_POST | LEDGER_READ).
 * Inside one process that is least-privilege discipline, not a sandbox.
 * Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtmxc_types.h"
#include "zm_internal.h"
#include "zmyra.h"

#define ZMX_SLOTS 16
#define ZMX_ARG   4096u /* largest name, path or map argument */

static zm_schema *g_slot[ZMX_SLOTS];
static zxv_ctx *g_ctx;
static zxv_cap *g_root, *g_poster;

/* gtm_string_t -> NUL-terminated copy (malloc). */
static char *dup_str(const gtm_string_t *s) {
    size_t n = (s && s->length > 0) ? (size_t) s->length : 0;
    char *b = (char *) malloc(n + 1u);
    if (!b) return NULL;
    if (n) memcpy(b, s->address, n);
    b[n] = '\0';
    return b;
}

static size_t len_of(const gtm_string_t *s) {
    return (s && s->length > 0) ? (size_t) s->length : 0;
}

/* Copy a result into a preallocated output string of `cap` bytes. */
static gtm_long_t put_out(gtm_string_t *out, size_t cap, const char *s, size_t n) {
    if (!out) return ZXV_E_ARG;
    if (n > cap) {
        out->length = 0;
        zm_set_errorf("the result is %zu bytes; the external-call table allows %zu", n, cap);
        return ZXV_E_BUFFER;
    }
    if (n) memcpy(out->address, s, n);
    out->length = (gtm_long_t) n;
    return ZXV_OK;
}

#define OUT_VALUE 32767u /* O:gtm_string_t*[32767] in zmyra.xc.in */
#define OUT_SHORT 600u   /* O:gtm_string_t*[600]                  */

static zm_schema *slot(gtm_long_t h) {
    if (h < 0 || h >= ZMX_SLOTS) return NULL;
    return g_slot[h];
}

/* load(path): a global map or MYRA file -> handle 0..15. */
gtm_long_t zmx_load(int argc, gtm_string_t *path);
gtm_long_t zmx_load(int argc, gtm_string_t *path) {
    char *p;
    FILE *f;
    char *t = NULL;
    size_t n = 0, cap = 0;
    zm_schema *s = NULL;
    zm_diag d;
    int32_t st;
    int h;
    if (argc != 1) return ZXV_E_ARG;
    for (h = 0; h < ZMX_SLOTS && g_slot[h]; h++) {
    }
    if (h == ZMX_SLOTS) return ZXV_E_CAPACITY;
    if (!(p = dup_str(path))) return ZXV_E_NOMEM;
    f = fopen(p, "rb");
    if (!f) {
        zm_set_errorf("cannot open %s", p);
        free(p);
        return ZXV_E_IO;
    }
    free(p);
    for (;;) {
        size_t got;
        if (cap - n < 4096u) {
            char *nb = (char *) realloc(t, cap ? cap * 2u : 8192u);
            if (!nb) {
                free(t);
                fclose(f);
                return ZXV_E_NOMEM;
            }
            t = nb;
            cap = cap ? cap * 2u : 8192u;
        }
        got = fread(t + n, 1, cap - n, f);
        n += got;
        if (got == 0) break;
    }
    fclose(f);
    st = zm_schema_parse(t, n, &s, &d);
    free(t);
    if (st != ZXV_OK) return st;
    g_slot[h] = s;
    return h;
}

gtm_long_t zmx_unload(int argc, gtm_long_t h);
gtm_long_t zmx_unload(int argc, gtm_long_t h) {
    if (argc != 1 || !slot(h)) return ZXV_E_ARG;
    zm_schema_destroy(g_slot[h]);
    g_slot[h] = NULL;
    return ZXV_OK;
}

/* Decode one node; the caller destroys the record. */
static int32_t decode(gtm_long_t h, const gtm_string_t *ref, const gtm_string_t *val,
                      zm_record **out) {
    zm_schema *s = slot(h);
    if (!s) return ZXV_E_ARG;
    return zm_node_decode(s, ref ? ref->address : "", len_of(ref), val ? val->address : "",
                          len_of(val), out);
}

/* check(h, ref, value): 0 if the value is exactly valid for its node. */
gtm_long_t zmx_check(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val);
gtm_long_t zmx_check(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val) {
    zm_record *r = NULL;
    int32_t st;
    if (argc != 3) return ZXV_E_ARG;
    st = decode(h, ref, val, &r);
    zm_record_destroy(r);
    return st;
}

/* node(h, ref): the 1-based node a reference belongs to. */
gtm_long_t zmx_node(int argc, gtm_long_t h, gtm_string_t *ref);
gtm_long_t zmx_node(int argc, gtm_long_t h, gtm_string_t *ref) {
    uint32_t node = 0;
    int32_t st;
    if (argc != 2 || !slot(h)) return ZXV_E_ARG;
    st = zm_node_match(slot(h), ref->address, len_of(ref), &node);
    return st == ZXV_OK ? (gtm_long_t) node + 1 : st;
}

/* get(h, ref, value, field, .out): a field's display text. */
gtm_long_t zmx_get(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val,
                   gtm_string_t *name, gtm_string_t *out);
gtm_long_t zmx_get(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val,
                   gtm_string_t *name, gtm_string_t *out) {
    zm_record *r = NULL;
    char *nm, *b;
    size_t n = 0;
    int32_t st;
    if (argc != 5 || !out) return ZXV_E_ARG;
    out->length = 0;
    if (len_of(name) > ZMX_ARG) return ZXV_E_ARG;
    if ((st = decode(h, ref, val, &r)) != ZXV_OK) return st;
    nm = dup_str(name);
    b = (char *) malloc(OUT_VALUE + 1u);
    if (!nm || !b) st = ZXV_E_NOMEM;
    else st = zm_get_value(r, nm, b, OUT_VALUE + 1u, &n);
    if (st == ZXV_OK) st = (int32_t) put_out(out, OUT_VALUE, b, n);
    free(nm);
    free(b);
    zm_record_destroy(r);
    return st;
}

/* put(h, ref, value, field, text, .out): set a field from display text
 * and return the node's new canonical value string. */
gtm_long_t zmx_put(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val,
                   gtm_string_t *name, gtm_string_t *text, gtm_string_t *out);
gtm_long_t zmx_put(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val,
                   gtm_string_t *name, gtm_string_t *text, gtm_string_t *out) {
    zm_record *r = NULL;
    char *nm, *b;
    size_t n = 0;
    int32_t st;
    if (argc != 6 || !out) return ZXV_E_ARG;
    out->length = 0;
    if (len_of(name) > ZMX_ARG) return ZXV_E_ARG;
    if ((st = decode(h, ref, val, &r)) != ZXV_OK) return st;
    nm = dup_str(name);
    b = (char *) malloc(OUT_VALUE + 1u);
    if (!nm || !b) st = ZXV_E_NOMEM;
    else st = zm_put_value(r, nm, text ? text->address : "", len_of(text));
    if (st == ZXV_OK) st = zm_record_encode(r, b, OUT_VALUE + 1u, &n);
    if (st == ZXV_OK) st = (int32_t) put_out(out, OUT_VALUE, b, n);
    free(nm);
    free(b);
    zm_record_destroy(r);
    return st;
}

/* canon(h, ref, value, .out): the canonical value string. */
gtm_long_t zmx_canon(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val,
                     gtm_string_t *out);
gtm_long_t zmx_canon(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val,
                     gtm_string_t *out) {
    zm_record *r = NULL;
    char *b;
    size_t n = 0;
    int32_t st;
    if (argc != 3 || !out) return ZXV_E_ARG;
    out->length = 0;
    if ((st = decode(h, ref, val, &r)) != ZXV_OK) return st;
    b = (char *) malloc(OUT_VALUE + 1u);
    if (!b) st = ZXV_E_NOMEM;
    else st = zm_record_encode(r, b, OUT_VALUE + 1u, &n);
    if (st == ZXV_OK) st = (int32_t) put_out(out, OUT_VALUE, b, n);
    free(b);
    zm_record_destroy(r);
    return st;
}

/* collate(a, b): -1, 0 or 1 in M collation order. */
gtm_long_t zmx_collate(int argc, gtm_string_t *a, gtm_string_t *b);
gtm_long_t zmx_collate(int argc, gtm_string_t *a, gtm_string_t *b) {
    int32_t c = 0, st;
    if (argc != 2) return ZXV_E_ARG;
    st = zm_collate(a->address, len_of(a), b->address, len_of(b), &c);
    return st == ZXV_OK ? c : st;
}

/* canonic(x): 1 if M treats x as a canonical number. */
gtm_long_t zmx_canonic(int argc, gtm_string_t *x);
gtm_long_t zmx_canonic(int argc, gtm_string_t *x) {
    if (argc != 1) return ZXV_E_ARG;
    return zm_is_canonic(x->address, len_of(x));
}

/* hdate(h, .out): $H day -> YYYY-MM-DD. */
gtm_long_t zmx_hdate(int argc, gtm_long_t h, gtm_string_t *out);
gtm_long_t zmx_hdate(int argc, gtm_long_t h, gtm_string_t *out) {
    int32_t y, m, d, st;
    char b[32];
    if (argc != 2 || !out) return ZXV_E_ARG;
    out->length = 0;
    if ((st = zm_date_from_h(h, &y, &m, &d)) != ZXV_OK) return st;
    snprintf(b, sizeof b, "%04d-%02d-%02d", (int) y, (int) m, (int) d);
    return put_out(out, OUT_SHORT, b, strlen(b));
}

/* ---- the ledger ---- */

static void ledger_free(void) {
    zxv_cap_destroy(g_poster);
    zxv_cap_destroy(g_root);
    zxv_ctx_destroy(g_ctx);
    g_poster = g_root = NULL;
    g_ctx = NULL;
}

/* ledger(ccys): a fresh context with these currencies ("NGN,JPY"). */
gtm_long_t zmx_ledger(int argc, gtm_string_t *ccys);
gtm_long_t zmx_ledger(int argc, gtm_string_t *ccys) {
    char *list, *p;
    int32_t st;
    if (argc != 1 || len_of(ccys) > ZMX_ARG) return ZXV_E_ARG;
    ledger_free();
    if ((st = zxv_ctx_create(&g_ctx)) != ZXV_OK) return st;
    if ((st = zxv_cap_create(g_ctx, 1, NULL, ZXV_CAP_ALL, &g_root)) != ZXV_OK) return st;
    if ((st = zxv_cap_attenuate(g_root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &g_poster)) !=
        ZXV_OK)
        return st;
    if (!(list = dup_str(ccys))) return ZXV_E_NOMEM;
    for (p = strtok(list, ","); p; p = strtok(NULL, ","))
        if ((st = zxv_ccy_enable_cap(g_root, p)) != ZXV_OK) break;
    free(list);
    return st;
}

/* open(name, ccy, issuer): an account id, or a status. */
gtm_long_t zmx_open(int argc, gtm_string_t *name, gtm_string_t *ccy, gtm_long_t issuer);
gtm_long_t zmx_open(int argc, gtm_string_t *name, gtm_string_t *ccy, gtm_long_t issuer) {
    char *nm, *cc;
    uint32_t id = 0;
    int32_t st;
    if (argc != 3 || !g_root || len_of(name) > ZMX_ARG || len_of(ccy) > ZMX_ARG) return ZXV_E_ARG;
    nm = dup_str(name);
    cc = dup_str(ccy);
    if (!nm || !cc) st = ZXV_E_NOMEM;
    else st = zxv_ledger_open_account_cap(g_root, nm, cc,
                                          issuer ? ZXV_ACCT_ISSUER : ZXV_ACCT_HOLDER, &id);
    free(nm);
    free(cc);
    return st == ZXV_OK ? (gtm_long_t) id : st;
}

/* post(h, ref, value, map, .out): post one node through the poster cap.
 * map is "amount,from,to,currency,reference": field names; an empty
 * currency needs ",=NGN" style fixed currency ("amount,from,to,=NGN,ref");
 * an empty reference uses the node's own reference. out receives the
 * entry id on success. */
gtm_long_t zmx_post(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val,
                    gtm_string_t *map, gtm_string_t *out);
gtm_long_t zmx_post(int argc, gtm_long_t h, gtm_string_t *ref, gtm_string_t *val,
                    gtm_string_t *map, gtm_string_t *out) {
    zm_record *r = NULL;
    zm_posting_map pm;
    zm_posting p;
    char *m, *part[5] = {0};
    int k = 0;
    int32_t st;
    char b[32];
    if (argc != 5 || !out || !g_poster || len_of(map) > ZMX_ARG) return ZXV_E_ARG;
    out->length = 0;
    if ((st = decode(h, ref, val, &r)) != ZXV_OK) return st;
    if (!(m = dup_str(map))) {
        zm_record_destroy(r);
        return ZXV_E_NOMEM;
    }
    part[k++] = m;
    for (char *c = m; *c && k < 5; c++)
        if (*c == ',') {
            *c = '\0';
            part[k++] = c + 1;
        }
    if (k != 5) {
        free(m);
        zm_record_destroy(r);
        zm_set_error("map is amount,from,to,currency,reference");
        return ZXV_E_ARG;
    }
    memset(&pm, 0, sizeof pm);
    pm.amount = part[0];
    pm.from = part[1];
    pm.to = part[2];
    if (part[3][0] == '=') pm.fixed_currency = part[3] + 1;
    else pm.currency = part[3];
    pm.reference = part[4][0] ? part[4] : NULL;
    pm.minor_units = -1;
    st = zm_post(g_poster, r, &pm, &p);
    if (st == ZXV_OK) {
        snprintf(b, sizeof b, "%llu", (unsigned long long) p.entry_id);
        put_out(out, OUT_SHORT, b, strlen(b));
    }
    free(m);
    zm_record_destroy(r);
    return st;
}

/* equity(account): the spendable balance in minor units (or a status,
 * which a balance can never equal: balances are within +-(2^53-1)). */
gtm_long_t zmx_equity(int argc, gtm_long_t acct, gtm_string_t *out);
gtm_long_t zmx_equity(int argc, gtm_long_t acct, gtm_string_t *out) {
    int64_t e = 0;
    int32_t st;
    char b[32];
    if (argc != 2 || !g_poster || !out || acct < 0) return ZXV_E_ARG;
    out->length = 0;
    st = zxv_ledger_balance_cap(g_poster, (uint32_t) acct, NULL, NULL, &e);
    if (st != ZXV_OK) return st;
    snprintf(b, sizeof b, "%lld", (long long) e);
    return put_out(out, OUT_SHORT, b, strlen(b));
}

/* lcheck(): zxv_ledger_check through the poster cap. */
gtm_long_t zmx_lcheck(int argc);
gtm_long_t zmx_lcheck(int argc) {
    if (argc != 0 || !g_poster) return ZXV_E_ARG;
    return zxv_ledger_check_cap(g_poster);
}

/* revoke(): revoke the poster cap; later posts are ZXV_E_CAP. */
gtm_long_t zmx_revoke(int argc);
gtm_long_t zmx_revoke(int argc) {
    if (argc != 0 || !g_poster) return ZXV_E_ARG;
    return zxv_cap_revoke(g_poster);
}

/* error(.out): the last error detail; status(code, .out): its name. */
gtm_long_t zmx_error(int argc, gtm_string_t *out);
gtm_long_t zmx_error(int argc, gtm_string_t *out) {
    char b[OUT_SHORT + 1];
    size_t n = 0;
    if (argc != 1 || !out) return ZXV_E_ARG;
    if (zm_last_error(b, sizeof b, &n) != ZXV_OK) n = 0;
    return put_out(out, OUT_SHORT, b, n);
}

gtm_long_t zmx_status(int argc, gtm_long_t code, gtm_string_t *out);
gtm_long_t zmx_status(int argc, gtm_long_t code, gtm_string_t *out) {
    const char *s;
    if (argc != 2 || !out) return ZXV_E_ARG;
    s = zm_status_name((int32_t) code);
    return put_out(out, OUT_SHORT, s, strlen(s));
}
