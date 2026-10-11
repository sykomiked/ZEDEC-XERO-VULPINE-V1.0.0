/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zc_parse.c — the contract language: lexer, recursive-descent parser,
 * canonical printer, digest and introspection. Integer code only (R11).
 *
 * The grammar (README.md, "Syntax"):
 *
 *   contract  = { fn_decl } EOF
 *   fn_decl   = "fn" ident "(" [ param { "," param } ] ")"
 *               [ "->" type ] [ "proposes" type [ "max" uint ] ] ";"
 *   param     = name ":" type
 *   name      = ident { "." ident }               (no spaces inside)
 *   type      = "int"   [ "[" ibound ".." ibound "]" ]
 *             | "rat"   [ "[" rbound ".." rbound "]" ] [ "den" ( "<=" | "|" ) uint ]
 *             | "truth" [ "{" truthname { "," truthname } "}" ]
 *             | "tag"   [ "{" tagpat { "," tagpat } "}" ]
 *             | "bytes" [ "[" uint ".." ( uint | "*" ) "]" ]
 *   ibound    = "*" | int          int  = [ "-" ] uint [ "^" uint ]
 *   rbound    = "*" | rat          rat  = int | [ "-" ] uint "." digits | [ "-" ] uint "/" uint
 *   tagpat    = part "/" part "/" part    (part: a name of that position, or "*")
 *   comment   = "#" ... end of line
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zc_internal.h"

enum { T_EOF, T_IDENT, T_NUM, T_PUNCT };

typedef struct {
    int kind;
    size_t off, len;
    uint32_t line, col;
} tok;

typedef struct {
    const char *s;
    size_t n, pos;
    uint32_t line;
    size_t line_start;
    tok t;         /* current token */
    int failed, oom;
    uint32_t fcap; /* allocated entries of c->fns */
    tok err_at;
    char err[256];
    zc_contract *c;
} P;

/* ----- errors ----- */

static void describe(const P *p, const tok *t, char *out, size_t cap) {
    if (t->kind == T_EOF) {
        snprintf(out, cap, "end of input");
    } else {
        size_t n = t->len > 24u ? 24u : t->len;
        snprintf(out, cap, "'%.*s%s'", (int) n, p->s + t->off, t->len > 24u ? "..." : "");
    }
}

static int fail_at(P *p, const tok *t, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

static int fail_at(P *p, const tok *t, const char *fmt, ...) {
    va_list ap;
    if (p->failed) return -1;
    p->failed = 1;
    p->err_at = *t;
    va_start(ap, fmt);
    vsnprintf(p->err, sizeof p->err, fmt, ap);
    va_end(ap);
    return -1;
}

static int expected(P *p, const char *what) {
    char d[40];
    describe(p, &p->t, d, sizeof d);
    return fail_at(p, &p->t, "expected %s, found %s", what, d);
}

/* ----- lexer ----- */

static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int lex(P *p) {
    const char *s = p->s;
    size_t n = p->n;
    for (;;) {
        if (p->pos >= n) break;
        char c = s[p->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            p->pos++;
        } else if (c == '\n') {
            p->pos++;
            p->line++;
            p->line_start = p->pos;
        } else if (c == '#') {
            while (p->pos < n && s[p->pos] != '\n') p->pos++;
        } else {
            break;
        }
    }
    p->t.off = p->pos;
    p->t.line = p->line;
    p->t.col = (uint32_t) (p->pos - p->line_start + 1u);
    if (p->pos >= n) {
        p->t.kind = T_EOF;
        p->t.len = 0;
        return 0;
    }
    char c = s[p->pos];
    if (is_alpha(c)) {
        size_t a = p->pos;
        while (p->pos < n && (is_alpha(s[p->pos]) || is_digit(s[p->pos]))) p->pos++;
        p->t.kind = T_IDENT;
        p->t.len = p->pos - a;
        return 0;
    }
    if (is_digit(c)) {
        size_t a = p->pos;
        while (p->pos < n && is_digit(s[p->pos])) p->pos++;
        p->t.kind = T_NUM;
        p->t.len = p->pos - a;
        return 0;
    }
    static const char *const two[] = {"..", "->", "<="};
    for (size_t i = 0; i < 3; i++) {
        if (p->pos + 1u < n && s[p->pos] == two[i][0] && s[p->pos + 1u] == two[i][1]) {
            p->t.kind = T_PUNCT;
            p->t.len = 2;
            p->pos += 2;
            return 0;
        }
    }
    if (strchr("()[]{},:;./^-*|<>=!", c) && c != '\0') {
        p->t.kind = T_PUNCT;
        p->t.len = 1;
        p->pos++;
        return 0;
    }
    p->t.kind = T_PUNCT;
    p->t.len = 1;
    if ((unsigned char) c >= 0x20u && (unsigned char) c < 0x7Fu)
        return fail_at(p, &p->t, "unexpected character '%c'", c);
    return fail_at(p, &p->t, "unexpected byte 0x%02X (only ASCII outside comments)",
                   (unsigned) (unsigned char) c);
}

static int is_p(const P *p, const char *punct) {
    size_t l = strlen(punct);
    return p->t.kind == T_PUNCT && p->t.len == l && memcmp(p->s + p->t.off, punct, l) == 0;
}

static int is_kw(const P *p, const char *kw) {
    size_t l = strlen(kw);
    return p->t.kind == T_IDENT && p->t.len == l && memcmp(p->s + p->t.off, kw, l) == 0;
}

/* The next token starts right where the current one ends (no space). */
static int adjacent(const P *p) {
    return p->pos < p->n && p->t.off + p->t.len == p->pos;
}

static int expect_p(P *p, const char *punct, const char *what) {
    if (!is_p(p, punct)) return expected(p, what);
    return lex(p);
}

/* ----- numbers ----- */

static int num_u64(P *p, uint64_t *out) {
    uint64_t v = 0;
    if (p->t.kind != T_NUM) return expected(p, "a number");
    for (size_t i = 0; i < p->t.len; i++) {
        uint64_t d = (uint64_t) (p->s[p->t.off + i] - '0');
        if (v > (UINT64_MAX - d) / 10u) return fail_at(p, &p->t, "number does not fit in 64 bits");
        v = v * 10u + d;
    }
    *out = v;
    return lex(p);
}

/* int = [ "-" ] uint [ "^" uint ]  ->  int64 in [-(2^63-1), 2^63-1] */
static int parse_int(P *p, int64_t *out) {
    int neg = 0;
    uint64_t v, e;
    tok start = p->t;
    if (is_p(p, "-")) {
        neg = 1;
        if (lex(p)) return -1;
    }
    if (num_u64(p, &v)) return -1;
    if (is_p(p, "^")) {
        uint64_t b = v;
        if (lex(p) || num_u64(p, &e)) return -1;
        if (e > 64u) return fail_at(p, &start, "power does not fit in 64 bits");
        v = 1;
        for (uint64_t k = 0; k < e; k++) {
            if (b != 0 && v > UINT64_MAX / b) return fail_at(p, &start, "power does not fit in 64 bits");
            v *= b;
        }
    }
    if (v > (uint64_t) INT64_MAX)
        return fail_at(p, &start, "number is outside the exact range (|n| <= 2^63 - 1)");
    *out = neg ? -(int64_t) v : (int64_t) v;
    return 0;
}

/* rat = int | [ "-" ] uint "." digits | [ "-" ] uint "/" uint, reduced. */
static int parse_rat(P *p, int64_t *on, int64_t *od) {
    int neg = 0;
    uint64_t v;
    int64_t n, d = 1;
    tok start = p->t;
    if (is_p(p, "-")) {
        neg = 1;
        if (lex(p)) return -1;
    }
    if (p->t.kind != T_NUM) return expected(p, "a number");
    tok numtok = p->t;
    int dot = adjacent(p) && p->s[p->pos] == '.' && p->pos + 1u < p->n &&
              is_digit(p->s[p->pos + 1u]);
    if (num_u64(p, &v)) return -1;
    if (dot) {
        /* decimal: the "." token, then the fraction digits */
        if (lex(p)) return -1;
        tok frac = p->t;
        if (frac.kind != T_NUM) return expected(p, "decimal digits");
        uint64_t den = 1;
        for (size_t i = 0; i < frac.len; i++) {
            uint64_t dg = (uint64_t) (p->s[frac.off + i] - '0');
            if (den > UINT64_MAX / 10u || v > (UINT64_MAX - dg) / 10u)
                return fail_at(p, &start, "decimal has too many digits for an exact int64 rational");
            v = v * 10u + dg;
            den *= 10u;
        }
        if (lex(p)) return -1;
        if (v > (uint64_t) INT64_MAX || den > (uint64_t) INT64_MAX)
            return fail_at(p, &start, "decimal has too many digits for an exact int64 rational");
        n = (int64_t) v;
        d = (int64_t) den;
    } else if (is_p(p, "^")) {
        uint64_t b = v, e;
        if (lex(p) || num_u64(p, &e)) return -1;
        if (e > 64u) return fail_at(p, &start, "power does not fit in 64 bits");
        v = 1;
        for (uint64_t k = 0; k < e; k++) {
            if (b != 0 && v > UINT64_MAX / b) return fail_at(p, &start, "power does not fit in 64 bits");
            v *= b;
        }
        if (v > (uint64_t) INT64_MAX)
            return fail_at(p, &start, "number is outside the exact range (|n| <= 2^63 - 1)");
        n = (int64_t) v;
    } else {
        if (v > (uint64_t) INT64_MAX)
            return fail_at(p, &numtok, "number is outside the exact range (|n| <= 2^63 - 1)");
        n = (int64_t) v;
        if (is_p(p, "/")) {
            uint64_t dv;
            if (lex(p)) return -1;
            tok dt = p->t;
            if (num_u64(p, &dv)) return -1;
            if (dv == 0) return fail_at(p, &dt, "denominator is zero");
            if (dv > (uint64_t) INT64_MAX)
                return fail_at(p, &dt, "denominator is outside the exact range");
            d = (int64_t) dv;
        }
    }
    if (neg) n = -n;
    if (zxv_rat_normalize(n, d, on, od) != ZXV_OK)
        return fail_at(p, &start, "rational is outside the exact range");
    return 0;
}

/* ----- names ----- */

static int ident_eq(const P *p, const tok *t, const char *s) {
    size_t l = strlen(s);
    return t->len == l && memcmp(p->s + t->off, s, l) == 0;
}

/* name = ident { "." ident } with no spaces; dotted only when allowed. */
static int parse_name(P *p, char out[ZC_MAX_NAME + 1], int dotted, const char *what) {
    size_t len = 0;
    tok start = p->t;
    if (p->t.kind != T_IDENT) return expected(p, what);
    for (;;) {
        if (len + p->t.len > ZC_MAX_NAME)
            return fail_at(p, &start, "name is longer than %u bytes", ZC_MAX_NAME);
        memcpy(out + len, p->s + p->t.off, p->t.len);
        len += p->t.len;
        int more = dotted && adjacent(p) && p->s[p->pos] == '.' && p->pos + 1u < p->n &&
                   is_alpha(p->s[p->pos + 1u]);
        if (lex(p)) return -1;
        if (!more) break;
        if (len + 1u > ZC_MAX_NAME)
            return fail_at(p, &start, "name is longer than %u bytes", ZC_MAX_NAME);
        out[len++] = '.';
        if (lex(p)) return -1; /* the "." */
        if (p->t.kind != T_IDENT) return expected(p, "a name after '.'");
    }
    out[len] = '\0';
    return 0;
}

/* ----- types ----- */

static int lookup4(const char *const names[4], const P *p, const tok *t) {
    for (int i = 0; i < 4; i++)
        if (ident_eq(p, t, names[i])) return i;
    return -1;
}

static int parse_type(P *p, zc_type *ty) {
    tok kt = p->t;
    memset(ty, 0, sizeof *ty);
    ty->lo_d = ty->hi_d = 1;
    if (kt.kind != T_IDENT) return expected(p, "a type (int, rat, truth, tag or bytes)");
    if (ident_eq(p, &kt, "int")) ty->kind = ZC_T_INT;
    else if (ident_eq(p, &kt, "rat")) ty->kind = ZC_T_RAT;
    else if (ident_eq(p, &kt, "truth")) ty->kind = ZC_T_TRUTH;
    else if (ident_eq(p, &kt, "tag")) ty->kind = ZC_T_TAG;
    else if (ident_eq(p, &kt, "bytes")) ty->kind = ZC_T_BYTES;
    else {
        char d[40];
        describe(p, &kt, d, sizeof d);
        return fail_at(p, &kt, "unknown type %s (expected int, rat, truth, tag or bytes)", d);
    }
    if (lex(p)) return -1;

    if (ty->kind == ZC_T_INT || ty->kind == ZC_T_RAT) {
        if (is_p(p, "[")) {
            tok open = p->t;
            ty->ranged = 1;
            if (lex(p)) return -1;
            if (is_p(p, "*")) {
                if (lex(p)) return -1;
            } else {
                ty->has_lo = 1;
                if (ty->kind == ZC_T_INT ? parse_int(p, &ty->lo_n) : parse_rat(p, &ty->lo_n, &ty->lo_d))
                    return -1;
            }
            if (expect_p(p, "..", "'..' in the range")) return -1;
            if (is_p(p, "*")) {
                if (lex(p)) return -1;
            } else {
                ty->has_hi = 1;
                if (ty->kind == ZC_T_INT ? parse_int(p, &ty->hi_n) : parse_rat(p, &ty->hi_n, &ty->hi_d))
                    return -1;
            }
            if (expect_p(p, "]", "']' to close the range")) return -1;
            if (ty->has_lo && ty->has_hi) {
                int32_t cmp = 0;
                if (zxv_rat_cmp(ty->lo_n, ty->lo_d, ty->hi_n, ty->hi_d, &cmp) != ZXV_OK || cmp > 0)
                    return fail_at(p, &open, "empty range: the lower bound is above the upper bound");
            }
        }
        if (ty->kind == ZC_T_RAT && is_kw(p, "den")) {
            uint64_t b;
            if (lex(p)) return -1;
            if (is_p(p, "<=")) ty->den_mode = ZC_DEN_LE;
            else if (is_p(p, "|")) ty->den_mode = ZC_DEN_DIVIDES;
            else return expected(p, "'<=' or '|' after den");
            if (lex(p)) return -1;
            tok bt = p->t;
            if (num_u64(p, &b)) return -1;
            if (b == 0 || b > (uint64_t) INT64_MAX)
                return fail_at(p, &bt, "denominator bound must be 1 .. 2^63 - 1");
            ty->den_bound = (int64_t) b;
        }
        return 0;
    }

    if (ty->kind == ZC_T_BYTES) {
        ty->blo = 0;
        if (is_p(p, "[")) {
            tok open = p->t;
            ty->ranged = 1;
            if (lex(p) || num_u64(p, &ty->blo)) return -1;
            ty->has_lo = 1;
            if (expect_p(p, "..", "'..' in the length range")) return -1;
            if (is_p(p, "*")) {
                if (lex(p)) return -1;
            } else {
                ty->has_hi = 1;
                if (num_u64(p, &ty->bhi)) return -1;
                if (ty->bhi < ty->blo)
                    return fail_at(p, &open, "empty range: the lower bound is above the upper bound");
            }
            if (expect_p(p, "]", "']' to close the length range")) return -1;
        }
        return 0;
    }

    /* truth / tag: an optional set */
    ty->allowed = ty->kind == ZC_T_TRUTH ? 0xFu : UINT64_MAX;
    if (!is_p(p, "{")) return 0;
    if (lex(p)) return -1;
    ty->allowed = 0;
    for (;;) {
        tok et = p->t;
        uint8_t e[3] = {0xFF, 0xFF, 0xFF};
        if (ty->nset >= ZC_MAX_SET) return fail_at(p, &et, "more than %u entries in a set", ZC_MAX_SET);
        if (ty->kind == ZC_T_TRUTH) {
            static const char *const tn[4] = {"neither", "true", "false", "both"}; /* ZXV_LP_* */
            int v = et.kind == T_IDENT ? lookup4(tn, p, &et) : -1;
            if (v < 0) return expected(p, "neither, true, false or both");
            e[0] = (uint8_t) v;
            if (lex(p)) return -1;
            if (ty->allowed & (UINT64_C(1) << v)) return fail_at(p, &et, "listed twice");
            ty->allowed |= UINT64_C(1) << v;
        } else {
            static const char *const what[3] = {"a base (false, neither, both, true) or '*'",
                                                "a polarity (entropic, neutral, forbidden, negentropic) or '*'",
                                                "a phase (potential, emerging, dissolving, manifest) or '*'"};
            const char *const *names[3] = {zc_base_names, zc_pol_names, zc_phase_names};
            for (int k = 0; k < 3; k++) {
                if (k && expect_p(p, "/", "'/' between the parts of a tag")) return -1;
                if (is_p(p, "*")) {
                    if (lex(p)) return -1;
                    continue;
                }
                int v = p->t.kind == T_IDENT ? lookup4(names[k], p, &p->t) : -1;
                if (v < 0) return expected(p, what[k]);
                e[k] = (uint8_t) v;
                if (lex(p)) return -1;
            }
            for (uint32_t i = 0; i < ty->nset; i++)
                if (memcmp(ty->set[i], e, 3) == 0) return fail_at(p, &et, "listed twice");
            for (uint8_t code = 0; code < ZXV_HEX_STATES; code++) {
                uint8_t cb = 0, cp = 0, cf = 0;
                if (zxv_hex_unpack(code, &cb, &cp, &cf) != ZXV_OK) return fail_at(p, &et, "zxv_hex_unpack failed");
                if ((e[0] == 0xFF || e[0] == cb) && (e[1] == 0xFF || e[1] == cp) &&
                    (e[2] == 0xFF || e[2] == cf))
                    ty->allowed |= UINT64_C(1) << code;
            }
        }
        memcpy(ty->set[ty->nset++], e, 3);
        if (is_p(p, "}")) break;
        if (expect_p(p, ",", "',' or '}' in the set")) return -1;
    }
    return lex(p);
}

/* ----- declarations ----- */

static int parse_fn(P *p) {
    zc_contract *c = p->c;
    zc_fn *f;
    tok nt;
    if (!is_kw(p, "fn")) return expected(p, "'fn'");
    if (c->nfns >= ZC_MAX_FNS) return fail_at(p, &p->t, "more than %u functions", ZC_MAX_FNS);
    if (lex(p)) return -1;
    if (c->nfns == p->fcap) {
        uint32_t nc = p->fcap ? p->fcap * 2u : 8u;
        zc_fn *nf = (zc_fn *) realloc(c->fns, (size_t) nc * sizeof *nf);
        if (!nf) {
            p->oom = 1;
            return -1;
        }
        c->fns = nf;
        p->fcap = nc;
    }
    f = &c->fns[c->nfns];
    memset(f, 0, sizeof *f);
    nt = p->t;
    if (parse_name(p, f->name, 0, "a function name")) return -1;
    for (uint32_t i = 0; i < c->nfns; i++)
        if (strcmp(c->fns[i].name, f->name) == 0)
            return fail_at(p, &nt, "function '%s' is declared twice", f->name);
    if (expect_p(p, "(", "'(' after the function name")) return -1;
    if (!is_p(p, ")")) {
        for (;;) {
            tok pt = p->t;
            if (f->nparams >= ZC_MAX_PARAMS)
                return fail_at(p, &pt, "more than %u parameters", ZC_MAX_PARAMS);
            zc_param *pa = &f->params[f->nparams];
            if (parse_name(p, pa->name, 1, "a parameter name")) return -1;
            for (uint32_t i = 0; i < f->nparams; i++)
                if (strcmp(f->params[i].name, pa->name) == 0)
                    return fail_at(p, &pt, "parameter '%s' is declared twice", pa->name);
            if (expect_p(p, ":", "':' after the parameter name")) return -1;
            if (parse_type(p, &pa->type)) return -1;
            f->nparams++;
            if (is_p(p, ")")) break;
            if (expect_p(p, ",", "',' or ')' after a parameter")) return -1;
        }
    }
    if (lex(p)) return -1; /* ")" */
    if (is_p(p, "->")) {
        if (lex(p) || parse_type(p, &f->result)) return -1;
        f->has_result = 1;
    }
    if (is_kw(p, "proposes")) {
        if (lex(p)) return -1;
        tok tt = p->t;
        if (parse_type(p, &f->proposes)) return -1;
        if (f->proposes.kind != ZC_T_INT && f->proposes.kind != ZC_T_RAT)
            return fail_at(p, &tt, "proposals are exact rationals: 'proposes' takes int or rat");
        f->has_proposes = 1;
        f->max_proposals = 1;
        if (is_kw(p, "max")) {
            uint64_t m;
            if (lex(p)) return -1;
            tok mt = p->t;
            if (num_u64(p, &m)) return -1;
            if (m < 1 || m > ZC_MAX_PROPOSALS)
                return fail_at(p, &mt, "max must be 1 .. %u", ZC_MAX_PROPOSALS);
            f->max_proposals = (uint32_t) m;
        }
    }
    if (!is_p(p, ";")) {
        if (!f->has_result && !f->has_proposes) return expected(p, "'->', 'proposes' or ';'");
        if (!f->has_proposes) return expected(p, "'proposes' or ';'");
        return expected(p, "';' at the end of the declaration");
    }
    if (lex(p)) return -1;
    c->nfns++;
    return 0;
}

int32_t zc_parse(const char *text, size_t len, zc_contract **out_contract, uint32_t *out_line,
                 uint32_t *out_col, uint64_t *out_offset, char *msg, size_t msg_cap,
                 size_t *out_msg_len) {
    P p;
    char full[320];
    if (!out_contract || (!text && len)) {
        zc_set_error("zc_parse: NULL argument");
        return ZXV_E_ARG;
    }
    memset(&p, 0, sizeof p);
    p.s = text ? text : "";
    p.n = len;
    p.line = 1;
    if (len > ZC_MAX_TEXT) {
        tok t = {T_EOF, ZC_MAX_TEXT, 0, 1, 1};
        fail_at(&p, &t, "contract text is larger than %u bytes", ZC_MAX_TEXT);
    } else if (zc_utf8_check((const uint8_t *) p.s, len) < len) {
        size_t bad = zc_utf8_check((const uint8_t *) p.s, len), ls = 0;
        uint32_t line = 1;
        for (size_t i = 0; i < bad; i++)
            if (p.s[i] == '\n') {
                line++;
                ls = i + 1u;
            }
        tok t = {T_EOF, bad, 0, line, (uint32_t) (bad - ls + 1u)};
        fail_at(&p, &t, "the contract text is not valid UTF-8");
    } else {
        p.c = (zc_contract *) calloc(1, sizeof *p.c);
        if (!p.c) {
            zc_set_error("zc_parse: out of memory");
            return ZXV_E_NOMEM;
        }
        if (lex(&p) == 0) {
            while (p.t.kind != T_EOF && !p.oom)
                if (parse_fn(&p)) break;
        }
        if (p.oom) {
            zc_contract_destroy(p.c);
            zc_set_error("zc_parse: out of memory");
            return ZXV_E_NOMEM;
        }
    }
    if (p.failed) {
        if (p.c) zc_contract_destroy(p.c);
        snprintf(full, sizeof full, "line %u, column %u: %s", (unsigned) p.err_at.line,
                 (unsigned) p.err_at.col, p.err);
        zc_set_error(full);
        if (out_line) *out_line = p.err_at.line;
        if (out_col) *out_col = p.err_at.col;
        if (out_offset) *out_offset = p.err_at.off;
        zc_out_msg(full, msg, msg_cap, out_msg_len);
        return ZC_E_SYNTAX;
    }
    if (out_line) *out_line = 0;
    if (out_col) *out_col = 0;
    if (out_offset) *out_offset = 0;
    zc_out_msg("", msg, msg_cap, out_msg_len);
    *out_contract = p.c;
    return ZXV_OK;
}

void zc_contract_destroy(zc_contract *contract) {
    if (!contract) return;
    free(contract->fns);
    free(contract);
}

/* ----- canonical text ----- */

void zc_type_format(zc_buf *b, const zc_type *t) {
    static const char *const tn[4] = {"neither", "true", "false", "both"};
    switch (t->kind) {
    case ZC_T_INT:
    case ZC_T_RAT:
        zc_buf_puts(b, t->kind == ZC_T_INT ? "int" : "rat");
        if (t->has_lo || t->has_hi) {
            zc_buf_puts(b, "[");
            if (t->has_lo) {
                if (t->kind == ZC_T_INT) zc_buf_i64(b, t->lo_n);
                else zc_buf_rat(b, t->lo_n, t->lo_d);
            } else {
                zc_buf_puts(b, "*");
            }
            zc_buf_puts(b, "..");
            if (t->has_hi) {
                if (t->kind == ZC_T_INT) zc_buf_i64(b, t->hi_n);
                else zc_buf_rat(b, t->hi_n, t->hi_d);
            } else {
                zc_buf_puts(b, "*");
            }
            zc_buf_puts(b, "]");
        }
        if (t->den_mode) {
            zc_buf_puts(b, t->den_mode == ZC_DEN_LE ? " den<=" : " den|");
            zc_buf_i64(b, t->den_bound);
        }
        break;
    case ZC_T_BYTES:
        zc_buf_puts(b, "bytes");
        if (t->blo || t->has_hi) {
            zc_buf_puts(b, "[");
            zc_buf_u64(b, t->blo);
            zc_buf_puts(b, "..");
            if (t->has_hi) zc_buf_u64(b, t->bhi);
            else zc_buf_puts(b, "*");
            zc_buf_puts(b, "]");
        }
        break;
    case ZC_T_TRUTH:
    case ZC_T_TAG:
        zc_buf_puts(b, t->kind == ZC_T_TRUTH ? "truth" : "tag");
        if (t->nset) {
            zc_buf_puts(b, "{");
            for (uint32_t i = 0; i < t->nset; i++) {
                if (i) zc_buf_puts(b, ", ");
                if (t->kind == ZC_T_TRUTH) {
                    zc_buf_puts(b, tn[t->set[i][0] & 3]);
                } else {
                    const char *const *names[3] = {zc_base_names, zc_pol_names, zc_phase_names};
                    for (int k = 0; k < 3; k++) {
                        if (k) zc_buf_puts(b, "/");
                        zc_buf_puts(b, t->set[i][k] == 0xFF ? "*" : names[k][t->set[i][k] & 3]);
                    }
                }
            }
            zc_buf_puts(b, "}");
        }
        break;
    default: zc_buf_puts(b, "?"); break;
    }
}

static int canonical(const zc_contract *c, zc_buf *b) {
    for (uint32_t i = 0; i < c->nfns; i++) {
        const zc_fn *f = &c->fns[i];
        zc_buf_puts(b, "fn ");
        zc_buf_puts(b, f->name);
        zc_buf_puts(b, "(");
        for (uint32_t k = 0; k < f->nparams; k++) {
            if (k) zc_buf_puts(b, ", ");
            zc_buf_puts(b, f->params[k].name);
            zc_buf_puts(b, ": ");
            zc_type_format(b, &f->params[k].type);
        }
        zc_buf_puts(b, ")");
        if (f->has_result) {
            zc_buf_puts(b, " -> ");
            zc_type_format(b, &f->result);
        }
        if (f->has_proposes) {
            zc_buf_puts(b, " proposes ");
            zc_type_format(b, &f->proposes);
            zc_buf_puts(b, " max ");
            zc_buf_u64(b, f->max_proposals);
        }
        zc_buf_puts(b, ";\n");
    }
    if (!b->b) zc_buf_puts(b, "");
    return b->oom ? -1 : 0;
}

int32_t zc_contract_format(const zc_contract *contract, char *buf, size_t cap, size_t *out_len) {
    zc_buf b = {NULL, 0, 0, 0};
    if (!contract || !out_len) return ZXV_E_ARG;
    if (canonical(contract, &b)) {
        free(b.b);
        return ZXV_E_NOMEM;
    }
    *out_len = b.n;
    if (!buf || cap <= b.n) {
        free(b.b);
        return ZXV_E_BUFFER;
    }
    memcpy(buf, b.b, b.n + 1u);
    free(b.b);
    return ZXV_OK;
}

int32_t zc_contract_digest(const zc_contract *contract, uint8_t out[32]) {
    zc_buf b = {NULL, 0, 0, 0};
    int32_t st;
    if (!contract || !out) return ZXV_E_ARG;
    if (canonical(contract, &b)) {
        free(b.b);
        return ZXV_E_NOMEM;
    }
    st = zxv_sha3_256((const uint8_t *) b.b, b.n, out);
    free(b.b);
    return st;
}

/* ----- introspection ----- */

const zc_fn *zc_find_fn(const zc_contract *c, const char *name) {
    if (!c || !name) return NULL;
    for (uint32_t i = 0; i < c->nfns; i++)
        if (strcmp(c->fns[i].name, name) == 0) return &c->fns[i];
    return NULL;
}

int32_t zc_fn_count(const zc_contract *contract, uint32_t *out_count) {
    if (!contract || !out_count) return ZXV_E_ARG;
    *out_count = contract->nfns;
    return ZXV_OK;
}

int32_t zc_fn_name(const zc_contract *contract, uint32_t index, const char **out_name) {
    if (!contract || !out_name) return ZXV_E_ARG;
    if (index >= contract->nfns) return ZXV_E_NOT_FOUND;
    *out_name = contract->fns[index].name;
    return ZXV_OK;
}

int32_t zc_fn_info(const zc_contract *contract, const char *fn, uint32_t *out_nparams,
                   int32_t *out_has_result, int32_t *out_has_proposes,
                   uint32_t *out_max_proposals) {
    const zc_fn *f;
    if (!contract || !fn) return ZXV_E_ARG;
    f = zc_find_fn(contract, fn);
    if (!f) {
        zc_set_errorf("the contract declares no function '%.64s'", fn);
        return ZXV_E_NOT_FOUND;
    }
    if (out_nparams) *out_nparams = f->nparams;
    if (out_has_result) *out_has_result = f->has_result;
    if (out_has_proposes) *out_has_proposes = f->has_proposes;
    if (out_max_proposals) *out_max_proposals = f->has_proposes ? f->max_proposals : 0u;
    return ZXV_OK;
}

int32_t zc_slot_type(const zc_contract *contract, const char *fn, int32_t part, uint32_t index,
                     int32_t *out_type, const char **out_name) {
    const zc_fn *f;
    const zc_type *t = NULL;
    const char *name = "";
    if (!contract || !fn || !out_type) return ZXV_E_ARG;
    f = zc_find_fn(contract, fn);
    if (!f) {
        zc_set_errorf("the contract declares no function '%.64s'", fn);
        return ZXV_E_NOT_FOUND;
    }
    if (part == ZC_PART_PARAMS && index < f->nparams) {
        t = &f->params[index].type;
        name = f->params[index].name;
    } else if (part == ZC_PART_RESULT && index == 0 && f->has_result) {
        t = &f->result;
    } else if (part == ZC_PART_PROPOSALS && index == 0 && f->has_proposes) {
        t = &f->proposes;
    } else if (part != ZC_PART_PARAMS && part != ZC_PART_RESULT && part != ZC_PART_PROPOSALS) {
        return ZXV_E_ARG;
    }
    if (!t) return ZXV_E_NOT_FOUND;
    *out_type = t->kind;
    if (out_name) *out_name = name;
    return ZXV_OK;
}

int32_t zc_slot_bounds(const zc_contract *contract, const char *fn, int32_t part, uint32_t index,
                       zc_bounds *out) {
    const zc_fn *f;
    const zc_type *t = NULL;
    if (!contract || !fn || !out) return ZXV_E_ARG;
    f = zc_find_fn(contract, fn);
    if (!f) {
        zc_set_errorf("the contract declares no function '%.64s'", fn);
        return ZXV_E_NOT_FOUND;
    }
    if (part == ZC_PART_PARAMS && index < f->nparams) t = &f->params[index].type;
    else if (part == ZC_PART_RESULT && index == 0 && f->has_result) t = &f->result;
    else if (part == ZC_PART_PROPOSALS && index == 0 && f->has_proposes) t = &f->proposes;
    else if (part != ZC_PART_PARAMS && part != ZC_PART_RESULT && part != ZC_PART_PROPOSALS)
        return ZXV_E_ARG;
    if (!t) return ZXV_E_NOT_FOUND;
    memset(out, 0, sizeof *out);
    out->type = t->kind;
    out->lo_den = out->hi_den = 1;
    switch (t->kind) {
    case ZC_T_INT:
    case ZC_T_RAT:
        out->has_lo = t->has_lo ? 1 : 0;
        out->has_hi = t->has_hi ? 1 : 0;
        if (t->has_lo) {
            out->lo_num = t->lo_n;
            out->lo_den = t->kind == ZC_T_INT ? 1 : t->lo_d;
        }
        if (t->has_hi) {
            out->hi_num = t->hi_n;
            out->hi_den = t->kind == ZC_T_INT ? 1 : t->hi_d;
        }
        out->den_mode = t->den_mode;
        out->den_bound = t->den_bound;
        break;
    case ZC_T_BYTES:
        out->has_lo = 1;
        out->lo_num = (int64_t) t->blo;
        out->has_hi = t->has_hi ? 1 : 0;
        if (t->has_hi) out->hi_num = (int64_t) t->bhi;
        break;
    case ZC_T_TRUTH:
        out->set_mask = t->allowed & 0xFu;
        break;
    case ZC_T_TAG:
        out->set_mask = t->allowed;
        break;
    default: break;
    }
    return ZXV_OK;
}
