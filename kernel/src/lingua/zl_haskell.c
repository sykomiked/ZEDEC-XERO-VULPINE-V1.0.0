/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_haskell.c — the Haskell front-end (twin "Purewyrm"): an integer,
 * boolean and list subset of Haskell 2010 as GHC 9.4 runs it. This file
 * holds the lexer (with the layout rule), the parser and the twin printer;
 * the tree it builds is the ML family's (zl_ml_int.h), so the
 * Hindley-Milner type checker and the code generator are zl_ml.c's.
 *
 * Admitted: a module of top-level declarations (an optional "module Main
 * where" or "module Main (main) where" header) with main :: IO ();
 * functions defined by equations with patterns (variables, _, integer
 * literals, True and False, [], x:xs, [a, b], (), tuples of parameters),
 * guards (| g = e, otherwise), where; value bindings; type signatures
 * over Int, Bool, (), [t], type variables, tuples of parameters and
 * IO (); let ... in, if/then/else, case ... of with guards, do blocks of
 * expression statements and let; the layout rule (indentation) and
 * explicit braces; integer literals, + - * ^ (a negative exponent traps),
 * negation, div and mod (floored) and quot and rem (truncating) as
 * functions or `infix`, == /= < <= > >= (on integers, booleans, () and
 * lists), && || not, $, : ++ !!, [a .. b] and [a, b .. c]; the Prelude
 * functions abs signum negate even odd succ pred min max gcd lcm head tail
 * null length reverse sum product maximum minimum take drop elem notElem
 * replicate last init concat and or; print, putStrLn and putStr of
 * strings built from literals, show and ++; tuples written out as the
 * argument of print or show; return () and pure ().
 *
 * Int is GHC's 64-bit Int. GHC wraps silently on overflow; this subset
 * does not wrap: a result outside the word traps OVERFLOW (64 bits, or
 * +-2^62 in a program that uses lists, whose other values are references).
 * Integer is refused by name; a literal without a signature, which GHC
 * defaults to Integer, is an Int here, so a value past Int traps where GHC
 * would go on. Division by zero traps DIVZERO; head, tail, maximum, ...
 * of [] and a failed match or guard trap DOMAIN, !! out of range BOUNDS,
 * as GHC stops with an exception.
 *
 * Laziness. Haskell is lazy; this subset evaluates arguments, operands and
 * list elements eagerly (call by value) and let, where and top-level value
 * bindings on first use (call by need: a thunk runs once, when the name is
 * first used). The rule: a program's output is GHC's whenever the run ends
 * without a trap, because eager evaluation only adds work and pure work
 * that ends cannot change a value. Infinite lists ([a ..], iterate,
 * repeat, cycle), recursive value bindings (xs = 1 : xs), error and
 * undefined are refused when compiling, and [a, a .. b] (a zero step)
 * traps DOMAIN at run time; a trap or a run out of fuel may be work GHC
 * would never have forced, so a trap means only that the program is
 * outside what this subset runs, never a claim about GHC's output.
 *
 * Refused by name (ZL_E_UNSUPPORTED): type classes, instances and
 * contexts, data, newtype, type, records, modules other than Main and
 * imports, lambdas, operator sections, functions as values, partial
 * application, map/filter/fold and the other higher-order functions,
 * other tuples as values, Integer, Double and other types, characters and
 * strings as values, list comprehensions, <- in do, monads other than
 * main's IO, where on a case alternative, pattern guards and comma
 * guards, as-patterns, lazy and bang patterns, operator definitions and
 * fixity declarations, numeric literal extensions and tabs.
 *
 * The twin: one top-level declaration (signature, equation, value
 * binding or main) per line in the source order, comments between them
 * kept as "-- text" lines and dropped inside declarations; nested blocks
 * (do, let, where, case ... of) written with explicit braces and "; ";
 * one space between tokens, none inside parentheses and brackets, ", "
 * after commas; parentheses only where the grammar needs them, and around
 * if, case, let and do unless they stand whole (a right side, a branch, a
 * statement, an element); $ written as application, div mod quot rem
 * always `infix`, elem and every other function always in prefix; a
 * module header dropped. */
#include "zl_ml_int.h"

/* ------------------------------------------------------------ tokens */

enum {
    HT_EOF = 0,
    HT_INT,
    HT_VAR,
    HT_CON,
    HT_STR,
    HT_LP,
    HT_RP,
    HT_LB,
    HT_RB,
    HT_COMMA,
    HT_SEMI,
    HT_LBRACE,
    HT_RBRACE,
    HT_BAR,
    HT_ARROW,
    HT_LARROW,
    HT_EQ,
    HT_DCOLON,
    HT_DOTDOT,
    HT_WILD,
    HT_OP,    /* v: O_* or OP_DOLLAR */
    HT_MINUS, /* binary or unary */
    HT_BTICK, /* `name`: at, len are the name's */
    HK_CASE,
    HK_OF,
    HK_DO,
    HK_LET,
    HK_IN,
    HK_WHERE,
    HK_IF,
    HK_THEN,
    HK_ELSE,
    HK_MODULE
};

#define OP_DOLLAR 100
#define HS_CTX    96  /* nesting of layout blocks */
#define HS_GROUP  128 /* bindings in one group (let, where or the top level) */

typedef struct {
    int k;
    size_t at;
    uint32_t len, line, col;
    int64_t v;
    uint8_t virt;  /* a brace or semicolon the layout rule inserted */
    uint8_t first; /* the first token on its line */
} htok_t;

typedef struct {
    size_t pos, lstart;
    uint32_t line, lastline; /* lastline: the line of the last raw token */
    htok_t raw;              /* the next raw token, when have_raw */
    uint8_t have_raw, raw_bol, want_open, pending_close;
    uint16_t ctx[HS_CTX]; /* layout contexts: a column, or 0 for explicit braces */
    uint32_t nctx;
    uint32_t ncom;
} hstate_t;

typedef struct {
    mw_t m; /* first: the shared tree, types and code generator */
    hstate_t st;
    htok_t tok;
    size_t last_end;
    int32_t root, items; /* the top-level E_LET and its N_ITEM list */
    /* binding groups */
    int32_t gb[HS_GROUP];
    uint64_t reach[HS_GROUP][HS_GROUP / 64];
    uint8_t done[HS_GROUP];
    uint32_t ng;
} hp_t;

ZL_STATIC_ASSERT(sizeof(hp_t) <= sizeof(((zl_work_t *) 0)->fe), hp_fits);

#define W(h)     (&(h)->m)
#define HN(h, i) (&(h)->m.nodes[i])

static int hfail_at(hp_t *h, int st, uint32_t line, uint32_t col, const char *msg)
{
    return zl_ml_fail(W(h), st, line, col, msg);
}

static int hfail(hp_t *h, int st, const char *msg)
{
    return hfail_at(h, st, h->tok.line, h->tok.col, msg);
}

static int nfail_h(hp_t *h, int32_t n, int st, const char *msg)
{
    return hfail_at(h, st, HN(h, n)->line, HN(h, n)->col, msg);
}

static int same(const char *a, size_t n, const char *s)
{
    size_t i = 0;
    for (; i < n; i++)
        if (s[i] == 0 || s[i] != a[i]) return 0;
    return s[i] == 0;
}

/* ------------------------------------------------------------ raw lexer */

static int is_sym(int ch)
{
    switch (ch) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '*':
    case '+':
    case '.':
    case '/':
    case '<':
    case '=':
    case '>':
    case '?':
    case '@':
    case '\\':
    case '^':
    case '|':
    case '-':
    case '~':
    case ':':
        return 1;
    default:
        return 0;
    }
}

static int is_idch(int ch)
{
    return zl_isalnum(ch) || ch == '_' || ch == '\'';
}

static int lfail(hp_t *h, int st, size_t at, const char *msg)
{
    return hfail_at(h, st, h->st.line, (uint32_t) (at - h->st.lstart + 1), msg);
}

/* record a comment's text (trimmed) */
static int add_comment(hp_t *h, size_t a, size_t b, uint32_t line, uint32_t col)
{
    mw_t *w = W(h);
    while (a < b && (h->m.src[a] == ' ' || h->m.src[a] == '\n' || h->m.src[a] == '\r')) a++;
    while (b > a && (h->m.src[b - 1] == ' ' || h->m.src[b - 1] == '\n' || h->m.src[b - 1] == '\r'))
        b--;
    if (b - a > ZL_MAX_STRBYTES) return hfail_at(h, ZL_E_LIMIT, line, col, "comment too long");
    if (w->ncom >= M_COMS) return hfail_at(h, ZL_E_LIMIT, line, col, "too many comments");
    w->coms[w->ncom].at = a;
    w->coms[w->ncom].len = (uint32_t) (b - a);
    w->coms[w->ncom].line = line;
    w->ncom++;
    return ZL_OK;
}

/* skip blanks and comments */
static int skip(hp_t *h)
{
    hstate_t *l = &h->st;
    const char *s = h->m.src;
    size_t n = h->m.len;
    for (;;) {
        if (l->pos >= n) return ZL_OK;
        unsigned char ch = (unsigned char) s[l->pos];
        if (ch == '\n') {
            l->pos++;
            l->line++;
            l->lstart = l->pos;
        } else if (ch == ' ' || ch == '\r') {
            l->pos++;
        } else if (ch == '\t') {
            return lfail(
                h, ZL_E_UNSUPPORTED, l->pos,
                "tabs are not in this subset (the layout rule counts columns: use spaces)");
        } else if (ch == '{' && l->pos + 1 < n && s[l->pos + 1] == '-') {
            uint32_t line = l->line, col = (uint32_t) (l->pos - l->lstart + 1);
            if (l->pos + 2 < n && s[l->pos + 2] == '#')
                return lfail(h, ZL_E_UNSUPPORTED, l->pos, "pragmas are not in this subset");
            size_t a = l->pos + 2;
            int depth = 1;
            l->pos += 2;
            while (depth) {
                if (l->pos >= n) return hfail_at(h, ZL_E_SYNTAX, line, col, "unterminated comment");
                unsigned char d = (unsigned char) s[l->pos];
                if (d == '\n') {
                    l->pos++;
                    l->line++;
                    l->lstart = l->pos;
                } else if (d == '{' && l->pos + 1 < n && s[l->pos + 1] == '-') {
                    depth++;
                    l->pos += 2;
                } else if (d == '-' && l->pos + 1 < n && s[l->pos + 1] == '}') {
                    depth--;
                    l->pos += 2;
                } else if ((d < 32 && d != '\r') || d > 126) {
                    return lfail(h, ZL_E_UNSUPPORTED, l->pos,
                                 "only printable ASCII is in this subset's comments");
                } else {
                    l->pos++;
                }
            }
            TRY(add_comment(h, a, l->pos - 2, line, col));
        } else if (ch == '-' && l->pos + 1 < n && s[l->pos + 1] == '-') {
            /* a line comment: dashes not followed by another symbol */
            size_t p = l->pos;
            while (p < n && s[p] == '-') p++;
            if (p < n && is_sym((unsigned char) s[p])) return ZL_OK; /* an operator such as --> */
            uint32_t line = l->line, col = (uint32_t) (l->pos - l->lstart + 1);
            size_t a = p;
            while (p < n && s[p] != '\n') {
                unsigned char d = (unsigned char) s[p];
                if ((d < 32 && d != '\r' && d != '\t') || d > 126)
                    return lfail(h, ZL_E_UNSUPPORTED, p,
                                 "only printable ASCII is in this subset's comments");
                if (d == '\t')
                    return lfail(
                        h, ZL_E_UNSUPPORTED, p,
                        "tabs are not in this subset (the layout rule counts columns: use spaces)");
                p++;
            }
            TRY(add_comment(h, a, p, line, col));
            l->pos = p;
        } else {
            return ZL_OK;
        }
    }
}

typedef struct {
    const char *s;
    uint8_t k;
    const char *msg;
} hkw_t;

static const hkw_t hkws[] = {
    {"case", HK_CASE, 0},
    {"of", HK_OF, 0},
    {"do", HK_DO, 0},
    {"let", HK_LET, 0},
    {"in", HK_IN, 0},
    {"where", HK_WHERE, 0},
    {"if", HK_IF, 0},
    {"then", HK_THEN, 0},
    {"else", HK_ELSE, 0},
    {"module", HK_MODULE, 0},
    {"class", 0, "type classes are not in this subset"},
    {"instance", 0, "instances (type classes) are not in this subset"},
    {"data", 0, "data declarations are not in this subset"},
    {"newtype", 0, "newtype is not in this subset"},
    {"type", 0, "type synonyms are not in this subset"},
    {"deriving", 0, "deriving (type classes) is not in this subset"},
    {"default", 0, "default declarations are not in this subset"},
    {"import", 0, "imports are not in this subset"},
    {"foreign", 0, "the foreign function interface is not in this subset"},
    {"infix", 0, "fixity declarations are not in this subset"},
    {"infixl", 0, "fixity declarations are not in this subset"},
    {"infixr", 0, "fixity declarations are not in this subset"},
    {"forall", 0, "explicit forall is not in this subset"},
    {"mdo", 0, "mdo is not in this subset"},
};

typedef struct {
    const char *s;
    int k, v;
    const char *msg;
} hop_text_t;

static const hop_text_t hops[] = {
    {"..", HT_DOTDOT, 0, 0},
    {":", HT_OP, O_CONS, 0},
    {"::", HT_DCOLON, 0, 0},
    {"=", HT_EQ, 0, 0},
    {"|", HT_BAR, 0, 0},
    {"<-", HT_LARROW, 0, 0},
    {"->", HT_ARROW, 0, 0},
    {"+", HT_OP, O_ADD, 0},
    {"-", HT_MINUS, O_SUB, 0},
    {"*", HT_OP, O_MUL, 0},
    {"^", HT_OP, O_POW, 0},
    {"==", HT_OP, O_EQ, 0},
    {"/=", HT_OP, O_NE, 0},
    {"<", HT_OP, O_LT, 0},
    {"<=", HT_OP, O_LE, 0},
    {">", HT_OP, O_GT, 0},
    {">=", HT_OP, O_GE, 0},
    {"&&", HT_OP, O_ANDALSO, 0},
    {"||", HT_OP, O_ORELSE, 0},
    {"++", HT_OP, O_APPEND, 0},
    {"!!", HT_OP, O_INDEX, 0},
    {"$", HT_OP, OP_DOLLAR, 0},
    {"\\", 0, 0, "lambdas are not in this subset"},
    {"@", 0, 0, "as-patterns and type applications are not in this subset"},
    {"~", 0, 0, "lazy patterns are not in this subset"},
    {"!", 0, 0, "bang patterns are not in this subset"},
    {"=>", 0, 0, "type classes (contexts) are not in this subset"},
    {"/", 0, 0, "fractional division (/) is not in this subset (use div)"},
    {".", 0, 0, "function composition (.) is not in this subset"},
    {"**", 0, 0, "floating-point power (**) is not in this subset"},
    {"^^", 0, 0, "fractional power (^^) is not in this subset"},
    {">>", 0, 0, "monad operators (>>) are not in this subset (use do)"},
    {">>=", 0, 0, "monad operators (>>=) are not in this subset"},
    {"=<<", 0, 0, "monad operators (=<<) are not in this subset"},
    {"<$>", 0, 0, "functors (<$>) are not in this subset"},
    {"<*>", 0, 0, "applicatives (<*>) are not in this subset"},
    {"$!", 0, 0, "strict application ($!) is not in this subset"},
    {"<>", 0, 0, "monoids (<>) are not in this subset"},
};

/* the next raw token into h->st.raw */
static int raw(hp_t *h)
{
    hstate_t *l = &h->st;
    htok_t *t = &l->raw;
    TRY(skip(h));
    const char *s = h->m.src;
    size_t p = l->pos, n = h->m.len;
    zl_bzero(t, sizeof *t);
    t->at = p;
    t->line = l->line;
    t->col = (uint32_t) (p - l->lstart + 1);
    t->first = l->lastline != l->line;
    l->lastline = l->line;
    if (p >= n) {
        t->k = HT_EOF;
        t->first = 1;
        return ZL_OK;
    }
    unsigned char ch = (unsigned char) s[p];
    if (zl_isdigit(ch)) {
        uint64_t v = 0;
        int big = 0;
        if (ch == '0' && p + 1 < n &&
            (s[p + 1] == 'x' || s[p + 1] == 'X' || s[p + 1] == 'o' || s[p + 1] == 'O' ||
             s[p + 1] == 'b' || s[p + 1] == 'B'))
            return lfail(h, ZL_E_UNSUPPORTED, p,
                         "hexadecimal, octal and binary literals are not in this subset");
        while (p < n && zl_isdigit((unsigned char) s[p])) {
            v = v * 10 + (uint64_t) (s[p] - '0');
            if (v > (uint64_t) INT64_MAX) {
                big = 1;
                v = (uint64_t) INT64_MAX;
            }
            p++;
        }
        if (p + 1 < n && s[p] == '.' && zl_isdigit((unsigned char) s[p + 1]))
            return lfail(h, ZL_E_UNSUPPORTED, t->at,
                         "floating-point numbers are not in this subset");
        if (p < n && (s[p] == 'e' || s[p] == 'E') &&
            ((p + 1 < n && zl_isdigit((unsigned char) s[p + 1])) ||
             (p + 2 < n && (s[p + 1] == '-' || s[p + 1] == '+') &&
              zl_isdigit((unsigned char) s[p + 2]))))
            return lfail(h, ZL_E_UNSUPPORTED, t->at,
                         "floating-point numbers are not in this subset");
        if (p < n && s[p] == '_')
            return lfail(h, ZL_E_UNSUPPORTED, t->at, "numeric underscores are not in this subset");
        if (p < n && is_idch((unsigned char) s[p]))
            return lfail(h, ZL_E_SYNTAX, p, "a letter right after a number");
        if (big)
            return lfail(h, ZL_E_UNSUPPORTED, t->at,
                         "an integer literal outside Int (Integer is not in this subset)");
        t->k = HT_INT;
        t->v = (int64_t) v;
        t->len = (uint32_t) (p - t->at);
        l->pos = p;
        return ZL_OK;
    }
    if (zl_isalpha(ch) || ch == '_') {
        size_t a = p;
        while (p < n && is_idch((unsigned char) s[p])) p++;
        if (p - a > 64) return lfail(h, ZL_E_LIMIT, a, "name too long");
        t->len = (uint32_t) (p - a);
        l->pos = p;
        if (ch >= 'A' && ch <= 'Z') {
            if (p + 1 < n && s[p] == '.' &&
                (zl_isalpha((unsigned char) s[p + 1]) || is_sym((unsigned char) s[p + 1])))
                return lfail(h, ZL_E_UNSUPPORTED, a,
                             "qualified names (modules) are not in this subset");
            t->k = HT_CON;
            return ZL_OK;
        }
        if (ch == '_' && p == a + 1) {
            t->k = HT_WILD;
            return ZL_OK;
        }
        t->k = HT_VAR;
        for (size_t i = 0; i < sizeof hkws / sizeof hkws[0]; i++) {
            if (!same(s + a, p - a, hkws[i].s)) continue;
            if (!hkws[i].k) return lfail(h, ZL_E_UNSUPPORTED, a, hkws[i].msg);
            t->k = hkws[i].k;
            break;
        }
        return ZL_OK;
    }
    if (ch == '"') {
        p++;
        while (p < n && s[p] != '"') {
            unsigned char d = (unsigned char) s[p];
            if (d == '\\') {
                char e = p + 1 < n ? s[p + 1] : 0;
                if (e != 'n' && e != 't' && e != '\\' && e != '"')
                    return lfail(h, ZL_E_UNSUPPORTED, p,
                                 "only the escapes \\n \\t \\\\ \\\" are in this subset");
                p += 2;
                continue;
            }
            if (d < 32 || d > 126) {
                if (d == '\n') return lfail(h, ZL_E_SYNTAX, t->at, "unterminated string");
                return lfail(h, ZL_E_UNSUPPORTED, p,
                             "only printable ASCII is in this subset's strings");
            }
            p++;
        }
        if (p >= n) return lfail(h, ZL_E_SYNTAX, t->at, "unterminated string");
        p++;
        if (p - t->at > M_STR) return lfail(h, ZL_E_LIMIT, t->at, "string too long");
        t->k = HT_STR;
        t->len = (uint32_t) (p - t->at);
        l->pos = p;
        return ZL_OK;
    }
    int k = 0;
    switch (ch) {
    case '(':
        k = HT_LP;
        break;
    case ')':
        k = HT_RP;
        break;
    case '[':
        k = HT_LB;
        break;
    case ']':
        k = HT_RB;
        break;
    case ',':
        k = HT_COMMA;
        break;
    case ';':
        k = HT_SEMI;
        break;
    case '{':
        k = HT_LBRACE;
        break;
    case '}':
        k = HT_RBRACE;
        break;
    case '\'':
        return lfail(h, ZL_E_UNSUPPORTED, p, "characters are not in this subset");
    case '`': {
        size_t a = p + 1, b = a;
        while (b < n && is_idch((unsigned char) s[b])) b++;
        if (b == a || b >= n || s[b] != '`') return lfail(h, ZL_E_SYNTAX, p, "expected `name`");
        if (s[a] >= 'A' && s[a] <= 'Z')
            return lfail(h, ZL_E_UNSUPPORTED, p, "data constructors are not in this subset");
        if (b - a > 64) return lfail(h, ZL_E_LIMIT, a, "name too long");
        t->k = HT_BTICK;
        t->at = a;
        t->len = (uint32_t) (b - a);
        l->pos = b + 1;
        return ZL_OK;
    }
    default:
        break;
    }
    if (k) {
        t->k = k;
        t->len = 1;
        l->pos = p + 1;
        return ZL_OK;
    }
    if (is_sym(ch)) {
        size_t a = p;
        while (p < n && is_sym((unsigned char) s[p])) p++;
        for (size_t i = 0; i < sizeof hops / sizeof hops[0]; i++) {
            if (!same(s + a, p - a, hops[i].s)) continue;
            if (!hops[i].k) return lfail(h, ZL_E_UNSUPPORTED, a, hops[i].msg);
            t->k = hops[i].k;
            t->v = hops[i].v;
            t->len = (uint32_t) (p - a);
            l->pos = p;
            return ZL_OK;
        }
        return lfail(h, ZL_E_UNSUPPORTED, a, "this operator is not in this subset");
    }
    if (ch < 32 || ch > 126)
        return lfail(h, ZL_E_UNSUPPORTED, p, "only printable ASCII is in this subset");
    return lfail(h, ZL_E_SYNTAX, p, "unexpected character");
}

/* ------------------------------------------------------------ the layout rule */

static int is_layout_kw(int k)
{
    return k == HK_LET || k == HK_WHERE || k == HK_DO || k == HK_OF;
}

static void deliver_virtual(hp_t *h, int k)
{
    htok_t *t = &h->tok;
    *t = h->st.raw;
    t->k = k;
    t->len = 0;
    t->virt = 1;
}

static int push_ctx(hp_t *h, uint32_t col)
{
    if (h->st.nctx >= HS_CTX) return hfail(h, ZL_E_LIMIT, "blocks nested too deeply");
    h->st.ctx[h->st.nctx++] = (uint16_t) (col > 0xffff ? 0xffff : col);
    return ZL_OK;
}

/* the next token, braces and semicolons of the layout rule inserted */
static int lex(hp_t *h)
{
    hstate_t *l = &h->st;
    h->last_end = h->tok.virt ? h->last_end : h->tok.at + h->tok.len;
    if (!l->have_raw) {
        TRY(raw(h));
        l->have_raw = 1;
        l->raw_bol = l->raw.first;
    }
    htok_t *r = &l->raw;
    if (l->want_open) {
        l->want_open = 0;
        if (r->k == HT_LBRACE) {
            TRY(push_ctx(h, 0));
            h->tok = *r;
            l->have_raw = 0;
            return ZL_OK;
        }
        uint32_t n = r->k == HT_EOF ? 0 : r->col, m = l->nctx ? l->ctx[l->nctx - 1] : 0;
        deliver_virtual(h, HT_LBRACE);
        if (n > m) {
            TRY(push_ctx(h, n));
            l->raw_bol = 0;
        } else {
            l->pending_close = 1;
        }
        return ZL_OK;
    }
    if (l->pending_close) {
        l->pending_close = 0;
        l->raw_bol = 1;
        deliver_virtual(h, HT_RBRACE);
        return ZL_OK;
    }
    if (r->k == HT_EOF) {
        if (l->nctx) {
            if (l->ctx[l->nctx - 1] == 0)
                return hfail_at(h, ZL_E_SYNTAX, r->line, r->col, "expected } before the end");
            l->nctx--;
            deliver_virtual(h, HT_RBRACE);
            return ZL_OK;
        }
        h->tok = *r;
        return ZL_OK;
    }
    if (l->raw_bol) {
        uint32_t m = l->nctx ? l->ctx[l->nctx - 1] : 0;
        if (m > 0 && r->col == m) {
            l->raw_bol = 0;
            deliver_virtual(h, HT_SEMI);
            return ZL_OK;
        }
        if (m > 0 && r->col < m) {
            l->nctx--;
            deliver_virtual(h, HT_RBRACE);
            return ZL_OK;
        }
        l->raw_bol = 0;
    }
    l->have_raw = 0;
    h->tok = *r;
    if (is_layout_kw(r->k)) l->want_open = 1;
    return ZL_OK;
}

/* the end of a block: a } (explicit or inserted), or, by the parse-error
 * rule, the end of an implicit block before a token that cannot go on */
static int close_block(hp_t *h)
{
    hstate_t *l = &h->st;
    if (h->tok.k == HT_RBRACE && h->tok.virt) return lex(h);
    if (l->nctx && l->ctx[l->nctx - 1] > 0) {
        l->nctx--;
        return ZL_OK;
    }
    if (h->tok.k == HT_RBRACE && l->nctx && l->ctx[l->nctx - 1] == 0) {
        l->nctx--;
        return lex(h);
    }
    return hfail(h, ZL_E_SYNTAX, "expected the end of the block");
}

static int open_block(hp_t *h)
{
    if (h->tok.k != HT_LBRACE) return hfail(h, ZL_E_SYNTAX, "expected a block");
    return lex(h);
}

/* ------------------------------------------------------------ parser */

static int32_t node(hp_t *h, int kind, const htok_t *t)
{
    return zl_ml_node(W(h), kind, t->line, t->col);
}

static void name_of(hp_t *h, int32_t n, const htok_t *t)
{
    HN(h, n)->v = (int64_t) t->at;
    HN(h, n)->len = (uint16_t) t->len;
}

static int expect(hp_t *h, int k, const char *msg)
{
    if (h->tok.k != k) return hfail(h, ZL_E_SYNTAX, msg);
    return lex(h);
}

static int deeper(hp_t *h)
{
    if (++h->m.depth > M_DEPTH) return hfail(h, ZL_E_LIMIT, "expression nested too deeply");
    return ZL_OK;
}

typedef struct {
    int32_t head, tail;
    uint32_t n;
} hlist_t;

static void ladd(hp_t *h, hlist_t *l, int32_t n)
{
    if (l->head < 0)
        l->head = n;
    else
        HN(h, l->tail)->next = n;
    l->tail = n;
    l->n++;
}

static int is_name(hp_t *h, int32_t n, const char *s)
{
    return same(h->m.src + HN(h, n)->v, HN(h, n)->len, s);
}

static int tok_is(hp_t *h, const char *s)
{
    return same(h->m.src + h->tok.at, h->tok.len, s);
}

static int32_t p_exp(hp_t *h);
static int32_t p_pat(hp_t *h);
static int32_t p_decls(hp_t *h, int top);
static int32_t p_type(hp_t *h);

/* mark where IO actions run: a statement and what stands whole in it */
static void mark_stmt(hp_t *h, int32_t e)
{
    mnode_t *n = HN(h, e);
    n->flags |= F_STMT;
    switch (n->kind) {
    case E_IF:
        mark_stmt(h, n->b);
        mark_stmt(h, HN(h, e)->c);
        break;
    case E_CASE:
        for (int32_t r = n->b; r >= 0; r = HN(h, r)->next) mark_stmt(h, HN(h, r)->c);
        break;
    case E_LET:
        mark_stmt(h, n->b);
        break;
    case E_GUARDS:
        for (int32_t g = n->a; g >= 0; g = HN(h, g)->next) mark_stmt(h, HN(h, g)->b);
        break;
    case E_SEQ:
        for (int32_t q = n->a; q >= 0; q = HN(h, q)->next) mark_stmt(h, q);
        break;
    default:
        break;
    }
}

/* ---- types ---- */

static int32_t p_atype(hp_t *h)
{
    htok_t t = h->tok;
    int32_t n;
    TRY(deeper(h));
    switch (t.k) {
    case HT_CON:
        if (tok_is(h, "Int") || tok_is(h, "Bool")) {
            n = node(h, N_TYPE, &t);
            TRY(n);
            HN(h, n)->op = (uint8_t) (tok_is(h, "Int") ? TK_INT : TK_BOOL);
            TRY(lex(h));
            break;
        }
        if (tok_is(h, "Integer"))
            return hfail(h, ZL_E_UNSUPPORTED,
                         "Integer (arbitrary precision) is not in this subset (use Int)");
        if (tok_is(h, "IO"))
            return hfail(h, ZL_E_UNSUPPORTED, "IO is only main's type, IO (), in this subset");
        return hfail(h, ZL_E_UNSUPPORTED,
                     "this type is not in this subset (Int, Bool, (), lists, IO ())");
    case HT_VAR:
        n = node(h, N_TYPE, &t);
        TRY(n);
        HN(h, n)->op = TK_VAR;
        name_of(h, n, &t);
        TRY(lex(h));
        break;
    case HT_LP: {
        TRY(lex(h));
        if (h->tok.k == HT_RP) {
            n = node(h, N_TYPE, &t);
            TRY(n);
            HN(h, n)->op = TK_UNIT;
            TRY(lex(h));
            break;
        }
        int32_t a = p_type(h);
        TRY(a);
        n = a;
        if (h->tok.k == HT_COMMA) {
            n = node(h, N_TYPE, &t);
            TRY(n);
            HN(h, n)->op = TK_TUPLE;
            hlist_t l = {a, a, 1};
            while (h->tok.k == HT_COMMA) {
                TRY(lex(h));
                int32_t b = p_type(h);
                TRY(b);
                ladd(h, &l, b);
            }
            if (l.n > M_GROUPS) return hfail(h, ZL_E_LIMIT, "tuple too long");
            HN(h, n)->a = l.head;
            HN(h, n)->v = l.n;
        }
        TRY(expect(h, HT_RP, "expected ) to close the type"));
        break;
    }
    case HT_LB: {
        TRY(lex(h));
        int32_t a = p_type(h);
        TRY(a);
        TRY(expect(h, HT_RB, "expected ] to close the list type"));
        n = node(h, N_TYPE, &t);
        TRY(n);
        HN(h, n)->op = TK_LIST;
        HN(h, n)->a = a;
        break;
    }
    default:
        return hfail(h, ZL_E_SYNTAX, "expected a type");
    }
    h->m.depth--;
    return n;
}

static int32_t p_btype(hp_t *h)
{
    htok_t t = h->tok;
    if (t.k == HT_CON && tok_is(h, "IO")) {
        TRY(lex(h));
        int32_t a = p_atype(h);
        TRY(a);
        if (HN(h, a)->op != TK_UNIT)
            return nfail_h(h, a, ZL_E_UNSUPPORTED,
                           "IO of a value other than () is not in this subset");
        HN(h, a)->op = TK_IO;
        HN(h, a)->line = t.line;
        HN(h, a)->col = (uint16_t) t.col;
        return a;
    }
    int32_t a = p_atype(h);
    TRY(a);
    if (h->tok.k == HT_CON || h->tok.k == HT_VAR || h->tok.k == HT_LP || h->tok.k == HT_LB)
        return hfail(h, ZL_E_UNSUPPORTED,
                     "type constructors applied to types are not in this subset");
    return a;
}

static int32_t p_type(hp_t *h)
{
    htok_t t = h->tok;
    TRY(deeper(h));
    int32_t a = p_btype(h);
    TRY(a);
    if (h->tok.k == HT_ARROW) {
        TRY(lex(h));
        int32_t b = p_type(h);
        TRY(b);
        int32_t f = node(h, N_TYPE, &t);
        TRY(f);
        HN(h, f)->op = TK_FUN;
        HN(h, f)->a = a;
        HN(h, f)->b = b;
        a = f;
    }
    h->m.depth--;
    return a;
}

/* ---- patterns ---- */

static int32_t lit_pat(hp_t *h, const htok_t *t, int neg)
{
    int32_t n = node(h, P_INT, t);
    TRY(n);
    HN(h, n)->v = neg ? -h->tok.v : h->tok.v;
    TRY(lex(h));
    return n;
}

static int starts_apat(int k)
{
    return k == HT_VAR || k == HT_WILD || k == HT_INT || k == HT_CON || k == HT_LP || k == HT_LB ||
           k == HT_STR;
}

static int32_t p_apat(hp_t *h)
{
    htok_t t = h->tok;
    int32_t n;
    switch (t.k) {
    case HT_VAR:
        n = node(h, P_VAR, &t);
        TRY(n);
        name_of(h, n, &t);
        TRY(lex(h));
        return n;
    case HT_WILD:
        n = node(h, P_WILD, &t);
        TRY(n);
        TRY(lex(h));
        return n;
    case HT_INT:
        return lit_pat(h, &t, 0);
    case HT_CON:
        if (tok_is(h, "True") || tok_is(h, "False")) {
            n = node(h, P_BOOL, &t);
            TRY(n);
            HN(h, n)->v = tok_is(h, "True");
            TRY(lex(h));
            return n;
        }
        return hfail(h, ZL_E_UNSUPPORTED, "data constructors are not in this subset");
    case HT_STR:
        return hfail(h, ZL_E_UNSUPPORTED, "string patterns are not in this subset");
    case HT_LP: {
        TRY(lex(h));
        if (h->tok.k == HT_RP) {
            n = node(h, P_UNIT, &t);
            TRY(n);
            TRY(lex(h));
            return n;
        }
        TRY(deeper(h));
        int32_t p = p_pat(h);
        TRY(p);
        if (h->tok.k == HT_COMMA) {
            n = node(h, P_TUPLE, &t);
            TRY(n);
            hlist_t l = {p, p, 1};
            while (h->tok.k == HT_COMMA) {
                TRY(lex(h));
                int32_t q = p_pat(h);
                TRY(q);
                ladd(h, &l, q);
            }
            if (l.n > M_GROUPS) return hfail(h, ZL_E_LIMIT, "tuple too long");
            HN(h, n)->a = l.head;
            HN(h, n)->v = l.n;
            p = n;
        }
        h->m.depth--;
        TRY(expect(h, HT_RP, "expected ) to close the pattern"));
        return p;
    }
    case HT_LB: {
        h->m.lists = 1;
        TRY(lex(h));
        if (h->tok.k == HT_RB) {
            n = node(h, P_NIL, &t);
            TRY(n);
            TRY(lex(h));
            return n;
        }
        TRY(deeper(h));
        n = node(h, P_LIST, &t);
        TRY(n);
        hlist_t l = {-1, -1, 0};
        for (;;) {
            int32_t q = p_pat(h);
            TRY(q);
            ladd(h, &l, q);
            if (h->tok.k != HT_COMMA) break;
            TRY(lex(h));
        }
        HN(h, n)->a = l.head;
        HN(h, n)->v = l.n;
        h->m.depth--;
        TRY(expect(h, HT_RB, "expected ] to close the list pattern"));
        return n;
    }
    default:
        break;
    }
    return hfail(h, ZL_E_SYNTAX, "expected a pattern");
}

/* pat := [-] integer | apat [: pat] */
static int32_t p_pat(hp_t *h)
{
    htok_t t = h->tok;
    int32_t a;
    TRY(deeper(h));
    if (t.k == HT_MINUS) {
        TRY(lex(h));
        if (h->tok.k != HT_INT) return hfail(h, ZL_E_SYNTAX, "a number after - in a pattern");
        a = lit_pat(h, &t, 1);
    } else {
        a = p_apat(h);
    }
    TRY(a);
    if (h->tok.k == HT_OP && h->tok.v == O_CONS) {
        htok_t c = h->tok;
        h->m.lists = 1;
        TRY(lex(h));
        int32_t b = p_pat(h);
        TRY(b);
        int32_t n = node(h, P_CONS, &c);
        TRY(n);
        HN(h, n)->a = a;
        HN(h, n)->b = b;
        a = n;
    } else if (h->tok.k == HT_OP || h->tok.k == HT_BTICK || h->tok.k == HT_MINUS) {
        return hfail(h, ZL_E_UNSUPPORTED, "this operator in a pattern is not in this subset");
    }
    h->m.depth--;
    return a;
}

/* ---- expressions ---- */

typedef struct {
    int prec, assoc; /* assoc: 0 left, 1 right, 2 none */
} hfix_t;

static int fixity(hp_t *h, hfix_t *f)
{
    const htok_t *t = &h->tok;
    f->assoc = 0;
    if (t->k == HT_MINUS) {
        f->prec = 6;
        return 1;
    }
    if (t->k == HT_BTICK) {
        if (tok_is(h, "div") || tok_is(h, "mod") || tok_is(h, "quot") || tok_is(h, "rem"))
            f->prec = 7;
        else if (tok_is(h, "elem") || tok_is(h, "notElem")) {
            f->prec = 4;
            f->assoc = 2;
        } else {
            f->prec = 9;
        }
        return 1;
    }
    if (t->k != HT_OP) return 0;
    switch (t->v) {
    case O_INDEX:
        f->prec = 9;
        break;
    case O_POW:
        f->prec = 8;
        f->assoc = 1;
        break;
    case O_MUL:
        f->prec = 7;
        break;
    case O_ADD:
        f->prec = 6;
        break;
    case O_CONS:
    case O_APPEND:
        f->prec = 5;
        f->assoc = 1;
        break;
    case O_ANDALSO:
        f->prec = 3;
        f->assoc = 1;
        break;
    case O_ORELSE:
        f->prec = 2;
        f->assoc = 1;
        break;
    case OP_DOLLAR:
        f->prec = 0;
        f->assoc = 1;
        break;
    default: /* comparisons */
        f->prec = 4;
        f->assoc = 2;
        break;
    }
    return 1;
}

static int div_family(hp_t *h, int32_t n, int *op)
{
    if (is_name(h, n, "div"))
        *op = O_DIV;
    else if (is_name(h, n, "mod"))
        *op = O_MOD;
    else if (is_name(h, n, "quot"))
        *op = O_QUOT;
    else if (is_name(h, n, "rem"))
        *op = O_REM;
    else
        return 0;
    return 1;
}

/* an application of div, mod, quot or rem to two arguments is the operator */
static int32_t finish_app(hp_t *h, int32_t e)
{
    int op;
    mnode_t *n = HN(h, e);
    if (n->kind != E_APP || n->a < 0 || HN(h, n->a)->next < 0 ||
        HN(h, HN(h, n->a)->next)->next >= 0 || !div_family(h, e, &op))
        return e;
    int32_t a = n->a, b = HN(h, a)->next;
    HN(h, a)->next = -1;
    n->kind = E_BIN;
    n->op = (uint8_t) op;
    n->a = a;
    n->b = b;
    return e;
}

static int32_t fold_neg(hp_t *h, int32_t e, const htok_t *t)
{
    if (HN(h, e)->kind == E_INT) {
        HN(h, e)->v = -HN(h, e)->v;
        HN(h, e)->line = t->line;
        HN(h, e)->col = (uint16_t) t->col;
        return e;
    }
    int32_t n = node(h, E_NEG, t);
    TRY(n);
    HN(h, n)->a = e;
    return n;
}

static int starts_aexp(int k)
{
    return k == HT_VAR || k == HT_CON || k == HT_INT || k == HT_STR || k == HT_LP || k == HT_LB ||
           k == HT_WILD;
}

static int32_t p_aexp(hp_t *h)
{
    htok_t t = h->tok;
    int32_t n;
    switch (t.k) {
    case HT_VAR:
        n = node(h, E_VAR, &t);
        TRY(n);
        name_of(h, n, &t);
        TRY(lex(h));
        return n;
    case HT_CON:
        if (tok_is(h, "True") || tok_is(h, "False")) {
            n = node(h, E_BOOL, &t);
            TRY(n);
            HN(h, n)->v = tok_is(h, "True");
            TRY(lex(h));
            return n;
        }
        return hfail(h, ZL_E_UNSUPPORTED, "data constructors are not in this subset");
    case HT_INT:
        n = node(h, E_INT, &t);
        TRY(n);
        HN(h, n)->v = t.v;
        TRY(lex(h));
        return n;
    case HT_STR:
        n = node(h, E_STR, &t);
        TRY(n);
        name_of(h, n, &t);
        TRY(lex(h));
        return n;
    case HT_WILD:
        return hfail(h, ZL_E_SYNTAX, "a hole (_) in an expression");
    case HT_LP: {
        TRY(lex(h));
        if (h->tok.k == HT_RP) {
            n = node(h, E_UNIT, &t);
            TRY(n);
            TRY(lex(h));
            return n;
        }
        if (h->tok.k == HT_OP || h->tok.k == HT_BTICK)
            return hfail(h, ZL_E_UNSUPPORTED,
                         "operator sections and operators as values are not in this subset");
        TRY(deeper(h));
        int32_t e = p_exp(h);
        TRY(e);
        n = e;
        if (h->tok.k == HT_COMMA) {
            n = node(h, E_TUPLE, &t);
            TRY(n);
            hlist_t l = {e, e, 1};
            while (h->tok.k == HT_COMMA) {
                TRY(lex(h));
                if (h->tok.k == HT_RP || h->tok.k == HT_COMMA)
                    return hfail(h, ZL_E_UNSUPPORTED, "tuple sections are not in this subset");
                int32_t q = p_exp(h);
                TRY(q);
                ladd(h, &l, q);
            }
            if (l.n > M_GROUPS) return hfail(h, ZL_E_LIMIT, "tuple too long");
            HN(h, n)->a = l.head;
            HN(h, n)->v = l.n;
        }
        h->m.depth--;
        TRY(expect(h, HT_RP, "expected )"));
        return n;
    }
    case HT_LB: {
        h->m.lists = 1;
        TRY(lex(h));
        if (h->tok.k == HT_RB) {
            n = node(h, E_NIL, &t);
            TRY(n);
            TRY(lex(h));
            return n;
        }
        TRY(deeper(h));
        int32_t e = p_exp(h);
        TRY(e);
        hlist_t l = {e, e, 1};
        int range = 0;
        if (h->tok.k == HT_COMMA) {
            TRY(lex(h));
            int32_t q = p_exp(h);
            TRY(q);
            ladd(h, &l, q);
            if (h->tok.k == HT_DOTDOT) range = 1;
        } else if (h->tok.k == HT_DOTDOT) {
            range = 1;
        }
        if (range) {
            TRY(lex(h));
            if (h->tok.k == HT_RB)
                return hfail(h, ZL_E_UNSUPPORTED, "infinite lists ([a ..]) are not in this subset");
            int32_t q = p_exp(h);
            TRY(q);
            ladd(h, &l, q);
            n = node(h, E_RANGE, &t);
            TRY(n);
        } else {
            if (h->tok.k == HT_BAR)
                return hfail(h, ZL_E_UNSUPPORTED, "list comprehensions are not in this subset");
            while (h->tok.k == HT_COMMA) {
                TRY(lex(h));
                int32_t q = p_exp(h);
                TRY(q);
                ladd(h, &l, q);
            }
            n = node(h, E_LIST, &t);
            TRY(n);
        }
        HN(h, n)->a = l.head;
        HN(h, n)->v = l.n;
        h->m.depth--;
        TRY(expect(h, HT_RB, "expected ] to close the list"));
        return n;
    }
    default:
        break;
    }
    return hfail(h, ZL_E_SYNTAX, "expected an expression");
}

/* application: a named function and its arguments */
static int32_t p_fexp(hp_t *h)
{
    int32_t f = p_aexp(h);
    TRY(f);
    if (!starts_aexp(h->tok.k)) return f;
    int k = HN(h, f)->kind;
    if (k != E_VAR) {
        if (k == E_INT || k == E_BOOL || k == E_UNIT || k == E_NIL || k == E_LIST || k == E_STR ||
            k == E_TUPLE || k == E_RANGE)
            return hfail(h, ZL_E_SYNTAX, "type error: a value that is not a function is applied");
        return hfail(h, ZL_E_UNSUPPORTED, "only a named function can be applied");
    }
    HN(h, f)->kind = E_APP;
    hlist_t l = {-1, -1, 0};
    while (starts_aexp(h->tok.k)) {
        int32_t a = p_aexp(h);
        TRY(a);
        ladd(h, &l, a);
        if (l.n > M_GROUPS) return hfail(h, ZL_E_LIMIT, "too many arguments");
    }
    HN(h, f)->a = l.head;
    return finish_app(h, f);
}

static int32_t p_stmts(hp_t *h, const htok_t *t);

static int32_t p_lexp(hp_t *h)
{
    htok_t t = h->tok;
    int32_t n;
    switch (t.k) {
    case HK_LET: {
        TRY(lex(h));
        TRY(open_block(h));
        int32_t d = p_decls(h, 0);
        TRY(d);
        TRY(expect(h, HK_IN, "expected in after the declarations of let"));
        int32_t b = p_exp(h);
        TRY(b);
        n = d;
        HN(h, n)->b = b;
        HN(h, n)->line = t.line;
        HN(h, n)->col = (uint16_t) t.col;
        return n;
    }
    case HK_IF: {
        TRY(lex(h));
        int32_t c = p_exp(h);
        TRY(c);
        if (h->tok.k == HT_SEMI) TRY(lex(h)); /* DoAndIfThenElse */
        TRY(expect(h, HK_THEN, "expected then"));
        int32_t a = p_exp(h);
        TRY(a);
        if (h->tok.k == HT_SEMI) TRY(lex(h));
        TRY(expect(h, HK_ELSE, "expected else (if needs both branches)"));
        int32_t b = p_exp(h);
        TRY(b);
        n = node(h, E_IF, &t);
        TRY(n);
        HN(h, n)->a = c;
        HN(h, n)->b = a;
        HN(h, n)->c = b;
        return n;
    }
    case HK_CASE: {
        TRY(lex(h));
        int32_t s = p_exp(h);
        TRY(s);
        TRY(expect(h, HK_OF, "expected of"));
        TRY(open_block(h));
        hlist_t l = {-1, -1, 0};
        for (;;) {
            while (h->tok.k == HT_SEMI) TRY(lex(h));
            if (h->tok.k == HT_RBRACE || h->tok.k == HT_EOF ||
                !(starts_apat(h->tok.k) || h->tok.k == HT_MINUS))
                break;
            htok_t rt = h->tok;
            int32_t r = node(h, N_RULE, &rt);
            TRY(r);
            int32_t p = p_pat(h);
            TRY(p);
            int32_t e;
            if (h->tok.k == HT_BAR) {
                e = node(h, E_GUARDS, &h->tok);
                TRY(e);
                hlist_t g = {-1, -1, 0};
                while (h->tok.k == HT_BAR) {
                    htok_t gt = h->tok;
                    TRY(lex(h));
                    int32_t c = p_exp(h);
                    TRY(c);
                    if (h->tok.k == HT_COMMA)
                        return hfail(h, ZL_E_UNSUPPORTED,
                                     "several guards separated by commas are not in this subset");
                    if (h->tok.k == HT_LARROW)
                        return hfail(h, ZL_E_UNSUPPORTED, "pattern guards are not in this subset");
                    TRY(expect(h, HT_ARROW, "expected -> after the guard"));
                    int32_t b = p_exp(h);
                    TRY(b);
                    int32_t ga = node(h, N_GALT, &gt);
                    TRY(ga);
                    HN(h, ga)->a = c;
                    HN(h, ga)->b = b;
                    ladd(h, &g, ga);
                    if (g.n > 64) return hfail(h, ZL_E_LIMIT, "too many guards");
                }
                HN(h, e)->a = g.head;
            } else {
                TRY(expect(h, HT_ARROW, "expected -> in a case alternative"));
                e = p_exp(h);
                TRY(e);
            }
            if (h->tok.k == HK_WHERE)
                return hfail(h, ZL_E_UNSUPPORTED,
                             "where on a case alternative is not in this subset");
            HN(h, r)->a = p;
            HN(h, r)->c = e;
            ladd(h, &l, r);
            if (h->tok.k != HT_SEMI) break;
        }
        if (l.head < 0) return hfail(h, ZL_E_SYNTAX, "a case needs at least one alternative");
        TRY(close_block(h));
        n = node(h, E_CASE, &t);
        TRY(n);
        HN(h, n)->a = s;
        HN(h, n)->b = l.head;
        return n;
    }
    case HK_DO: {
        TRY(lex(h));
        TRY(open_block(h));
        n = p_stmts(h, &t);
        TRY(n);
        TRY(close_block(h));
        return n;
    }
    default:
        break;
    }
    return p_fexp(h);
}

static int32_t mk_op(hp_t *h, const htok_t *t, int32_t a, int32_t b)
{
    int32_t n;
    if (t->k == HT_OP && t->v == OP_DOLLAR) { /* f a $ b is f a b */
        int k = HN(h, a)->kind;
        if (k == E_VAR) {
            HN(h, a)->kind = E_APP;
            HN(h, a)->a = b;
            return finish_app(h, a);
        }
        if (k == E_APP) {
            uint32_t c = 1;
            int32_t q = HN(h, a)->a;
            while (HN(h, q)->next >= 0) q = HN(h, q)->next, c++;
            if (c >= M_GROUPS)
                return hfail_at(h, ZL_E_LIMIT, t->line, t->col, "too many arguments");
            HN(h, q)->next = b;
            return finish_app(h, a);
        }
        return hfail_at(h, ZL_E_UNSUPPORTED, t->line, t->col,
                        "$ needs a named function on its left in this subset");
    }
    if (t->k == HT_BTICK) {
        htok_t nt = *t;
        n = node(h, E_APP, &nt);
        TRY(n);
        name_of(h, n, &nt);
        HN(h, n)->a = a;
        HN(h, a)->next = b;
        return finish_app(h, n);
    }
    n = node(h, E_BIN, t);
    TRY(n);
    HN(h, n)->op = (uint8_t) (t->k == HT_MINUS ? O_SUB : t->v);
    HN(h, n)->a = a;
    HN(h, n)->b = b;
    return n;
}

/* operator expressions by precedence climbing; a minus sign before an
 * operand is negation at precedence 6, as the Haskell report fixes it */
static int32_t p_infix(hp_t *h, int minp)
{
    TRY(deeper(h));
    int32_t a;
    int lastp = -1, lasta = 0;
    if (h->tok.k == HT_MINUS) {
        htok_t t = h->tok;
        if (minp > 6) return hfail(h, ZL_E_SYNTAX, "a minus sign here needs parentheses");
        TRY(lex(h));
        int32_t e = p_infix(h, 7);
        TRY(e);
        a = fold_neg(h, e, &t);
        TRY(a);
        lastp = 6;
    } else {
        a = p_lexp(h);
        TRY(a);
    }
    for (;;) {
        hfix_t f;
        if (!fixity(h, &f) || f.prec < minp) break;
        if (f.prec == lastp && (f.assoc != lasta || f.assoc == 2))
            return hfail(h, ZL_E_SYNTAX,
                         "operators of the same precedence that do not associate need parentheses");
        htok_t t = h->tok;
        TRY(lex(h));
        if (h->tok.k == HT_RP)
            return hfail(h, ZL_E_UNSUPPORTED, "operator sections are not in this subset");
        int32_t b = p_infix(h, f.assoc == 1 ? f.prec : f.prec + 1);
        TRY(b);
        a = mk_op(h, &t, a, b);
        TRY(a);
        lastp = f.prec;
        lasta = f.assoc;
    }
    h->m.depth--;
    return a;
}

static int32_t p_exp(hp_t *h)
{
    int32_t e = p_infix(h, 0);
    TRY(e);
    if (h->tok.k == HT_DCOLON)
        return hfail(h, ZL_E_UNSUPPORTED, "type annotations in expressions are not in this subset");
    return e;
}

/* the statements of a do block, up to its end (the caller closes it); a
 * let statement takes the statements after it as its body */
static int32_t p_stmts(hp_t *h, const htok_t *t)
{
    TRY(deeper(h));
    int32_t s = node(h, E_SEQ, t);
    TRY(s);
    HN(h, s)->flags |= F_DO;
    hlist_t l = {-1, -1, 0};
    for (;;) {
        while (h->tok.k == HT_SEMI) TRY(lex(h));
        if (h->tok.k == HT_RBRACE || h->tok.k == HT_EOF) break;
        /* the parse-error rule: a token no statement starts with ends the block */
        if (!starts_aexp(h->tok.k) && h->tok.k != HK_LET && h->tok.k != HK_IF &&
            h->tok.k != HK_CASE && h->tok.k != HK_DO && h->tok.k != HT_MINUS && h->tok.k != HT_OP)
            break;
        htok_t st = h->tok;
        int32_t e;
        if (st.k == HK_LET) {
            TRY(lex(h));
            TRY(open_block(h));
            e = p_decls(h, 0);
            TRY(e);
            if (h->tok.k == HK_IN) { /* an expression statement let ... in e */
                TRY(lex(h));
                int32_t b = p_exp(h);
                TRY(b);
                HN(h, e)->b = b;
            } else {
                HN(h, e)->op = 2;
                HN(h, e)->line = st.line;
                HN(h, e)->col = (uint16_t) st.col;
                if (h->tok.k == HT_SEMI) TRY(lex(h));
                while (h->tok.k == HT_SEMI) TRY(lex(h));
                if (h->tok.k == HT_RBRACE || h->tok.k == HT_EOF || h->tok.k == HK_IN)
                    return hfail_at(h, ZL_E_SYNTAX, st.line, st.col,
                                    "the last statement in a do block must be an expression");
                int32_t rest = p_stmts(h, &h->tok);
                TRY(rest);
                HN(h, e)->b = rest;
                ladd(h, &l, e);
                break;
            }
        } else {
            e = p_exp(h);
            TRY(e);
            if (h->tok.k == HT_LARROW)
                return hfail(h, ZL_E_UNSUPPORTED,
                             "binding the result of an action (<-) is not in this subset");
        }
        ladd(h, &l, e);
        if (l.n > 4096) return hfail(h, ZL_E_LIMIT, "too many statements");
        if (h->tok.k != HT_SEMI) break;
    }
    if (l.head < 0) return hfail_at(h, ZL_E_SYNTAX, t->line, t->col, "an empty do block");
    HN(h, s)->a = l.head;
    HN(h, s)->v = l.n;
    mark_stmt(h, s);
    h->m.depth--;
    return s;
}

/* ---- declarations ---- */

/* rhs := = e | (| g = e)+, then where decls */
static int32_t p_rhs(hp_t *h)
{
    htok_t t = h->tok;
    int32_t e;
    if (t.k == HT_BAR) {
        e = node(h, E_GUARDS, &t);
        TRY(e);
        hlist_t g = {-1, -1, 0};
        while (h->tok.k == HT_BAR) {
            htok_t gt = h->tok;
            TRY(lex(h));
            int32_t c = p_exp(h);
            TRY(c);
            if (h->tok.k == HT_COMMA)
                return hfail(h, ZL_E_UNSUPPORTED,
                             "several guards separated by commas are not in this subset");
            if (h->tok.k == HT_LARROW)
                return hfail(h, ZL_E_UNSUPPORTED, "pattern guards are not in this subset");
            TRY(expect(h, HT_EQ, "expected = after the guard"));
            int32_t b = p_exp(h);
            TRY(b);
            int32_t ga = node(h, N_GALT, &gt);
            TRY(ga);
            HN(h, ga)->a = c;
            HN(h, ga)->b = b;
            ladd(h, &g, ga);
            if (g.n > 64) return hfail(h, ZL_E_LIMIT, "too many guards");
        }
        HN(h, e)->a = g.head;
    } else {
        TRY(expect(h, HT_EQ, "expected = or a guard"));
        e = p_exp(h);
        TRY(e);
    }
    if (h->tok.k == HK_WHERE) {
        htok_t wt = h->tok;
        TRY(lex(h));
        TRY(open_block(h));
        int32_t d = p_decls(h, 0);
        TRY(d);
        HN(h, d)->op = 1;
        HN(h, d)->b = e;
        HN(h, d)->line = wt.line;
        HN(h, d)->col = (uint16_t) wt.col;
        e = d;
    }
    return e;
}

/* a pattern variable bound twice in one equation */
static int dup_vars(hp_t *h, int32_t p, int32_t *seen, uint32_t *n)
{
    for (; p >= 0; p = HN(h, p)->next) {
        mnode_t *x = HN(h, p);
        if (x->kind == P_VAR) {
            for (uint32_t i = 0; i < *n; i++)
                if (HN(h, seen[i])->len == x->len && same(h->m.src + x->v, x->len, "") == 0 &&
                    is_name(h, seen[i], "") == 0) {
                    const char *a = h->m.src + HN(h, seen[i])->v, *b = h->m.src + x->v;
                    uint32_t k = 0;
                    while (k < x->len && a[k] == b[k]) k++;
                    if (k == x->len)
                        return nfail_h(h, p, ZL_E_SYNTAX, "a variable bound twice in one pattern");
                }
            if (*n >= 64) return nfail_h(h, p, ZL_E_LIMIT, "too many variables in one pattern");
            seen[(*n)++] = p;
        } else if (x->kind == P_CONS) {
            int32_t a = x->a;
            HN(h, a)->next = -1;
            TRY(dup_vars(h, a, seen, n));
            TRY(dup_vars(h, HN(h, p)->b, seen, n));
        } else if (x->kind == P_LIST || x->kind == P_TUPLE) {
            TRY(dup_vars(h, x->a, seen, n));
        }
    }
    return ZL_OK;
}

static int32_t item(hp_t *h, hlist_t *items, int32_t x)
{
    int32_t it = zl_ml_node(W(h), N_ITEM, HN(h, x)->line, HN(h, x)->col);
    TRY(it);
    HN(h, it)->a = x;
    ladd(h, items, it);
    return it;
}

static int same_node_name(hp_t *h, int32_t a, int32_t b)
{
    mnode_t *x = HN(h, a), *y = HN(h, b);
    if (x->len != y->len) return 0;
    for (uint32_t i = 0; i < x->len; i++)
        if (h->m.src[x->v + i] != h->m.src[y->v + i]) return 0;
    return 1;
}

static int32_t p_group(hp_t *h, int32_t items, int top);

/* comments before `upto` that start after `from` become top-level items */
static int take_comments(hp_t *h, hlist_t *items, size_t from, size_t upto)
{
    mw_t *w = W(h);
    for (; w->ncomuse < w->ncom && w->coms[w->ncomuse].at < upto; w->ncomuse++) {
        if (w->coms[w->ncomuse].at < from) continue;
        int32_t n = zl_ml_node(w, N_COMMENT, w->coms[w->ncomuse].line, 1);
        TRY(n);
        HN(h, n)->v = (int64_t) w->coms[w->ncomuse].at;
        HN(h, n)->sym = (int32_t) w->coms[w->ncomuse].len;
        TRY(item(h, items, n));
    }
    return ZL_OK;
}

/* declarations up to the end of the block (which this closes); returns
 * an E_LET whose c is the source order and a the dependency order */
static int32_t p_decls(hp_t *h, int top)
{
    htok_t t0 = h->tok;
    hlist_t items = {-1, -1, 0};
    int32_t lastfb = -1; /* the function the last equation defined */
    size_t prev_end = 0;
    TRY(deeper(h));
    for (;;) {
        while (h->tok.k == HT_SEMI) TRY(lex(h));
        if (top)
            TRY(take_comments(h, &items, prev_end,
                              h->tok.k == HT_EOF || h->tok.k == HT_RBRACE ? h->m.len + 1
                                                                          : h->tok.at));
        if (h->tok.k != HT_VAR) {
            int k = h->tok.k;
            if (k == HT_LP || k == HT_LB || k == HT_WILD || k == HT_INT || k == HT_MINUS ||
                k == HT_STR)
                return hfail(h, ZL_E_UNSUPPORTED, "pattern bindings are not in this subset");
            if (k == HT_CON)
                return hfail(h, ZL_E_UNSUPPORTED,
                             "pattern bindings of constructors are not in this subset");
            break;
        }
        htok_t nt = h->tok;
        TRY(lex(h));
        if (h->tok.k == HT_COMMA || h->tok.k == HT_DCOLON) { /* a type signature */
            int32_t sg = node(h, N_SIG, &nt);
            TRY(sg);
            int32_t v = node(h, P_VAR, &nt);
            TRY(v);
            name_of(h, v, &nt);
            hlist_t names = {v, v, 1};
            while (h->tok.k == HT_COMMA) {
                TRY(lex(h));
                if (h->tok.k != HT_VAR)
                    return hfail(h, ZL_E_SYNTAX, "expected a name in the signature");
                int32_t q = node(h, P_VAR, &h->tok);
                TRY(q);
                name_of(h, q, &h->tok);
                ladd(h, &names, q);
                TRY(lex(h));
            }
            TRY(expect(h, HT_DCOLON, "expected ::"));
            int32_t ty = p_type(h);
            TRY(ty);
            HN(h, sg)->a = names.head;
            HN(h, sg)->b = ty;
            TRY(item(h, &items, sg));
            lastfb = -1;
        } else if (h->tok.k == HT_OP || h->tok.k == HT_BTICK || h->tok.k == HT_MINUS) {
            return hfail(h, ZL_E_UNSUPPORTED,
                         "operator definitions and infix pattern bindings are not in this subset");
        } else {
            /* an equation: name apat* rhs */
            hlist_t ps = {-1, -1, 0};
            while (starts_apat(h->tok.k)) {
                int32_t p = p_apat(h);
                TRY(p);
                ladd(h, &ps, p);
                if (ps.n > M_GROUPS) return hfail(h, ZL_E_LIMIT, "too many parameters");
            }
            if (h->tok.k != HT_EQ && h->tok.k != HT_BAR)
                return hfail(h, ZL_E_SYNTAX, "expected = or a guard");
            int32_t seen[64];
            uint32_t ns = 0;
            if (ps.n) {
                /* check duplicate variables without disturbing the list */
                for (int32_t p = ps.head; p >= 0; p = HN(h, p)->next) {
                    int32_t nx = HN(h, p)->next;
                    HN(h, p)->next = -1;
                    int s = dup_vars(h, p, seen, &ns);
                    HN(h, p)->next = nx;
                    TRY(s);
                }
            }
            int32_t rhs = p_rhs(h);
            TRY(rhs);
            int32_t nm = node(h, P_VAR, &nt);
            TRY(nm);
            name_of(h, nm, &nt);
            if (ps.n == 0) {
                int32_t d = node(h, D_VAL, &nt);
                TRY(d);
                HN(h, d)->a = nm;
                HN(h, d)->b = rhs;
                HN(h, d)->flags |= F_LAZY;
                if (top && is_name(h, nm, "main")) {
                    HN(h, d)->flags = F_TOP;
                    if (HN(h, rhs)->kind == E_GUARDS ||
                        (HN(h, rhs)->kind == E_LET && HN(h, HN(h, rhs)->b)->kind == E_GUARDS))
                        return nfail_h(h, rhs, ZL_E_UNSUPPORTED,
                                       "guards on main are not in this subset");
                }
                TRY(item(h, &items, d));
                lastfb = -1;
            } else {
                int32_t c = node(h, N_CLAUSE, &nt);
                TRY(c);
                HN(h, c)->a = ps.head;
                HN(h, c)->b = rhs;
                HN(h, c)->v = ps.n;
                mark_stmt(h, rhs);
                if (lastfb >= 0 && same_node_name(h, lastfb, nm)) {
                    int32_t q = HN(h, lastfb)->a;
                    while (HN(h, q)->next >= 0) q = HN(h, q)->next;
                    if (HN(h, q)->v != ps.n)
                        return hfail_at(
                            h, ZL_E_SYNTAX, nt.line, nt.col,
                            "the equations of a function have different numbers of arguments");
                    HN(h, q)->next = c;
                } else {
                    int32_t fb = node(h, N_FB, &nt);
                    TRY(fb);
                    name_of(h, fb, &nt);
                    HN(h, fb)->a = c;
                    TRY(item(h, &items, fb));
                    lastfb = fb;
                }
            }
        }
        prev_end = h->last_end;
        if (h->tok.k != HT_SEMI) break;
    }
    if (top)
        TRY(take_comments(h, &items, prev_end,
                          h->tok.k == HT_EOF || h->tok.k == HT_RBRACE ? h->m.len + 1 : h->tok.at));
    TRY(close_block(h));
    int32_t let = node(h, E_LET, &t0);
    TRY(let);
    HN(h, let)->c = items.head;
    HN(h, let)->a = p_group(h, items.head, top);
    TRY(HN(h, let)->a == -1 ? 0 : HN(h, let)->a);
    if (h->m.c->failed) return h->m.c->d->status;
    h->m.depth--;
    return let;
}

/* ---- binding groups: dependency order ---- */

static int bind_name(hp_t *h, int32_t b, int32_t *nm)
{
    *nm = HN(h, b)->kind == N_FB ? b : HN(h, b)->a;
    return ZL_OK;
}

/* the group members whose names the tree under n uses */
static void scan(hp_t *h, int32_t n, uint32_t i, uint32_t depth)
{
    for (; n >= 0; n = HN(h, n)->next) {
        mnode_t *x = HN(h, n);
        if (x->kind == N_TYPE || x->kind == N_ITEM || x->kind == N_SIG || x->kind == N_COMMENT)
            continue;
        if (x->kind == E_VAR || x->kind == E_APP) {
            for (uint32_t j = 0; j < h->ng; j++) {
                int32_t nm;
                bind_name(h, h->gb[j], &nm);
                if (same_node_name(h, nm, n)) h->reach[i][j / 64] |= (uint64_t) 1 << (j % 64);
            }
        }
        if (depth > 4 * M_DEPTH) continue;
        if (x->a >= 0) scan(h, x->a, i, depth + 1);
        if (x->b >= 0) scan(h, x->b, i, depth + 1);
        if (x->c >= 0 && x->kind != E_LET) scan(h, x->c, i, depth + 1);
    }
}

static int reaches(hp_t *h, uint32_t i, uint32_t j)
{
    return (h->reach[i][j / 64] >> (j % 64)) & 1;
}

static uint32_t reach_count(hp_t *h, uint32_t i)
{
    uint32_t c = 0;
    for (uint32_t j = 0; j < h->ng; j++) c += reaches(h, i, j) || i == j;
    return c;
}

static int32_t p_group(hp_t *h, int32_t items, int top)
{
    mw_t *w = W(h);
    h->ng = 0;
    int32_t main_d = -1;
    /* the bindings */
    for (int32_t it = items; it >= 0; it = HN(h, it)->next) {
        int32_t x = HN(h, it)->a;
        if (HN(h, x)->kind == D_VAL && (HN(h, x)->flags & F_TOP)) {
            if (main_d >= 0) return nfail_h(h, x, ZL_E_SYNTAX, "main is defined twice");
            main_d = x;
            continue;
        }
        if (HN(h, x)->kind != N_FB && HN(h, x)->kind != D_VAL) continue;
        int32_t nm;
        bind_name(h, x, &nm);
        for (uint32_t j = 0; j < h->ng; j++) {
            int32_t m;
            bind_name(h, h->gb[j], &m);
            if (same_node_name(h, m, nm))
                return nfail_h(h, x, ZL_E_SYNTAX, "conflicting definitions of one name");
        }
        if (top && zl_ml_prelude(w, (size_t) HN(h, nm)->v, HN(h, nm)->len))
            return nfail_h(h, x, ZL_E_UNSUPPORTED,
                           "a top-level name the Prelude also defines (ambiguous in GHC) is not in "
                           "this subset");
        if (is_name(h, nm, "div") || is_name(h, nm, "mod") || is_name(h, nm, "quot") ||
            is_name(h, nm, "rem"))
            return nfail_h(h, x, ZL_E_UNSUPPORTED,
                           "redefining div, mod, quot or rem is not in this subset");
        if (h->ng >= HS_GROUP)
            return nfail_h(h, x, ZL_E_LIMIT, "too many declarations in one group");
        h->gb[h->ng++] = x;
    }
    if (top && main_d < 0) return hfail(h, ZL_E_UNDEFINED, "main is not defined");
    /* signatures */
    for (int32_t it = items; it >= 0; it = HN(h, it)->next) {
        int32_t x = HN(h, it)->a;
        if (HN(h, x)->kind != N_SIG) continue;
        for (int32_t v = HN(h, x)->a; v >= 0; v = HN(h, v)->next) {
            if (top && is_name(h, v, "main")) {
                if (HN(h, HN(h, x)->b)->op != TK_IO)
                    return nfail_h(h, HN(h, x)->b, ZL_E_UNSUPPORTED,
                                   "main's type is IO () in this subset");
                if (HN(h, main_d)->c >= 0)
                    return nfail_h(h, v, ZL_E_SYNTAX, "duplicate type signature");
                HN(h, main_d)->c = HN(h, x)->b;
                continue;
            }
            int32_t found = -1;
            for (uint32_t j = 0; j < h->ng; j++) {
                int32_t m;
                bind_name(h, h->gb[j], &m);
                if (same_node_name(h, m, v)) found = h->gb[j];
            }
            if (found < 0) return nfail_h(h, v, ZL_E_SYNTAX, "a type signature without a binding");
            if (HN(h, found)->c >= 0) return nfail_h(h, v, ZL_E_SYNTAX, "duplicate type signature");
            HN(h, found)->c = HN(h, x)->b;
        }
    }
    /* who uses whom, then the transitive closure */
    for (uint32_t i = 0; i < h->ng; i++) {
        for (uint32_t k = 0; k < HS_GROUP / 64; k++) h->reach[i][k] = 0;
        h->done[i] = 0;
        int32_t b = h->gb[i];
        if (HN(h, b)->kind == N_FB)
            scan(h, HN(h, b)->a, i, 0);
        else
            scan(h, HN(h, b)->b, i, 0);
    }
    for (uint32_t k = 0; k < h->ng; k++)
        for (uint32_t i = 0; i < h->ng; i++)
            if (reaches(h, i, k))
                for (uint32_t q = 0; q < HS_GROUP / 64; q++) h->reach[i][q] |= h->reach[k][q];
    /* components, those used first */
    hlist_t out = {-1, -1, 0};
    for (uint32_t emitted = 0; emitted < h->ng;) {
        uint32_t best = h->ng, bc = 0;
        for (uint32_t i = 0; i < h->ng; i++) {
            if (h->done[i]) continue;
            uint32_t c = reach_count(h, i);
            if (best == h->ng || c < bc) {
                best = i;
                bc = c;
            }
        }
        int has_val = 0, cyc = reaches(h, best, best);
        uint32_t size = 0;
        for (uint32_t j = 0; j < h->ng; j++)
            if (j == best || (reaches(h, best, j) && reaches(h, j, best))) {
                size++;
                has_val |= HN(h, h->gb[j])->kind == D_VAL;
            }
        if (has_val && (size > 1 || cyc)) {
            for (uint32_t j = 0; j < h->ng; j++)
                if ((j == best || (reaches(h, best, j) && reaches(h, j, best))) &&
                    HN(h, h->gb[j])->kind == D_VAL)
                    return nfail_h(h, h->gb[j], ZL_E_UNSUPPORTED,
                                   "recursive value bindings (infinite or cyclic data) are not in "
                                   "this subset");
        }
        if (has_val) {
            int32_t d = h->gb[best];
            HN(h, d)->next = -1;
            ladd(h, &out, d);
            h->done[best] = 1;
            emitted++;
            continue;
        }
        int32_t fb = h->gb[best];
        int32_t d = zl_ml_node(w, D_FUN, HN(h, fb)->line, HN(h, fb)->col);
        TRY(d);
        HN(h, d)->flags |= F_REC;
        hlist_t fl = {-1, -1, 0};
        for (uint32_t j = 0; j < h->ng; j++)
            if (j == best || (reaches(h, best, j) && reaches(h, j, best))) {
                HN(h, h->gb[j])->next = -1;
                ladd(h, &fl, h->gb[j]);
                h->done[j] = 1;
                emitted++;
            }
        HN(h, d)->a = fl.head;
        ladd(h, &out, d);
    }
    if (top) h->root = main_d;
    return out.head;
}

static int32_t p_program(hp_t *h)
{
    hstate_t *l = &h->st;
    TRY(raw(h));
    l->have_raw = 1;
    l->raw_bol = l->raw.first;
    if (l->raw.k == HK_MODULE) {
        TRY(lex(h)); /* the token module */
        TRY(lex(h));
        if (h->tok.k != HT_CON) return hfail(h, ZL_E_SYNTAX, "expected the module's name");
        if (!tok_is(h, "Main"))
            return hfail(h, ZL_E_UNSUPPORTED, "a module other than Main is not in this subset");
        TRY(lex(h));
        if (h->tok.k == HT_LP) {
            TRY(lex(h));
            if (h->tok.k != HT_VAR || !tok_is(h, "main"))
                return hfail(h, ZL_E_UNSUPPORTED,
                             "an export list other than (main) is not in this subset");
            TRY(lex(h));
            TRY(expect(h, HT_RP, "expected ) after the export list"));
        }
        if (h->tok.k != HK_WHERE)
            return hfail(h, ZL_E_SYNTAX, "expected where after the module header");
        TRY(lex(h));
    } else {
        l->want_open = 1;
        TRY(lex(h));
    }
    TRY(open_block(h));
    int32_t top = p_decls(h, 1);
    TRY(top);
    if (h->tok.k != HT_EOF) return hfail(h, ZL_E_SYNTAX, "unexpected text after the declarations");
    int32_t main_d = h->root;
    /* the program: let <top-level declarations> in <main's right side> */
    HN(h, top)->b = HN(h, main_d)->b;
    if (HN(h, main_d)->c >= 0 && HN(h, HN(h, main_d)->c)->op != TK_IO)
        return nfail_h(h, HN(h, main_d)->c, ZL_E_UNSUPPORTED,
                       "main's type is IO () in this subset");
    mark_stmt(h, top);
    int32_t d = zl_ml_node(W(h), D_EXP, HN(h, main_d)->line, HN(h, main_d)->col);
    TRY(d);
    HN(h, d)->a = top;
    HN(h, d)->flags |= F_TOP;
    h->m.tops[0] = d;
    h->m.ntops = 1;
    h->root = top;
    h->items = HN(h, top)->c;
    return ZL_OK;
}

/* ------------------------------------------------------------ the twin */

static void tw(hp_t *h, const char *s)
{
    zl_puts(h->m.tw, s);
}

static void twn(hp_t *h, int32_t n)
{
    zl_put(h->m.tw, h->m.src + HN(h, n)->v, HN(h, n)->len);
}

static void pr_exp(hp_t *h, int32_t e, int minp, int full);
static void pr_items(hp_t *h, int32_t items, int top);

static void pr_type(hp_t *h, int32_t t, int arg)
{
    mnode_t *n = HN(h, t);
    switch (n->op) {
    case TK_INT:
        tw(h, "Int");
        return;
    case TK_BOOL:
        tw(h, "Bool");
        return;
    case TK_UNIT:
        tw(h, "()");
        return;
    case TK_IO:
        tw(h, "IO ()");
        return;
    case TK_VAR:
        twn(h, t);
        return;
    case TK_LIST:
        tw(h, "[");
        pr_type(h, n->a, 0);
        tw(h, "]");
        return;
    case TK_TUPLE:
        tw(h, "(");
        for (int32_t q = n->a; q >= 0; q = HN(h, q)->next) {
            pr_type(h, q, 0);
            if (HN(h, q)->next >= 0) tw(h, ", ");
        }
        tw(h, ")");
        return;
    case TK_FUN:
        if (arg) tw(h, "(");
        pr_type(h, n->a, 1);
        tw(h, " -> ");
        pr_type(h, HN(h, t)->b, 0);
        if (arg) tw(h, ")");
        return;
    default:
        return;
    }
}

/* patterns: lev 0 anywhere, 1 left of :, 2 a parameter */
static void pr_pat(hp_t *h, int32_t p, int lev)
{
    mnode_t *n = HN(h, p);
    switch (n->kind) {
    case P_WILD:
        tw(h, "_");
        return;
    case P_VAR:
        twn(h, p);
        return;
    case P_INT:
        if (n->v < 0 && lev >= 2) {
            tw(h, "(");
            zl_puti(h->m.tw, n->v);
            tw(h, ")");
            return;
        }
        zl_puti(h->m.tw, n->v);
        return;
    case P_BOOL:
        tw(h, n->v ? "True" : "False");
        return;
    case P_UNIT:
        tw(h, "()");
        return;
    case P_NIL:
        tw(h, "[]");
        return;
    case P_CONS:
        if (lev >= 1) tw(h, "(");
        pr_pat(h, n->a, 1);
        tw(h, " : ");
        pr_pat(h, HN(h, p)->b, 0);
        if (lev >= 1) tw(h, ")");
        return;
    case P_LIST:
    case P_TUPLE:
        tw(h, n->kind == P_LIST ? "[" : "(");
        for (int32_t q = n->a; q >= 0; q = HN(h, q)->next) {
            pr_pat(h, q, 0);
            if (HN(h, q)->next >= 0) tw(h, ", ");
        }
        tw(h, n->kind == P_LIST ? "]" : ")");
        return;
    default:
        return;
    }
}

static void bin_fix(int op, int *prec, int *assoc, const char **text)
{
    *assoc = 0;
    switch (op) {
    case O_INDEX:
        *prec = 9;
        *text = "!!";
        return;
    case O_POW:
        *prec = 8;
        *assoc = 1;
        *text = "^";
        return;
    case O_MUL:
        *prec = 7;
        *text = "*";
        return;
    case O_DIV:
        *prec = 7;
        *text = "`div`";
        return;
    case O_MOD:
        *prec = 7;
        *text = "`mod`";
        return;
    case O_QUOT:
        *prec = 7;
        *text = "`quot`";
        return;
    case O_REM:
        *prec = 7;
        *text = "`rem`";
        return;
    case O_ADD:
        *prec = 6;
        *text = "+";
        return;
    case O_SUB:
        *prec = 6;
        *text = "-";
        return;
    case O_CONS:
        *prec = 5;
        *assoc = 1;
        *text = ":";
        return;
    case O_APPEND:
        *prec = 5;
        *assoc = 1;
        *text = "++";
        return;
    case O_ANDALSO:
        *prec = 3;
        *assoc = 1;
        *text = "&&";
        return;
    case O_ORELSE:
        *prec = 2;
        *assoc = 1;
        *text = "||";
        return;
    default:
        *prec = 4;
        *assoc = 2;
        *text = op == O_EQ   ? "=="
                : op == O_NE ? "/="
                : op == O_LT ? "<"
                : op == O_LE ? "<="
                : op == O_GT ? ">"
                             : ">=";
        return;
    }
}

static int is_open(hp_t *h, int32_t e)
{
    int k = HN(h, e)->kind;
    return k == E_IF || k == E_CASE || (k == E_LET && HN(h, e)->op == 0) || k == E_SEQ;
}

static void pr_list(hp_t *h, int32_t first, const char *sep)
{
    for (int32_t q = first; q >= 0; q = HN(h, q)->next) {
        pr_exp(h, q, 0, 1);
        if (HN(h, q)->next >= 0) tw(h, sep);
    }
}

/* a right side: = e, or guards, then where */
static void pr_rhs(hp_t *h, int32_t e, const char *eq)
{
    int32_t wh = -1;
    if (HN(h, e)->kind == E_LET && HN(h, e)->op == 1) {
        wh = e;
        e = HN(h, e)->b;
    }
    if (HN(h, e)->kind == E_GUARDS) {
        for (int32_t g = HN(h, e)->a; g >= 0; g = HN(h, g)->next) {
            tw(h, " | ");
            pr_exp(h, HN(h, g)->a, 0, 0);
            tw(h, eq);
            pr_exp(h, HN(h, g)->b, 0, 1);
        }
    } else {
        tw(h, eq);
        pr_exp(h, e, 0, 1);
    }
    if (wh >= 0 && HN(h, wh)->c >= 0) {
        tw(h, " where { ");
        pr_items(h, HN(h, wh)->c, 0);
        tw(h, " }");
    }
}

/* the statements of a do block, a let's body continuing them */
static void pr_stmts(hp_t *h, int32_t s)
{
    for (int32_t q = HN(h, s)->a; q >= 0; q = HN(h, q)->next) {
        mnode_t *n = HN(h, q);
        if (n->kind == E_LET && n->op == 2) {
            if (n->c >= 0) {
                tw(h, "let { ");
                pr_items(h, n->c, 0);
                tw(h, " }; ");
            }
            pr_stmts(h, n->b);
        } else {
            pr_exp(h, q, 0, 1);
        }
        if (HN(h, q)->next >= 0) tw(h, "; ");
    }
}

static void pr_exp(hp_t *h, int32_t e, int minp, int full)
{
    mnode_t *n = HN(h, e);
    if (is_open(h, e) && !full) {
        tw(h, "(");
        pr_exp(h, e, 0, 1);
        tw(h, ")");
        return;
    }
    switch (n->kind) {
    case E_INT:
        if (n->v < 0 && minp > 6) {
            tw(h, "(");
            zl_puti(h->m.tw, n->v);
            tw(h, ")");
            return;
        }
        zl_puti(h->m.tw, n->v);
        return;
    case E_BOOL:
        tw(h, n->v ? "True" : "False");
        return;
    case E_UNIT:
        tw(h, "()");
        return;
    case E_NIL:
        tw(h, "[]");
        return;
    case E_VAR:
    case E_STR:
        twn(h, e);
        return;
    case E_LIST:
        tw(h, "[");
        pr_list(h, n->a, ", ");
        tw(h, "]");
        return;
    case E_TUPLE:
        tw(h, "(");
        pr_list(h, n->a, ", ");
        tw(h, ")");
        return;
    case E_RANGE: {
        int32_t a = n->a, b = HN(h, a)->next;
        tw(h, "[");
        pr_exp(h, a, 0, 1);
        if (HN(h, b)->next >= 0) {
            tw(h, ", ");
            pr_exp(h, b, 0, 1);
            b = HN(h, b)->next;
        }
        tw(h, " .. ");
        pr_exp(h, b, 0, 1);
        tw(h, "]");
        return;
    }
    case E_BIN: {
        int prec, assoc;
        const char *text;
        bin_fix(n->op, &prec, &assoc, &text);
        int paren = prec < minp;
        if (paren) tw(h, "(");
        pr_exp(h, n->a, assoc == 0 ? prec : prec + 1, 0);
        tw(h, " ");
        tw(h, text);
        tw(h, " ");
        pr_exp(h, HN(h, e)->b, assoc == 1 ? prec : prec + 1, 0);
        if (paren) tw(h, ")");
        return;
    }
    case E_NEG: {
        int paren = minp > 6;
        if (paren) tw(h, "(");
        tw(h, "-");
        pr_exp(h, n->a, 7, 0);
        if (paren) tw(h, ")");
        return;
    }
    case E_APP: {
        int paren = minp > 10;
        if (paren) tw(h, "(");
        twn(h, e);
        for (int32_t q = n->a; q >= 0; q = HN(h, q)->next) {
            tw(h, " ");
            pr_exp(h, q, 11, 0);
        }
        if (paren) tw(h, ")");
        return;
    }
    case E_IF:
        tw(h, "if ");
        pr_exp(h, n->a, 0, 0);
        tw(h, " then ");
        pr_exp(h, HN(h, e)->b, 0, 1);
        tw(h, " else ");
        pr_exp(h, HN(h, e)->c, 0, 1);
        return;
    case E_CASE:
        tw(h, "case ");
        pr_exp(h, n->a, 0, 0);
        tw(h, " of { ");
        for (int32_t r = HN(h, e)->b; r >= 0; r = HN(h, r)->next) {
            pr_pat(h, HN(h, r)->a, 0);
            pr_rhs(h, HN(h, r)->c, " -> ");
            if (HN(h, r)->next >= 0) tw(h, "; ");
        }
        tw(h, " }");
        return;
    case E_LET:
        if (n->c >= 0) {
            tw(h, "let { ");
            pr_items(h, n->c, 0);
            tw(h, " } in ");
        }
        pr_exp(h, HN(h, e)->b, 0, 1);
        return;
    case E_SEQ:
        tw(h, "do { ");
        pr_stmts(h, e);
        tw(h, " }");
        return;
    default:
        return;
    }
}

static void pr_sig(hp_t *h, int32_t s)
{
    for (int32_t v = HN(h, s)->a; v >= 0; v = HN(h, v)->next) {
        twn(h, v);
        if (HN(h, v)->next >= 0) tw(h, ", ");
    }
    tw(h, " :: ");
    pr_type(h, HN(h, s)->b, 0);
}

static void pr_comment(hp_t *h, int32_t c)
{
    const char *s = h->m.src + HN(h, c)->v;
    uint32_t n = (uint32_t) HN(h, c)->sym, i = 0;
    int any = 0;
    while (i < n) {
        uint32_t a = i;
        while (i < n && s[i] != '\n') i++;
        uint32_t b = i;
        while (a < b && (s[a] == ' ' || s[a] == '\r')) a++;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\r')) b--;
        if (b > a) {
            tw(h, "-- ");
            zl_put(h->m.tw, s + a, b - a);
            tw(h, "\n");
            any = 1;
        }
        i++;
    }
    if (!any) tw(h, "--\n");
}

/* declarations in source order: one per line at the top, else "; " */
static void pr_items(hp_t *h, int32_t items, int top)
{
    const char *sep = top ? "\n" : "; ";
    int first = 1;
    for (int32_t it = items; it >= 0; it = HN(h, it)->next) {
        int32_t x = HN(h, it)->a;
        mnode_t *n = HN(h, x);
        if (n->kind == N_COMMENT) {
            pr_comment(h, x);
            continue;
        }
        if (!first && !top) tw(h, sep);
        first = 0;
        if (n->kind == N_SIG) {
            pr_sig(h, x);
        } else if (n->kind == D_VAL) {
            twn(h, n->a);
            pr_rhs(h, HN(h, x)->b, " = ");
        } else if (n->kind == N_FB) {
            for (int32_t c = n->a; c >= 0; c = HN(h, c)->next) {
                twn(h, x);
                for (int32_t p = HN(h, c)->a; p >= 0; p = HN(h, p)->next) {
                    tw(h, " ");
                    pr_pat(h, p, 2);
                }
                pr_rhs(h, HN(h, c)->b, " = ");
                if (HN(h, c)->next >= 0) tw(h, sep);
            }
        }
        if (top) tw(h, "\n");
    }
}

/* ------------------------------------------------------------ entry */

int zl_haskell_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw_out, void *work)
{
    hp_t *h = (hp_t *) work;
    zl_ml_begin(W(h), c, tw_out, src, len, D_HS);
    zl_bzero(&h->st, sizeof h->st);
    zl_bzero(&h->tok, sizeof h->tok);
    h->st.line = 1;
    h->last_end = 0;
    h->root = h->items = -1;
    TRY(p_program(h));
    TRY(zl_ml_finish(W(h)));
    if (c->failed) return c->d->status;
    if (tw_out) pr_items(h, h->items, 1);
    return ZL_OK;
}
