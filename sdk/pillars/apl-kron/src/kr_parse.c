/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kr_parse.c — three front ends (APL glyphs, K-style ASCII, canonical
 * S-expressions) that build one syntax tree; the analysis that infers
 * static ranks and folds spelling-only differences into one canonical
 * form; and the program accessors. Integer code only (R11).
 *
 * APL and K are read the way their interpreters read them: a statement
 * is a row of nouns and functions taken right to left, a function is
 * dyadic when a noun stands on its left, and operators (/ \ ⌿ ⍀ ∘. .
 * \:) bind to the function on their left. */
#include <stdlib.h>
#include <string.h>

#include "kr_internal.h"

const char *const kr_fn_word[F_COUNT_] = {
    "",       "id",      "neg",  "sgn",  "recip", "ceil",   "floor",  "abs",  "not",
    "iota",   "shape",   "tally", "rev", "revf",  "flip",   "ravel",  "gup",  "gdown",
    "sort",   "add",     "sub",  "mul",  "div",   "max",    "min",    "res",  "pow",
    "lt",     "le",      "eq",   "ge",   "gt",    "ne",     "and",    "or",   "match",
    "reshape", "cat",    "catf", "take", "drop",  "rotate", "find",   "index"};

/* Words that are not functions but may not be names either. */
static const char *const g_reserved[] = {"kron",   "vec",   "tvec",  "mat",   "tmat",
                                         "let",    "reduce", "reducef", "foldl", "scan",
                                         "scanf",  "scanl", "outer", "inner", "index2",
                                         "true",   "false", "neither", "both"};

static int is_reserved(const char *w) {
    for (size_t i = 0; i < sizeof g_reserved / sizeof g_reserved[0]; i++)
        if (strcmp(w, g_reserved[i]) == 0) return 1;
    for (int f = 1; f < F_COUNT_; f++)
        if (strcmp(w, kr_fn_word[f]) == 0) return 1;
    return 0;
}

static int fn_by_word(const char *w) {
    for (int f = 1; f < F_COUNT_; f++)
        if (strcmp(w, kr_fn_word[f]) == 0) return f;
    return 0;
}

static int truth_by_word(const char *w) {
    if (strcmp(w, "true") == 0) return ZXV_LP_TRUE;
    if (strcmp(w, "false") == 0) return ZXV_LP_FALSE;
    if (strcmp(w, "neither") == 0) return ZXV_LP_NEITHER;
    if (strcmp(w, "both") == 0) return ZXV_LP_BOTH;
    return -1;
}

/* ===== Tokens ===== */
enum {
    T_END = 0, T_NUM, T_TRUTH, T_NAME, T_FN, T_ADV, T_JOTDOT, T_DOT, T_ASSIGN, T_LPAR, T_RPAR,
    T_LBRK, T_RBRK, T_SEMI, T_SEP, T_EMPTY
};
/* adverbs */
enum { A_RED_LAST = 1, A_RED_FIRST, A_SCAN_LAST, A_SCAN_FIRST, A_FOLDL, A_SCANL, A_EACHLEFT };

typedef struct {
    uint8_t t;
    uint8_t mon, dya, dya_not, adv;
    const char *mon_no, *dya_no; /* why a valence is not in KRON */
    int64_t num, den;
    char name[KR_MAX_NAME + 1];
    const char *spell; /* the source spelling of a function, for messages */
    uint32_t line, col;
    uint64_t off;
} tok;

typedef struct {
    const char *s;
    uint8_t t, mon, dya, dya_not, adv;
    const char *mon_no, *dya_no;
} symdef;

/* APL glyphs (⎕IO 0). Longest spellings first where one is a prefix. */
static const symdef g_apl[] = {
    {"∘.", T_JOTDOT, 0, 0, 0, 0, 0, 0},
    {"+", T_FN, F_ID, F_ADD, 0, 0, 0, 0},
    {"-", T_FN, F_NEG, F_SUB, 0, 0, 0, 0},
    {"×", T_FN, F_SGN, F_MUL, 0, 0, 0, 0},
    {"÷", T_FN, F_RECIP, F_DIV, 0, 0, 0, 0},
    {"⌈", T_FN, F_CEIL, F_MAX, 0, 0, 0, 0},
    {"⌊", T_FN, F_FLOOR, F_MIN, 0, 0, 0, 0},
    {"|", T_FN, F_ABS, F_RES, 0, 0, 0, 0},
    {"*", T_FN, 0, F_POW, 0, 0, "monadic * (exponential) is not exact", 0},
    {"<", T_FN, 0, F_LT, 0, 0, "monadic < is not APL", 0},
    {"≤", T_FN, 0, F_LE, 0, 0, "monadic ≤ is not APL", 0},
    {"=", T_FN, 0, F_EQ, 0, 0, "monadic = is not in KRON", 0},
    {"≥", T_FN, 0, F_GE, 0, 0, "monadic ≥ is not APL", 0},
    {">", T_FN, 0, F_GT, 0, 0, "monadic > is not APL", 0},
    {"≠", T_FN, 0, F_NE, 0, 0, "monadic ≠ (unique mask) is not in KRON", 0},
    {"≡", T_FN, 0, F_MATCH, 0, 0, "monadic ≡ (depth) is not in KRON: arrays are flat", 0},
    {"≢", T_FN, F_TALLY, F_MATCH, 1, 0, 0, 0},
    {"∧", T_FN, 0, F_AND, 0, 0, "monadic ∧ is not APL", 0},
    {"∨", T_FN, 0, F_OR, 0, 0, "monadic ∨ is not APL", 0},
    {"~", T_FN, F_NOT, 0, 0, 0, 0, "dyadic ~ (without) is not in KRON"},
    {"⍳", T_FN, F_IOTA, F_FIND, 0, 0, 0, 0},
    {"⍴", T_FN, F_SHAPE, F_RESHAPE, 0, 0, 0, 0},
    {",", T_FN, F_RAVEL, F_CAT, 0, 0, 0, 0},
    {"⍪", T_FN, 0, F_CATF, 0, 0, "monadic ⍪ (table) is not in KRON", 0},
    {"⌽", T_FN, F_REV, F_ROTATE, 0, 0, 0, 0},
    {"⊖", T_FN, F_REVF, 0, 0, 0, 0, "dyadic ⊖ (rotate first) is not in KRON"},
    {"⍉", T_FN, F_FLIP, 0, 0, 0, 0, "dyadic ⍉ is not in KRON"},
    {"↑", T_FN, 0, F_TAKE, 0, 0, "monadic ↑ (first, mix) is not in KRON: arrays are flat", 0},
    {"↓", T_FN, 0, F_DROP, 0, 0, "monadic ↓ (split) is not in KRON: arrays are flat", 0},
    {"⍋", T_FN, F_GUP, 0, 0, 0, 0, "dyadic ⍋ (collation) is not in KRON"},
    {"⍒", T_FN, F_GDOWN, 0, 0, 0, 0, "dyadic ⍒ (collation) is not in KRON"},
    {"/", T_ADV, 0, 0, 0, A_RED_LAST, 0, 0},
    {"⌿", T_ADV, 0, 0, 0, A_RED_FIRST, 0, 0},
    {"\\", T_ADV, 0, 0, 0, A_SCAN_LAST, 0, 0},
    {"⍀", T_ADV, 0, 0, 0, A_SCAN_FIRST, 0, 0},
    {".", T_DOT, 0, 0, 0, 0, 0, 0},
    {"←", T_ASSIGN, 0, 0, 0, 0, 0, 0},
    {"⋄", T_SEP, 0, 0, 0, 0, 0, 0},
    {"⍬", T_EMPTY, 0, 0, 0, 0, 0, 0},
    {"(", T_LPAR, 0, 0, 0, 0, 0, 0},
    {")", T_RPAR, 0, 0, 0, 0, 0, 0},
    {"[", T_LBRK, 0, 0, 0, 0, 0, 0},
    {"]", T_RBRK, 0, 0, 0, 0, 0, 0},
    {";", T_SEMI, 0, 0, 0, 0, 0, 0},
};

/* K-style ASCII. Each symbol means what it means in K, or is refused. */
static const symdef g_k[] = {
    /* q's <= >= <> are two functions in K (x<=y is x<(=y) in ngn/k): refused, never reread */
    {"<=", T_FN, 0, 0, 0, 0, "'<=' is < and = in K, not 'at most'; write x le y or ~x>y",
     "'<=' is < and = in K, not 'at most'; write x le y or ~x>y"},
    {">=", T_FN, 0, 0, 0, 0, "'>=' is > and = in K, not 'at least'; write x ge y or ~x<y",
     "'>=' is > and = in K, not 'at least'; write x ge y or ~x<y"},
    {"<>", T_FN, 0, 0, 0, 0, "'<>' is < and > in K, not 'differs'; write x ne y or ~x=y",
     "'<>' is < and > in K, not 'differs'; write x ne y or ~x=y"},
    {"\\:", T_ADV, 0, 0, 0, A_EACHLEFT, 0, 0},
    {"+", T_FN, F_FLIP, F_ADD, 0, 0, 0, 0},
    {"-", T_FN, F_NEG, F_SUB, 0, 0, 0, 0},
    {"*", T_FN, 0, F_MUL, 0, 0, "monadic * (first) is not in KRON", 0},
    {"%", T_FN, 0, F_DIV, 0, 0, "monadic % (square root) is not exact; write recip x for 1%x", 0},
    {"|", T_FN, F_REVF, F_MAX, 0, 0, 0, 0},
    {"&", T_FN, 0, F_MIN, 0, 0, "monadic & (where) is not in KRON", 0},
    {"<", T_FN, F_GUP, F_LT, 0, 0, 0, 0},
    {">", T_FN, F_GDOWN, F_GT, 0, 0, 0, 0},
    {"=", T_FN, 0, F_EQ, 0, 0, "monadic = (group) is not in KRON", 0},
    {"~", T_FN, F_NOT, F_MATCH, 0, 0, 0, 0},
    {"!", T_FN, F_IOTA, 0, 0, 0, 0, "dyadic ! (dict, mod) is not in KRON; write m res x"},
    {"#", T_FN, F_TALLY, F_RESHAPE, 0, 0, 0, 0},
    {"_", T_FN, F_FLOOR, F_DROP, 0, 0, 0, 0},
    {",", T_FN, 0, F_CATF, 0, 0, "monadic , (enlist) is not in KRON; write ravel x", 0},
    {"^", T_FN, 0, 0, 0, 0, "monadic ^ is null in ngn/k and sort in k9; KRON refuses it: write sort x or x@<x",
     "dyadic ^ (fill, without) is not in KRON"},
    {"?", T_FN, 0, F_FIND, 0, 0, "monadic ? (distinct) is not in KRON", 0},
    {"@", T_FN, 0, F_INDEX, 0, 0, "monadic @ (type) is not in KRON", 0},
    {"/", T_ADV, 0, 0, 0, A_FOLDL, 0, 0},
    {"\\", T_ADV, 0, 0, 0, A_SCANL, 0, 0},
    {":", T_ASSIGN, 0, 0, 0, 0, 0, 0},
    {"(", T_LPAR, 0, 0, 0, 0, 0, 0},
    {")", T_RPAR, 0, 0, 0, 0, 0, 0},
    {"[", T_LBRK, 0, 0, 0, 0, 0, 0},
    {"]", T_RBRK, 0, 0, 0, 0, 0, 0},
    {";", T_SEMI, 0, 0, 0, 0, 0, 0},
};

typedef struct {
    kr_program *p;
    const uint8_t *s;
    size_t n;
    size_t i;
    uint32_t line;
    size_t line_start;
    kr_diag *diag;
    tok *toks;
    uint32_t ntok, cap_tok;
    uint32_t pos;
    int32_t spelling;
} P;

static int32_t fail(P *ps, int32_t st, uint32_t line, uint32_t col, uint64_t off, const char *msg) {
    return kr_diag_at(ps->diag, st, line, col, off, "%s", msg);
}

/* ===== Building the program ===== */

static int32_t add_lit(kr_program *p, int64_t num, int64_t den, uint32_t *out) {
    if (p->nlit >= KR_MAX_LITERALS) return KR_E_LIMIT;
    if (p->nlit == p->cap_lit) {
        uint32_t nc = p->cap_lit ? p->cap_lit * 2u : 64u;
        int64_t *a = (int64_t *) realloc(p->lnum, nc * sizeof *a);
        if (!a) return ZXV_E_NOMEM;
        p->lnum = a;
        a = (int64_t *) realloc(p->lden, nc * sizeof *a);
        if (!a) return ZXV_E_NOMEM;
        p->lden = a;
        p->cap_lit = nc;
    }
    p->lnum[p->nlit] = num;
    p->lden[p->nlit] = den;
    *out = p->nlit++;
    return ZXV_OK;
}

static int32_t add_sym(kr_program *p, const char *name, int32_t *out) {
    for (uint32_t i = 0; i < p->nsyms; i++)
        if (strcmp(p->syms[i].name, name) == 0) {
            *out = (int32_t) i;
            return ZXV_OK;
        }
    if (p->nsyms == p->cap_syms) {
        uint32_t nc = p->cap_syms ? p->cap_syms * 2u : 16u;
        kr_sym *a;
        if (nc > KR_MAX_NODES) return KR_E_LIMIT;
        a = (kr_sym *) realloc(p->syms, nc * sizeof *a);
        if (!a) return ZXV_E_NOMEM;
        p->syms = a;
        p->cap_syms = nc;
    }
    memset(&p->syms[p->nsyms], 0, sizeof p->syms[0]);
    memcpy(p->syms[p->nsyms].name, name, strlen(name) + 1u);
    *out = (int32_t) p->nsyms++;
    return ZXV_OK;
}


/* A new node from a template; its depth is 1 + its deepest child. */
static int32_t mk(P *ps, const kr_node *t, int32_t *out) {
    kr_program *p = ps->p;
    kr_node nd = *t;
    uint32_t d = 0;
    int32_t ch[3];
    ch[0] = nd.a;
    ch[1] = nd.b;
    ch[2] = nd.c;
    for (int k = 0; k < 3; k++)
        if (ch[k] >= 0 && p->nodes[ch[k]].depth > d) d = p->nodes[ch[k]].depth;
    if (d + 1u > KR_MAX_DEPTH)
        return kr_diag_at(ps->diag, KR_E_LIMIT, nd.line, nd.col, nd.offset,
                          "the expression is nested deeper than %u", (unsigned) KR_MAX_DEPTH);
    nd.depth = (uint16_t) (d + 1u);
    nd.rank = -1;
    if (p->nnodes >= KR_MAX_NODES)
        return kr_diag_at(ps->diag, KR_E_LIMIT, nd.line, nd.col, nd.offset,
                          "more than %u syntax nodes", (unsigned) KR_MAX_NODES);
    if (p->nnodes == p->cap_nodes) {
        uint32_t nc = p->cap_nodes ? p->cap_nodes * 2u : 64u;
        kr_node *a = (kr_node *) realloc(p->nodes, nc * sizeof *a);
        if (!a) return ZXV_E_NOMEM;
        p->nodes = a;
        p->cap_nodes = nc;
    }
    p->nodes[p->nnodes] = nd;
    *out = (int32_t) p->nnodes++;
    return ZXV_OK;
}

static kr_node blank(uint8_t kind, uint32_t line, uint32_t col, uint64_t off) {
    kr_node n;
    memset(&n, 0, sizeof n);
    n.kind = kind;
    n.a = n.b = n.c = -1;
    n.sym = -1;
    n.line = line;
    n.col = col;
    n.offset = off;
    return n;
}

static int32_t push_stmt(kr_program *p, int32_t node) {
    if (p->nstmts == p->cap_stmts) {
        uint32_t nc = p->cap_stmts ? p->cap_stmts * 2u : 16u;
        int32_t *a = (int32_t *) realloc(p->stmts, nc * sizeof *a);
        if (!a) return ZXV_E_NOMEM;
        p->stmts = a;
        p->cap_stmts = nc;
    }
    p->stmts[p->nstmts++] = node;
    return ZXV_OK;
}

/* ===== Lexing APL and K ===== */

static void here(const P *ps, size_t off, uint32_t *line, uint32_t *col) {
    *line = ps->line;
    *col = (uint32_t) (off - ps->line_start + 1u);
}

static int32_t push_tok(P *ps, tok *t) {
    if (ps->ntok == ps->cap_tok) {
        uint32_t nc = ps->cap_tok ? ps->cap_tok * 2u : 64u;
        tok *a;
        if (nc > 4u * KR_MAX_NODES) return kr_diag_at(ps->diag, KR_E_LIMIT, t->line, t->col, t->off, "too many tokens");
        a = (tok *) realloc(ps->toks, nc * sizeof *a);
        if (!a) return ZXV_E_NOMEM;
        ps->toks = a;
        ps->cap_tok = nc;
    }
    ps->toks[ps->ntok++] = *t;
    return ZXV_OK;
}

static int is_digit(uint8_t c) { return c >= '0' && c <= '9'; }
static int is_alpha(uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_alnum(uint8_t c) { return is_digit(c) || is_alpha(c); }

/* The UTF-8 character at s[i] as text, for messages. */
static void char_at(const P *ps, size_t i, char out[8]) {
    size_t k = 1;
    uint8_t c = ps->s[i];
    if (c >= 0xF0u) k = 4;
    else if (c >= 0xE0u) k = 3;
    else if (c >= 0xC0u) k = 2;
    if (k > ps->n - i) k = ps->n - i;
    memcpy(out, ps->s + i, k);
    out[k] = '\0';
    if (c < 0x20u || c == 0x7Fu) {
        out[0] = '?';
        out[1] = '\0';
    }
}

/* Parse the digits of a literal at s[i] into an exact value: D, D.D or
 * DrD (and a sign); exponents and suffixes are refused. *end is set past
 * the literal. Text is checked by zxv_rat_parse after leading zeros go. */
static int32_t lex_digits(P *ps, size_t i, int neg, uint32_t line, uint32_t col, uint64_t off,
                          int64_t *num, int64_t *den, size_t *end) {
    char b[160];
    size_t k = 0, j = i, s0, s1;
    int32_t st;
    if (neg) b[k++] = '-';
    s0 = j;
    while (j < ps->n && is_digit(ps->s[j])) j++;
    s1 = j;
    while (s0 + 1u < s1 && ps->s[s0] == '0') s0++; /* leading zeros */
    if (s1 - s0 > 40u) goto range;
    memcpy(b + k, ps->s + s0, s1 - s0);
    k += s1 - s0;
    if (j + 1u < ps->n && ps->s[j] == '.' && is_digit(ps->s[j + 1u])) {
        size_t f0 = ++j, f1;
        while (j < ps->n && is_digit(ps->s[j])) j++;
        f1 = j;
        while (f1 > f0 && ps->s[f1 - 1u] == '0') f1--; /* trailing zeros */
        if (f1 - f0 > 40u) goto range;
        if (f1 > f0) {
            b[k++] = '.';
            memcpy(b + k, ps->s + f0, f1 - f0);
            k += f1 - f0;
        }
    } else if (j < ps->n && ps->s[j] == 'r') {
        size_t d0 = ++j, d1;
        int zero = 1;
        while (j < ps->n && is_digit(ps->s[j])) j++;
        d1 = j;
        if (d1 == d0)
            return fail(ps, KR_E_SYNTAX, line, col, off, "a rational literal needs digits after r (1r3)");
        for (size_t q = d0; q < d1; q++)
            if (ps->s[q] != '0') zero = 0;
        if (zero) return fail(ps, KR_E_DIVZERO, line, col, off, "a rational literal with denominator 0");
        while (d0 + 1u < d1 && ps->s[d0] == '0') d0++;
        if (d1 - d0 > 40u) goto range;
        b[k++] = '/';
        memcpy(b + k, ps->s + d0, d1 - d0);
        k += d1 - d0;
    }
    if (j < ps->n && (is_alnum(ps->s[j]) || ps->s[j] == '.')) {
        if ((ps->s[j] == 'e' || ps->s[j] == 'E') && j + 1u < ps->n &&
            (is_digit(ps->s[j + 1u]) || ps->s[j + 1u] == '-' || ps->s[j + 1u] == '+'))
            return fail(ps, KR_E_SYNTAX, line, col, off,
                        "exponent notation is refused: write the exact value (1000, 1r1000)");
        {
            char c[8];
            char_at(ps, j, c);
            return kr_diag_at(ps->diag, KR_E_SYNTAX, line, col, off,
                              "a number runs into '%s' (no suffixes, no 1e3, no 0N)", c);
        }
    }
    b[k] = '\0';
    st = zxv_rat_parse(b, num, den);
    if (st == ZXV_E_RANGE) goto range;
    if (st != ZXV_OK) return kr_diag_at(ps->diag, KR_E_SYNTAX, line, col, off, "bad number '%s'", b);
    *end = j;
    return ZXV_OK;
range:
    return fail(ps, ZXV_E_RANGE, line, col, off, "the literal does not fit int64/int64 (refused, never rounded)");
}

static int noun_end(uint8_t t) {
    return t == T_NUM || t == T_TRUTH || t == T_NAME || t == T_RPAR || t == T_RBRK || t == T_EMPTY;
}

static int starts(const P *ps, size_t i, const char *lit) {
    size_t k = strlen(lit);
    return k <= ps->n - i && memcmp(ps->s + i, lit, k) == 0;
}

static int32_t lex_word(P *ps, size_t i, size_t *end, tok *t) {
    size_t j = i;
    int f, tv;
    while (j < ps->n && is_alnum(ps->s[j])) j++;
    if (j - i > KR_MAX_NAME)
        return kr_diag_at(ps->diag, KR_E_NAME, t->line, t->col, t->off, "a name longer than %u bytes",
                          (unsigned) KR_MAX_NAME);
    memcpy(t->name, ps->s + i, j - i);
    t->name[j - i] = '\0';
    *end = j;
    if ((tv = truth_by_word(t->name)) >= 0) {
        t->t = T_TRUTH;
        t->num = tv;
        t->den = 1;
        return ZXV_OK;
    }
    if ((f = fn_by_word(t->name)) != 0) {
        t->t = T_FN;
        if (F_IS_MONADIC(f)) t->mon = (uint8_t) f;
        else t->dya = (uint8_t) f;
        memcpy(t->name, ps->s + i, j - i);
        return ZXV_OK;
    }
    if (is_reserved(t->name))
        return kr_diag_at(ps->diag, KR_E_SYNTAX, t->line, t->col, t->off,
                          "'%s' is a word of canonical KRON, not a name", t->name);
    t->t = T_NAME;
    return ZXV_OK;
}

static int32_t lex_ak(P *ps) {
    int apl = ps->spelling == KR_SPELL_APL;
    const symdef *tab = apl ? g_apl : g_k;
    size_t ntab = apl ? sizeof g_apl / sizeof g_apl[0] : sizeof g_k / sizeof g_k[0];
    int bracket = 0;
    size_t i = 0;
    int32_t st;
    while (i < ps->n) {
        uint8_t c = ps->s[i];
        tok t;
        memset(&t, 0, sizeof t);
        here(ps, i, &t.line, &t.col);
        t.off = i;
        if (c == ' ' || c == '\t' || c == '\r') {
            i++;
            continue;
        }
        if (c == '\n') {
            t.t = T_SEP;
            if ((st = push_tok(ps, &t)) != ZXV_OK) return st;
            ps->line++;
            ps->line_start = ++i;
            continue;
        }
        if (apl && starts(ps, i, "⍝")) {
            while (i < ps->n && ps->s[i] != '\n') i++;
            continue;
        }
        if (!apl && c == '/' &&
            (i == ps->line_start || ps->s[i - 1u] == ' ' || ps->s[i - 1u] == '\t')) {
            while (i < ps->n && ps->s[i] != '\n') i++; /* K: " /" starts a comment */
            continue;
        }
        if (is_digit(c) || (apl && starts(ps, i, "¯")) ||
            (!apl && c == '-' && i + 1u < ps->n && is_digit(ps->s[i + 1u]))) {
            int neg = !is_digit(c);
            size_t from = i;
            if (!apl && neg) {
                const tok *pv = ps->ntok ? &ps->toks[ps->ntok - 1u] : NULL;
                int ws = i > 0 && (ps->s[i - 1u] == ' ' || ps->s[i - 1u] == '\t');
                if (pv && noun_end(pv->t) && pv->t != T_NUM && ws && pv->line == t.line)
                    return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off,
                                "in K, 'x -1' applies x to -1, which KRON does not have; write x - 1");
                if (pv && noun_end(pv->t) && !ws) goto symbol; /* x-1: subtract */
            }
            if (neg) {
                from = i + (apl ? 2u : 1u);
                if (from >= ps->n || !is_digit(ps->s[from]))
                    return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off, "¯ must be followed by digits");
            }
            if ((st = lex_digits(ps, from, neg, t.line, t.col, t.off, &t.num, &t.den, &i)) != ZXV_OK)
                return st;
            t.t = T_NUM;
            if ((st = push_tok(ps, &t)) != ZXV_OK) return st;
            continue;
        }
        if (is_alpha(c)) {
            if ((st = lex_word(ps, i, &i, &t)) != ZXV_OK) return st;
            if ((st = push_tok(ps, &t)) != ZXV_OK) return st;
            continue;
        }
        if (!apl && c == ';' && bracket == 0) {
            t.t = T_SEP;
            i++;
            if ((st = push_tok(ps, &t)) != ZXV_OK) return st;
            continue;
        }
        if (apl && c == ';' && bracket == 0)
            return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off, "';' outside brackets: separate statements with ⋄");
        if (!apl && starts(ps, i, "/:"))
            return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off, "each-right (/:) is not in KRON; x f\\: y is the outer product");
        if (!apl && c == '\'')
            return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off, "each (') is not in KRON");
    symbol:
        {
            size_t k;
            for (k = 0; k < ntab; k++)
                if (starts(ps, i, tab[k].s)) break;
            if (k == ntab) {
                char ch[8];
                char_at(ps, i, ch);
                return kr_diag_at(ps->diag, KR_E_SYNTAX, t.line, t.col, t.off,
                                  "'%s' is not in the %s spelling of KRON", ch, apl ? "APL" : "K");
            }
            t.t = tab[k].t;
            t.mon = tab[k].mon;
            t.dya = tab[k].dya;
            t.dya_not = tab[k].dya_not;
            t.adv = tab[k].adv;
            t.mon_no = tab[k].mon_no;
            t.dya_no = tab[k].dya_no;
            t.spell = tab[k].s;
            {
                size_t sl = strlen(tab[k].s);
                memcpy(t.name, tab[k].s, sl);
                t.name[sl] = '\0';
                i += sl;
            }
            if (t.t == T_LBRK) bracket++;
            if (t.t == T_RBRK && bracket > 0) bracket--;
            if (t.t == T_SEMI && bracket == 0)
                return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off, "';' outside brackets");
            if ((st = push_tok(ps, &t)) != ZXV_OK) return st;
        }
    }
    {
        tok t;
        memset(&t, 0, sizeof t);
        here(ps, ps->n, &t.line, &t.col);
        t.off = ps->n;
        t.t = T_END;
        return push_tok(ps, &t);
    }
}

/* ===== Parsing APL and K ===== */

enum { IT_NOUN = 1, IT_FN, IT_ASSIGN };
enum { FK_PLAIN = 0, FK_RED, FK_SCAN, FK_OUTER, FK_INNER };

typedef struct {
    uint8_t kind, fk, mon, dya, dya_not, fn2, variant;
    const char *mon_no, *dya_no;
    char sp[KR_MAX_NAME + 1];
    int32_t node, sym;
    uint32_t line, col;
    uint64_t off;
} item;

typedef struct {
    item *v;
    uint32_t n, cap;
} items;

static int32_t push_item(items *it, const item *x) {
    if (it->n == it->cap) {
        uint32_t nc = it->cap ? it->cap * 2u : 16u;
        item *a = (item *) realloc(it->v, nc * sizeof *a);
        if (!a) return ZXV_E_NOMEM;
        it->v = a;
        it->cap = nc;
    }
    it->v[it->n++] = *x;
    return ZXV_OK;
}

static const tok *cur(const P *ps) { return &ps->toks[ps->pos]; }
static const tok *peek(const P *ps, uint32_t k) {
    uint32_t q = ps->pos + k;
    return q < ps->ntok ? &ps->toks[q] : &ps->toks[ps->ntok - 1u];
}

static int32_t parse_expr(P *ps, uint32_t depth, int32_t *out);

static int32_t strand(P *ps, int32_t *out) {
    const tok *t0 = cur(ps);
    uint8_t tt = t0->t;
    uint32_t first = 0, cnt = 0;
    int32_t st;
    kr_node nd;
    while (cur(ps)->t == T_NUM || cur(ps)->t == T_TRUTH) {
        const tok *t = cur(ps);
        uint32_t li;
        if (t->t != tt)
            return fail(ps, KR_E_SYNTAX, t->line, t->col, t->off,
                        "numbers and truth values cannot share one vector");
        if ((st = add_lit(ps->p, t->num, t->den, &li)) != ZXV_OK) {
            if (st == KR_E_LIMIT) return fail(ps, st, t->line, t->col, t->off, "too many literal elements");
            return st;
        }
        if (cnt == 0) first = li;
        cnt++;
        ps->pos++;
    }
    nd = blank(cnt == 1 ? (tt == T_NUM ? N_NUM : N_TRUTH) : (tt == T_NUM ? N_VEC : N_TVEC), t0->line,
               t0->col, t0->off);
    nd.lit = first;
    nd.cnt = cnt;
    return mk(ps, &nd, out);
}

/* x[i] and x[i;j]: the index is evaluated before x, as in APL. */
static int32_t index_suffix(P *ps, uint32_t depth, int32_t *noun) {
    while (cur(ps)->t == T_LBRK) {
        const tok *lb = cur(ps);
        int32_t parts[2] = {-1, -1};
        uint32_t np = 0;
        uint32_t l = lb->line, c = lb->col;
        uint64_t o = lb->off;
        int32_t st;
        kr_node nd;
        ps->pos++;
        for (;;) {
            int32_t e = -1;
            if (cur(ps)->t != T_SEMI && cur(ps)->t != T_RBRK) {
                if ((st = parse_expr(ps, depth + 1u, &e)) != ZXV_OK) return st;
            }
            if (np >= 2) return fail(ps, KR_E_RANK, l, c, o, "more than two indices: KRON arrays have rank <= 2");
            parts[np++] = e;
            if (cur(ps)->t == T_SEMI) {
                ps->pos++;
                continue;
            }
            if (cur(ps)->t == T_RBRK) {
                ps->pos++;
                break;
            }
            return fail(ps, KR_E_SYNTAX, cur(ps)->line, cur(ps)->col, cur(ps)->off, "expected ] or ;");
        }
        if (np == 1) {
            if (parts[0] < 0) return fail(ps, KR_E_SYNTAX, l, c, o, "an empty index x[]");
            nd = blank(N_DYA, l, c, o);
            nd.fn = F_INDEX;
            nd.a = *noun;
            nd.b = parts[0];
        } else {
            nd = blank(N_INDEX2, l, c, o);
            nd.a = *noun;
            nd.b = parts[0];
            nd.c = parts[1];
        }
        if ((st = mk(ps, &nd, noun)) != ZXV_OK) return st;
    }
    return ZXV_OK;
}

static const char *op_list(void) {
    return "a scalar dyadic function (+ - × ÷ ⌈ ⌊ | * < ≤ = ≥ > ≠ ∧ ∨, or K's + - * % | & < > =)";
}

static int32_t fn_item(P *ps, item *it) {
    const tok *t = cur(ps);
    memset(it, 0, sizeof *it);
    it->kind = IT_FN;
    it->fk = FK_PLAIN;
    it->mon = t->mon;
    it->dya = t->dya;
    it->dya_not = t->dya_not;
    it->mon_no = t->mon_no;
    it->dya_no = t->dya_no;
    memcpy(it->sp, t->name, sizeof it->sp);
    it->line = t->line;
    it->col = t->col;
    it->off = t->off;
    it->node = -1;
    ps->pos++;
    if (cur(ps)->t == T_ADV) {
        const tok *a = cur(ps);
        if (!F_IS_SCALAR2(it->dya) || it->dya_not)
            return kr_diag_at(ps->diag, KR_E_SYNTAX, a->line, a->col, a->off, "the operator %s needs %s",
                              a->name, op_list());
        switch (a->adv) {
        case A_EACHLEFT: it->fk = FK_OUTER; break;
        case A_RED_LAST: it->fk = FK_RED; it->variant = V_LAST; break;
        case A_RED_FIRST: it->fk = FK_RED; it->variant = V_FIRST; break;
        case A_FOLDL: it->fk = FK_RED; it->variant = V_LEFT; break;
        case A_SCAN_LAST: it->fk = FK_SCAN; it->variant = V_LAST; break;
        case A_SCAN_FIRST: it->fk = FK_SCAN; it->variant = V_FIRST; break;
        default: it->fk = FK_SCAN; it->variant = V_LEFT; break;
        }
        ps->pos++;
        if (cur(ps)->t == T_ADV || cur(ps)->t == T_DOT)
            return fail(ps, KR_E_SYNTAX, cur(ps)->line, cur(ps)->col, cur(ps)->off,
                        "one operator per function in KRON");
    } else if (cur(ps)->t == T_DOT && peek(ps, 1)->t == T_FN) {
        const tok *g = peek(ps, 1);
        if (!F_IS_SCALAR2(it->dya) || it->dya_not || !F_IS_SCALAR2(g->dya) || g->dya_not)
            return kr_diag_at(ps->diag, KR_E_SYNTAX, cur(ps)->line, cur(ps)->col, cur(ps)->off,
                              "inner product f.g needs %s on each side", op_list());
        it->fk = FK_INNER;
        it->fn2 = g->dya;
        ps->pos += 2u;
        if (cur(ps)->t == T_ADV || cur(ps)->t == T_DOT)
            return fail(ps, KR_E_SYNTAX, cur(ps)->line, cur(ps)->col, cur(ps)->off,
                        "one operator per function in KRON");
    }
    return ZXV_OK;
}

static int32_t parse_expr(P *ps, uint32_t depth, int32_t *out) {
    items its = {NULL, 0, 0};
    int32_t st = ZXV_OK, curn;
    int32_t k;
    const tok *start = cur(ps);
    if (depth > KR_MAX_DEPTH)
        return kr_diag_at(ps->diag, KR_E_LIMIT, start->line, start->col, start->off,
                          "parentheses nested deeper than %u", (unsigned) KR_MAX_DEPTH);
    for (;;) {
        const tok *t = cur(ps);
        item it;
        int stop = 0;
        memset(&it, 0, sizeof it);
        it.line = t->line;
        it.col = t->col;
        it.off = t->off;
        it.node = -1;
        it.sym = -1;
        switch (t->t) {
        case T_END:
        case T_SEP:
        case T_RPAR:
        case T_RBRK:
        case T_SEMI: stop = 1; break;
        case T_NUM:
        case T_TRUTH:
            it.kind = IT_NOUN;
            st = strand(ps, &it.node);
            break;
        case T_EMPTY: {
            kr_node nd = blank(N_VEC, t->line, t->col, t->off);
            ps->pos++;
            it.kind = IT_NOUN;
            st = mk(ps, &nd, &it.node);
            break;
        }
        case T_NAME:
            if (peek(ps, 1)->t == T_ASSIGN) {
                it.kind = IT_ASSIGN;
                st = add_sym(ps->p, t->name, &it.sym);
                ps->pos += 2u;
            } else {
                kr_node nd = blank(N_NAME, t->line, t->col, t->off);
                it.kind = IT_NOUN;
                if ((st = add_sym(ps->p, t->name, &nd.sym)) == ZXV_OK) st = mk(ps, &nd, &it.node);
                ps->pos++;
            }
            break;
        case T_LPAR:
            ps->pos++;
            it.kind = IT_NOUN;
            if (cur(ps)->t == T_RPAR) {
                kr_node nd = blank(N_VEC, t->line, t->col, t->off);
                ps->pos++;
                st = mk(ps, &nd, &it.node);
                break;
            }
            if ((st = parse_expr(ps, depth + 1u, &it.node)) != ZXV_OK) break;
            if (cur(ps)->t != T_RPAR) {
                st = fail(ps, KR_E_SYNTAX, cur(ps)->line, cur(ps)->col, cur(ps)->off, "expected )");
                break;
            }
            ps->pos++;
            break;
        case T_FN: st = fn_item(ps, &it); break;
        case T_JOTDOT: {
            const tok *g = peek(ps, 1);
            if (g->t != T_FN || !F_IS_SCALAR2(g->dya) || g->dya_not) {
                st = kr_diag_at(ps->diag, KR_E_SYNTAX, t->line, t->col, t->off, "∘. needs %s", op_list());
                break;
            }
            it.kind = IT_FN;
            it.fk = FK_OUTER;
            it.dya = g->dya;
            memcpy(it.sp, "∘.", sizeof "∘.");
            ps->pos += 2u;
            if (cur(ps)->t == T_ADV || cur(ps)->t == T_DOT)
                st = fail(ps, KR_E_SYNTAX, cur(ps)->line, cur(ps)->col, cur(ps)->off,
                          "one operator per function in KRON");
            break;
        }
        case T_ADV:
            st = kr_diag_at(ps->diag, KR_E_SYNTAX, t->line, t->col, t->off,
                            "the operator %s needs a function on its left (compress and replicate are not in KRON)",
                            t->name);
            break;
        case T_DOT:
            st = fail(ps, KR_E_SYNTAX, t->line, t->col, t->off, "inner product f.g needs a function on each side");
            break;
        case T_ASSIGN:
            st = fail(ps, KR_E_SYNTAX, t->line, t->col, t->off, "assignment needs a name on its left");
            break;
        case T_LBRK:
            st = fail(ps, KR_E_SYNTAX, t->line, t->col, t->off, "indexing needs a value on its left");
            break;
        default: st = fail(ps, KR_E_SYNTAX, t->line, t->col, t->off, "unexpected token"); break;
        }
        if (st != ZXV_OK || stop) break;
        if (it.kind == IT_NOUN && (st = index_suffix(ps, depth, &it.node)) != ZXV_OK) break;
        if ((st = push_item(&its, &it)) != ZXV_OK) break;
    }
    if (st != ZXV_OK) goto done;
    if (its.n == 0) {
        st = fail(ps, KR_E_SYNTAX, start->line, start->col, start->off, "an empty expression");
        goto done;
    }
    for (uint32_t q = 1; q < its.n; q++)
        if (its.v[q].kind == IT_NOUN && its.v[q - 1u].kind == IT_NOUN) {
            st = fail(ps, KR_E_SYNTAX, its.v[q].line, its.v[q].col, its.v[q].off,
                      "two values side by side with no function between them");
            goto done;
        }
    if (its.v[its.n - 1u].kind != IT_NOUN) {
        const item *l = &its.v[its.n - 1u];
        st = kr_diag_at(ps->diag, KR_E_SYNTAX, l->line, l->col, l->off, "'%s' has no right argument",
                        l->kind == IT_ASSIGN ? "assignment" : l->sp);
        goto done;
    }
    curn = its.v[its.n - 1u].node;
    k = (int32_t) its.n - 2;
    while (k >= 0) {
        const item *it = &its.v[k];
        kr_node nd = blank(N_MON, it->line, it->col, it->off);
        if (it->kind == IT_NOUN) {
            st = fail(ps, KR_E_SYNTAX, it->line, it->col, it->off, "a value with no function on its right");
            goto done;
        }
        if (it->kind == IT_ASSIGN) {
            nd.kind = N_LET;
            nd.sym = it->sym;
            nd.a = curn;
            if ((st = mk(ps, &nd, &curn)) != ZXV_OK) goto done;
            k--;
            continue;
        }
        if (k >= 1 && its.v[k - 1].kind == IT_NOUN) {
            int32_t left = its.v[k - 1].node;
            switch (it->fk) {
            case FK_PLAIN:
                if (!it->dya) {
                    if (it->dya_no) st = fail(ps, KR_E_SYNTAX, it->line, it->col, it->off, it->dya_no);
                    else st = kr_diag_at(ps->diag, KR_E_SYNTAX, it->line, it->col, it->off,
                                         "'%s' has no dyadic form", it->sp);
                    goto done;
                }
                nd.kind = N_DYA;
                nd.fn = it->dya;
                nd.a = left;
                nd.b = curn;
                if ((st = mk(ps, &nd, &curn)) != ZXV_OK) goto done;
                if (it->dya_not) {
                    kr_node nn = blank(N_MON, it->line, it->col, it->off);
                    nn.fn = F_NOT;
                    nn.a = curn;
                    if ((st = mk(ps, &nn, &curn)) != ZXV_OK) goto done;
                }
                break;
            case FK_RED:
            case FK_SCAN:
                st = fail(ps, KR_E_SYNTAX, it->line, it->col, it->off,
                          "a reduction or scan takes no left argument (n-wise reduction is not in KRON)");
                goto done;
            case FK_OUTER:
                nd.kind = N_OUTER;
                nd.fn = it->dya;
                nd.a = left;
                nd.b = curn;
                if ((st = mk(ps, &nd, &curn)) != ZXV_OK) goto done;
                break;
            default:
                nd.kind = N_INNER;
                nd.fn = it->dya;
                nd.fn2 = it->fn2;
                nd.a = left;
                nd.b = curn;
                if ((st = mk(ps, &nd, &curn)) != ZXV_OK) goto done;
                break;
            }
            k -= 2;
        } else {
            switch (it->fk) {
            case FK_PLAIN:
                if (!it->mon) {
                    if (it->mon_no) st = fail(ps, KR_E_SYNTAX, it->line, it->col, it->off, it->mon_no);
                    else st = kr_diag_at(ps->diag, KR_E_SYNTAX, it->line, it->col, it->off,
                                         "'%s' has no monadic form", it->sp);
                    goto done;
                }
                nd.fn = it->mon;
                nd.a = curn;
                break;
            case FK_RED:
            case FK_SCAN:
                nd.kind = it->fk == FK_RED ? N_RED : N_SCAN;
                nd.fn = it->dya;
                nd.variant = it->variant;
                nd.a = curn;
                break;
            default:
                st = kr_diag_at(ps->diag, KR_E_SYNTAX, it->line, it->col, it->off,
                                "'%s' needs a left argument", it->fk == FK_OUTER ? "outer product" : "inner product");
                goto done;
            }
            if ((st = mk(ps, &nd, &curn)) != ZXV_OK) goto done;
            k--;
        }
    }
    *out = curn;
done:
    free(its.v);
    return st;
}

static int32_t parse_ak(P *ps) {
    int32_t st;
    if ((st = lex_ak(ps)) != ZXV_OK) return st;
    ps->pos = 0;
    for (;;) {
        int32_t node;
        while (cur(ps)->t == T_SEP) ps->pos++;
        if (cur(ps)->t == T_END) break;
        if ((st = parse_expr(ps, 0, &node)) != ZXV_OK) return st;
        if (cur(ps)->t != T_SEP && cur(ps)->t != T_END) {
            const tok *t = cur(ps);
            return kr_diag_at(ps->diag, KR_E_SYNTAX, t->line, t->col, t->off, "unmatched '%s'", t->name[0] ? t->name : (t->t == T_RPAR ? ")" : "]"));
        }
        if ((st = push_stmt(ps->p, node)) != ZXV_OK) return st;
    }
    if (ps->p->nstmts == 0)
        return fail(ps, KR_E_SYNTAX, 1, 1, 0, "no statement");
    return ZXV_OK;
}

/* ===== Parsing canonical KRON ===== */

enum { CT_END = 0, CT_LP, CT_RP, CT_WORD, CT_NUM, CT_UNDER };

typedef struct {
    uint8_t t;
    char w[KR_MAX_NAME + 1];
    int64_t num, den;
    uint32_t line, col;
    uint64_t off;
} ctok;

static int32_t cnext(P *ps, ctok *t) {
    memset(t, 0, sizeof *t);
    while (ps->i < ps->n) {
        uint8_t c = ps->s[ps->i];
        if (c == '\n') {
            ps->line++;
            ps->line_start = ++ps->i;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            ps->i++;
        } else if (c == ';') { /* a comment to the end of the line; never printed */
            while (ps->i < ps->n && ps->s[ps->i] != '\n') ps->i++;
        } else {
            break;
        }
    }
    here(ps, ps->i, &t->line, &t->col);
    t->off = ps->i;
    if (ps->i >= ps->n) {
        t->t = CT_END;
        return ZXV_OK;
    }
    {
        uint8_t c = ps->s[ps->i];
        if (c == '(') {
            t->t = CT_LP;
            ps->i++;
        } else if (c == ')') {
            t->t = CT_RP;
            ps->i++;
        } else if (c == '_') {
            t->t = CT_UNDER;
            ps->i++;
        } else if (is_digit(c) || (c == '-' && ps->i + 1u < ps->n && is_digit(ps->s[ps->i + 1u]))) {
            int neg = c == '-';
            t->t = CT_NUM;
            return lex_digits(ps, ps->i + (neg ? 1u : 0u), neg, t->line, t->col, t->off, &t->num, &t->den, &ps->i);
        } else if (is_alpha(c)) {
            size_t j = ps->i;
            while (j < ps->n && is_alnum(ps->s[j])) j++;
            if (j - ps->i > KR_MAX_NAME)
                return kr_diag_at(ps->diag, KR_E_NAME, t->line, t->col, t->off, "a name longer than %u bytes",
                                  (unsigned) KR_MAX_NAME);
            memcpy(t->w, ps->s + ps->i, j - ps->i);
            t->w[j - ps->i] = '\0';
            t->t = CT_WORD;
            ps->i = j;
        } else {
            char ch[8];
            char_at(ps, ps->i, ch);
            return kr_diag_at(ps->diag, KR_E_SYNTAX, t->line, t->col, t->off,
                              "'%s' is not in canonical KRON", ch);
        }
    }
    return ZXV_OK;
}

static int32_t cexpect(P *ps, uint8_t want, ctok *t, const char *what) {
    int32_t st = cnext(ps, t);
    if (st != ZXV_OK) return st;
    if (t->t != want) return kr_diag_at(ps->diag, KR_E_SYNTAX, t->line, t->col, t->off, "expected %s", what);
    return ZXV_OK;
}

static int32_t cexpr(P *ps, uint32_t depth, const ctok *first, int32_t *out);

static int32_t cexpr_next(P *ps, uint32_t depth, int32_t *out) {
    ctok t;
    int32_t st = cnext(ps, &t);
    if (st != ZXV_OK) return st;
    return cexpr(ps, depth, &t, out);
}

static int32_t cfn(P *ps, int scalar2, uint8_t *out) {
    ctok t;
    int32_t st = cexpect(ps, CT_WORD, &t, "a function word");
    int f;
    if (st != ZXV_OK) return st;
    f = fn_by_word(t.w);
    if (!f || (scalar2 && !F_IS_SCALAR2(f)))
        return kr_diag_at(ps->diag, KR_E_SYNTAX, t.line, t.col, t.off, "'%s' is not %s", t.w,
                          scalar2 ? "a scalar dyadic function word" : "a function word");
    *out = (uint8_t) f;
    return ZXV_OK;
}

static int32_t clit(P *ps, kr_node *nd, int truth, uint64_t want) {
    ctok t;
    int32_t st;
    uint64_t cnt = 0;
    for (;;) {
        uint32_t li;
        if ((st = cnext(ps, &t)) != ZXV_OK) return st;
        if (t.t == CT_RP) break;
        if (truth) {
            int tv = t.t == CT_WORD ? truth_by_word(t.w) : -1;
            if (tv < 0) return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off, "expected a truth word or )");
            t.num = tv;
            t.den = 1;
        } else if (t.t != CT_NUM) {
            return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off, "expected a number or )");
        }
        if ((st = add_lit(ps->p, t.num, t.den, &li)) != ZXV_OK) {
            if (st == KR_E_LIMIT) return fail(ps, st, t.line, t.col, t.off, "too many literal elements");
            return st;
        }
        if (cnt == 0) nd->lit = li;
        cnt++;
    }
    if (want != UINT64_MAX && cnt != want)
        return kr_diag_at(ps->diag, KR_E_LENGTH, t.line, t.col, t.off, "%llu elements where the shape needs %llu",
                          (unsigned long long) cnt, (unsigned long long) want);
    nd->cnt = (uint32_t) cnt;
    return ZXV_OK;
}

static int32_t cextent(P *ps, uint32_t *out) {
    ctok t;
    int32_t st = cexpect(ps, CT_NUM, &t, "an extent");
    if (st != ZXV_OK) return st;
    if (t.den != 1 || t.num < 0 || t.num > (int64_t) KR_MAX_ELEMENTS)
        return fail(ps, KR_E_DOMAIN, t.line, t.col, t.off, "an extent must be a whole number 0..2^22");
    *out = (uint32_t) t.num;
    return ZXV_OK;
}

static int32_t cexpr(P *ps, uint32_t depth, const ctok *t, int32_t *out) {
    kr_node nd = blank(N_NUM, t->line, t->col, t->off);
    int32_t st;
    ctok h;
    if (depth > KR_MAX_DEPTH)
        return kr_diag_at(ps->diag, KR_E_LIMIT, t->line, t->col, t->off, "nested deeper than %u",
                          (unsigned) KR_MAX_DEPTH);
    switch (t->t) {
    case CT_NUM:
        if ((st = add_lit(ps->p, t->num, t->den, &nd.lit)) != ZXV_OK) return st;
        nd.cnt = 1;
        return mk(ps, &nd, out);
    case CT_WORD: {
        int tv = truth_by_word(t->w);
        if (tv >= 0) {
            nd.kind = N_TRUTH;
            if ((st = add_lit(ps->p, tv, 1, &nd.lit)) != ZXV_OK) return st;
            nd.cnt = 1;
            return mk(ps, &nd, out);
        }
        if (is_reserved(t->w))
            return kr_diag_at(ps->diag, KR_E_SYNTAX, t->line, t->col, t->off, "'%s' needs parentheses", t->w);
        nd.kind = N_NAME;
        if ((st = add_sym(ps->p, t->w, &nd.sym)) != ZXV_OK) return st;
        return mk(ps, &nd, out);
    }
    case CT_LP: break;
    default: return fail(ps, KR_E_SYNTAX, t->line, t->col, t->off, "expected an expression");
    }
    if ((st = cexpect(ps, CT_WORD, &h, "a word after (")) != ZXV_OK) return st;
    if (strcmp(h.w, "vec") == 0 || strcmp(h.w, "tvec") == 0) {
        nd.kind = h.w[0] == 't' ? N_TVEC : N_VEC;
        if ((st = clit(ps, &nd, nd.kind == N_TVEC, UINT64_MAX)) != ZXV_OK) return st;
        return mk(ps, &nd, out);
    }
    if (strcmp(h.w, "mat") == 0 || strcmp(h.w, "tmat") == 0) {
        nd.kind = h.w[0] == 't' ? N_TMAT : N_MAT;
        if ((st = cextent(ps, &nd.rows)) != ZXV_OK || (st = cextent(ps, &nd.cols)) != ZXV_OK) return st;
        if ((uint64_t) nd.rows * nd.cols > KR_MAX_ELEMENTS)
            return fail(ps, KR_E_LIMIT, h.line, h.col, h.off, "a matrix literal above the element limit");
        if ((st = clit(ps, &nd, nd.kind == N_TMAT, (uint64_t) nd.rows * nd.cols)) != ZXV_OK) return st;
        return mk(ps, &nd, out);
    }
    if (strcmp(h.w, "let") == 0) {
        ctok nm;
        if ((st = cexpect(ps, CT_WORD, &nm, "a name")) != ZXV_OK) return st;
        if (is_reserved(nm.w))
            return kr_diag_at(ps->diag, KR_E_NAME, nm.line, nm.col, nm.off, "'%s' is reserved", nm.w);
        nd.kind = N_LET;
        if ((st = add_sym(ps->p, nm.w, &nd.sym)) != ZXV_OK) return st;
        if ((st = cexpr_next(ps, depth + 1u, &nd.a)) != ZXV_OK) return st;
    } else if (!strcmp(h.w, "reduce") || !strcmp(h.w, "reducef") || !strcmp(h.w, "foldl") ||
               !strcmp(h.w, "scan") || !strcmp(h.w, "scanf") || !strcmp(h.w, "scanl")) {
        nd.kind = h.w[0] == 's' ? N_SCAN : N_RED;
        nd.variant = (!strcmp(h.w, "reduce") || !strcmp(h.w, "scan")) ? V_LAST
                     : (!strcmp(h.w, "foldl") || !strcmp(h.w, "scanl")) ? V_LEFT
                                                                          : V_FIRST;
        if ((st = cfn(ps, 1, &nd.fn)) != ZXV_OK) return st;
        if ((st = cexpr_next(ps, depth + 1u, &nd.a)) != ZXV_OK) return st;
    } else if (!strcmp(h.w, "outer") || !strcmp(h.w, "inner")) {
        nd.kind = h.w[0] == 'o' ? N_OUTER : N_INNER;
        if ((st = cfn(ps, 1, &nd.fn)) != ZXV_OK) return st;
        if (nd.kind == N_INNER && (st = cfn(ps, 1, &nd.fn2)) != ZXV_OK) return st;
        if ((st = cexpr_next(ps, depth + 1u, &nd.a)) != ZXV_OK) return st;
        if ((st = cexpr_next(ps, depth + 1u, &nd.b)) != ZXV_OK) return st;
    } else if (!strcmp(h.w, "index2")) {
        ctok q;
        nd.kind = N_INDEX2;
        if ((st = cexpr_next(ps, depth + 1u, &nd.a)) != ZXV_OK) return st;
        for (int k = 0; k < 2; k++) {
            int32_t e = -1;
            if ((st = cnext(ps, &q)) != ZXV_OK) return st;
            if (q.t != CT_UNDER && (st = cexpr(ps, depth + 1u, &q, &e)) != ZXV_OK) return st;
            if (k == 0) nd.b = e;
            else nd.c = e;
        }
        if (nd.b < 0 && nd.c < 0)
            return fail(ps, KR_E_SYNTAX, h.line, h.col, h.off, "index2 with both indices elided");
    } else {
        int f = fn_by_word(h.w);
        if (!f) return kr_diag_at(ps->diag, KR_E_SYNTAX, h.line, h.col, h.off, "unknown word '%s'", h.w);
        nd.fn = (uint8_t) f;
        if (F_IS_MONADIC(f)) {
            nd.kind = N_MON;
            if ((st = cexpr_next(ps, depth + 1u, &nd.a)) != ZXV_OK) return st;
        } else {
            nd.kind = N_DYA;
            if ((st = cexpr_next(ps, depth + 1u, &nd.a)) != ZXV_OK) return st;
            if ((st = cexpr_next(ps, depth + 1u, &nd.b)) != ZXV_OK) return st;
        }
    }
    if ((st = cexpect(ps, CT_RP, &h, ")")) != ZXV_OK) return st;
    return mk(ps, &nd, out);
}

static int32_t parse_canon(P *ps) {
    ctok t;
    int32_t st = cnext(ps, &t);
    if (st != ZXV_OK) return st;
    if (t.t != CT_WORD || strcmp(t.w, "kron") != 0)
        return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off, "canonical KRON starts with the word kron");
    for (;;) {
        int32_t node;
        if ((st = cnext(ps, &t)) != ZXV_OK) return st;
        if (t.t == CT_END) break;
        if ((st = cexpr(ps, 0, &t, &node)) != ZXV_OK) return st;
        if ((st = push_stmt(ps->p, node)) != ZXV_OK) return st;
    }
    if (ps->p->nstmts == 0) return fail(ps, KR_E_SYNTAX, t.line, t.col, t.off, "no statement");
    return ZXV_OK;
}

/* ===== Analysis: static ranks, in evaluation order (right before left),
 * and the canonical rewrites they allow:
 *   reducef f x, scanf f x            -> reduce/scan when x has rank <= 1
 *   foldl f x, scanl f x (f assoc.)   -> reducef/scanf, or reduce/scan
 *   revf x -> rev x and catf -> cat   when the ranks are <= 1
 * Each rewrite names the same function, so it changes no result and no
 * refusal. Unknown ranks (free names) are left alone. ===== */

typedef struct {
    kr_program *p;
    int8_t *rank;
    uint8_t *set;
    uint8_t *isfree;
} A;

static int8_t joinr(int8_t a, int8_t b) {
    if (a < 0 || b < 0) return -1;
    if (a == 0) return b;
    if (b == 0) return a;
    return a == b ? a : -1;
}

static int32_t note_free(A *an, int32_t sym) {
    kr_program *p = an->p;
    if (an->isfree[sym]) return ZXV_OK;
    an->isfree[sym] = 1;
    {
        int32_t *a = (int32_t *) realloc(p->free_syms, (p->nfree + 1u) * sizeof *a);
        if (!a) return ZXV_E_NOMEM;
        p->free_syms = a;
        p->free_syms[p->nfree++] = sym;
    }
    return ZXV_OK;
}

static int32_t analyze(A *an, int32_t n, int8_t *out) {
    kr_node *nd = &an->p->nodes[n];
    int8_t ra = -1, rb = -1, rc = -1, r = -1;
    int32_t st;
    /* evaluation order: b, c (indices) before a */
    if (nd->b >= 0 && (st = analyze(an, nd->b, &rb)) != ZXV_OK) return st;
    if (nd->c >= 0 && (st = analyze(an, nd->c, &rc)) != ZXV_OK) return st;
    if (nd->a >= 0 && (st = analyze(an, nd->a, &ra)) != ZXV_OK) return st;
    nd = &an->p->nodes[n];
    switch (nd->kind) {
    case N_NUM:
    case N_TRUTH: r = 0; break;
    case N_VEC:
    case N_TVEC: r = 1; break;
    case N_MAT:
    case N_TMAT: r = 2; break;
    case N_NAME:
        if (an->set[nd->sym]) r = an->rank[nd->sym];
        else if ((st = note_free(an, nd->sym)) != ZXV_OK) return st;
        break;
    case N_LET:
        r = ra;
        an->set[nd->sym] = 1;
        an->rank[nd->sym] = ra;
        break;
    case N_MON:
        switch (nd->fn) {
        case F_IOTA:
        case F_SHAPE:
        case F_RAVEL:
        case F_GUP:
        case F_GDOWN: r = 1; break;
        case F_TALLY: r = 0; break;
        case F_REVF:
            if (ra >= 0 && ra <= 1) nd->fn = F_REV;
            r = ra;
            break;
        default: r = ra; break;
        }
        break;
    case N_DYA:
        if (F_IS_SCALAR2(nd->fn)) {
            r = joinr(ra, rb);
        } else {
            switch (nd->fn) {
            case F_MATCH: r = 0; break;
            case F_RESHAPE: {
                const kr_node *l = &an->p->nodes[nd->a];
                if (l->kind == N_NUM) r = 1;
                else if (l->kind == N_VEC && l->cnt <= 2u) r = (int8_t) l->cnt;
                break;
            }
            case F_CATF:
                if (ra >= 0 && rb >= 0 && ra <= 1 && rb <= 1) nd->fn = F_CAT;
                /* fall through */
            case F_CAT:
                if (ra >= 0 && rb >= 0) r = (int8_t) (ra > rb ? (ra > 1 ? ra : 1) : (rb > 1 ? rb : 1));
                break;
            case F_TAKE:
            case F_DROP:
                if (rb >= 1) r = rb;
                break;
            case F_ROTATE: r = rb; break;
            case F_FIND: r = rb; break;
            case F_INDEX:
                if (ra == 1) r = rb;
                else if (ra == 2 && rb >= 0) r = (int8_t) (rb + 1);
                break;
            default: break;
            }
        }
        break;
    case N_RED:
    case N_SCAN:
        if (nd->variant == V_LEFT && F_IS_ASSOC(nd->fn)) nd->variant = V_FIRST;
        if (nd->variant == V_FIRST && ra >= 0 && ra <= 1) nd->variant = V_LAST;
        if (nd->kind == N_SCAN) r = ra;
        else if (ra >= 0) r = (int8_t) (ra > 0 ? ra - 1 : 0);
        break;
    case N_OUTER:
        if (ra >= 0 && rb >= 0) r = (int8_t) (ra + rb);
        break;
    case N_INNER:
        if (ra >= 0 && rb >= 0) r = (int8_t) (ra + rb >= 2 ? ra + rb - 2 : 0);
        break;
    case N_INDEX2: {
        int8_t ri = nd->b < 0 ? 1 : rb, rj = nd->c < 0 ? 1 : rc;
        if (ri >= 0 && rj >= 0) r = (int8_t) (ri + rj);
        break;
    }
    default: break;
    }
    if (r > 2) r = -1; /* refused at run time */
    nd->rank = r;
    *out = r;
    return ZXV_OK;
}

/* ===== Entry points ===== */

void kr_program_destroy(kr_program *p) {
    if (!p) return;
    free(p->nodes);
    free(p->stmts);
    free(p->syms);
    free(p->lnum);
    free(p->lden);
    free(p->free_syms);
    free(p->text);
    free(p);
}

int32_t kr_parse(const char *text, size_t len, int32_t spelling, kr_program **out_program,
                 kr_diag *diag) {
    P ps;
    kr_program *p;
    int32_t st;
    size_t ok;
    if (diag) memset(diag, 0, sizeof *diag);
    if (!out_program) return ZXV_E_ARG;
    *out_program = NULL;
    if (!text && len) return ZXV_E_ARG;
    if (spelling < KR_SPELL_AUTO || spelling > KR_SPELL_CANON) return ZXV_E_ARG;
    if (len > KR_MAX_TEXT) return kr_diag_at(diag, KR_E_LIMIT, 0, 0, 0, "the text is longer than %u bytes", (unsigned) KR_MAX_TEXT);
    ok = kr_utf8_check((const uint8_t *) text, len);
    if (ok != len) {
        uint32_t line = 1, col = 1;
        for (size_t q = 0; q < ok; q++) {
            if (text[q] == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
        }
        return kr_diag_at(diag, ZXV_E_UTF8, line, col, ok, "invalid UTF-8");
    }
    if (spelling == KR_SPELL_AUTO) {
        size_t q = 0;
        while (q < len && (text[q] == ' ' || text[q] == '\t' || text[q] == '\r' || text[q] == '\n')) q++;
        if (len - q >= 4u && memcmp(text + q, "kron", 4) == 0 &&
            (len - q == 4u || !is_alnum((uint8_t) text[q + 4u]))) {
            spelling = KR_SPELL_CANON;
        } else {
            spelling = KR_SPELL_K;
            for (q = 0; q < len; q++)
                if ((uint8_t) text[q] >= 0x80u) {
                    spelling = KR_SPELL_APL;
                    break;
                }
        }
    }
    p = (kr_program *) calloc(1, sizeof *p);
    if (!p) return ZXV_E_NOMEM;
    p->spelling = spelling;
    memset(&ps, 0, sizeof ps);
    ps.p = p;
    ps.s = (const uint8_t *) text;
    ps.n = len;
    ps.line = 1;
    ps.diag = diag;
    ps.spelling = spelling;
    st = spelling == KR_SPELL_CANON ? parse_canon(&ps) : parse_ak(&ps);
    free(ps.toks);
    if (st == ZXV_OK) {
        A an;
        an.p = p;
        an.rank = (int8_t *) calloc(p->nsyms + 1u, 1);
        an.set = (uint8_t *) calloc(p->nsyms + 1u, 1);
        an.isfree = (uint8_t *) calloc(p->nsyms + 1u, 1);
        if (!an.rank || !an.set || !an.isfree) st = ZXV_E_NOMEM;
        for (uint32_t s = 0; st == ZXV_OK && s < p->nstmts; s++) {
            int8_t r;
            st = analyze(&an, p->stmts[s], &r);
        }
        free(an.rank);
        free(an.set);
        free(an.isfree);
    }
    if (st == ZXV_OK) st = kr_print_program(p, &p->text, &p->text_len);
    if (st == ZXV_OK) st = zxv_sha3_256((const uint8_t *) p->text, p->text_len, p->digest);
    if (st != ZXV_OK) {
        if (st == ZXV_E_NOMEM) kr_diag_at(diag, st, 0, 0, 0, "out of memory");
        kr_program_destroy(p);
        return st;
    }
    *out_program = p;
    return ZXV_OK;
}

int32_t kr_program_text(const kr_program *p, char *buf, size_t cap, size_t *out_len) {
    if (!p) return ZXV_E_ARG;
    return kr_out_text(p->text, p->text_len, buf, cap, out_len);
}

int32_t kr_program_digest(const kr_program *p, uint8_t out[32]) {
    if (!p || !out) return ZXV_E_ARG;
    memcpy(out, p->digest, 32);
    return ZXV_OK;
}

int32_t kr_program_spelling(const kr_program *p, int32_t *out) {
    if (!p || !out) return ZXV_E_ARG;
    *out = p->spelling;
    return ZXV_OK;
}

int32_t kr_free_count(const kr_program *p, uint32_t *out) {
    if (!p || !out) return ZXV_E_ARG;
    *out = p->nfree;
    return ZXV_OK;
}

int32_t kr_free_at(const kr_program *p, uint32_t index, char *buf, size_t cap, size_t *out_len) {
    const char *nm;
    if (!p) return ZXV_E_ARG;
    if (index >= p->nfree) return ZXV_E_NOT_FOUND;
    nm = p->syms[p->free_syms[index]].name;
    return kr_out_text(nm, strlen(nm), buf, cap, out_len);
}
