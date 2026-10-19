/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_ada.c — the Ada front end: a subset of a package specification.
 *
 *   spec        = "package" name [ "with" aspect { "," aspect } ] "is"
 *                 { declaration } "end" [ name ] ";"
 *   aspect      = "Pure" | "Preelaborate"
 *   declaration = "type" name "is" type_def ";"
 *               | "subtype" name "is" use ";"
 *               | "pragma" ( "Pure" | "Preelaborate" ) [ "(" name ")" ] ";"
 *   type_def    = "range" expr ".." expr
 *               | "delta" literal "digits" expr [ "range" dec ".." dec ]
 *               | "(" name { "," name } ")"
 *               | "array" "(" index { "," index } ")" "of" use
 *               | "record" component { component } "end" "record"
 *   index       = type_name | expr ".." expr | ( "Integer" | "Natural"
 *                 | "Positive" ) "range" expr ".." expr
 *   component   = name { "," name } ":" use [ ":=" default ] ";"
 *   use         = type_name [ "range" bound ".." bound ]
 *               | "String" "(" "1" ".." expr ")"
 *
 * Everything else that is legal Ada is refused with ZE_E_UNSUPPORTED and
 * a reason; README.md lists the refusals. A default (":= ...") changes no
 * layout and is skipped. Integer code only (R11). */
#include <string.h>

#include "ze_internal.h"

typedef struct {
    ze_lexer *lx;
    ze_pkg *p;
} ap;

static const ze_tok *cur(ap *a) { return &a->lx->t[a->lx->i]; }
static const ze_tok *peek(ap *a, size_t k) {
    size_t j = a->lx->i + k;
    return &a->lx->t[j < a->lx->n ? j : a->lx->n - 1u];
}
static void adv(ap *a) {
    if (a->lx->t[a->lx->i].kind != ZT_EOF && a->lx->t[a->lx->i].kind != ZT_ERR) a->lx->i++;
}
static int sym(const ze_tok *t, int s) { return t->kind == ZT_SYM && t->sym == s; }
static int kw(ap *a, const ze_tok *t, const char *w) {
    size_t n = strlen(w);
    if (t->kind != ZT_ID || t->len != n) return 0;
    for (size_t k = 0; k < n; k++) {
        char c = a->lx->text[t->off + k];
        if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
        if (c != w[k]) return 0;
    }
    return 1;
}

#define AERR(st, off, ...) ZE_ERR(a->lx, (st), (off), __VA_ARGS__)

static int32_t expect_sym(ap *a, int s, const char *what) {
    if (!sym(cur(a), s)) return ze_lex_error_at(a->lx, cur(a), what);
    adv(a);
    return ZXV_OK;
}

static int32_t expect_kw(ap *a, const char *w, const char *what) {
    if (!kw(a, cur(a), w)) return ze_lex_error_at(a->lx, cur(a), what);
    adv(a);
    return ZXV_OK;
}

/* An integer expression that must fit [lo, hi]. */
static int32_t int_expr(ap *a, int64_t lo, int64_t hi, const char *what, int64_t *out) {
    size_t at = cur(a)->off;
    ze_i128 v;
    int32_t st = ze_expr(a->lx, &v);
    if (st != ZXV_OK) return st;
    if (v < (ze_i128) lo || v > (ze_i128) hi) return AERR(ZE_E_CONSTRAINT, at, "%s is out of range", what);
    *out = (int64_t) v;
    return ZXV_OK;
}

/* Why a name from package Standard cannot be used, or NULL if it can. */
static const char *standard_refusal(const char *low) {
    static const struct {
        const char *name, *why;
    } k[] = {
        {"integer", "the range of Integer is implementation-defined; declare type T is range L .. H"},
        {"natural", "the range of Natural is implementation-defined; declare type T is range 0 .. H"},
        {"positive", "the range of Positive is implementation-defined; declare type T is range 1 .. H"},
        {"long_integer", "the range of Long_Integer is implementation-defined; declare a range type"},
        {"long_long_integer", "the range of Long_Long_Integer is implementation-defined; declare a range type"},
        {"short_integer", "the range of Short_Integer is implementation-defined; declare a range type"},
        {"short_short_integer", "the range of Short_Short_Integer is implementation-defined; declare a range type"},
        {"float", "floating point is refused (R11): use a decimal type such as delta 0.01 digits 15"},
        {"long_float", "floating point is refused (R11): use a decimal type such as delta 0.01 digits 15"},
        {"short_float", "floating point is refused (R11): use a decimal type such as delta 0.01 digits 15"},
        {"long_long_float", "floating point is refused (R11): use a decimal type such as delta 0.01 digits 15"},
        {"duration", "Duration is a binary fixed-point type; use a decimal type"},
        {"character", "Character is not supported; use String (1 .. 1)"},
        {"wide_character", "Wide_Character is not supported; use String"},
        {"wide_wide_character", "Wide_Wide_Character is not supported; use String"},
        {"wide_string", "Wide_String is not supported; use String"},
        {"wide_wide_string", "Wide_Wide_String is not supported; use String"},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (strcmp(low, k[i].name) == 0) return k[i].why;
    return NULL;
}

/* The type a name denotes, or a refusal. */
static int32_t type_name(ap *a, const ze_tok *t, uint32_t *out) {
    char low[ZE_MAX_NAME + 1];
    const char *why;
    if (t->kind != ZT_ID) return ze_lex_error_at(a->lx, t, "a type name");
    ze_lower(low, a->lx->text + t->off, t->len);
    if (ze_reserved(low, t->len)) {
        if (!strcmp(low, "access") || !strcmp(low, "aliased") || !strcmp(low, "not"))
            return AERR(ZE_E_UNSUPPORTED, t->off, "access types and aliased components are not supported");
        return ze_lex_error_at(a->lx, t, "a type name");
    }
    if ((why = standard_refusal(low)) != NULL) return AERR(ZE_E_UNSUPPORTED, t->off, "%s", why);
    *out = ze_pkg_lookup(a->p, low);
    if (*out == UINT32_MAX) {
        if (ze_pkg_is_literal(a->p, low) || !strcmp(low, "true") || !strcmp(low, "false"))
            return AERR(ZE_E_NAME, t->off, "'%s' is an enumeration literal, not a type", low);
        return AERR(ZE_E_NAME, t->off, "unknown type '%s' (types are declared before they are used)", low);
    }
    return ZXV_OK;
}

/* "L .. H" for a scalar base: integers, decimals in units, or literals. */
static int32_t bounds(ap *a, uint32_t base, int64_t *lo, int64_t *hi) {
    const ze_titem *b = &a->p->types[base];
    int32_t st;
    for (int k = 0; k < 2; k++) {
        int64_t *v = k ? hi : lo;
        if (k && (st = expect_sym(a, ZS_DOTDOT, "'..'")) != ZXV_OK) return st;
        if (sym(cur(a), ZS_BOX)) return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "unconstrained ranges (<>) are not supported");
        if (b->t.kind == ZE_K_INT) {
            if ((st = int_expr(a, INT64_MIN, INT64_MAX, "the bound", v)) != ZXV_OK) return st;
        } else if (b->t.kind == ZE_K_DEC) {
            if ((st = ze_dec_bound(a->lx, b->t.scale, v)) != ZXV_OK) return st;
        } else if (b->t.kind == ZE_K_ENUM) {
            const ze_tok *t = cur(a);
            char low[ZE_MAX_NAME + 1];
            uint32_t pos;
            if (t->kind != ZT_ID) return ze_lex_error_at(a->lx, t, "an enumeration literal");
            ze_lower(low, a->lx->text + t->off, t->len);
            if (ze_enum_pos(a->p, base, low, &pos) != ZXV_OK)
                return AERR(ZE_E_NAME, t->off, "'%s' is not a literal of %s", low, b->t.name);
            *v = pos;
            adv(a);
        } else {
            return AERR(ZE_E_CONSTRAINT, cur(a)->off, "only a scalar type takes a range constraint");
        }
    }
    return ZXV_OK;
}

/* A subtype indication. `name` is the subtype being declared, or "" for
 * a use inside a declaration (an anonymous type is made only when the use
 * adds a constraint). */
static int32_t use(ap *a, const char *name, uint32_t *out) {
    const ze_tok *t = cur(a);
    uint64_t at = t->off;
    uint32_t base;
    int32_t st;
    if (kw(a, t, "string")) {
        int64_t lo, hi;
        adv(a);
        if ((st = expect_sym(a, '(', "'(' after String (String (1 .. n))")) != ZXV_OK) return st;
        if ((st = int_expr(a, INT32_MIN, INT32_MAX, "a String bound", &lo)) != ZXV_OK) return st;
        if ((st = expect_sym(a, ZS_DOTDOT, "'..'")) != ZXV_OK) return st;
        if ((st = int_expr(a, INT32_MIN, INT32_MAX, "a String bound", &hi)) != ZXV_OK) return st;
        if ((st = expect_sym(a, ')', "')'")) != ZXV_OK) return st;
        if (lo != 1) return AERR(ZE_E_UNSUPPORTED, at, "only String (1 .. n) is supported");
        return ze_decl_text(a->lx, a->p, name, hi, at, out);
    }
    if ((st = type_name(a, t, &base)) != ZXV_OK) return st;
    adv(a);
    t = cur(a);
    if (kw(a, t, "range")) {
        int64_t lo, hi;
        adv(a);
        if ((st = bounds(a, base, &lo, &hi)) != ZXV_OK) return st;
        return ze_decl_subtype(a->lx, a->p, name, base, 1, lo, hi, at, out);
    }
    if (sym(t, '(')) return AERR(ZE_E_UNSUPPORTED, t->off, "index and discriminant constraints are not supported");
    if (kw(a, t, "digits") || kw(a, t, "delta"))
        return AERR(ZE_E_UNSUPPORTED, t->off, "digits and delta constraints are not supported");
    if (name[0]) return ze_decl_subtype(a->lx, a->p, name, base, 0, 0, 0, at, out);
    *out = base;
    return ZXV_OK;
}

static int32_t index_def(ap *a, ze_dim *d) {
    const ze_tok *t = cur(a);
    int32_t st;
    if (t->kind == ZT_ID) {
        char low[ZE_MAX_NAME + 1];
        uint32_t ix;
        ze_lower(low, a->lx->text + t->off, t->len);
        if (!strcmp(low, "integer") || !strcmp(low, "natural") || !strcmp(low, "positive")) {
            int64_t min = !strcmp(low, "integer") ? INT32_MIN : !strcmp(low, "natural") ? 0 : 1;
            adv(a);
            if (!kw(a, cur(a), "range"))
                return AERR(ZE_E_UNSUPPORTED, t->off,
                            "%s as a whole index is implementation-defined; write %s range L .. H", low, low);
            adv(a);
            if (sym(cur(a), ZS_BOX)) return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "unconstrained arrays are not supported");
            if ((st = int_expr(a, min, INT32_MAX, "an Integer index bound", &d->lo)) != ZXV_OK) return st;
            if ((st = expect_sym(a, ZS_DOTDOT, "'..'")) != ZXV_OK) return st;
            if ((st = int_expr(a, min, INT32_MAX, "an Integer index bound", &d->hi)) != ZXV_OK) return st;
            d->index = UINT32_MAX;
            return ZXV_OK;
        }
        if (sym(peek(a, 1), ZS_DOTDOT))
            return AERR(ZE_E_UNSUPPORTED, t->off, "a range of names as an index; declare a subtype and index by it");
        if ((st = type_name(a, t, &ix)) != ZXV_OK) return st;
        if (a->p->types[ix].t.kind != ZE_K_INT && a->p->types[ix].t.kind != ZE_K_ENUM)
            return AERR(ZE_E_CONSTRAINT, t->off, "an index is an integer or enumeration type");
        adv(a);
        if (kw(a, cur(a), "range")) {
            if (sym(peek(a, 1), ZS_BOX))
                return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "unconstrained arrays are not supported");
            return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "a range on an index type; declare a subtype and index by it");
        }
        d->index = ix;
        d->lo = a->p->types[ix].t.lo;
        d->hi = a->p->types[ix].t.hi;
        return ZXV_OK;
    }
    if ((st = int_expr(a, INT32_MIN, INT32_MAX, "an Integer index bound", &d->lo)) != ZXV_OK) return st;
    if ((st = expect_sym(a, ZS_DOTDOT, "'..'")) != ZXV_OK) return st;
    if ((st = int_expr(a, INT32_MIN, INT32_MAX, "an Integer index bound", &d->hi)) != ZXV_OK) return st;
    d->index = UINT32_MAX;
    return ZXV_OK;
}

/* Skip a default expression up to the ';' that ends the component. */
static int32_t skip_default(ap *a) {
    int depth = 0;
    for (;;) {
        const ze_tok *t = cur(a);
        if (t->kind == ZT_EOF || t->kind == ZT_ERR) return ze_lex_error_at(a->lx, t, "';' after the default");
        if (sym(t, '(')) depth++;
        if (sym(t, ')') && depth-- == 0) return ze_lex_error_at(a->lx, t, "';' after the default");
        if (sym(t, ';') && depth == 0) return ZXV_OK;
        adv(a);
    }
}

static int32_t no_aspects(ap *a) {
    if (kw(a, cur(a), "with"))
        return AERR(ZE_E_UNSUPPORTED, cur(a)->off,
                    "aspect specifications are not supported (Size, Pack and the like change the representation)");
    return ZXV_OK;
}

static int32_t record_def(ap *a, const char *name, uint64_t at, uint32_t *out) {
    uint32_t comp0 = a->p->ncomps;
    int32_t st;
    for (;;) {
        const ze_tok *t = cur(a);
        size_t first = a->lx->i, count = 0;
        uint32_t ty;
        if (kw(a, t, "end")) break;
        if (kw(a, t, "null")) return AERR(ZE_E_UNSUPPORTED, t->off, "null components are not supported");
        if (kw(a, t, "case")) return AERR(ZE_E_UNSUPPORTED, t->off, "variant parts are not supported");
        for (;;) {
            t = cur(a);
            if (t->kind != ZT_ID) return ze_lex_error_at(a->lx, t, "a component name");
            if (ze_reserved(a->lx->text + t->off, t->len)) return ze_lex_error_at(a->lx, t, "a component name");
            count++;
            adv(a);
            if (!sym(cur(a), ',')) break;
            adv(a);
        }
        if ((st = expect_sym(a, ':', "':'")) != ZXV_OK) return st;
        if (kw(a, cur(a), "aliased") || kw(a, cur(a), "access"))
            return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "access types and aliased components are not supported");
        if ((st = use(a, "", &ty)) != ZXV_OK) return st;
        for (size_t k = 0; k < count; k++) {
            const ze_tok *ct = &a->lx->t[first + 2u * k];
            char low[ZE_MAX_NAME + 1];
            ze_lower(low, a->lx->text + ct->off, ct->len);
            if (!strcmp(low, name)) return AERR(ZE_E_NAME, ct->off, "the component '%s' has the record's own name", low);
            if ((st = ze_decl_component(a->lx, a->p, comp0, ct, ty)) != ZXV_OK) return st;
        }
        if (sym(cur(a), ZS_ASSIGN)) {
            adv(a);
            if ((st = skip_default(a)) != ZXV_OK) return st;
        }
        if ((st = no_aspects(a)) != ZXV_OK) return st;
        if ((st = expect_sym(a, ';', "';'")) != ZXV_OK) return st;
    }
    adv(a); /* end */
    if ((st = expect_kw(a, "record", "'record' after 'end'")) != ZXV_OK) return st;
    return ze_decl_record(a->lx, a->p, name, comp0, at, out);
}

static int32_t type_decl(ap *a) {
    const ze_tok *nt, *t;
    char name[ZE_MAX_NAME + 1];
    uint64_t at;
    uint32_t out;
    int32_t st;
    adv(a); /* type */
    nt = cur(a);
    at = nt->off;
    if ((st = ze_check_new_name(a->lx, a->p, nt, name)) != ZXV_OK) return st;
    adv(a);
    if (sym(cur(a), '(')) return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "discriminants are not supported");
    if (sym(cur(a), ';')) return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "incomplete type declarations are not supported");
    if ((st = expect_kw(a, "is", "'is'")) != ZXV_OK) return st;
    t = cur(a);
    if (kw(a, t, "range")) {
        int64_t lo, hi;
        adv(a);
        if ((st = int_expr(a, INT64_MIN, INT64_MAX, "the bound (int64)", &lo)) != ZXV_OK) return st;
        if ((st = expect_sym(a, ZS_DOTDOT, "'..'")) != ZXV_OK) return st;
        if ((st = int_expr(a, INT64_MIN, INT64_MAX, "the bound (int64)", &hi)) != ZXV_OK) return st;
        st = ze_decl_int(a->lx, a->p, name, lo, hi, at, &out);
    } else if (kw(a, t, "delta")) {
        const ze_tok *dt;
        int64_t num, digits, lo = 0, hi = 0;
        uint32_t scale = 0;
        int has_range = 0;
        adv(a);
        dt = cur(a);
        if (dt->kind == ZT_REAL) {
            if ((st = ze_real_exact(a->lx, dt, &num, &scale)) != ZXV_OK) return st;
        } else {
            return ze_lex_error_at(a->lx, dt, "a real literal delta such as 0.01 or 1.0");
        }
        if (num != 1) {
            if (scale == 0 && num > 1)
                return AERR(ZE_E_UNSUPPORTED, dt->off, "a delta above 1.0 is not supported");
            return AERR(ZE_E_CONSTRAINT, dt->off, "the delta of a decimal type is a power of ten (0.01, 1.0E-4)");
        }
        adv(a);
        if (sym(cur(a), ZS_POW))
            return AERR(ZE_E_UNSUPPORTED, dt->off, "write the delta as a literal (0.01), not an expression");
        if (!kw(a, cur(a), "digits"))
            return AERR(ZE_E_UNSUPPORTED, dt->off,
                        "ordinary fixed point (delta without digits) has a binary small; use delta ... digits ...");
        adv(a);
        if ((st = int_expr(a, 1, ZE_MAX_DIGITS, "digits (1 .. 18)", &digits)) != ZXV_OK) return st;
        if (kw(a, cur(a), "range")) {
            adv(a);
            has_range = 1;
            if ((st = ze_dec_bound(a->lx, scale, &lo)) != ZXV_OK) return st;
            if ((st = expect_sym(a, ZS_DOTDOT, "'..'")) != ZXV_OK) return st;
            if ((st = ze_dec_bound(a->lx, scale, &hi)) != ZXV_OK) return st;
        }
        st = ze_decl_dec(a->lx, a->p, name, (uint32_t) digits, scale, has_range, lo, hi, at, &out);
    } else if (sym(t, '(')) {
        uint32_t lit0 = a->p->nlits;
        adv(a);
        for (;;) {
            const ze_tok *lt = cur(a);
            if (lt->kind == ZT_ID && lt->len == strlen(name)) {
                char low[ZE_MAX_NAME + 1];
                ze_lower(low, a->lx->text + lt->off, lt->len);
                if (!strcmp(low, name)) return AERR(ZE_E_NAME, lt->off, "the literal '%s' is the type's own name", low);
            }
            if ((st = ze_decl_literal(a->lx, a->p, lit0, lt)) != ZXV_OK) return st;
            adv(a);
            if (!sym(cur(a), ',')) break;
            adv(a);
        }
        if ((st = expect_sym(a, ')', "',' or ')'")) != ZXV_OK) return st;
        st = ze_decl_enum(a->lx, a->p, name, lit0, at, &out);
    } else if (kw(a, t, "array")) {
        ze_dim dims[ZE_MAX_DIMS];
        uint32_t nd = 0, elem;
        adv(a);
        if ((st = expect_sym(a, '(', "'('")) != ZXV_OK) return st;
        for (;;) {
            if (nd == ZE_MAX_DIMS) return AERR(ZE_E_LIMIT, cur(a)->off, "an array has at most 4 dimensions");
            if ((st = index_def(a, &dims[nd])) != ZXV_OK) return st;
            nd++;
            if (!sym(cur(a), ',')) break;
            adv(a);
        }
        if ((st = expect_sym(a, ')', "',' or ')'")) != ZXV_OK) return st;
        if ((st = expect_kw(a, "of", "'of'")) != ZXV_OK) return st;
        if (kw(a, cur(a), "aliased") || kw(a, cur(a), "access"))
            return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "access types and aliased components are not supported");
        if ((st = use(a, "", &elem)) != ZXV_OK) return st;
        st = ze_decl_array(a->lx, a->p, name, dims, nd, elem, at, &out);
    } else if (kw(a, t, "record")) {
        adv(a);
        st = record_def(a, name, at, &out);
    } else if (kw(a, t, "null") && kw(a, peek(a, 1), "record")) {
        return AERR(ZE_E_UNSUPPORTED, t->off, "a null record has no data; a record needs at least one component");
    } else if (kw(a, t, "mod")) {
        return AERR(ZE_E_UNSUPPORTED, t->off, "modular types wrap around; declare type T is range 0 .. H");
    } else if (kw(a, t, "digits")) {
        return AERR(ZE_E_UNSUPPORTED, t->off, "floating point is refused (R11): use delta ... digits ...");
    } else if (kw(a, t, "new")) {
        return AERR(ZE_E_UNSUPPORTED, t->off, "derived types are not supported; declare a subtype");
    } else if (kw(a, t, "access") || kw(a, t, "not")) {
        return AERR(ZE_E_UNSUPPORTED, t->off, "access types are not supported");
    } else if (kw(a, t, "tagged") || kw(a, t, "abstract") || kw(a, t, "interface") || kw(a, t, "limited") ||
               kw(a, t, "private") || kw(a, t, "synchronized") || kw(a, t, "task") || kw(a, t, "protected")) {
        return AERR(ZE_E_UNSUPPORTED, t->off, "tagged, limited, private, task and protected types are not supported");
    } else {
        return ze_lex_error_at(a->lx, t, "a type definition");
    }
    if (st != ZXV_OK) return st;
    if ((st = no_aspects(a)) != ZXV_OK) return st;
    return expect_sym(a, ';', "';'");
}

static int32_t subtype_decl(ap *a) {
    const ze_tok *nt;
    char name[ZE_MAX_NAME + 1];
    uint32_t out;
    int32_t st;
    adv(a); /* subtype */
    nt = cur(a);
    if ((st = ze_check_new_name(a->lx, a->p, nt, name)) != ZXV_OK) return st;
    adv(a);
    if ((st = expect_kw(a, "is", "'is'")) != ZXV_OK) return st;
    if (kw(a, cur(a), "not") || kw(a, cur(a), "access"))
        return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "access types are not supported");
    if ((st = use(a, name, &out)) != ZXV_OK) return st;
    if ((st = no_aspects(a)) != ZXV_OK) return st;
    return expect_sym(a, ';', "';'");
}

static int32_t pure_pragma(ap *a) {
    const ze_tok *t;
    int32_t st;
    adv(a); /* pragma */
    t = cur(a);
    if (!kw(a, t, "pure") && !kw(a, t, "preelaborate")) {
        if (t->kind == ZT_ID) return AERR(ZE_E_UNSUPPORTED, t->off, "only pragma Pure and pragma Preelaborate are supported");
        return ze_lex_error_at(a->lx, t, "a pragma name");
    }
    adv(a);
    if (sym(cur(a), '(')) {
        adv(a);
        if (cur(a)->kind != ZT_ID) return ze_lex_error_at(a->lx, cur(a), "the package name");
        adv(a);
        if ((st = expect_sym(a, ')', "')'")) != ZXV_OK) return st;
    }
    return expect_sym(a, ';', "';'");
}

static int32_t spec(ap *a) {
    const ze_tok *t = cur(a);
    int32_t st;
    if (kw(a, t, "with") || kw(a, t, "use") || kw(a, t, "limited") || kw(a, t, "private"))
        return AERR(ZE_E_UNSUPPORTED, t->off, "context clauses are not supported: the package stands alone");
    if (kw(a, t, "pragma")) return AERR(ZE_E_UNSUPPORTED, t->off, "configuration pragmas are not supported");
    if (kw(a, t, "generic")) return AERR(ZE_E_UNSUPPORTED, t->off, "generic packages are not supported");
    if ((st = expect_kw(a, "package", "'package'")) != ZXV_OK) return st;
    t = cur(a);
    if (kw(a, t, "body")) return AERR(ZE_E_UNSUPPORTED, t->off, "a package body; give the specification");
    if (t->kind != ZT_ID || ze_reserved(a->lx->text + t->off, t->len)) return ze_lex_error_at(a->lx, t, "the package name");
    if (ze_predefined(a->lx->text + t->off, t->len))
        return AERR(ZE_E_NAME, t->off, "the package cannot have a name from package Standard");
    ze_lower(a->p->name, a->lx->text + t->off, t->len);
    adv(a);
    if (sym(cur(a), '.')) return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "child packages are not supported");
    if (kw(a, cur(a), "with")) {
        adv(a);
        for (;;) {
            t = cur(a);
            if (!kw(a, t, "pure") && !kw(a, t, "preelaborate")) {
                if (t->kind == ZT_ID)
                    return AERR(ZE_E_UNSUPPORTED, t->off, "only the aspects Pure and Preelaborate are supported");
                return ze_lex_error_at(a->lx, t, "an aspect");
            }
            adv(a);
            if (!sym(cur(a), ',')) break;
            adv(a);
        }
    }
    if ((st = expect_kw(a, "is", "'is'")) != ZXV_OK) return st;
    if (kw(a, cur(a), "new")) return AERR(ZE_E_UNSUPPORTED, cur(a)->off, "generic instantiations are not supported");
    for (;;) {
        t = cur(a);
        if (kw(a, t, "end")) break;
        if (kw(a, t, "type")) st = type_decl(a);
        else if (kw(a, t, "subtype")) st = subtype_decl(a);
        else if (kw(a, t, "pragma")) st = pure_pragma(a);
        else if (kw(a, t, "private")) st = AERR(ZE_E_UNSUPPORTED, t->off, "a private part is not supported");
        else if (kw(a, t, "for")) st = AERR(ZE_E_UNSUPPORTED, t->off,
                                            "representation clauses are not supported: the canonical encoding fixes the layout");
        else if (kw(a, t, "function") || kw(a, t, "procedure") || kw(a, t, "overriding"))
            st = AERR(ZE_E_UNSUPPORTED, t->off, "subprograms are not supported: only data types");
        else if (kw(a, t, "package") || kw(a, t, "generic") || kw(a, t, "task") || kw(a, t, "protected") ||
                 kw(a, t, "use"))
            st = AERR(ZE_E_UNSUPPORTED, t->off, "only type and subtype declarations are supported");
        else if (t->kind == ZT_ID && !ze_reserved(a->lx->text + t->off, t->len))
            st = AERR(ZE_E_UNSUPPORTED, t->off, "object and number declarations are not supported: only types");
        else st = ze_lex_error_at(a->lx, t, "a declaration or 'end'");
        if (st != ZXV_OK) return st;
    }
    adv(a); /* end */
    t = cur(a);
    if (t->kind == ZT_ID) {
        char low[ZE_MAX_NAME + 1];
        ze_lower(low, a->lx->text + t->off, t->len);
        if (strcmp(low, a->p->name) != 0)
            return AERR(ZE_E_NAME, t->off, "'end %s' does not match 'package %s'", low, a->p->name);
        adv(a);
    }
    if ((st = expect_sym(a, ';', "';'")) != ZXV_OK) return st;
    if (cur(a)->kind != ZT_EOF) return ze_lex_error_at(a->lx, cur(a), "the end of the text after the package");
    return ZXV_OK;
}

int32_t ze_ada_parse(const char *text, size_t len, ze_pkg **out_pkg, ze_diag *diag) {
    ze_lexer lx;
    ze_pkg *p = NULL;
    ap a;
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
    if ((st = ze_lex(&lx, text, len, ZE_LEX_ADA, diag)) != ZXV_OK) return st;
    if ((st = ze_pkg_new(&p)) != ZXV_OK) {
        ze_lex_free(&lx);
        return st;
    }
    a.lx = &lx;
    a.p = p;
    st = spec(&a);
    ze_lex_free(&lx);
    if (st != ZXV_OK) {
        ze_pkg_destroy(p);
        return st;
    }
    *out_pkg = p;
    return ZXV_OK;
}
