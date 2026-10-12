/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kr_run.c — the one-call text interface used by the APL and K bindings,
 * and the posting bridge to the libzxv ledger. Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "kr_internal.h"

static int32_t out_buf(kr_buf *b, char *buf, size_t cap, size_t *out_len) {
    int32_t st;
    if (b->oom) {
        free(b->b);
        return ZXV_E_NOMEM;
    }
    st = kr_out_text(b->b ? b->b : "", b->n, buf, cap, out_len);
    free(b->b);
    return st;
}

static void nd_text(const kr_value *v, kr_buf *b) {
    /* (shape;num;den) in K syntax; 1-element lists are written ,x */
    kr_buf_putc(b, '(');
    if (v->rank == 0) {
        kr_buf_puts(b, "!0");
    } else {
        if (v->rank == 1) kr_buf_putc(b, ',');
        for (uint32_t i = 0; i < v->rank; i++) {
            if (i) kr_buf_putc(b, ' ');
            kr_buf_i64(b, v->shape[i]);
        }
    }
    for (int part = 0; part < 2; part++) {
        kr_buf_putc(b, ';');
        if (v->rank == 0) {
            kr_buf_i64(b, v->kind == KR_KIND_TRUTH ? (part ? 0 : v->t[0]) : (part ? v->den[0] : v->num[0]));
            continue;
        }
        if (v->n == 0) {
            kr_buf_puts(b, "!0");
            continue;
        }
        if (v->n == 1) kr_buf_putc(b, ',');
        for (uint64_t i = 0; i < v->n; i++) {
            if (i) kr_buf_putc(b, ' ');
            kr_buf_i64(b, v->kind == KR_KIND_TRUTH ? (part ? 0 : v->t[i]) : (part ? v->den[i] : v->num[i]));
        }
    }
    kr_buf_putc(b, ')');
}

int32_t kr_run_text(const char *text, size_t len, int32_t spelling, int32_t what, char *buf, size_t cap,
                    size_t *out_len) {
    kr_program *p = NULL;
    kr_value *v = NULL;
    kr_diag d;
    kr_buf b = {NULL, 0, 0, 0};
    int32_t st;
    if (!out_len || what < KR_TEXT_VALUE || what > KR_TEXT_ND) return ZXV_E_ARG;
    st = kr_parse(text, len, spelling, &p, &d);
    if (st == ZXV_OK) {
        if (what == KR_TEXT_PROGRAM) {
            kr_buf_put(&b, p->text, p->text_len);
        } else if (what == KR_TEXT_DIGEST) {
            static const char hx[] = "0123456789abcdef";
            for (int i = 0; i < 32; i++) {
                kr_buf_putc(&b, hx[p->digest[i] >> 4]);
                kr_buf_putc(&b, hx[p->digest[i] & 15]);
            }
        } else {
            st = kr_eval(p, NULL, 0, 0, &v, &d);
            if (st == ZXV_OK) {
                if (what == KR_TEXT_VALUE) kr_print_value(v, &b);
                else if (what == KR_TEXT_ND) nd_text(v, &b);
                else {
                    size_t need = 0;
                    if (kr_value_display(v, NULL, 0, &need) == ZXV_E_BUFFER) {
                        char *t = (char *) malloc(need + 1u);
                        if (!t) b.oom = 1;
                        else {
                            if (kr_value_display(v, t, need + 1u, &need) == ZXV_OK) kr_buf_put(&b, t, need);
                            free(t);
                        }
                    }
                }
            }
        }
    }
    if (st != ZXV_OK) {
        free(b.b);
        b.b = NULL;
        b.n = b.cap = 0;
        b.oom = 0;
        kr_buf_puts(&b, d.msg[0] ? d.msg : kr_status_name(st));
        {
            size_t ignored = 0;
            int32_t ws = out_buf(&b, buf, cap, &ignored);
            *out_len = ignored;
            kr_value_destroy(v);
            kr_program_destroy(p);
            return ws != ZXV_OK ? ws : st; /* ZXV_E_BUFFER: ask again with *out_len + 1 */
        }
    }
    kr_value_destroy(v);
    kr_program_destroy(p);
    return out_buf(&b, buf, cap, out_len);
}

int32_t kr_post(const zxv_cap *cap, const kr_value *amounts, uint32_t from, uint32_t to, const char *ccy,
                const char *ref_prefix, uint64_t *entry_ids, uint64_t ids_cap, uint64_t *out_posted) {
    size_t plen;
    if (out_posted) *out_posted = 0;
    if (!cap || !amounts || !ccy || !ref_prefix || !out_posted) return ZXV_E_ARG;
    if (amounts->rank > 1) {
        kr_set_error("kr_post: the amounts must be a scalar or a vector");
        return KR_E_RANK;
    }
    if (amounts->kind != KR_KIND_NUM) {
        kr_set_error("kr_post: the amounts are truth values, not numbers");
        return KR_E_DOMAIN;
    }
    plen = strlen(ref_prefix);
    if (plen + 1u + 20u > 35u) {
        kr_set_error("kr_post: the reference prefix leaves no room for -<index> in 35 bytes");
        return ZXV_E_TOO_LONG;
    }
    for (uint64_t i = 0; i < amounts->n; i++) {
        if (amounts->den[i] != 1 || amounts->num[i] <= 0) {
            kr_set_errorf("kr_post: amount %llu is %s: postings need whole minor units above 0 (never rounded)",
                          (unsigned long long) i, kr_rat_str(amounts->num[i], amounts->den[i]));
            return amounts->den[i] != 1 ? KR_E_DOMAIN : ZXV_E_RANGE;
        }
    }
    for (uint64_t i = 0; i < amounts->n; i++) {
        kr_buf rb = {NULL, 0, 0, 0};
        uint64_t id = 0;
        int32_t st;
        kr_buf_put(&rb, ref_prefix, plen);
        kr_buf_putc(&rb, '-');
        kr_buf_u64(&rb, i);
        if (rb.oom) {
            free(rb.b);
            return ZXV_E_NOMEM;
        }
        st = zxv_ledger_post_cap(cap, from, to, amounts->num[i], ccy, rb.b, &id);
        if (st != ZXV_OK) {
            char why[256];
            size_t n = 0;
            if (zxv_last_error(why, sizeof why, &n) != ZXV_OK) why[0] = '\0';
            kr_set_errorf("ledger refused %s after %llu postings: %s%s%s", rb.b, (unsigned long long) i,
                          zxv_status_name(st), why[0] ? ": " : "", why);
            free(rb.b);
            return st;
        }
        free(rb.b);
        if (entry_ids && i < ids_cap) entry_ids[i] = id;
        *out_posted = i + 1u;
    }
    return ZXV_OK;
}
