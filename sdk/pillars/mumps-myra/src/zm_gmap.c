/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_gmap.c — the global map, the long form of a schema:
 *
 *   GLOBAL ^ZXPAY
 *   NODE ^ZXPAY(BATCH,SEQ)
 *     KEY   BATCH  INTEGER(9)
 *     KEY   SEQ    INTEGER(9)
 *     PIECE 1 REF    TEXT(16)     REQUIRED
 *     PIECE 2 AMOUNT NUMBER(15,2) REQUIRED
 *
 * Keywords and names are not case-sensitive; the global name is.
 * Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "zm_internal.h"

static int word_is(const char *w, size_t n, const char *kw) {
    size_t k = strlen(kw);
    if (n != k) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = w[i];
        if (c >= 'a' && c <= 'z') c = (char) (c - 32);
        if (c != kw[i]) return 0;
    }
    return 1;
}

/* "(" n [ "," s ] ")" with blanks allowed inside. */
static int32_t paren_args(zm_lx *lx, uint32_t max, uint32_t *a, uint32_t *b, int two,
                          const char *tname) {
    int32_t st;
    zm_lx_ws(lx);
    if (lx->i >= lx->len || lx->t[lx->i] != '(')
        return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, two ? "%s needs (p,s)" : "%s needs (n)", tname);
    lx->i++;
    zm_lx_ws(lx);
    if ((st = zm_lx_uint(lx, max, a, two ? "the NUMBER precision" : "the length")) != ZXV_OK)
        return st;
    zm_lx_ws(lx);
    if (two) {
        if (lx->i >= lx->len || lx->t[lx->i] != ',')
            return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "NUMBER needs (p,s): precision and decimals");
        lx->i++;
        zm_lx_ws(lx);
        if ((st = zm_lx_uint(lx, max, b, "the NUMBER decimals")) != ZXV_OK) return st;
        zm_lx_ws(lx);
    }
    if (lx->i >= lx->len || lx->t[lx->i] != ')')
        return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "expected \")\"");
    lx->i++;
    return ZXV_OK;
}

static int32_t gmap_type(zm_lx *lx, zm_field *f) {
    const char *w;
    size_t at, n;
    zm_lx_ws(lx);
    at = lx->i;
    n = zm_lx_word(lx, &w);
    if (word_is(w, n, "TEXT")) {
        f->type = ZM_T_TEXT;
        return paren_args(lx, ZM_MAX_TEXTLEN, &f->len, NULL, 0, "TEXT");
    }
    if (word_is(w, n, "INTEGER")) {
        f->type = ZM_T_INT;
        return paren_args(lx, ZM_MAX_DIGITS, &f->len, NULL, 0, "INTEGER");
    }
    if (word_is(w, n, "NUMBER")) {
        f->type = ZM_T_NUM;
        return paren_args(lx, ZM_MAX_DIGITS, &f->len, &f->scale, 1, "NUMBER");
    }
    if (word_is(w, n, "DATE")) {
        f->type = ZM_T_DATE;
        return ZXV_OK;
    }
    if (word_is(w, n, "SET"))
        return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "SET (set of codes) is not supported; use TEXT(n)");
    if (word_is(w, n, "POINTER"))
        return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "POINTER is not supported; use INTEGER(n)");
    if (word_is(w, n, "WP") || word_is(w, n, "WORD-PROCESSING"))
        return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "word-processing fields are not supported");
    if (word_is(w, n, "DATETIME") || word_is(w, n, "TIME"))
        return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "$H times are not supported; dates only (DATE)");
    if (word_is(w, n, "FLOAT") || word_is(w, n, "REAL") || word_is(w, n, "DOUBLE"))
        return zm_lx_err(lx, ZM_E_UNSUPPORTED, at, "floating point is refused (R11); use NUMBER(p,s)");
    return zm_lx_err(lx, ZM_E_SYNTAX, at, "expected a type: TEXT(n), INTEGER(n), NUMBER(p,s) or DATE");
}

static int32_t end_of_statement(zm_lx *lx) {
    zm_lx_ws(lx);
    if (!zm_lx_eol(lx)) return zm_lx_err(lx, ZM_E_SYNTAX, lx->i, "unexpected text at the end of the line");
    zm_lx_next_line(lx);
    return ZXV_OK;
}

/* The names written in the current NODE line, per subscript. */
typedef struct {
    char name[ZM_MAX_SUBS][ZM_MAX_NAME + 1];
    zm_pos pos[ZM_MAX_SUBS];
} node_names;

/* Every named subscript of the node must have had a KEY line. */
static int32_t close_node(zm_lx *lx, zm_node *nd, const node_names *nn) {
    if (!nd) return ZXV_OK;
    for (uint32_t i = 0; i < nd->nsubs; i++)
        if (nd->subs[i].kind == ZM_SUB_KEY && nd->subs[i].field == UINT32_MAX)
            return zm_lx_err(lx, ZM_E_SYNTAX, nn->pos[i].offset,
                             "subscript %s has no KEY line in this node", nn->name[i]);
    return ZXV_OK;
}

static int32_t node_line(zm_lx *lx, zm_schema *s, zm_node **out, node_names *nn) {
    char g[ZM_MAX_GNAME + 1];
    size_t at;
    zm_node *nd;
    int32_t st;
    zm_lx_ws(lx);
    at = lx->i;
    if ((st = zm_lx_gname(lx, g)) != ZXV_OK) return st;
    if (strcmp(s->gname, g) != 0)
        return zm_lx_err(lx, ZM_E_UNSUPPORTED, at,
                         "^%s: a global map declares one global (^%s); write a second map", g,
                         s->gname);
    nd = zm_schema_add_node(s, &st);
    if (!nd) return zm_lx_err(lx, st, at, "more than %u nodes", (unsigned) ZM_MAX_NODES);
    nd->pos = zm_lx_pos(lx, at);
    memset(nn, 0, sizeof *nn);
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
                if ((st = zm_lx_name(lx, nn->name[nd->nsubs], &nn->pos[nd->nsubs])) != ZXV_OK)
                    return st;
                for (uint32_t j = 0; j < nd->nsubs; j++)
                    if (nd->subs[j].kind == ZM_SUB_KEY && strcmp(nn->name[j], nn->name[nd->nsubs]) == 0)
                        return zm_lx_err(lx, ZM_E_SYNTAX, nn->pos[nd->nsubs].offset,
                                         "the subscript %s appears twice", nn->name[j]);
                memset(x, 0, sizeof *x);
                x->kind = ZM_SUB_KEY;
                x->field = UINT32_MAX; /* bound by its KEY line */
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
    *out = nd;
    return end_of_statement(lx);
}

static int32_t key_line(zm_lx *lx, zm_node *nd, const node_names *nn) {
    zm_item it;
    uint32_t i;
    int32_t st;
    memset(&it, 0, sizeof it);
    zm_lx_ws(lx);
    if ((st = zm_lx_name(lx, it.f.name, &it.pos)) != ZXV_OK) return st;
    for (i = 0; i < nd->nsubs; i++)
        if (nd->subs[i].kind == ZM_SUB_KEY && strcmp(nn->name[i], it.f.name) == 0) break;
    if (i == nd->nsubs)
        return zm_lx_err(lx, ZM_E_SYNTAX, it.pos.offset,
                         "KEY %s is not a subscript of the NODE line", it.f.name);
    if (nd->subs[i].field != UINT32_MAX)
        return zm_lx_err(lx, ZM_E_SYNTAX, it.pos.offset, "KEY %s is declared twice", it.f.name);
    if ((st = gmap_type(lx, &it.f)) != ZXV_OK) return st;
    zm_lx_ws(lx);
    {
        const char *w;
        size_t at = lx->i, n = zm_lx_word(lx, &w);
        if (word_is(w, n, "REQUIRED"))
            return zm_lx_err(lx, ZM_E_SYNTAX, at, "a KEY is always required; drop REQUIRED");
        lx->i = at;
    }
    it.f.sub = i + 1u;
    nd->subs[i].field = nd->nf;
    if ((st = zm_node_add_field(nd, &it)) != ZXV_OK)
        return zm_lx_err(lx, st, it.pos.offset, "too many fields");
    return end_of_statement(lx);
}

static int32_t piece_line(zm_lx *lx, zm_node *nd) {
    zm_item it;
    int32_t st;
    memset(&it, 0, sizeof it);
    zm_lx_ws(lx);
    if ((st = zm_lx_uint(lx, ZM_MAX_PIECES, &it.f.piece, "a piece number")) != ZXV_OK) return st;
    if (it.f.piece == 0) return zm_lx_err(lx, ZM_E_SYNTAX, lx->i - 1u, "pieces are numbered from 1");
    zm_lx_ws(lx);
    if ((st = zm_lx_name(lx, it.f.name, &it.pos)) != ZXV_OK) return st;
    if ((st = gmap_type(lx, &it.f)) != ZXV_OK) return st;
    zm_lx_ws(lx);
    {
        const char *w;
        size_t at = lx->i, n = zm_lx_word(lx, &w);
        if (word_is(w, n, "REQUIRED")) it.f.required = 1;
        else lx->i = at;
    }
    if ((st = zm_node_add_field(nd, &it)) != ZXV_OK)
        return zm_lx_err(lx, st, it.pos.offset, "too many fields");
    return end_of_statement(lx);
}

int32_t zm_gmap_parse(const char *text, size_t len, zm_schema **out, zm_diag *d) {
    zm_lx lx;
    zm_schema *s;
    zm_node *nd = NULL;
    node_names *nn;
    int32_t st;
    if (!out) return ZXV_E_ARG;
    if ((st = zm_lx_start(&lx, text, len, d)) != ZXV_OK) return st;
    if (!(s = zm_schema_new())) return ZXV_E_NOMEM;
    if (!(nn = (node_names *) calloc(1, sizeof *nn))) {
        zm_schema_destroy(s);
        return ZXV_E_NOMEM;
    }
    while (st == ZXV_OK && lx.i < lx.len) {
        const char *w;
        size_t at, n;
        zm_lx_ws(&lx);
        if (zm_lx_eol(&lx)) {
            zm_lx_next_line(&lx);
            continue;
        }
        at = lx.i;
        n = zm_lx_word(&lx, &w);
        if (word_is(w, n, "GLOBAL")) {
            if (s->gname[0]) {
                st = zm_lx_err(&lx, ZM_E_UNSUPPORTED, at, "a global map declares one GLOBAL");
                break;
            }
            zm_lx_ws(&lx);
            if ((st = zm_lx_gname(&lx, s->gname)) == ZXV_OK) st = end_of_statement(&lx);
        } else if (!s->gname[0]) {
            st = zm_lx_err(&lx, ZM_E_SYNTAX, at, "a global map starts with GLOBAL ^NAME");
        } else if (word_is(w, n, "NODE")) {
            if ((st = close_node(&lx, nd, nn)) == ZXV_OK) st = node_line(&lx, s, &nd, nn);
        } else if (word_is(w, n, "KEY") || word_is(w, n, "PIECE")) {
            if (!nd) st = zm_lx_err(&lx, ZM_E_SYNTAX, at, "%s before the first NODE line",
                                    word_is(w, n, "KEY") ? "KEY" : "PIECE");
            else if (word_is(w, n, "KEY")) st = key_line(&lx, nd, nn);
            else st = piece_line(&lx, nd);
        } else {
            st = zm_lx_err(&lx, ZM_E_SYNTAX, at, "expected GLOBAL, NODE, KEY or PIECE");
        }
    }
    if (st == ZXV_OK) st = close_node(&lx, nd, nn);
    free(nn);
    if (st == ZXV_OK && !s->gname[0])
        st = zm_diag_set(d, ZM_E_SYNTAX, text, len, len, "a global map starts with GLOBAL ^NAME");
    if (st == ZXV_OK) st = zm_schema_finish(s, text, len, d);
    if (st != ZXV_OK) {
        zm_schema_destroy(s);
        return st;
    }
    *out = s;
    return ZXV_OK;
}
