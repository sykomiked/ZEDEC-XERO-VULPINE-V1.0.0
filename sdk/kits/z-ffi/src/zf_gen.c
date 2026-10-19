/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zf_gen.c — the spec parser and the wrapper generator.
 *
 *   spec    := item*
 *   item    := "library" NAME ";"
 *            | "include" ( "<" PATH ">" | '"' PATH '"' ) ";"
 *            | "fn" NAME "(" [ param { "," param } ] ")" "->" result
 *              [ "needs" ( "none" | CAP { "|" CAP } ) ] ";"
 *   param   := NAME ":" ( INT [ range ] | FLOAT "from" "rat" | "cstr" [ size ] )
 *   result  := "void" | INT [ range ] | FLOAT "as" "rat"
 *   range   := "[" bound ".." bound "]"      bound := [ "-" ] ( DIGITS | MACRO )
 *   size    := "[" DIGITS ".." DIGITS "]"
 *   INT     := schar uchar short ushort int uint long ulong llong ullong size
 *              i8 i16 i32 i64 u8 u16 u32 u64
 *   FLOAT   := f32 | f64
 *   CAP     := LEDGER_READ LEDGER_POST CRYPTO_SIGN CRYPTO_KEM AI_INFERENCE
 *              BUDGET PHASE ATTEST SWARM EVM          (the ZXV_CAP_* bits)
 *   '#' starts a comment to the end of the line.
 *
 * A MACRO bound (INT_MAX, SIZE_MAX, ...) is copied into the generated C;
 * the generated code checks with _Static_assert that every range fits its
 * C type and has lo <= hi, and that every declared signature is the one
 * the library's header declares. Integer code only (R11): this file writes
 * C text that mentions float types, and holds none. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zf_internal.h"

/* ===== Types ===== */

enum { K_INT = 1, K_FLOAT, K_CSTR, K_VOID };

typedef struct {
    const char *kw, *ctext, *tmin, *tmax;
    int is_signed, float_bits;
} ctype;

static const ctype k_types[] = {
    {"schar", "signed char", "SCHAR_MIN", "SCHAR_MAX", 1, 0},
    {"uchar", "unsigned char", "0", "UCHAR_MAX", 0, 0},
    {"short", "short", "SHRT_MIN", "SHRT_MAX", 1, 0},
    {"ushort", "unsigned short", "0", "USHRT_MAX", 0, 0},
    {"int", "int", "INT_MIN", "INT_MAX", 1, 0},
    {"uint", "unsigned int", "0", "UINT_MAX", 0, 0},
    {"long", "long", "LONG_MIN", "LONG_MAX", 1, 0},
    {"ulong", "unsigned long", "0", "ULONG_MAX", 0, 0},
    {"llong", "long long", "LLONG_MIN", "LLONG_MAX", 1, 0},
    {"ullong", "unsigned long long", "0", "ULLONG_MAX", 0, 0},
    {"size", "size_t", "0", "SIZE_MAX", 0, 0},
    {"i8", "int8_t", "INT8_MIN", "INT8_MAX", 1, 0},
    {"i16", "int16_t", "INT16_MIN", "INT16_MAX", 1, 0},
    {"i32", "int32_t", "INT32_MIN", "INT32_MAX", 1, 0},
    {"i64", "int64_t", "INT64_MIN", "INT64_MAX", 1, 0},
    {"u8", "uint8_t", "0", "UINT8_MAX", 0, 0},
    {"u16", "uint16_t", "0", "UINT16_MAX", 0, 0},
    {"u32", "uint32_t", "0", "UINT32_MAX", 0, 0},
    {"u64", "uint64_t", "0", "UINT64_MAX", 0, 0},
    {"f32", "float", "", "", 0, 32},
    {"f64", "double", "", "", 0, 64},
};

static const char *const k_caps[] = {"LEDGER_READ", "LEDGER_POST", "CRYPTO_SIGN", "CRYPTO_KEM",
                                     "AI_INFERENCE", "BUDGET",     "PHASE",       "ATTEST",
                                     "SWARM",        "EVM"};

static const char *const k_keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else",
    "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register",
    "restrict", "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
    "union", "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool",
    "_Complex", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
    "bool", "true", "false", "NULL", "main"};

#define BOUND_MAX 96

typedef struct {
    int kind;
    const ctype *ct;
    char lo[BOUND_MAX], hi[BOUND_MAX]; /* C expressions; "" = the type's own */
    uint64_t slo, shi;                 /* cstr length bounds */
    char name[ZF_MAX_NAME + 1];
} slot;

typedef struct {
    char name[ZF_MAX_NAME + 1];
    slot p[ZF_MAX_PARAMS];
    uint32_t np;
    slot r;
    char needs[160]; /* C expression */
    char needs_text[160];
} fndecl;

typedef struct {
    const char *s;
    size_t n, pos;
    uint32_t line;
    size_t line_start;
    /* error */
    int failed;
    uint32_t eline, ecol;
    uint64_t eoff;
    char emsg[400];
    /* model */
    char lib[32];
    char inc[ZF_MAX_INCLUDES][130];
    uint32_t ninc;
    fndecl *fns;
    uint32_t nfns;
} P;

#define DEFAULT_CSTR_MAX 65536u

/* ===== Errors and lexing ===== */

ZF_PRINTF(3, 4) static void fail_at(P *p, size_t off, const char *fmt, ...) {
    va_list ap;
    uint32_t line = 1;
    size_t ls = 0;
    if (p->failed) return;
    for (size_t i = 0; i < off && i < p->n; i++)
        if (p->s[i] == '\n') {
            line++;
            ls = i + 1u;
        }
    p->failed = 1;
    p->eline = line;
    p->ecol = (uint32_t) (off - ls + 1u);
    p->eoff = off;
    va_start(ap, fmt);
    vsnprintf(p->emsg, sizeof p->emsg, fmt, ap);
    va_end(ap);
}

static void skip_ws(P *p) {
    while (p->pos < p->n) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            p->pos++;
        } else if (c == '#') {
            while (p->pos < p->n && p->s[p->pos] != '\n') p->pos++;
        } else {
            break;
        }
    }
}

static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }

/* A printable description of the next token for messages. */
static void describe(P *p, char *out, size_t cap) {
    size_t i = p->pos, k = 0;
    if (i >= p->n) {
        snprintf(out, cap, "end of input");
        return;
    }
    unsigned char c = (unsigned char) p->s[i];
    if (is_alpha((char) c) || is_digit((char) c)) {
        while (i < p->n && (is_alpha(p->s[i]) || is_digit(p->s[i])) && k < 40u) {
            i++;
            k++;
        }
        snprintf(out, cap, "'%.*s'", (int) k, p->s + p->pos);
    } else if (c >= 0x21 && c < 0x7f) {
        snprintf(out, cap, "'%c'", c);
    } else {
        snprintf(out, cap, "byte 0x%02x", c);
    }
}

/* Read an identifier at the cursor (after whitespace). Returns its length
 * (0 if none) and copies it into buf (cap must exceed ZF_MAX_NAME). */
static size_t ident(P *p, char *buf, size_t *at) {
    size_t a, len;
    skip_ws(p);
    a = p->pos;
    if (at) *at = a;
    if (a >= p->n || !is_alpha(p->s[a])) return 0;
    while (p->pos < p->n && (is_alpha(p->s[p->pos]) || is_digit(p->s[p->pos]))) p->pos++;
    len = p->pos - a;
    if (len > ZF_MAX_NAME) {
        fail_at(p, a, "a name is longer than %u bytes", ZF_MAX_NAME);
        return 0;
    }
    memcpy(buf, p->s + a, len);
    buf[len] = '\0';
    return len;
}

static int peek_char(P *p, char c) {
    skip_ws(p);
    return p->pos < p->n && p->s[p->pos] == c;
}

static int expect(P *p, const char *tok, const char *what) {
    size_t l = strlen(tok);
    skip_ws(p);
    if (p->n - p->pos >= l && memcmp(p->s + p->pos, tok, l) == 0) {
        p->pos += l;
        return 1;
    }
    char d[64];
    describe(p, d, sizeof d);
    fail_at(p, p->pos, "expected %s, found %s", what, d);
    return 0;
}

static int keyword_at(P *p, const char *kw) {
    size_t l = strlen(kw);
    skip_ws(p);
    if (p->n - p->pos < l || memcmp(p->s + p->pos, kw, l) != 0) return 0;
    if (p->pos + l < p->n && (is_alpha(p->s[p->pos + l]) || is_digit(p->s[p->pos + l]))) return 0;
    p->pos += l;
    return 1;
}

static int memmem_dotdot(const char *s, size_t n) {
    for (size_t i = 0; i + 1u < n; i++)
        if (s[i] == '.' && s[i + 1u] == '.') return 1;
    return 0;
}

static int digits(P *p, uint64_t *v, size_t *at) {
    uint64_t x = 0;
    skip_ws(p);
    *at = p->pos;
    if (p->pos >= p->n || !is_digit(p->s[p->pos])) return 0;
    if (p->s[p->pos] == '0' && p->pos + 1u < p->n && is_digit(p->s[p->pos + 1u])) {
        fail_at(p, p->pos, "a number has a leading zero");
        return 0;
    }
    while (p->pos < p->n && is_digit(p->s[p->pos])) {
        unsigned d = (unsigned) (p->s[p->pos] - '0');
        if (x > (UINT64_MAX - d) / 10u) {
            fail_at(p, *at, "a number does not fit in 64 bits");
            return 0;
        }
        x = x * 10u + d;
        p->pos++;
    }
    if (p->pos < p->n && is_alpha(p->s[p->pos])) {
        fail_at(p, p->pos, "a number is followed by a letter");
        return 0;
    }
    *v = x;
    return 1;
}

static int is_keyword(const char *s) {
    for (size_t i = 0; i < sizeof k_keywords / sizeof k_keywords[0]; i++)
        if (strcmp(s, k_keywords[i]) == 0) return 1;
    return 0;
}

static const ctype *find_type(const char *s) {
    for (size_t i = 0; i < sizeof k_types / sizeof k_types[0]; i++)
        if (strcmp(s, k_types[i].kw) == 0) return &k_types[i];
    return NULL;
}

static int is_macro(const char *s) {
    if (!*s || is_digit(*s)) return 0;
    for (; *s; s++)
        if (!((*s >= 'A' && *s <= 'Z') || is_digit(*s) || *s == '_')) return 0;
    return 1;
}

/* ===== Parsing ===== */

/* One bound into a C expression; *lit / *neg / *val describe a literal. */
static int bound(P *p, const ctype *t, char *out, int *lit, int *neg, uint64_t *val) {
    size_t at;
    char name[ZF_MAX_NAME + 1];
    int minus = 0;
    skip_ws(p);
    at = p->pos;
    if (p->pos < p->n && p->s[p->pos] == '-') {
        minus = 1;
        p->pos++;
    }
    if (digits(p, val, &at)) {
        *lit = 1;
        *neg = minus && *val != 0;
        if (!t->is_signed && *neg) {
            fail_at(p, at, "a negative bound for the unsigned type %s", t->kw);
            return 0;
        }
        if (t->is_signed && *val > (uint64_t) INT64_MAX) {
            fail_at(p, at, "a bound beyond the int64 range (use INT64_MIN or INT64_MAX)");
            return 0;
        }
        if (t->is_signed)
            snprintf(out, BOUND_MAX, "%sINT64_C(%llu)%s", *neg ? "(-" : "", (unsigned long long) *val,
                     *neg ? ")" : "");
        else
            snprintf(out, BOUND_MAX, "UINT64_C(%llu)", (unsigned long long) *val);
        return 1;
    }
    if (p->failed) return 0;
    if (ident(p, name, &at)) {
        if (!is_macro(name)) {
            fail_at(p, at, "a bound must be a number or an upper-case macro such as INT_MAX, "
                           "found '%s'", name);
            return 0;
        }
        if (minus && !t->is_signed) {
            fail_at(p, at, "a negative bound for the unsigned type %s", t->kw);
            return 0;
        }
        *lit = 0;
        *neg = minus;
        snprintf(out, BOUND_MAX, "%s(%s)%s", minus ? "(-" : "", name, minus ? ")" : "");
        return 1;
    }
    if (p->failed) return 0;
    char d[64];
    describe(p, d, sizeof d);
    fail_at(p, p->pos, "expected a bound (a number or a macro), found %s", d);
    return 0;
}

static int range(P *p, slot *s) {
    int llit = 0, lneg = 0, hlit = 0, hneg = 0;
    uint64_t lv = 0, hv = 0;
    size_t at;
    skip_ws(p);
    at = p->pos;
    p->pos++; /* '[' */
    if (!bound(p, s->ct, s->lo, &llit, &lneg, &lv)) return 0;
    if (!expect(p, "..", "'..'")) return 0;
    if (!bound(p, s->ct, s->hi, &hlit, &hneg, &hv)) return 0;
    if (!expect(p, "]", "']'")) return 0;
    if (llit && hlit) {
        int bad = lneg ? (hneg && hv > lv) : (hneg || hv < lv);
        if (bad) {
            fail_at(p, at, "the range is empty (lo > hi)");
            return 0;
        }
    }
    return 1;
}

static int size_range(P *p, slot *s) {
    size_t at;
    p->pos++; /* '[' */
    if (!digits(p, &s->slo, &at)) {
        if (!p->failed) fail_at(p, at, "expected a length");
        return 0;
    }
    if (!expect(p, "..", "'..'")) return 0;
    if (!digits(p, &s->shi, &at)) {
        if (!p->failed) fail_at(p, at, "expected a length");
        return 0;
    }
    if (s->shi > (uint64_t) INT32_MAX) {
        fail_at(p, at, "a cstr length bound above 2^31-1");
        return 0;
    }
    if (s->slo > s->shi) {
        fail_at(p, at, "the length range is empty (lo > hi)");
        return 0;
    }
    return expect(p, "]", "']'");
}

static int type_name(P *p, char *tn, size_t *at) {
    if (ident(p, tn, at)) return 1;
    if (!p->failed) {
        char d[64];
        describe(p, d, sizeof d);
        fail_at(p, *at, "expected a type, found %s", d);
    }
    return 0;
}

static int param(P *p, slot *s) {
    char tn[ZF_MAX_NAME + 1];
    size_t at;
    if (!type_name(p, tn, &at)) return 0;
    if (strcmp(tn, "cstr") == 0) {
        s->kind = K_CSTR;
        s->slo = 0;
        s->shi = DEFAULT_CSTR_MAX;
        if (peek_char(p, '[')) return size_range(p, s);
        return 1;
    }
    s->ct = find_type(tn);
    if (!s->ct) {
        fail_at(p, at, "unknown parameter type '%s'", tn);
        return 0;
    }
    if (s->ct->float_bits) {
        s->kind = K_FLOAT;
        if (!keyword_at(p, "from") || !keyword_at(p, "rat")) {
            fail_at(p, at, "a parameter of type %s must say 'from rat' (it is given as an exact "
                           "rational)", tn);
            return 0;
        }
        return 1;
    }
    s->kind = K_INT;
    if (peek_char(p, '[')) return range(p, s);
    return 1;
}

static int result(P *p, slot *s) {
    char tn[ZF_MAX_NAME + 1];
    size_t at;
    if (!type_name(p, tn, &at)) return 0;
    if (strcmp(tn, "void") == 0) {
        s->kind = K_VOID;
        return 1;
    }
    if (strcmp(tn, "cstr") == 0) {
        fail_at(p, at, "pointer results are not supported (z-ffi cannot know who owns them)");
        return 0;
    }
    s->ct = find_type(tn);
    if (!s->ct) {
        fail_at(p, at, "unknown result type '%s'", tn);
        return 0;
    }
    if (s->ct->float_bits) {
        s->kind = K_FLOAT;
        if (!keyword_at(p, "as") || !keyword_at(p, "rat")) {
            fail_at(p, at, "a result of type %s is refused unless the spec says 'as rat' (exact "
                           "conversion with zxv_rat_from_ieee754)", tn);
            return 0;
        }
        return 1;
    }
    s->kind = K_INT;
    if (peek_char(p, '[')) return range(p, s);
    return 1;
}

static int needs(P *p, fndecl *f) {
    char cn[ZF_MAX_NAME + 1];
    size_t at;
    uint32_t seen = 0;
    f->needs[0] = f->needs_text[0] = '\0';
    if (keyword_at(p, "none")) {
        snprintf(f->needs, sizeof f->needs, "UINT64_C(0)");
        snprintf(f->needs_text, sizeof f->needs_text, "none");
        return 1;
    }
    for (;;) {
        size_t k;
        if (!ident(p, cn, &at)) {
            if (!p->failed) fail_at(p, at, "expected a capability name or 'none'");
            return 0;
        }
        for (k = 0; k < sizeof k_caps / sizeof k_caps[0]; k++)
            if (strcmp(cn, k_caps[k]) == 0) break;
        if (k == sizeof k_caps / sizeof k_caps[0]) {
            fail_at(p, at, "unknown capability '%s' (one of LEDGER_READ, LEDGER_POST, "
                           "CRYPTO_SIGN, CRYPTO_KEM, AI_INFERENCE, BUDGET, PHASE, ATTEST, SWARM, "
                           "EVM)", cn);
            return 0;
        }
        if (seen & (1u << k)) {
            fail_at(p, at, "capability '%s' is named twice", cn);
            return 0;
        }
        seen |= 1u << k;
        size_t l = strlen(f->needs);
        snprintf(f->needs + l, sizeof f->needs - l, "%sZXV_CAP_%s", l ? " | " : "", cn);
        l = strlen(f->needs_text);
        snprintf(f->needs_text + l, sizeof f->needs_text - l, "%s%s", l ? "|" : "", cn);
        if (!peek_char(p, '|')) break;
        p->pos++;
    }
    if (strchr(f->needs, '|')) {
        char tmp[160];
        snprintf(tmp, sizeof tmp, "(%s)", f->needs);
        memcpy(f->needs, tmp, sizeof tmp);
    }
    return 1;
}

static int name_ok(P *p, const char *name, size_t at, const char *what) {
    if (strncmp(name, "zf_", 3) == 0 || strncmp(name, "ZF_", 3) == 0) {
        fail_at(p, at, "%s '%s': names starting with zf_ are reserved for generated code", what,
                name);
        return 0;
    }
    if (is_keyword(name) || find_type(name) || strcmp(name, "cstr") == 0) {
        fail_at(p, at, "%s '%s' is a C keyword or a type name", what, name);
        return 0;
    }
    return 1;
}

static int fn_decl(P *p) {
    fndecl f;
    size_t at;
    memset(&f, 0, sizeof f);
    if (!ident(p, f.name, &at)) {
        if (!p->failed) fail_at(p, at, "expected a function name");
        return 0;
    }
    if (!name_ok(p, f.name, at, "function")) return 0;
    for (uint32_t i = 0; i < p->nfns; i++)
        if (strcmp(p->fns[i].name, f.name) == 0) {
            fail_at(p, at, "function '%s' is declared twice", f.name);
            return 0;
        }
    if (p->nfns >= ZF_MAX_FNS) {
        fail_at(p, at, "more than %u functions", ZF_MAX_FNS);
        return 0;
    }
    if (!expect(p, "(", "'('")) return 0;
    if (!peek_char(p, ')')) {
        for (;;) {
            slot *s;
            if (f.np >= ZF_MAX_PARAMS) {
                skip_ws(p);
                fail_at(p, p->pos, "more than %u parameters", ZF_MAX_PARAMS);
                return 0;
            }
            s = &f.p[f.np];
            if (!ident(p, s->name, &at)) {
                if (!p->failed) {
                    char d[64];
                    describe(p, d, sizeof d);
                    fail_at(p, at, "expected a parameter name, found %s", d);
                }
                return 0;
            }
            if (!name_ok(p, s->name, at, "parameter")) return 0;
            if (strcmp(s->name, f.name) == 0) {
                fail_at(p, at, "parameter '%s' would hide the function it calls", s->name);
                return 0;
            }
            /* the generated C names must be distinct: x, x_len, x_num, x_den */
            for (uint32_t j = 0; j < f.np; j++) {
                const char *o = f.p[j].name;
                size_t ol = strlen(o);
                int clash = 0;
                if (strcmp(o, s->name) == 0) {
                    fail_at(p, at, "parameter '%s' is declared twice", o);
                    return 0;
                }
                if (strncmp(s->name, o, ol) == 0)
                    clash = strcmp(s->name + ol, "_len") == 0 || strcmp(s->name + ol, "_num") == 0 ||
                            strcmp(s->name + ol, "_den") == 0;
                if (clash) {
                    fail_at(p, at, "parameter '%s' clashes with parameter '%s'", s->name, o);
                    return 0;
                }
            }
            if (!expect(p, ":", "':'")) return 0;
            if (!param(p, s)) return 0;
            for (uint32_t j = 0; j < f.np; j++) {
                const char *o = f.p[j].name;
                size_t ol = strlen(s->name);
                const char *sfx = strncmp(o, s->name, ol) == 0 ? o + ol : "";
                if ((s->kind == K_CSTR && strcmp(sfx, "_len") == 0) ||
                    (s->kind == K_FLOAT && (strcmp(sfx, "_num") == 0 || strcmp(sfx, "_den") == 0))) {
                    fail_at(p, at, "parameter '%s' clashes with parameter '%s'", s->name, o);
                    return 0;
                }
            }
            f.np++;
            if (peek_char(p, ',')) {
                p->pos++;
                continue;
            }
            break;
        }
    }
    if (!expect(p, ")", "',' or ')'")) return 0;
    if (!expect(p, "->", "'->' and a result type")) return 0;
    if (!result(p, &f.r)) return 0;
    if (keyword_at(p, "needs")) {
        if (!needs(p, &f)) return 0;
    } else {
        snprintf(f.needs, sizeof f.needs, "UINT64_C(0)");
        snprintf(f.needs_text, sizeof f.needs_text, "none");
    }
    if (!expect(p, ";", "';' (or 'needs')")) return 0;
    if (p->nfns % 16u == 0) {
        fndecl *nf = (fndecl *) realloc(p->fns, (p->nfns + 16u) * sizeof *nf);
        if (!nf) return -1;
        p->fns = nf;
    }
    p->fns[p->nfns++] = f;
    return 1;
}

static int include_decl(P *p) {
    char open, close;
    size_t a;
    skip_ws(p);
    if (p->pos >= p->n || (p->s[p->pos] != '<' && p->s[p->pos] != '"')) {
        char d[64];
        describe(p, d, sizeof d);
        fail_at(p, p->pos, "expected <header.h> or \"header.h\", found %s", d);
        return 0;
    }
    open = p->s[p->pos];
    close = open == '<' ? '>' : '"';
    a = ++p->pos;
    while (p->pos < p->n && p->s[p->pos] != close) {
        char c = p->s[p->pos];
        if (!(is_alpha(c) || is_digit(c) || c == '.' || c == '/' || c == '-' || c == '+')) {
            fail_at(p, p->pos, "a header path may hold letters, digits, '_', '.', '/', '-', '+'");
            return 0;
        }
        p->pos++;
    }
    if (p->pos >= p->n) {
        fail_at(p, a - 1u, "unterminated header path");
        return 0;
    }
    size_t len = p->pos - a;
    if (len == 0 || len > 120u || p->s[a] == '/' ||
        memmem_dotdot(p->s + a, len)) {
        fail_at(p, a, "a header path must be 1..120 bytes, relative, without '..'");
        return 0;
    }
    if (p->ninc >= ZF_MAX_INCLUDES) {
        fail_at(p, a, "more than %u includes", ZF_MAX_INCLUDES);
        return 0;
    }
    snprintf(p->inc[p->ninc++], sizeof p->inc[0], "%c%.*s%c", open, (int) len, p->s + a, close);
    p->pos++;
    return expect(p, ";", "';'");
}

static int parse(P *p) {
    char kw[ZF_MAX_NAME + 1];
    size_t at;
    for (;;) {
        int r;
        skip_ws(p);
        if (p->pos >= p->n) break;
        if (!ident(p, kw, &at)) {
            if (!p->failed) {
                char d[64];
                describe(p, d, sizeof d);
                fail_at(p, at, "expected 'library', 'include' or 'fn', found %s", d);
            }
            return 0;
        }
        if (strcmp(kw, "library") == 0) {
            char ln[ZF_MAX_NAME + 1];
            size_t la;
            if (p->lib[0]) {
                fail_at(p, at, "'library' is given twice");
                return 0;
            }
            if (p->nfns) {
                fail_at(p, at, "'library' must come before the first fn");
                return 0;
            }
            if (!ident(p, ln, &la)) {
                if (!p->failed) fail_at(p, la, "expected a library name");
                return 0;
            }
            for (const char *c = ln; *c; c++)
                if (!((*c >= 'a' && *c <= 'z') || is_digit(*c) || *c == '_')) {
                    fail_at(p, la, "a library name is lower-case letters, digits and '_'");
                    return 0;
                }
            if (strlen(ln) > 24u || strncmp(ln, "zf_", 3) == 0) {
                fail_at(p, la, "a library name is at most 24 bytes and does not start with zf_");
                return 0;
            }
            memcpy(p->lib, ln, strlen(ln) + 1u);
            if (!expect(p, ";", "';'")) return 0;
        } else if (strcmp(kw, "include") == 0) {
            if (!include_decl(p)) return 0;
        } else if (strcmp(kw, "fn") == 0) {
            if (!p->lib[0]) {
                fail_at(p, at, "a spec starts with 'library NAME;'");
                return 0;
            }
            r = fn_decl(p);
            if (r <= 0) return r;
        } else {
            fail_at(p, at, "expected 'library', 'include' or 'fn', found '%s'", kw);
            return 0;
        }
    }
    if (!p->lib[0]) {
        fail_at(p, p->pos, "a spec starts with 'library NAME;'");
        return 0;
    }
    if (p->nfns == 0) {
        fail_at(p, p->pos, "the spec declares no fn");
        return 0;
    }
    return 1;
}

/* ===== Emitting ===== */

typedef struct {
    char *b;
    size_t n, cap;
    int oom;
} sb;

ZF_PRINTF(2, 3) static void out(sb *s, const char *fmt, ...) {
    va_list ap;
    int k;
    if (s->oom) return;
    va_start(ap, fmt);
    k = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (k < 0) {
        s->oom = 1;
        return;
    }
    if (s->n + (size_t) k + 1u > s->cap) {
        size_t nc = s->cap ? s->cap : 4096u;
        while (nc < s->n + (size_t) k + 1u) nc *= 2u;
        char *nb = (char *) realloc(s->b, nc);
        if (!nb) {
            s->oom = 1;
            return;
        }
        s->b = nb;
        s->cap = nc;
    }
    va_start(ap, fmt);
    vsnprintf(s->b + s->n, s->cap - s->n, fmt, ap);
    va_end(ap);
    s->n += (size_t) k;
}

static const char *ctext(const slot *s) {
    if (s->kind == K_VOID) return "void";
    if (s->kind == K_CSTR) return "const char *";
    return s->ct->ctext;
}

static void lo_of(const slot *s, char *b) {
    if (s->lo[0]) snprintf(b, BOUND_MAX + 24, "%s", s->lo);
    else if (s->ct->is_signed) snprintf(b, BOUND_MAX + 24, "(int64_t) (%s)", s->ct->tmin);
    else snprintf(b, BOUND_MAX + 24, "UINT64_C(0)");
}

static void hi_of(const slot *s, char *b) {
    if (s->hi[0]) snprintf(b, BOUND_MAX + 24, "%s", s->hi);
    else snprintf(b, BOUND_MAX + 24, "(%s) (%s)", s->ct->is_signed ? "int64_t" : "uint64_t", s->ct->tmax);
}

/* "double (double, int)" or "double (*)(double, int)" */
static void c_signature(sb *o, const fndecl *f, const char *mid) {
    out(o, "%s %s(", ctext(&f->r), mid);
    if (f->np == 0) out(o, "void");
    for (uint32_t i = 0; i < f->np; i++) out(o, "%s%s", i ? ", " : "", ctext(&f->p[i]));
    out(o, ")");
}

static void wrapper_params(sb *o, const fndecl *f) {
    out(o, "const zxv_cap *zf_cap");
    for (uint32_t i = 0; i < f->np; i++) {
        const slot *s = &f->p[i];
        if (s->kind == K_INT) out(o, ", %s %s", s->ct->is_signed ? "int64_t" : "uint64_t", s->name);
        else if (s->kind == K_FLOAT) out(o, ", int64_t %s_num, int64_t %s_den", s->name, s->name);
        else out(o, ", const char *%s, size_t %s_len", s->name, s->name);
    }
    if (f->r.kind == K_INT) out(o, ", %s *zf_out", f->r.ct->is_signed ? "int64_t" : "uint64_t");
    else if (f->r.kind == K_FLOAT) out(o, ", int64_t *zf_out_num, int64_t *zf_out_den");
}

static void slot_doc(sb *o, const slot *s, const char *label) {
    char lo[BOUND_MAX + 24], hi[BOUND_MAX + 24];
    if (s->kind == K_INT) {
        snprintf(lo, sizeof lo, "%s", s->lo[0] ? s->lo : s->ct->tmin);
        snprintf(hi, sizeof hi, "%s", s->hi[0] ? s->hi : s->ct->tmax);
        out(o, " *   %-10s %s in [%s .. %s]\n", label, s->ct->ctext, lo, hi);
    } else if (s->kind == K_FLOAT) {
        out(o, " *   %-10s %s, %s\n", label, s->ct->ctext,
            label[0] == '-' ? "returned as the exact rational of its bits (NaN, inf refused)"
                            : "given as an exact rational (refused if the type cannot hold it)");
    } else if (s->kind == K_CSTR) {
        out(o, " *   %-10s const char *, %llu..%llu bytes, no NUL, copied\n", label,
            (unsigned long long) s->slo, (unsigned long long) s->shi);
    } else {
        out(o, " *   %-10s void\n", label);
    }
}

static void range_assert(sb *o, const char *lib, const fndecl *f, const slot *s, const char *what) {
    char lo[BOUND_MAX + 24], hi[BOUND_MAX + 24];
    if (s->kind != K_INT || (!s->lo[0] && !s->hi[0])) return;
    lo_of(s, lo);
    hi_of(s, hi);
    if (s->ct->is_signed)
        out(o, "_Static_assert((intmax_t) (%s) >= (intmax_t) (%s) && (intmax_t) (%s) <= (intmax_t) "
               "(%s) &&\n               (intmax_t) (%s) <= (intmax_t) (%s),\n"
               "               \"%s.%s: the range of %s must fit %s and have lo <= hi\");\n",
            lo, s->ct->tmin, hi, s->ct->tmax, lo, hi, lib, f->name, what, s->ct->ctext);
    else
        out(o, "_Static_assert((uintmax_t) (%s) <= (uintmax_t) (%s) && (uintmax_t) (%s) <= "
               "(uintmax_t) (%s),\n               \"%s.%s: the range of %s must fit %s and have "
               "lo <= hi\");\n",
            lo, hi, hi, s->ct->tmax, lib, f->name, what, s->ct->ctext);
}

static void emit_header(sb *o, const P *p, const char *hex) {
    char up[32];
    size_t i;
    for (i = 0; p->lib[i]; i++) up[i] = (char) (p->lib[i] >= 'a' && p->lib[i] <= 'z' ? p->lib[i] - 32 : p->lib[i]);
    up[i] = '\0';
    out(o, "/* Generated by zf-gen (z-ffi %d.%d) from the spec for library '%s'.\n"
           " * Spec SHA3-256: %s\n"
           " * Do not edit: change the spec and generate again. */\n"
           "#ifndef ZF_GEN_%s_H\n#define ZF_GEN_%s_H\n\n#include <stddef.h>\n#include <stdint.h>\n\n"
           "#include \"zffi.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
           "#define ZF_%s_SPEC_SHA3 \"%s\"\n",
        ZF_VERSION_MAJOR, ZF_VERSION_MINOR, p->lib, hex, up, up, up, hex);
    for (uint32_t k = 0; k < p->nfns; k++) {
        const fndecl *f = &p->fns[k];
        out(o, "\n/* %s.%s: calls ", p->lib, f->name);
        c_signature(o, f, f->name);
        out(o, ";\n *   needs      %s (and a live cap attested TRUE)\n", f->needs_text);
        for (uint32_t i2 = 0; i2 < f->np; i2++) slot_doc(o, &f->p[i2], f->p[i2].name);
        slot_doc(o, &f->r, "-> result");
        out(o, " */\nint32_t zf_%s_%s(", p->lib, f->name);
        wrapper_params(o, f);
        out(o, ");\n");
    }
    out(o, "\n#ifdef __cplusplus\n}\n#endif\n\n#endif /* ZF_GEN_%s_H */\n", up);
}

static void emit_source(sb *o, const P *p, const char *hex, const char *header_name) {
    out(o, "/* Generated by zf-gen (z-ffi %d.%d) from the spec for library '%s'.\n"
           " * Spec SHA3-256: %s\n"
           " * Do not edit: change the spec and generate again.\n"
           " *\n"
           " * This file holds float values for the length of a call; it is outside\n"
           " * z-ffi's no-float check (R11). Every value crosses into and out of it\n"
           " * through the checked, integer-only runtime in zf_runtime.c. */\n"
           "#include <limits.h>\n#include <stdint.h>\n#include <string.h>\n\n",
        ZF_VERSION_MAJOR, ZF_VERSION_MINOR, p->lib, hex);
    for (uint32_t i = 0; i < p->ninc; i++) out(o, "#include %s\n", p->inc[i]);
    out(o, "\n#include \"%s\"\n", header_name);
    for (uint32_t k = 0; k < p->nfns; k++) {
        const fndecl *f = &p->fns[k];
        char lo[BOUND_MAX + 24], hi[BOUND_MAX + 24];
        int has_cstr = 0;
        out(o, "\n/* ---- %s.%s ---- */\n_Static_assert(_Generic(&%s, ", p->lib, f->name, f->name);
        c_signature(o, f, "(*)");
        out(o, ": 1, default: 0),\n               \"%s.%s: the spec says '", p->lib, f->name);
        c_signature(o, f, f->name);
        out(o, "', which is not how the included headers declare it\");\n");
        for (uint32_t i = 0; i < f->np; i++) {
            char what[ZF_MAX_NAME + 16];
            snprintf(what, sizeof what, "'%s'", f->p[i].name);
            range_assert(o, p->lib, f, &f->p[i], what);
        }
        range_assert(o, p->lib, f, &f->r, "the result");

        out(o, "\nint32_t zf_%s_%s(", p->lib, f->name);
        wrapper_params(o, f);
        out(o, ") {\n    static const char zf_fn[] = \"%s.%s\";\n    int32_t zf_st;\n", p->lib, f->name);
        for (uint32_t i = 0; i < f->np; i++) {
            const slot *s = &f->p[i];
            if (s->kind == K_FLOAT && s->ct->float_bits == 64)
                out(o, "    uint64_t zf_b%u;\n    double zf_a%u;\n", (unsigned) i, (unsigned) i);
            else if (s->kind == K_FLOAT)
                out(o, "    uint32_t zf_b%u;\n    float zf_a%u;\n", (unsigned) i, (unsigned) i);
            else if (s->kind == K_CSTR) {
                out(o, "    char *zf_s%u = NULL;\n", (unsigned) i);
                has_cstr = 1;
            }
        }
        if (f->r.kind == K_INT) out(o, "    if (!zf_out) return zf_null_out(zf_fn);\n");
        if (f->r.kind == K_FLOAT) out(o, "    if (!zf_out_num || !zf_out_den) return zf_null_out(zf_fn);\n");
        out(o, "    zf_st = zf_enter(zf_cap, %s, zf_fn);\n    if (zf_st != ZXV_OK) goto zf_done;\n", f->needs);
        for (uint32_t i = 0; i < f->np; i++) {
            const slot *s = &f->p[i];
            if (s->kind == K_INT) {
                lo_of(s, lo);
                hi_of(s, hi);
                out(o, "    zf_st = zf_arg_%s(zf_fn, %u, \"%s\", %s, %s, %s);\n", s->ct->is_signed ? "i64" : "u64",
                    (unsigned) i, s->name, s->name, lo, hi);
            } else if (s->kind == K_FLOAT) {
                out(o, "    zf_st = zf_arg_f%d(zf_fn, %u, \"%s\", %s_num, %s_den, &zf_b%u);\n", s->ct->float_bits,
                    (unsigned) i, s->name, s->name, s->name, (unsigned) i);
            } else {
                out(o, "    zf_st = zf_arg_cstr(zf_fn, %u, \"%s\", %s, %s_len, (size_t) %lluu, (size_t) %lluu, "
                       "&zf_s%u);\n",
                    (unsigned) i, s->name, s->name, s->name, (unsigned long long) s->slo,
                    (unsigned long long) s->shi, (unsigned) i);
            }
            out(o, "    if (zf_st != ZXV_OK) goto zf_done;\n");
            if (s->kind == K_FLOAT)
                out(o, "    memcpy(&zf_a%u, &zf_b%u, sizeof zf_a%u);\n", (unsigned) i, (unsigned) i, (unsigned) i);
        }
        out(o, "    {\n");
        if (f->r.kind == K_VOID) out(o, "        %s(", f->name);
        else out(o, "        %s zf_r = %s(", f->r.ct->ctext, f->name);
        for (uint32_t i = 0; i < f->np; i++) {
            const slot *s = &f->p[i];
            if (i) out(o, ", ");
            if (s->kind == K_INT) out(o, "(%s) %s", s->ct->ctext, s->name);
            else if (s->kind == K_FLOAT) out(o, "zf_a%u", (unsigned) i);
            else out(o, "zf_s%u", (unsigned) i);
        }
        out(o, ");\n");
        if (f->r.kind == K_VOID) {
            out(o, "        zf_st = ZXV_OK;\n");
        } else if (f->r.kind == K_INT) {
            lo_of(&f->r, lo);
            hi_of(&f->r, hi);
            out(o, "        zf_st = zf_res_%s(zf_fn, (%s) zf_r, %s, %s, zf_out);\n",
                f->r.ct->is_signed ? "i64" : "u64", f->r.ct->is_signed ? "int64_t" : "uint64_t", lo, hi);
        } else if (f->r.ct->float_bits == 64) {
            out(o, "        uint64_t zf_rb;\n        memcpy(&zf_rb, &zf_r, sizeof zf_rb);\n"
                   "        zf_st = zf_res_f64(zf_fn, zf_rb, zf_out_num, zf_out_den);\n");
        } else {
            out(o, "        double zf_rd = (double) zf_r; /* exact: binary32 widens to binary64 */\n"
                   "        uint64_t zf_rb;\n        memcpy(&zf_rb, &zf_rd, sizeof zf_rb);\n"
                   "        zf_st = zf_res_f64(zf_fn, zf_rb, zf_out_num, zf_out_den);\n");
        }
        out(o, "    }\nzf_done:\n");
        if (has_cstr)
            for (uint32_t i = 0; i < f->np; i++)
                if (f->p[i].kind == K_CSTR) out(o, "    zf_cstr_free(zf_s%u);\n", (unsigned) i);
        out(o, "    return zf_st;\n}\n");
    }
}

void zf_gen_free(char *text) { free(text); }

int32_t zf_gen(const char *spec, size_t len, const char *header_name, char **out_header,
               size_t *out_header_len, char **out_source, size_t *out_source_len,
               uint32_t *out_line, uint32_t *out_col, uint64_t *out_offset, char *msg,
               size_t msg_cap) {
    P p;
    sb h = {NULL, 0, 0, 0}, c = {NULL, 0, 0, 0};
    uint8_t dg[32];
    char hex[65], full[480];
    int r;
    if (msg && msg_cap) msg[0] = '\0';
    if (!out_header || !out_source || !header_name || (!spec && len)) return ZXV_E_ARG;
    *out_header = *out_source = NULL;
    if (!*header_name || strlen(header_name) > 120u) return ZXV_E_ARG;
    for (const char *q = header_name; *q; q++)
        if (!(is_alpha(*q) || is_digit(*q) || *q == '.' || *q == '/' || *q == '-')) return ZXV_E_ARG;
    memset(&p, 0, sizeof p);
    p.s = spec ? spec : "";
    p.n = len;
    if (len > ZF_MAX_SPEC) {
        fail_at(&p, 0, "the spec is longer than %u bytes", ZF_MAX_SPEC);
        r = 0;
    } else {
        r = parse(&p);
    }
    if (r < 0) {
        free(p.fns);
        return ZXV_E_NOMEM;
    }
    if (!r) {
        snprintf(full, sizeof full, "line %u, column %u: %s", (unsigned) p.eline, (unsigned) p.ecol,
                 p.emsg);
        if (out_line) *out_line = p.eline;
        if (out_col) *out_col = p.ecol;
        if (out_offset) *out_offset = p.eoff;
        if (msg && msg_cap) {
            size_t k = strlen(full);
            if (k >= msg_cap) k = msg_cap - 1u;
            memcpy(msg, full, k);
            msg[k] = '\0';
        }
        zf_set_errorf("zf_gen: %s", full);
        free(p.fns);
        return ZF_E_SPEC;
    }
    if (zxv_sha3_256((const uint8_t *) p.s, len, dg) != ZXV_OK) {
        free(p.fns);
        return ZXV_E_INTERNAL;
    }
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", dg[i]);
    emit_header(&h, &p, hex);
    emit_source(&c, &p, hex, header_name);
    free(p.fns);
    if (h.oom || c.oom) {
        free(h.b);
        free(c.b);
        return ZXV_E_NOMEM;
    }
    *out_header = h.b;
    *out_source = c.b;
    if (out_header_len) *out_header_len = h.n;
    if (out_source_len) *out_source_len = c.n;
    return ZXV_OK;
}
