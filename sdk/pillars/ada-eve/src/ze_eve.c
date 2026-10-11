/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_eve.c — Eve, the short form of the same declarations. One
 * declaration per line, "#" comments:
 *
 *   eve     = "eve" name NL { decl }
 *   decl    = name "=" def NL | name "{" { field ( NL | ";" ) } "}" NL
 *   def     = lo ".." hi                         type T is range lo .. hi
 *           | "dec(" digits "," scale ")" [ lo ".." hi ]
 *                                                type T is delta 10.0**(-scale)
 *                                                  digits d [range lo .. hi]
 *           | "(" name { "," name } ")"          type T is (A, B, C)
 *           | "str(" n ")"                       subtype T is String (1 .. n)
 *           | "[" index { "," index } "]" use    type T is array (...) of use
 *           | type [ lo ".." hi ]                subtype T is type [range ...]
 *   field   = name { "," name } ":" use
 *   use     = type [ lo ".." hi ] | "str(" n ")"
 *   index   = type | n (1 .. n) | lo ".." hi
 *
 * Bounds are integer expressions (2**32 - 1), decimal literals (-999.99)
 * or enumeration literals (mon .. fri), as the base type needs. Names
 * follow Ada's rules and Ada's reserved words are reserved here too, so
 * every Eve file prints as an Ada spec. Integer code only (R11). */
#include <string.h>

#include "ze_internal.h"

typedef struct {
    ze_lexer *lx;
    ze_pkg *p;
} ep;

static const ze_tok *cur(ep *e) { return &e->lx->t[e->lx->i]; }
static const ze_tok *peek(ep *e, size_t k) {
    size_t j = e->lx->i + k;
    return &e->lx->t[j < e->lx->n ? j : e->lx->n - 1u];
}
static void adv(ep *e) {
    if (e->lx->t[e->lx->i].kind != ZT_EOF && e->lx->t[e->lx->i].kind != ZT_ERR) e->lx->i++;
}
static int sym(const ze_tok *t, int s) { return t->kind == ZT_SYM && t->sym == s; }
static int word(ep *e, const ze_tok *t, const char *w) {
    size_t n = strlen(w);
    if (t->kind != ZT_ID || t->len != n) return 0;
    for (size_t k = 0; k < n; k++) {
        char c = e->lx->text[t->off + k];
        if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
        if (c != w[k]) return 0;
    }
    return 1;
}
static int at_end(const ze_tok *t) {
    return t->kind == ZT_NL || t->kind == ZT_EOF || sym(t, ';') || sym(t, '}');
}

#define EERR(st, off, ...) ZE_ERR(e->lx, (st), (off), __VA_ARGS__)

static int32_t expect_sym(ep *e, int s, const char *what) {
    if (!sym(cur(e), s)) return ze_lex_error_at(e->lx, cur(e), what);
    adv(e);
    return ZXV_OK;
}

static int32_t int_expr(ep *e, int64_t lo, int64_t hi, const char *what, int64_t *out) {
    size_t at = cur(e)->off;
    ze_i128 v;
    int32_t st = ze_expr(e->lx, &v);
    if (st != ZXV_OK) return st;
    if (v < (ze_i128) lo || v > (ze_i128) hi) return EERR(ZE_E_CONSTRAINT, at, "%s is out of range", what);
    *out = (int64_t) v;
    return ZXV_OK;
}

static int32_t end_line(ep *e) {
    const ze_tok *t = cur(e);
    if (t->kind == ZT_NL) {
        adv(e);
        return ZXV_OK;
    }
    if (t->kind == ZT_EOF) return ZXV_OK;
    return ze_lex_error_at(e->lx, t, "the end of the line");
}

static int32_t type_name(ep *e, const ze_tok *t, uint32_t *out) {
    char low[ZE_MAX_NAME + 1];
    if (t->kind != ZT_ID) return ze_lex_error_at(e->lx, t, "a type name");
    ze_lower(low, e->lx->text + t->off, t->len);
    *out = ze_pkg_lookup(e->p, low);
    if (*out != UINT32_MAX) return ZXV_OK;
    if (ze_pkg_is_literal(e->p, low))
        return EERR(ZE_E_NAME, t->off, "'%s' is an enumeration literal, not a type", low);
    if (strcmp(low, "dec") == 0)
        return EERR(ZE_E_UNSUPPORTED, t->off, "a decimal type is declared on its own line and named (money = dec(15, 2)), "
                    "as in Ada; use the name here");
    if (ze_predefined(low, t->len))
        return EERR(ZE_E_UNSUPPORTED, t->off, "'%s' is not an Eve type: declare a range, dec(d, s) or str(n)", low);
    return EERR(ZE_E_NAME, t->off, "unknown type '%s' (types are declared before they are used)", low);
}

static int32_t bounds(ep *e, uint32_t base, int64_t *lo, int64_t *hi) {
    const ze_titem *b = &e->p->types[base];
    int32_t st;
    for (int k = 0; k < 2; k++) {
        int64_t *v = k ? hi : lo;
        if (k && (st = expect_sym(e, ZS_DOTDOT, "'..'")) != ZXV_OK) return st;
        if (b->t.kind == ZE_K_INT) {
            if ((st = int_expr(e, INT64_MIN, INT64_MAX, "the bound", v)) != ZXV_OK) return st;
        } else if (b->t.kind == ZE_K_DEC) {
            if ((st = ze_dec_bound(e->lx, b->t.scale, v)) != ZXV_OK) return st;
        } else if (b->t.kind == ZE_K_ENUM) {
            const ze_tok *t = cur(e);
            char low[ZE_MAX_NAME + 1];
            uint32_t pos;
            if (t->kind != ZT_ID) return ze_lex_error_at(e->lx, t, "an enumeration literal");
            ze_lower(low, e->lx->text + t->off, t->len);
            if (ze_enum_pos(e->p, base, low, &pos) != ZXV_OK)
                return EERR(ZE_E_NAME, t->off, "'%s' is not a literal of %s", low, b->t.name);
            *v = pos;
            adv(e);
        } else {
            return EERR(ZE_E_CONSTRAINT, cur(e)->off, "only a scalar type takes a range constraint");
        }
    }
    return ZXV_OK;
}

/* str(n) */
static int32_t text_def(ep *e, const char *name, uint32_t *out) {
    uint64_t at = cur(e)->off;
    int64_t n;
    int32_t st;
    adv(e); /* str */
    if ((st = expect_sym(e, '(', "'('")) != ZXV_OK) return st;
    if ((st = int_expr(e, INT32_MIN, INT32_MAX, "the length", &n)) != ZXV_OK) return st;
    if ((st = expect_sym(e, ')', "')'")) != ZXV_OK) return st;
    return ze_decl_text(e->lx, e->p, name, n, at, out);
}

static int32_t use(ep *e, const char *name, uint32_t *out) {
    const ze_tok *t = cur(e);
    uint64_t at = t->off;
    uint32_t base;
    int32_t st;
    if (word(e, t, "str") && sym(peek(e, 1), '(')) return text_def(e, name, out);
    if ((st = type_name(e, t, &base)) != ZXV_OK) return st;
    adv(e);
    if (!at_end(cur(e)) && !sym(cur(e), ']')) {
        int64_t lo, hi;
        if ((st = bounds(e, base, &lo, &hi)) != ZXV_OK) return st;
        return ze_decl_subtype(e->lx, e->p, name, base, 1, lo, hi, at, out);
    }
    if (name[0]) return ze_decl_subtype(e->lx, e->p, name, base, 0, 0, 0, at, out);
    *out = base;
    return ZXV_OK;
}

static int32_t index_def(ep *e, ze_dim *d) {
    const ze_tok *t = cur(e);
    int32_t st;
    if (t->kind == ZT_ID) {
        uint32_t ix;
        if ((st = type_name(e, t, &ix)) != ZXV_OK) return st;
        if (e->p->types[ix].t.kind != ZE_K_INT && e->p->types[ix].t.kind != ZE_K_ENUM)
            return EERR(ZE_E_CONSTRAINT, t->off, "an index is an integer or enumeration type");
        adv(e);
        d->index = ix;
        d->lo = e->p->types[ix].t.lo;
        d->hi = e->p->types[ix].t.hi;
        return ZXV_OK;
    }
    if ((st = int_expr(e, INT32_MIN, INT32_MAX, "an index bound", &d->lo)) != ZXV_OK) return st;
    d->index = UINT32_MAX;
    if (sym(cur(e), ZS_DOTDOT)) {
        adv(e);
        return int_expr(e, INT32_MIN, INT32_MAX, "an index bound", &d->hi);
    }
    d->hi = d->lo; /* [n] is 1 .. n */
    d->lo = 1;
    return ZXV_OK;
}

static int32_t def(ep *e, const char *name, uint64_t at, uint32_t *out) {
    const ze_tok *t = cur(e);
    int32_t st;
    if (word(e, t, "dec") && sym(peek(e, 1), '(')) {
        int64_t digits, scale, lo = 0, hi = 0;
        int has_range = 0;
        adv(e);
        adv(e);
        if ((st = int_expr(e, 1, ZE_MAX_DIGITS, "digits (1 .. 18)", &digits)) != ZXV_OK) return st;
        if ((st = expect_sym(e, ',', "',' between digits and scale")) != ZXV_OK) return st;
        if ((st = int_expr(e, 0, ZE_MAX_DIGITS, "the scale (0 .. 18)", &scale)) != ZXV_OK) return st;
        if ((st = expect_sym(e, ')', "')'")) != ZXV_OK) return st;
        if (!at_end(cur(e))) {
            has_range = 1;
            if ((st = ze_dec_bound(e->lx, (uint32_t) scale, &lo)) != ZXV_OK) return st;
            if ((st = expect_sym(e, ZS_DOTDOT, "'..'")) != ZXV_OK) return st;
            if ((st = ze_dec_bound(e->lx, (uint32_t) scale, &hi)) != ZXV_OK) return st;
        }
        return ze_decl_dec(e->lx, e->p, name, (uint32_t) digits, (uint32_t) scale, has_range, lo, hi, at, out);
    }
    if (word(e, t, "str") && sym(peek(e, 1), '(')) return text_def(e, name, out);
    if (sym(t, '(') && peek(e, 1)->kind == ZT_ID) {
        uint32_t lit0 = e->p->nlits;
        adv(e);
        for (;;) {
            const ze_tok *lt = cur(e);
            if (lt->kind == ZT_ID && lt->len == strlen(name)) {
                char low[ZE_MAX_NAME + 1];
                ze_lower(low, e->lx->text + lt->off, lt->len);
                if (!strcmp(low, name)) return EERR(ZE_E_NAME, lt->off, "the literal '%s' is the type's own name", low);
            }
            if ((st = ze_decl_literal(e->lx, e->p, lit0, lt)) != ZXV_OK) return st;
            adv(e);
            if (!sym(cur(e), ',')) break;
            adv(e);
        }
        if ((st = expect_sym(e, ')', "',' or ')'")) != ZXV_OK) return st;
        return ze_decl_enum(e->lx, e->p, name, lit0, at, out);
    }
    if (sym(t, '[')) {
        ze_dim dims[ZE_MAX_DIMS];
        uint32_t nd = 0, elem;
        adv(e);
        for (;;) {
            if (nd == ZE_MAX_DIMS) return EERR(ZE_E_LIMIT, cur(e)->off, "an array has at most 4 dimensions");
            if ((st = index_def(e, &dims[nd])) != ZXV_OK) return st;
            nd++;
            if (!sym(cur(e), ',')) break;
            adv(e);
        }
        if ((st = expect_sym(e, ']', "',' or ']'")) != ZXV_OK) return st;
        if ((st = use(e, "", &elem)) != ZXV_OK) return st;
        return ze_decl_array(e->lx, e->p, name, dims, nd, elem, at, out);
    }
    if (t->kind == ZT_ID) return use(e, name, out);
    {
        int64_t lo, hi;
        if ((st = int_expr(e, INT64_MIN, INT64_MAX, "the bound (int64)", &lo)) != ZXV_OK) return st;
        if ((st = expect_sym(e, ZS_DOTDOT, "'..'")) != ZXV_OK) return st;
        if ((st = int_expr(e, INT64_MIN, INT64_MAX, "the bound (int64)", &hi)) != ZXV_OK) return st;
        return ze_decl_int(e->lx, e->p, name, lo, hi, at, out);
    }
}

static int32_t record_def(ep *e, const char *name, uint64_t at, uint32_t *out) {
    uint32_t comp0 = e->p->ncomps;
    int32_t st;
    adv(e); /* { */
    for (;;) {
        const ze_tok *t = cur(e);
        size_t first, count = 0;
        uint32_t ty;
        if (t->kind == ZT_NL || sym(t, ';')) {
            adv(e);
            continue;
        }
        if (sym(t, '}')) break;
        first = e->lx->i;
        for (;;) {
            t = cur(e);
            if (t->kind != ZT_ID) return ze_lex_error_at(e->lx, t, "a component name or '}'");
            count++;
            adv(e);
            if (!sym(cur(e), ',')) break;
            adv(e);
        }
        if ((st = expect_sym(e, ':', "':'")) != ZXV_OK) return st;
        if ((st = use(e, "", &ty)) != ZXV_OK) return st;
        for (size_t k = 0; k < count; k++) {
            const ze_tok *ct = &e->lx->t[first + 2u * k];
            char low[ZE_MAX_NAME + 1];
            ze_lower(low, e->lx->text + ct->off, ct->len);
            if (!strcmp(low, name)) return EERR(ZE_E_NAME, ct->off, "the component '%s' has the record's own name", low);
            if ((st = ze_decl_component(e->lx, e->p, comp0, ct, ty)) != ZXV_OK) return st;
        }
        if (!at_end(cur(e))) return ze_lex_error_at(e->lx, cur(e), "the end of the component");
        if (cur(e)->kind == ZT_EOF) return ze_lex_error_at(e->lx, cur(e), "'}'");
    }
    adv(e); /* } */
    return ze_decl_record(e->lx, e->p, name, comp0, at, out);
}

static int32_t eve(ep *e) {
    const ze_tok *t;
    int32_t st;
    while (cur(e)->kind == ZT_NL) adv(e);
    t = cur(e);
    if (!word(e, t, "eve")) return ze_lex_error_at(e->lx, t, "'eve' and the package name");
    adv(e);
    t = cur(e);
    if (t->kind != ZT_ID || ze_reserved(e->lx->text + t->off, t->len))
        return ze_lex_error_at(e->lx, t, "the package name");
    if (ze_predefined(e->lx->text + t->off, t->len))
        return EERR(ZE_E_NAME, t->off, "the package cannot have a name from package Standard");
    ze_lower(e->p->name, e->lx->text + t->off, t->len);
    adv(e);
    if ((st = end_line(e)) != ZXV_OK) return st;
    for (;;) {
        char name[ZE_MAX_NAME + 1];
        uint32_t out;
        uint64_t at;
        t = cur(e);
        if (t->kind == ZT_NL) {
            adv(e);
            continue;
        }
        if (t->kind == ZT_EOF) return ZXV_OK;
        at = t->off;
        if ((st = ze_check_new_name(e->lx, e->p, t, name)) != ZXV_OK) return st;
        adv(e);
        if (sym(cur(e), '{')) st = record_def(e, name, at, &out);
        else if (sym(cur(e), '=')) {
            adv(e);
            st = def(e, name, at, &out);
        } else st = ze_lex_error_at(e->lx, cur(e), "'=' or '{'");
        if (st != ZXV_OK) return st;
        if ((st = end_line(e)) != ZXV_OK) return st;
    }
}

int32_t ze_eve_parse(const char *text, size_t len, ze_pkg **out_pkg, ze_diag *diag) {
    ze_lexer lx;
    ze_pkg *p = NULL;
    ep e;
    int32_t st;
    if (diag) memset(diag, 0, sizeof *diag);
    if (!out_pkg || (!text && len)) return ZXV_E_ARG;
    *out_pkg = NULL;
    if (!text) text = "";
    if (len > ZE_MAX_TEXT) return ze_diag_set(diag, ZE_E_LIMIT, text, 0, 0, "the text is longer than 1 MiB");
    {
        size_t ok = ze_utf8_check((const uint8_t *) text, len);
        if (ok < len) return ze_diag_set(diag, ZE_E_SYNTAX, text, len, ok, "not valid UTF-8");
    }
    if ((st = ze_lex(&lx, text, len, ZE_LEX_EVE, diag)) != ZXV_OK) return st;
    if ((st = ze_pkg_new(&p)) != ZXV_OK) {
        ze_lex_free(&lx);
        return st;
    }
    e.lx = &lx;
    e.p = p;
    st = eve(&e);
    ze_lex_free(&lx);
    if (st != ZXV_OK) {
        ze_pkg_destroy(p);
        return st;
    }
    *out_pkg = p;
    return ZXV_OK;
}
