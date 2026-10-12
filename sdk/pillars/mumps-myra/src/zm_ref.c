/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_ref.c — global references as M prints them (^ZXPAY(7,"B",1)):
 * parsing, node matching and $QUERY order. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm_internal.h"

void zm_sb_sub(zm_sb *sb, const char *s, size_t n, int isnum) {
    if (isnum) {
        zm_sb_put(sb, s, n);
        return;
    }
    zm_sb_chr(sb, '"');
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '"') zm_sb_chr(sb, '"');
        zm_sb_chr(sb, s[i]);
    }
    zm_sb_chr(sb, '"');
}

size_t zm_rsub_text(const zm_rsub *r, char *b, size_t cap) {
    size_t k = 0;
    if (!r->quoted) {
        if (r->n >= cap) return (size_t) -1;
        memcpy(b, r->s, r->n);
        b[r->n] = '\0';
        return r->n;
    }
    for (size_t i = 0; i < r->n; i++) {
        if (r->s[i] == '"') i++; /* "" inside a string is one quote */
        if (k + 1u >= cap) return (size_t) -1;
        b[k++] = r->s[i];
    }
    b[k] = '\0';
    return k;
}

int32_t zm_ref_parse(const char *s, size_t n, zm_ref *out) {
    char sh[64];
    size_t i = 1;
    memset(out, 0, sizeof *out);
    if (!s || n == 0 || s[0] != '^') {
        zm_set_errorf("a global reference starts with \"^\": \"%s\"", zm_show(s ? s : "", s ? n : 0, sh, sizeof sh));
        return ZM_E_KEY;
    }
    if (n > ZM_MAX_REF) {
        zm_set_errorf("the reference is longer than %u bytes", (unsigned) ZM_MAX_REF);
        return ZM_E_KEY;
    }
    out->g = s + 1;
    while (i < n && s[i] != '(') i++;
    out->gn = i - 1u;
    if (!zm_valid_gname(out->g, out->gn)) {
        zm_set_errorf("\"%s\" is not a global name", zm_show(out->g, out->gn, sh, sizeof sh));
        return ZM_E_KEY;
    }
    if (i == n) return ZXV_OK;
    i++; /* '(' */
    for (;;) {
        zm_rsub *r;
        if (out->n >= ZM_MAX_SUBS) {
            zm_set_errorf("more than %u subscripts", (unsigned) ZM_MAX_SUBS);
            return ZM_E_KEY;
        }
        r = &out->sub[out->n];
        if (i < n && s[i] == '"') {
            size_t b = ++i;
            for (;;) {
                if (i >= n) {
                    zm_set_errorf("subscript %u: the string is not closed", (unsigned) out->n + 1u);
                    return ZM_E_KEY;
                }
                if (s[i] == '"') {
                    if (i + 1u < n && s[i + 1u] == '"') {
                        i += 2u;
                        continue;
                    }
                    break;
                }
                if ((unsigned char) s[i] < 0x20u || s[i] == 0x7F) {
                    zm_set_errorf("subscript %u holds a control character", (unsigned) out->n + 1u);
                    return ZM_E_KEY;
                }
                i++;
            }
            r->s = s + b;
            r->n = i - b;
            r->quoted = 1;
            i++; /* closing quote */
            if (r->n == 0) {
                zm_set_errorf("subscript %u is \"\" (M globals refuse null subscripts)",
                              (unsigned) out->n + 1u);
                return ZM_E_KEY;
            }
            {
                char *t = (char *) malloc(r->n + 1u);
                size_t tn;
                if (!t) return ZXV_E_NOMEM;
                tn = zm_rsub_text(r, t, r->n + 1u);
                r->isnum = zm_is_canonic(t, tn);
                free(t);
            }
        } else {
            size_t b = i;
            while (i < n && s[i] != ',' && s[i] != ')') i++;
            r->s = s + b;
            r->n = i - b;
            if (r->n == 0) {
                zm_set_errorf("subscript %u is empty", (unsigned) out->n + 1u);
                return ZM_E_KEY;
            }
            if (!zm_is_canonic(r->s, r->n)) {
                zm_set_errorf("subscript %u (%s) is not a canonical number or a quoted string; "
                              "M would store it under another subscript",
                              (unsigned) out->n + 1u, zm_show(r->s, r->n, sh, sizeof sh));
                return ZM_E_KEY;
            }
            r->isnum = 1;
        }
        out->n++;
        if (i < n && s[i] == ',') {
            i++;
            continue;
        }
        if (i < n && s[i] == ')' && i + 1u == n) return ZXV_OK;
        zm_set_errorf("expected \",\" or a final \")\" at byte %zu of the reference", i + 1u);
        return ZM_E_KEY;
    }
}

int32_t zm_ref_match_node(const zm_schema *s, const zm_node *nd, const zm_ref *r, char *msg,
                          size_t cap) {
    char *t;
    int32_t st = ZXV_OK;
    if (r->gn != strlen(s->gname) || memcmp(r->g, s->gname, r->gn) != 0) {
        snprintf(msg, cap, "the schema is for ^%s", s->gname);
        return ZM_E_KEY;
    }
    if (r->n != nd->nsubs) {
        snprintf(msg, cap, "%u subscripts; the node has %u", (unsigned) r->n,
                 (unsigned) nd->nsubs);
        return ZM_E_KEY;
    }
    t = (char *) malloc(ZM_MAX_REF + 1u);
    if (!t) return ZXV_E_NOMEM;
    for (uint32_t i = 0; i < r->n && st == ZXV_OK; i++) {
        const zm_sub *x = &nd->subs[i];
        size_t tn = zm_rsub_text(&r->sub[i], t, ZM_MAX_REF + 1u);
        if (x->kind == ZM_SUB_LIT) {
            if (x->isnum != r->sub[i].isnum || x->litlen != tn || memcmp(x->lit, t, tn) != 0) {
                char a[64];
                zm_sb sb;
                zm_sb_init(&sb, 200);
                zm_sb_sub(&sb, x->lit, x->litlen, x->isnum);
                snprintf(msg, cap, "subscript %u is %s; the node has %s", (unsigned) i + 1u,
                         zm_show(t, tn, a, sizeof a), sb.err == ZXV_OK && sb.b ? sb.b : "?");
                zm_sb_free(&sb);
                st = ZM_E_KEY;
            }
        } else {
            char why[300];
            if (zm_key_check(&nd->f[x->field].f, t, tn, r->sub[i].isnum, NULL, why, sizeof why) !=
                ZXV_OK) {
                snprintf(msg, cap, "subscript %u: %s", (unsigned) i + 1u, why);
                st = ZM_E_KEY;
            }
        }
    }
    free(t);
    return st;
}

int32_t zm_node_match(const zm_schema *s, const char *ref, size_t ref_len, uint32_t *out_node) {
    zm_ref r;
    char msg[400], first[400] = "";
    int32_t st, have_same_len = 0;
    if (!s || !ref || !out_node) return ZXV_E_ARG;
    if ((st = zm_ref_parse(ref, ref_len, &r)) != ZXV_OK) return st;
    for (uint32_t i = 0; i < s->n; i++) {
        st = zm_ref_match_node(s, &s->nodes[i], &r, msg, sizeof msg);
        if (st == ZXV_OK) {
            *out_node = i;
            return ZXV_OK;
        }
        if (st != ZM_E_KEY) return st;
        if (!first[0] || (!have_same_len && s->nodes[i].nsubs == r.n)) {
            memcpy(first, msg, sizeof first);
            if (s->nodes[i].nsubs == r.n) have_same_len = 1;
        }
    }
    {
        char sh[64];
        zm_set_errorf("%s matches no node of ^%s: %s", zm_show(ref, ref_len, sh, sizeof sh),
                      s->gname, first);
    }
    return ZM_E_KEY;
}

int32_t zm_ref_collate(const char *a, size_t an, const char *b, size_t bn, int32_t *out_cmp) {
    zm_ref x, y;
    int32_t st, c = 0;
    char *ta, *tb;
    size_t m;
    int r;
    if (!out_cmp || !a || !b) return ZXV_E_ARG;
    if ((st = zm_ref_parse(a, an, &x)) != ZXV_OK) return st;
    if ((st = zm_ref_parse(b, bn, &y)) != ZXV_OK) return st;
    m = x.gn < y.gn ? x.gn : y.gn;
    r = memcmp(x.g, y.g, m);
    if (r || x.gn != y.gn) {
        *out_cmp = r ? (r < 0 ? -1 : 1) : (x.gn < y.gn ? -1 : 1);
        return ZXV_OK;
    }
    ta = (char *) malloc(ZM_MAX_REF + 1u);
    tb = (char *) malloc(ZM_MAX_REF + 1u);
    if (!ta || !tb) {
        free(ta);
        free(tb);
        return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < x.n && i < y.n && c == 0; i++) {
        size_t na = zm_rsub_text(&x.sub[i], ta, ZM_MAX_REF + 1u);
        size_t nb = zm_rsub_text(&y.sub[i], tb, ZM_MAX_REF + 1u);
        zm_collate(ta, na, tb, nb, &c);
    }
    free(ta);
    free(tb);
    if (c == 0 && x.n != y.n) c = x.n < y.n ? -1 : 1;
    *out_cmp = c;
    return ZXV_OK;
}
