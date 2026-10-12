/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_myra.c — MYRA, the one-line-per-node shorthand:
 *
 *   ^ZXPAY(batch:i9,seq:i9)=ref:t16!^amount:n15.2!^ccy:t3!^from:i10^to:i10
 *   ^ZXPAY("B",ref:t16,batch:i9,seq:i9)=
 *
 * Integer code only (R11). */
#include <string.h>

#include "zm_internal.h"

static int is_word_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '.';
}

/* tN | iN | nP.S | d */
static int32_t myra_type(zm_lx *lx, zm_field *f) {
    size_t at = lx->i;
    char c;
    int32_t st = ZXV_OK;
    if (lx->i >= lx->len) return zm_lx_err(lx, ZM_E_SYNTAX, at, "expected a type: tN, iN, nP.S or d");
    c = lx->t[lx->i];
    if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
    switch (c) {
    case 't':
        lx->i++;
        f->type = ZM_T_TEXT;
        st = zm_lx_uint(lx, ZM_MAX_TEXTLEN, &f->len, "the TEXT length");
        break;
    case 'i':
        lx->i++;
        f->type = ZM_T_INT;
        st = zm_lx_uint(lx, ZM_MAX_DIGITS, &f->len, "the INTEGER digits");
        break;
    case 'n':
        lx->i++;
        f->type = ZM_T_NUM;
        st = zm_lx_uint(lx, ZM_MAX_DIGITS, &f->len, "the NUMBER precision");
        if (st != ZXV_OK) break;
        if (lx->i >= lx->len || lx->t[lx->i] != '.')
            return zm_lx_err(lx, ZM_E_SYNTAX, lx->i,
                             "a NUMBER is nP.S: precision, a point, then decimals (n15.2)");
        lx->i++;
        st = zm_lx_uint(lx, ZM_MAX_DIGITS, &f->scale, "the NUMBER decimals");
        break;
    case 'd':
        lx->i++;
        f->type = ZM_T_DATE;
        break;
    case 's': return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "set-of-codes fields are not supported; use tN");
    case 'p': return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "pointer fields are not supported; use iN");
    case 'w': return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "word-processing fields are not supported");
    case 'h': return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "$H date-and-time fields are not supported; dates only (d)");
    case 'f':
    case 'r': return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "floating point is refused (R11); use nP.S");
    default: return zm_lx_err(lx, ZM_E_SYNTAX, at, "expected a type: tN, iN, nP.S or d");
    }
    if (st != ZXV_OK) return st;
    if (lx->i < lx->len && is_word_char(lx->t[lx->i]))
        return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "unexpected character after the type");
    return ZXV_OK;
}

/* name ":" type, with the name's position. */
static int32_t myra_field(zm_lx *lx, zm_item *it) {
    int32_t st;
    memset(it, 0, sizeof *it);
    if ((st = zm_lx_name(lx, it->f.name, &it->pos)) != ZXV_OK) return st;
    zm_lx_ws(lx);
    if (lx->i >= lx->len || lx->t[lx->i] != ':')
        return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "expected \":\" and a type after %s", it->f.name);
    lx->i++;
    zm_lx_ws(lx);
    return myra_type(lx, &it->f);
}

static int32_t myra_line(zm_lx *lx, zm_schema *s) {
    char g[ZM_MAX_GNAME + 1];
    size_t at = lx->i;
    zm_node *nd;
    uint32_t p = 1;
    int filled = 0;
    int32_t st;
    if ((st = zm_lx_gname(lx, g)) != ZXV_OK) return st;
    if (!s->gname[0]) memcpy(s->gname, g, sizeof g);
    else if (strcmp(s->gname, g) != 0)
        return zm_lx_err(lx, ZM_E_UNSUPPORTED, at,
                         "^%s: a schema declares one global (^%s); write a second file", g,
                         s->gname);
    nd = zm_schema_add_node(s, &st);
    if (!nd) return zm_lx_err(lx, st, at, "more than %u nodes", (unsigned) ZM_MAX_NODES);
    nd->pos = zm_lx_pos(lx, at);
    if (lx->i < lx->len && lx->t[lx->i] == '(') {
        lx->i++;
        for (;;) {
            zm_sub *x;
            zm_lx_ws(lx);
            if (nd->nsubs >= ZM_MAX_SUBS)
                return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "more than %u subscripts",
                                 (unsigned) ZM_MAX_SUBS);
            x = &nd->subs[nd->nsubs];
            if (zm_lx_at_literal(lx)) {
                if ((st = zm_lx_literal(lx, x)) != ZXV_OK) return st;
            } else {
                zm_item it;
                if ((st = myra_field(lx, &it)) != ZXV_OK) return st;
                zm_lx_ws(lx);
                if (lx->i < lx->len && lx->t[lx->i] == '!')
                    return zm_lx_err(lx, ZM_E_SYNTAX, lx->i,
                                     "a key is always required; drop the \"!\"");
                it.f.sub = nd->nsubs + 1u;
                memset(x, 0, sizeof *x);
                x->kind = ZM_SUB_KEY;
                x->field = nd->nf;
                if ((st = zm_node_add_field(nd, &it)) != ZXV_OK)
                    return zm_lx_err(lx, st, it.pos.offset, "too many fields");
            }
            nd->nsubs++;
            zm_lx_ws(lx);
            if (lx->i < lx->len && lx->t[lx->i] == ',') {
                lx->i++;
                continue;
            }
            if (lx->i < lx->len && lx->t[lx->i] == ')') {
                lx->i++;
                break;
            }
            return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "expected \",\" or \")\"");
        }
    }
    zm_lx_ws(lx);
    if (lx->i >= lx->len || lx->t[lx->i] != '=')
        return zm_lx_err(lx, ZM_E_SYNTAX, lx->i,
                         "expected \"=\" and the pieces (an index node ends with \"=\")");
    lx->i++;
    for (;;) {
        zm_item it;
        zm_lx_ws(lx);
        if (zm_lx_eol(lx)) break;
        if (lx->t[lx->i] == '^') {
            if (p >= ZM_MAX_PIECES)
                return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "more than %u pieces",
                                 (unsigned) ZM_MAX_PIECES);
            p++;
            filled = 0;
            lx->i++;
            continue;
        }
        if (filled)
            return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "expected \"^\" between two pieces");
        if ((st = myra_field(lx, &it)) != ZXV_OK) return st;
        zm_lx_ws(lx);
        if (lx->i < lx->len && lx->t[lx->i] == '!') {
            it.f.required = 1;
            lx->i++;
        }
        it.f.piece = p;
        if ((st = zm_node_add_field(nd, &it)) != ZXV_OK)
            return zm_lx_err(lx, st, it.pos.offset, "too many fields");
        filled = 1;
    }
    zm_lx_next_line(lx);
    return ZXV_OK;
}

int32_t zm_myra_parse(const char *text, size_t len, zm_schema **out, zm_diag *d) {
    zm_lx lx;
    zm_schema *s;
    int32_t st;
    if (!out) return ZXV_E_ARG;
    if ((st = zm_lx_start(&lx, text, len, d)) != ZXV_OK) return st;
    if (!(s = zm_schema_new())) return ZXV_E_NOMEM;
    while (lx.i < lx.len) {
        zm_lx_ws(&lx);
        if (zm_lx_eol(&lx)) {
            zm_lx_next_line(&lx);
            continue;
        }
        if ((st = myra_line(&lx, s)) != ZXV_OK) break;
    }
    if (st == ZXV_OK) st = zm_schema_finish(s, text, len, d);
    if (st != ZXV_OK) {
        zm_schema_destroy(s);
        return st;
    }
    *out = s;
    return ZXV_OK;
}

int32_t zm_schema_parse(const char *text, size_t len, zm_schema **out, zm_diag *d) {
    size_t i = 0;
    if (!out || (!text && len)) return ZXV_E_ARG;
    while (i < len) {
        char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            i++;
        } else if (c == ';') {
            while (i < len && text[i] != '\n') i++;
        } else {
            break;
        }
    }
    if (len - i >= 6u) {
        char w[7];
        zm_lower(w, text + i, 6);
        if (memcmp(w, "global", 6) == 0 &&
            (len - i == 6u || text[i + 6] == ' ' || text[i + 6] == '\t'))
            return zm_gmap_parse(text, len, out, d);
    }
    return zm_myra_parse(text, len, out, d);
}
