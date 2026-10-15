/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_algol.c — ALGOL 60 front-end: the Revised Report on the Algorithmic
 * Language ALGOL 60 (1963), integer and Boolean subset, and its twin
 * "Rootwyrm" (the root most later languages grew from).
 *
 * Hardware representation. The Report fixes the language but not its
 * characters, and there is no single standard; this is the one the GNU
 * MARST translator reads. Keywords are reserved lower-case words without
 * quotes: begin end integer Boolean procedure value array if then else for
 * step until while do goto (or go to) true false comment. Identifiers are
 * letters and digits, case-sensitive. Strings are in double quotes with the
 * escapes \n \t \\ \". The operators are
 *     :=   + - * / ^ (or **)   % (or div: integer division)
 *     < <= = >= > != (not equal)   ! (not) & (and) | (or) -> (implies)
 *     == (equivalent)
 * and the Report's own symbols are read too, in UTF-8: × ÷ ↑ ≠ ≤ ≥ ¬ ∧ ∨ ⊃ ≡.
 *
 * Admitted: blocks declaring integer and Boolean variables and one-
 * dimensional integer and Boolean arrays a[l:u], whose bounds may be
 * expressions of variables of enclosing blocks, evaluated when the block is
 * entered (dynamic arrays); procedures, integer and Boolean procedures
 * (the value is assigned to the procedure's name), with value, name and
 * array parameters, nested to any depth and recursive; assignment, also
 * multiple (a := b := e); compound statements; if/then/else statements and
 * conditional expressions; for statements with any list of arithmetic,
 * step-until and while elements; goto a label of the same or an enclosing
 * block of the same procedure; comment; and from MARST's library
 * outinteger(1, e), outstring(1, "text"), ininteger(0, v), abs, iabs, sign
 * and entier.
 *
 * Semantics follow the Report: the expansion of 4.6.4 for for statements
 * (the step and the limit are evaluated again on every round, in the order
 * controlled variable, limit, step), left-part subscripts before the right
 * side, every operand evaluated (no short circuit), '^' associates to the
 * left. Words are 64 bits. Exact subset: '/' is the Report's real division,
 * so it must divide exactly or the program stops with ZL_E_INEXACT; '%'
 * (the Report's ÷) truncates toward zero and needs integer operands; a ^ i
 * with an integer exponent stops with INEXACT when the result is not an
 * integer (2 ^ (-1)) and with DOMAIN for 0 ^ 0 and 0 ^ (-i), which the Report
 * leaves undefined; abs returns a real (exact) value as in the Report and
 * MARST, iabs an integer. A variable starts at 0 when its block is first
 * entered in that activation; a block entered again finds the values it
 * left (the Report leaves them undefined).
 *
 * Output (MARST's library): outinteger prints the number and one space
 * ("-5 "); outstring prints its bytes; nothing prints a newline unless the
 * program does. Only channel 1 (output) and channel 0 (input, read from the
 * caller's input list) exist.
 *
 * Refused by name (ZL_E_UNSUPPORTED): real numbers and declarations, own,
 * switch, string, label and procedure parameters, value arrays, plain
 * (real) arrays, arrays of more than one dimension, code bodies, numeric
 * labels, conditional designational expressions, a subscripted controlled
 * variable, end comments (text after end; write comment ... ; instead),
 * the real-valued library (sqrt, sin, ln, outreal, ...), other channels,
 * goto out of a procedure, and goto out of a block with dynamic arrays.
 *
 * Storage. The outermost program (level 0) keeps its variables in slots.
 * Each procedure activation is an IR frame (ENTER/LEAVE) whose static link
 * is the frame of the procedure's declaring level, so a variable of an
 * enclosing procedure is reached with LOADF/STOREF and the right number of
 * hops; inner blocks share their procedure's frame. ENTER takes a constant
 * size, so a block with dynamic arrays allocates its arrays in frames of 64
 * cells: a small recursive helper enters ceil(n/64) of them back to back
 * (frame cells are contiguous) and runs the block's statements inside the
 * last one, as one more static level whose link is the enclosing frame.
 * The arrays are then addressed by absolute address with LOADA/STOREA
 * against a (base, lower bound, length) descriptor and an explicit bounds
 * check. Leaving the block returns through the helper; that is why a goto
 * out of such a block is refused.
 *
 * Call by name. CALL needs a constant target, so a name parameter holds a
 * descriptor of two cells. A simple variable is passed by its address
 * (a slot, or a frame cell from ADDRF), which never changes during the call.
 * Anything else (an array element, a constant, any expression) is passed as
 * a thunk: a closed piece of code, compiled at the call site, that ENTERs a
 * frame of size 0 whose static link is the caller's frame, computes the
 * expression's value or the element's address afresh, and returns. The
 * descriptor then holds -(thunk number + 1) and the caller's frame handle.
 * Using the parameter calls one of two dispatchers built at the end of the
 * program, which find the thunk by binary search on its number and call it
 * (CALL is constant, the search is not): the reading dispatcher returns the
 * value, the address dispatcher the address, and stops with ZL_E_DOMAIN when
 * the actual parameter is an expression, which cannot be assigned. So
 * Jensen's device works: sum(i, 1, n, a[i] * i) re-evaluates its argument
 * on every use. The caller's frame outlives every use because no
 * procedure value can escape (procedure parameters are refused).
 *
 * The compiler makes two passes over the text. The first only parses and
 * records each block's declarations (and labels), so the second can resolve
 * a call to a procedure declared later in the same block, as ALGOL allows.
 * Statement and expression nesting are bounded by ZL_MAX_DEPTH.
 *
 * Rootwyrm (the twin) is the same program in one spelling, accepted by
 * MARST: one declaration or statement per line, two spaces of indentation
 * per open begin, statements under if/then/else, for/do and a procedure
 * heading indented one step unless they are begin ... end, which opens on
 * the same line ("then begin", "end else begin"); one space around binary
 * operators and :=, ", " between list items, minimal parentheses, no unary
 * plus, goto for go to, % for div and the ASCII operators for the Report's
 * symbols; empty statements dropped unless labelled; comments kept, with
 * their spacing collapsed, on a line of their own (inline in a procedure
 * heading). */
#include "zl_int.h"

enum {
    T_EOF = 0,
    T_ID,
    T_NUM,
    T_STR,
    K_ARRAY, /* keywords, in the order of kwname[] */
    K_BEGIN,
    K_BOOLEAN,
    K_CODE,
    K_COMMENT,
    K_DIV,
    K_DO,
    K_ELSE,
    K_END,
    K_FALSE,
    K_FOR,
    K_GO,
    K_GOTO,
    K_IF,
    K_INTEGER,
    K_LABEL,
    K_OWN,
    K_PROCEDURE,
    K_REAL,
    K_STEP,
    K_STRING,
    K_SWITCH,
    K_THEN,
    K_TRUE,
    K_UNTIL,
    K_VALUE,
    K_WHILE,
    O_ASSIGN, /* operators, in the order of opname[] */
    O_COLON,
    O_SEMI,
    O_COMMA,
    O_LP,
    O_RP,
    O_LB,
    O_RB,
    O_PLUS,
    O_MINUS,
    O_MUL,
    O_SLASH,
    O_PCT,
    O_POW,
    O_LT,
    O_LE,
    O_EQ,
    O_GE,
    O_GT,
    O_NE,
    O_NOT,
    O_AND,
    O_OR,
    O_IMP,
    O_EQV
};

static const char *const kwname[] = {
    "array", "begin", "Boolean", "code",   "comment", "div",     "do",    "else",  "end",
    "false", "for",   "go",      "goto",   "if",      "integer", "label", "own",   "procedure",
    "real",  "step",  "string",  "switch", "then",    "true",    "until", "value", "while"};
static const char *const opname[] = {
    ":=", ":", ";",  ",", "(",  ")", "[",  "]", "+", "-", "*",  "/", "%",
    "^",  "<", "<=", "=", ">=", ">", "!=", "!", "&", "|", "->", "=="};
static const struct {
    const char *s;
    int t;
} optab[] = {
    {":=", O_ASSIGN},
    {"<=", O_LE},
    {">=", O_GE},
    {"!=", O_NE},
    {"->", O_IMP},
    {"==", O_EQV},
    {"**", O_POW},
    {"\xc3\x97", O_MUL},
    {"\xc3\xb7", O_PCT},
    {"\xe2\x86\x91", O_POW},
    {"\xe2\x89\xa0", O_NE},
    {"\xe2\x89\xa4", O_LE},
    {"\xe2\x89\xa5", O_GE},
    {"\xc2\xac", O_NOT},
    {"\xe2\x88\xa7", O_AND},
    {"\xe2\x88\xa8", O_OR},
    {"\xe2\x8a\x83", O_IMP},
    {"\xe2\x89\xa1", O_EQV},
    {":", O_COLON},
    {";", O_SEMI},
    {",", O_COMMA},
    {"(", O_LP},
    {")", O_RP},
    {"[", O_LB},
    {"]", O_RB},
    {"+", O_PLUS},
    {"-", O_MINUS},
    {"*", O_MUL},
    {"/", O_SLASH},
    {"%", O_PCT},
    {"^", O_POW},
    {"<", O_LT},
    {"=", O_EQ},
    {">", O_GT},
    {"!", O_NOT},
    {"&", O_AND},
    {"|", O_OR},
};

enum { N_NUM = 1, N_BOOL, N_ID, N_ELEM, N_CALL, N_STR, N_NEG, N_NOT, N_BIN, N_REL, N_COND };
enum { TY_NONE = 0, TY_INT, TY_REAL, TY_BOOL };
enum { S_VAR = 1, S_ARR, S_NAME, S_PROC, S_LABEL, S_STD };
enum { F_VALUE = 1, F_NAME, F_ARRAY };
enum { L_MAIN = 0, L_PROC, L_DYN, L_THUNK };
enum { E_ARITH = 1, E_STEP, E_WHILE };
enum { C_PLAIN = 0, C_THEN };
enum { TG_CELL = 1, TG_SLOTX, TG_ADDR };
enum {
    STD_OUTINTEGER = 1,
    STD_OUTSTRING,
    STD_ININTEGER,
    STD_ABS,
    STD_IABS,
    STD_SIGN,
    STD_ENTIER,
    STD_REAL
};

static const struct {
    const char *nm;
    int which;
} stdtab[] = {
    {"outinteger", STD_OUTINTEGER},
    {"outstring", STD_OUTSTRING},
    {"ininteger", STD_ININTEGER},
    {"abs", STD_ABS},
    {"iabs", STD_IABS},
    {"sign", STD_SIGN},
    {"entier", STD_ENTIER},
    {"sqrt", STD_REAL},
    {"sin", STD_REAL},
    {"cos", STD_REAL},
    {"arctan", STD_REAL},
    {"ln", STD_REAL},
    {"exp", STD_REAL},
    {"outreal", STD_REAL},
    {"inreal", STD_REAL},
    {"maxreal", STD_REAL},
    {"minreal", STD_REAL},
    {"epsilon", STD_REAL},
};

#define A_POOL    1024
#define A_SYMS    512
#define A_DECLS   1024
#define A_FORMALS 512
#define A_SCOPES  1024
#define A_OPEN    128
#define A_FIX     2048
#define A_DFIX    1024
#define A_THUNKS  512
#define A_LEVELS  128
#define A_FEL     256
#define A_PAR     256
#define A_SEG     64
#define A_TARGETS 16
#define A_IDLEN   63
#define A_CHUNK   64 /* cells per frame of a dynamic array */

typedef struct {
    uint8_t kind, op, h, pad;
    uint16_t nlen, pad2;
    uint32_t noff, line, col;
    int32_t l, r, x, nx; /* nx: the next argument of a call */
    int64_t v;
} anode_t;

typedef struct {
    const char *nm;
    uint16_t nlen;
    uint8_t kind, type;
    uint8_t dyn;   /* S_ARR: 0 constant bounds, 1 dynamic, 2 formal (both by descriptor) */
    uint8_t busy;  /* S_PROC: its body is being compiled */
    int16_t level; /* data: storage level; S_PROC: declaring level; S_LABEL: code level */
    int32_t off, addr, fbase, nf, res; /* res: result cell, or which standard procedure */
    int64_t lo, n;
} asym_t;

typedef struct {
    const char *nm;
    uint16_t nlen;
    uint8_t kind, type, dyn;
    uint16_t scope;
    int32_t fbase, nf;
    int64_t lo, n;
    uint32_t line, col;
} adecl_t;

typedef struct {
    const char *nm;
    uint16_t nlen;
    uint8_t mode, type;
    uint32_t line, col;
} aformal_t;

typedef struct {
    int t;
    uint32_t off, len, line, col;
    int64_t v;
    uint32_t pos, pline, pcol; /* where the next token's scan starts */
} alex_t;

typedef struct {
    int32_t node, s;
    int kind, type, level;
    int32_t off;
} atg_t;

typedef struct {
    anode_t pool[A_POOL];
    uint32_t npool;
    asym_t sym[A_SYMS];
    uint32_t nsym;
    adecl_t decl[A_DECLS]; /* pass 1: every block's declarations and labels */
    uint32_t ndecl;
    aformal_t fm[A_FORMALS];
    uint32_t nfm;
    uint8_t sdyn[A_SCOPES]; /* pass 1: the block declares dynamic arrays */
    uint32_t nscope;
    struct {
        uint32_t id, base;
    } open[A_OPEN];
    uint32_t nopen;
    struct {
        int32_t at, sym;
    } fix[A_FIX];
    uint32_t nfix;
    struct {
        int32_t at, addr;
    } dfix[A_DFIX];
    uint32_t ndfix;
    struct {
        int32_t entry, addr;
        uint32_t line;
    } th[A_THUNKS];
    uint32_t nth;
    struct {
        int32_t kind, store, next, max;
    } lv[A_LEVELS];
    int level;
    struct {
        int32_t kind, a, b, c, cont;
    } fel[A_FEL];
    uint32_t nfel;
    struct {
        uint32_t off, len, line, col;
        uint8_t value, type, arr, spec;
    } par[A_PAR];
    uint32_t npar;
    struct {
        uint32_t off, len, line, col;
    } seg[A_SEG];
    uint32_t sdepth, edepth, ind, bound_base;
    int pass, bounds;
    char sbuf[ZL_MAX_LINE];
    alex_t L;
    const char *src;
    uint32_t len;
    zl_cc_t *c;
    zl_out_t *tw;
} aw_t;

ZL_STATIC_ASSERT(sizeof(aw_t) <= sizeof(((zl_work_t *) 0)->fe), aw_fits);

static int stmt(aw_t *w, int ctx);
static int block(aw_t *w);
static int32_t pexpr(aw_t *w);
static int lw(aw_t *w, int32_t id);

/* ------------------------------------------------------------ helpers */

#define ARITH(t) ((t) == TY_INT || (t) == TY_REAL)

static int st_of(aw_t *w)
{
    return w->c->failed ? w->c->d->status : ZL_OK;
}

static int afail(aw_t *w, int st, const char *msg)
{
    if (!w->c->failed) {
        w->c->line = w->L.line;
        w->c->col = w->L.col;
    }
    return zl_fail(w->c, st, msg);
}

static int pfail(aw_t *w, uint32_t line, uint32_t col, int st, const char *msg)
{
    if (!w->c->failed) {
        w->c->line = line;
        w->c->col = col;
    }
    return zl_fail(w->c, st, msg);
}

static int nfail(aw_t *w, int32_t id, int st, const char *msg)
{
    return pfail(w, w->pool[id].line, w->pool[id].col, st, msg);
}

static int tyerr(aw_t *w, int32_t id, const char *msg)
{
    nfail(w, id, ZL_E_SYNTAX, msg);
    return -1;
}

static int32_t em(aw_t *w, int op, int64_t a, int32_t b)
{
    return w->pass == 2 ? zl_emit(w->c, op, a, b) : -1;
}

static int32_t here(aw_t *w)
{
    return (int32_t) w->c->p->ncode;
}

static void patchto(aw_t *w, int32_t at)
{
    if (w->pass == 2 && at >= 0) zl_patch(w->c, at, here(w));
}

static int nameq(const char *a, uint32_t al, const char *b, uint32_t bl)
{
    if (al != bl) return 0;
    for (uint32_t i = 0; i < al; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int tk(aw_t *w)
{
    return w->L.t;
}

/* ------------------------------------------------------------ twin text */

static void ts(aw_t *w, const char *s)
{
    if (w->tw) zl_puts(w->tw, s);
}
static void tn(aw_t *w, uint32_t off, uint32_t len)
{
    if (w->tw) zl_put(w->tw, w->src + off, len);
}
static void nl(aw_t *w, uint32_t ind)
{
    if (!w->tw) return;
    zl_putc(w->tw, '\n');
    for (uint32_t k = 0; k < ind; k++) zl_puts(w->tw, "  ");
}

/* ------------------------------------------------------------ lexing */

static int kwof(const char *s, uint32_t n)
{
    for (int k = 0; k < (int) (sizeof kwname / sizeof kwname[0]); k++) {
        const char *q = kwname[k];
        uint32_t j = 0;
        while (j < n && q[j] && q[j] == s[j]) j++;
        if (j == n && !q[j]) return K_ARRAY + k;
    }
    return T_ID;
}

static void lex(aw_t *w)
{
    alex_t *L = &w->L;
    const char *s = w->src;
    uint32_t n = w->len, i = L->pos, line = L->pline, col = L->pcol, j;
    while (i < n) {
        char ch = s[i];
        if (ch == '\n') {
            line++;
            col = 1;
        } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\f' || ch == '\v') {
            col++;
        } else {
            break;
        }
        i++;
    }
    L->off = i;
    L->line = line;
    L->col = col;
    L->v = 0;
    L->len = 0;
    L->t = T_EOF;
    L->pos = i;
    L->pline = line;
    L->pcol = col;
    if (i >= n || w->c->failed) return;
    unsigned char ch = (unsigned char) s[i];
    j = i;
    if (zl_isalpha(ch)) {
        while (j < n && zl_isalnum((unsigned char) s[j])) j++;
        if (j - i > A_IDLEN) {
            afail(w, ZL_E_LIMIT, "identifier longer than 63 characters");
            return;
        }
        L->t = kwof(s + i, j - i);
        if (L->t == K_DIV) L->t = O_PCT;
    } else if (zl_isdigit(ch)) {
        int64_t v = 0;
        for (; j < n && zl_isdigit((unsigned char) s[j]); j++)
            if (__builtin_mul_overflow(v, 10, &v) || __builtin_add_overflow(v, s[j] - '0', &v)) {
                afail(w, ZL_E_UNSUPPORTED, "number too large for 64 bits");
                return;
            }
        if (j < n && (s[j] == '.' || s[j] == '#' || s[j] == 'e' || s[j] == 'E')) {
            afail(w, ZL_E_UNSUPPORTED, "real numbers are not in the exact integer subset");
            return;
        }
        if (j < n && zl_isalpha((unsigned char) s[j])) {
            afail(w, ZL_E_SYNTAX, "a number runs into a name");
            return;
        }
        L->t = T_NUM;
        L->v = v;
    } else if (ch == '"') {
        for (j++; j < n && s[j] != '"' && s[j] != '\n'; j++)
            if (s[j] == '\\' && j + 1 < n && s[j + 1] != '\n') j++;
        if (j >= n || s[j] != '"') {
            afail(w, ZL_E_SYNTAX, "unterminated string");
            return;
        }
        j++;
        L->t = T_STR;
    } else if (ch == '.' && i + 1 < n && zl_isdigit((unsigned char) s[i + 1])) {
        afail(w, ZL_E_UNSUPPORTED, "real numbers are not in the exact integer subset");
        return;
    } else {
        for (size_t k = 0; k < sizeof optab / sizeof optab[0]; k++) {
            uint32_t m = 0;
            while (optab[k].s[m]) m++;
            if (n - i < m) continue;
            uint32_t q = 0;
            while (q < m && s[i + q] == optab[k].s[q]) q++;
            if (q == m) {
                L->t = optab[k].t;
                j = i + m;
                break;
            }
        }
        if (j == i) {
            afail(w, ZL_E_SYNTAX, "unexpected character");
            return;
        }
    }
    L->len = j - i;
    L->pos = j;
    L->pcol = col + (j - i);
}

/* The current token is 'comment': take the text up to the next ';'. */
static int comment_text(aw_t *w, uint32_t *off, uint32_t *n)
{
    alex_t *L = &w->L;
    uint32_t i = L->pos, line = L->pline, col = L->pcol;
    while (i < w->len && w->src[i] != ';') {
        if (w->src[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
        i++;
    }
    if (i >= w->len) return afail(w, ZL_E_SYNTAX, "comment without its closing ';'");
    *off = L->pos;
    *n = i - L->pos;
    L->pos = i + 1;
    L->pline = line;
    L->pcol = col + 1;
    lex(w);
    return st_of(w);
}

/* "comment" and the text with each run of blanks and newlines made one space */
static void tw_comment(aw_t *w, uint32_t off, uint32_t n)
{
    int gap = 1;
    if (!w->tw) return;
    zl_puts(w->tw, "comment");
    for (uint32_t i = 0; i < n; i++) {
        char ch = w->src[off + i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v') {
            gap = 1;
            continue;
        }
        if (gap) zl_putc(w->tw, ' ');
        gap = 0;
        zl_putc(w->tw, ch);
    }
    zl_putc(w->tw, ';');
}

/* ------------------------------------------------------------ expressions */

static int32_t mknode(aw_t *w, int kind, int op, uint32_t line, uint32_t col, int32_t l, int32_t r,
                      int32_t x)
{
    uint32_t h = 0;
    if (w->c->failed) return -1;
    if (w->npool >= A_POOL) {
        afail(w, ZL_E_LIMIT, "expression too large");
        return -1;
    }
    int32_t kids[3] = {l, r, x};
    for (int k = 0; k < 3; k++)
        if (kids[k] >= 0 && w->pool[kids[k]].h > h) h = w->pool[kids[k]].h;
    if (kind == N_CALL)
        for (int32_t a = l; a >= 0; a = w->pool[a].nx)
            if (w->pool[a].h > h) h = w->pool[a].h;
    if (h + 1 > ZL_MAX_DEPTH) {
        afail(w, ZL_E_LIMIT, "expression nested too deeply");
        return -1;
    }
    anode_t *nd = &w->pool[w->npool];
    zl_bzero(nd, sizeof *nd);
    nd->kind = (uint8_t) kind;
    nd->op = (uint8_t) op;
    nd->h = (uint8_t) (h + 1);
    nd->line = line;
    nd->col = col;
    nd->l = l;
    nd->r = r;
    nd->x = x;
    nd->nx = -1;
    return (int32_t) w->npool++;
}

static int32_t primary(aw_t *w)
{
    alex_t at = w->L;
    int32_t nd;
    switch (at.t) {
    case T_NUM:
        lex(w);
        if ((nd = mknode(w, N_NUM, 0, at.line, at.col, -1, -1, -1)) >= 0) w->pool[nd].v = at.v;
        return nd;
    case K_TRUE:
    case K_FALSE:
        lex(w);
        if ((nd = mknode(w, N_BOOL, 0, at.line, at.col, -1, -1, -1)) >= 0)
            w->pool[nd].v = at.t == K_TRUE;
        return nd;
    case T_STR:
        lex(w);
        if ((nd = mknode(w, N_STR, 0, at.line, at.col, -1, -1, -1)) >= 0) {
            w->pool[nd].noff = at.off + 1;
            w->pool[nd].nlen = (uint16_t) (at.len - 2);
            if (at.len - 2 > 0xffff) return afail(w, ZL_E_LIMIT, "string too long"), -1;
        }
        return nd;
    case O_LP:
        lex(w);
        if (++w->edepth > ZL_MAX_DEPTH)
            return afail(w, ZL_E_LIMIT, "expression nested too deeply"), -1;
        nd = pexpr(w);
        w->edepth--;
        if (nd < 0) return -1;
        if (tk(w) != O_RP) return afail(w, ZL_E_SYNTAX, "')' expected"), -1;
        lex(w);
        return nd;
    case T_ID: {
        int32_t l = -1, kind = N_ID;
        lex(w);
        if (tk(w) == O_LB) {
            lex(w);
            if (++w->edepth > ZL_MAX_DEPTH)
                return afail(w, ZL_E_LIMIT, "expression nested too deeply"), -1;
            l = pexpr(w);
            w->edepth--;
            if (l < 0) return -1;
            if (tk(w) == O_COMMA)
                return afail(w, ZL_E_UNSUPPORTED,
                             "arrays of more than one dimension are not in this subset"),
                       -1;
            if (tk(w) != O_RB) return afail(w, ZL_E_SYNTAX, "']' expected"), -1;
            lex(w);
            kind = N_ELEM;
        } else if (tk(w) == O_LP) {
            int32_t last = -1;
            lex(w);
            if (++w->edepth > ZL_MAX_DEPTH)
                return afail(w, ZL_E_LIMIT, "expression nested too deeply"), -1;
            for (;;) {
                int32_t a = pexpr(w);
                if (a < 0) return -1;
                if (last < 0)
                    l = a;
                else
                    w->pool[last].nx = a;
                last = a;
                if (tk(w) != O_COMMA) break;
                lex(w);
            }
            w->edepth--;
            if (tk(w) != O_RP)
                return afail(w, ZL_E_SYNTAX, "',' or ')' expected in the arguments"), -1;
            lex(w);
            kind = N_CALL;
        }
        if ((nd = mknode(w, kind, 0, at.line, at.col, l, -1, -1)) >= 0) {
            w->pool[nd].noff = at.off;
            w->pool[nd].nlen = (uint16_t) at.len;
        }
        return nd;
    }
    case K_IF:
        return afail(w, ZL_E_SYNTAX,
                     "a conditional expression inside an expression needs parentheses"),
               -1;
    default:
        return afail(w, ZL_E_SYNTAX, "expression expected"), -1;
    }
}

static int binlev(int t)
{
    switch (t) {
    case O_EQV:
        return 1;
    case O_IMP:
        return 2;
    case O_OR:
        return 3;
    case O_AND:
        return 4;
    case O_PLUS:
    case O_MINUS:
        return 7;
    case O_MUL:
    case O_SLASH:
    case O_PCT:
        return 8;
    case O_POW:
        return 9;
    default:
        return 0;
    }
}

static int isrel(int t)
{
    return t >= O_LT && t <= O_NE;
}

/* Precedence levels: 1 ==, 2 ->, 3 |, 4 &, 5 !, 6 relations, 7 + - (and a
 * leading sign), 8 * / %, 9 ^, 10 primaries. */
static int32_t plev(aw_t *w, int lev)
{
    alex_t at = w->L;
    int32_t l, r;
    int t;
    if (lev >= 10) return primary(w);
    if (lev == 5) {
        if (tk(w) != O_NOT) return plev(w, 6);
        lex(w);
        r = plev(w, 6);
        return r < 0 ? -1 : mknode(w, N_NOT, O_NOT, at.line, at.col, r, -1, -1);
    }
    if (lev == 6) {
        l = plev(w, 7);
        if (l >= 0 && isrel(t = tk(w))) {
            alex_t ot = w->L;
            lex(w);
            r = plev(w, 7);
            l = r < 0 ? -1 : mknode(w, N_REL, t, ot.line, ot.col, l, r, -1);
        }
        return l;
    }
    if (lev == 7 && (tk(w) == O_PLUS || tk(w) == O_MINUS)) {
        t = tk(w);
        lex(w);
        l = plev(w, 8);
        if (l >= 0 && t == O_MINUS) l = mknode(w, N_NEG, O_MINUS, at.line, at.col, l, -1, -1);
    } else {
        l = plev(w, lev + 1);
    }
    while (l >= 0 && binlev(tk(w)) == lev) {
        alex_t ot = w->L;
        t = ot.t;
        lex(w);
        r = plev(w, lev + 1);
        l = r < 0 ? -1 : mknode(w, N_BIN, t, ot.line, ot.col, l, r, -1);
    }
    return l;
}

static int32_t pexpr(aw_t *w)
{
    if (tk(w) == K_IF) {
        alex_t at = w->L;
        int32_t c, a, b;
        lex(w);
        if (++w->edepth > ZL_MAX_DEPTH)
            return afail(w, ZL_E_LIMIT, "expression nested too deeply"), -1;
        c = pexpr(w);
        if (c < 0) return -1;
        if (tk(w) != K_THEN) return afail(w, ZL_E_SYNTAX, "'then' expected"), -1;
        lex(w);
        a = plev(w, 1);
        if (a < 0) return -1;
        if (tk(w) != K_ELSE)
            return afail(w, ZL_E_SYNTAX, "a conditional expression needs its 'else'"), -1;
        lex(w);
        b = pexpr(w);
        w->edepth--;
        return b < 0 ? -1 : mknode(w, N_COND, K_IF, at.line, at.col, c, a, b);
    }
    return plev(w, 1);
}

static int nprec(const anode_t *nd)
{
    switch (nd->kind) {
    case N_COND:
        return 0;
    case N_BIN:
        return binlev(nd->op);
    case N_REL:
        return 6;
    case N_NOT:
        return 5;
    case N_NEG:
        return 7;
    default:
        return 10;
    }
}

static void pe(aw_t *w, int32_t id, int need)
{
    zl_out_t *o = w->tw;
    const anode_t *nd = &w->pool[id];
    int p = nprec(nd), par = p < need;
    if (!o) return;
    if (par) zl_putc(o, '(');
    switch (nd->kind) {
    case N_NUM:
        zl_puti(o, nd->v);
        break;
    case N_BOOL:
        zl_puts(o, nd->v ? "true" : "false");
        break;
    case N_STR:
        zl_putc(o, '"');
        zl_put(o, w->src + nd->noff, nd->nlen);
        zl_putc(o, '"');
        break;
    case N_ID:
    case N_ELEM:
    case N_CALL:
        zl_put(o, w->src + nd->noff, nd->nlen);
        if (nd->kind == N_ELEM) {
            zl_putc(o, '[');
            pe(w, nd->l, 0);
            zl_putc(o, ']');
        } else if (nd->kind == N_CALL) {
            zl_putc(o, '(');
            for (int32_t a = nd->l; a >= 0; a = w->pool[a].nx) {
                if (a != nd->l) zl_puts(o, ", ");
                pe(w, a, 0);
            }
            zl_putc(o, ')');
        }
        break;
    case N_NEG:
        zl_putc(o, '-');
        pe(w, nd->l, 8);
        break;
    case N_NOT:
        zl_putc(o, '!');
        pe(w, nd->l, 6);
        break;
    case N_REL:
    case N_BIN:
        pe(w, nd->l, nd->kind == N_REL ? 7 : p);
        zl_putc(o, ' ');
        zl_puts(o, opname[nd->op - O_ASSIGN]);
        zl_putc(o, ' ');
        pe(w, nd->r, nd->kind == N_REL ? 7 : p + 1);
        break;
    case N_COND:
        zl_puts(o, "if ");
        pe(w, nd->l, 0);
        zl_puts(o, " then ");
        pe(w, nd->r, 1);
        zl_puts(o, " else ");
        pe(w, nd->x, 0);
        break;
    }
    if (par) zl_putc(o, ')');
}

/* A bound made only of literals, + - * and signs is known when compiling. */
static int fold(aw_t *w, int32_t id, int64_t *v)
{
    const anode_t *nd = &w->pool[id];
    int64_t a, b;
    switch (nd->kind) {
    case N_NUM:
        *v = nd->v;
        return 1;
    case N_NEG:
        if (!fold(w, nd->l, &a) || a == INT64_MIN) return 0;
        *v = -a;
        return 1;
    case N_BIN:
        if (!fold(w, nd->l, &a) || !fold(w, nd->r, &b)) return 0;
        if (nd->op == O_PLUS) return !__builtin_add_overflow(a, b, v);
        if (nd->op == O_MINUS) return !__builtin_sub_overflow(a, b, v);
        if (nd->op == O_MUL) return !__builtin_mul_overflow(a, b, v);
        return 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------ symbols */

static int32_t lookup(aw_t *w, const char *nm, uint32_t nl)
{
    for (uint32_t i = w->nsym; i-- > 0;)
        if (nameq(w->sym[i].nm, w->sym[i].nlen, nm, nl)) return (int32_t) i;
    return -1;
}

static int32_t lookup_local(aw_t *w, uint32_t off, uint32_t len)
{
    uint32_t base = w->nopen ? w->open[w->nopen - 1].base : 0;
    for (uint32_t i = w->nsym; i-- > base;)
        if (nameq(w->sym[i].nm, w->sym[i].nlen, w->src + off, len)) return (int32_t) i;
    zl_fail(w->c, ZL_E_VERIFY, "internal: a declaration was not recorded");
    return -1;
}

static int32_t add_sym(aw_t *w, const char *nm, uint32_t nl, int kind, int type, uint32_t line,
                       uint32_t col)
{
    uint32_t base = w->nopen ? w->open[w->nopen - 1].base : 0;
    for (uint32_t i = base; i < w->nsym; i++)
        if (nameq(w->sym[i].nm, w->sym[i].nlen, nm, nl)) {
            pfail(w, line, col, ZL_E_SYNTAX, "identifier declared twice in one block");
            return -1;
        }
    if (w->nsym >= A_SYMS) {
        pfail(w, line, col, ZL_E_LIMIT, "too many identifiers in scope");
        return -1;
    }
    asym_t *S = &w->sym[w->nsym];
    zl_bzero(S, sizeof *S);
    S->nm = nm;
    S->nlen = (uint16_t) nl;
    S->kind = (uint8_t) kind;
    S->type = (uint8_t) type;
    S->addr = -1;
    return (int32_t) w->nsym++;
}

/* Pass 1: note a declaration (or label) of the innermost open block. */
static adecl_t *record(aw_t *w, int kind, int type, const alex_t *at)
{
    if (w->pass != 1 || w->c->failed) return 0;
    if (w->ndecl >= A_DECLS) {
        afail(w, ZL_E_LIMIT, "too many declarations");
        return 0;
    }
    adecl_t *d = &w->decl[w->ndecl++];
    zl_bzero(d, sizeof *d);
    d->nm = w->src + at->off;
    d->nlen = (uint16_t) at->len;
    d->kind = (uint8_t) kind;
    d->type = (uint8_t) type;
    d->scope = (uint16_t) w->open[w->nopen - 1].id;
    d->line = at->line;
    d->col = at->col;
    return d;
}

static int set_level(aw_t *w, int k, int kind, int store)
{
    if (k >= A_LEVELS) return afail(w, ZL_E_LIMIT, "procedures and blocks nested too deeply");
    w->lv[k].kind = kind;
    w->lv[k].store = store;
    w->lv[k].next = 0;
    w->lv[k].max = 0;
    return ZL_OK;
}

/* Reserve n cells at storage level S: slots for the main program, frame
 * cells for a procedure. */
static int32_t alloc(aw_t *w, int S, int64_t n)
{
    if (w->pass != 2 || w->c->failed) return -1;
    if (S < 0 || n < 1 || n > ZL_MAX_FRAMES) {
        zl_fail(w->c, ZL_E_LIMIT, "too much data for one block");
        return -1;
    }
    if (S == 0) return zl_slots(w->c, (uint32_t) n);
    if (w->lv[S].next + n > ZL_MAX_FRAMES) {
        zl_fail(w->c, ZL_E_LIMIT, "procedure frame too large");
        return -1;
    }
    w->lv[S].next += (int32_t) n;
    if (w->lv[S].next > w->lv[S].max) w->lv[S].max = w->lv[S].next;
    return w->lv[S].next - (int32_t) n;
}

/* Open a block: code is the level of its statements and procedures,
 * store the level that holds its variables. Pass 2 declares at once every
 * identifier pass 1 recorded for it. */
static int scope_open(aw_t *w, int code, int store)
{
    if (w->nscope >= A_SCOPES || w->nopen >= A_OPEN) return afail(w, ZL_E_LIMIT, "too many blocks");
    uint32_t id = w->nscope++;
    w->open[w->nopen].id = id;
    w->open[w->nopen].base = w->nsym;
    w->nopen++;
    if (w->pass != 2) return st_of(w);
    for (uint32_t i = 0; i < w->ndecl && !w->c->failed; i++) {
        const adecl_t *d = &w->decl[i];
        if (d->scope != id) continue;
        int32_t s = add_sym(w, d->nm, d->nlen, d->kind, d->type, d->line, d->col);
        if (s < 0) break;
        asym_t *S = &w->sym[s];
        switch (d->kind) {
        case S_VAR:
            S->level = (int16_t) store;
            S->off = alloc(w, store, 1);
            break;
        case S_ARR:
            S->level = (int16_t) store;
            S->dyn = d->dyn;
            S->lo = d->lo;
            S->n = d->n;
            S->off = alloc(w, store, d->dyn ? 3 : d->n);
            break;
        case S_PROC:
            S->level = (int16_t) code;
            S->fbase = d->fbase;
            S->nf = d->nf;
            break;
        default: /* S_LABEL */
            S->level = (int16_t) code;
            break;
        }
    }
    return st_of(w);
}

/* Close a block: place its forward jumps and calls, forget its names. */
static void scope_close(aw_t *w)
{
    uint32_t base = w->open[--w->nopen].base, k = 0;
    if (w->pass == 2) {
        for (uint32_t i = 0; i < w->nfix; i++) {
            if ((uint32_t) w->fix[i].sym < base) {
                w->fix[k++] = w->fix[i];
                continue;
            }
            const asym_t *S = &w->sym[w->fix[i].sym];
            if (S->addr < 0)
                zl_fail(w->c, ZL_E_UNDEFINED, "internal: a label or procedure was never placed");
            else
                zl_patch(w->c, w->fix[i].at, S->addr);
        }
        w->nfix = k;
    }
    w->nsym = base;
}

static int addfix(aw_t *w, int32_t at, int32_t s)
{
    if (w->pass != 2 || at < 0) return st_of(w);
    if (w->nfix >= A_FIX) return afail(w, ZL_E_LIMIT, "too many forward jumps and calls");
    w->fix[w->nfix].at = at;
    w->fix[w->nfix].sym = s;
    w->nfix++;
    return ZL_OK;
}

/* ------------------------------------------------------------ lowering */

static void ld(aw_t *w, int S, int32_t off)
{
    if (S == 0)
        em(w, ZL_LOAD, off, 0);
    else
        em(w, ZL_LOADF, off, w->level - S);
}
static void st(aw_t *w, int S, int32_t off)
{
    if (S == 0)
        em(w, ZL_STORE, off, 0);
    else
        em(w, ZL_STOREF, off, w->level - S);
}
static void ad(aw_t *w, int S, int32_t off)
{
    if (S == 0)
        em(w, ZL_PUSH, off, 0);
    else
        em(w, ZL_ADDRF, off, w->level - S);
}

static int mix(int a, int b)
{
    return a == TY_REAL || b == TY_REAL ? TY_REAL : TY_INT;
}
static int compat(int want, int t)
{
    return want == TY_BOOL ? t == TY_BOOL : ARITH(t);
}

static int32_t resolve(aw_t *w, int32_t id)
{
    const anode_t *nd = &w->pool[id];
    int32_t s = lookup(w, w->src + nd->noff, nd->nlen);
    if (s < 0) {
        nfail(w, id, ZL_E_UNDEFINED, "undeclared identifier");
        return -1;
    }
    if (w->bounds && (uint32_t) s >= w->bound_base) {
        nfail(w, id, ZL_E_SYNTAX, "an array bound may use only identifiers of enclosing blocks");
        return -1;
    }
    return s;
}

static int dfix_add(aw_t *w, int32_t at, int addr)
{
    if (at < 0) return st_of(w);
    if (w->ndfix >= A_DFIX) return afail(w, ZL_E_LIMIT, "too many uses of name parameters");
    w->dfix[w->ndfix].at = at;
    w->dfix[w->ndfix].addr = addr;
    w->ndfix++;
    return ZL_OK;
}

/* A name parameter's value: an address to read, or a thunk to call. */
static void name_read(aw_t *w, const asym_t *S)
{
    ld(w, S->level, S->off);
    em(w, ZL_DUP, 0, 0);
    em(w, ZL_PUSH, 0, 0);
    em(w, ZL_LT, 0, 0);
    int32_t jt = em(w, ZL_JNZ, 0, 0);
    em(w, ZL_LOADA, 0, 0);
    int32_t jd = em(w, ZL_JMP, 0, 0);
    patchto(w, jt);
    ld(w, S->level, S->off + 1);
    em(w, ZL_SWAP, 0, 0);
    dfix_add(w, em(w, ZL_CALL, 0, 0), 0);
    patchto(w, jd);
}

/* A name parameter's address (for an assignment to it). */
static void name_addr(aw_t *w, const asym_t *S)
{
    ld(w, S->level, S->off);
    em(w, ZL_DUP, 0, 0);
    em(w, ZL_PUSH, 0, 0);
    em(w, ZL_LT, 0, 0);
    int32_t jd = em(w, ZL_JZ, 0, 0);
    ld(w, S->level, S->off + 1);
    em(w, ZL_SWAP, 0, 0);
    dfix_add(w, em(w, ZL_CALL, 0, 0), 1);
    patchto(w, jd);
}

/* An array element. mode 0: push its address; 1: push its value; 2 (a
 * main-program array of known bounds): push the index for STOREX. */
static int elem(aw_t *w, int32_t id, int mode)
{
    const anode_t *nd = &w->pool[id];
    int32_t s = resolve(w, id);
    int t;
    if (s < 0) return -1;
    const asym_t *S = &w->sym[s];
    if (S->kind != S_ARR) return tyerr(w, id, "a subscript needs an array");
    if ((t = lw(w, nd->l)) < 0) return -1;
    if (!ARITH(t)) return tyerr(w, id, "a subscript must be a number");
    w->c->line = nd->line;
    if (!S->dyn) {
        if (S->lo) {
            em(w, ZL_PUSH, S->lo, 0);
            em(w, ZL_SUB, 0, 0);
        }
        if (S->level == 0 && mode) {
            if (mode == 1) em(w, ZL_LOADX, S->off, (int32_t) S->n);
            return S->type;
        }
        em(w, ZL_BOUND, S->n, 0);
        ad(w, S->level, S->off);
        em(w, ZL_ADD, 0, 0);
    } else {
        ld(w, S->level, S->off + 1);
        em(w, ZL_SUB, 0, 0);
        em(w, ZL_DUP, 0, 0);
        em(w, ZL_PUSH, 0, 0);
        em(w, ZL_LT, 0, 0);
        em(w, ZL_OVER, 0, 0);
        ld(w, S->level, S->off + 2);
        em(w, ZL_GE, 0, 0);
        em(w, ZL_OR, 0, 0);
        int32_t ok = em(w, ZL_JZ, 0, 0);
        em(w, ZL_TRAP, ZL_E_BOUNDS, 0);
        patchto(w, ok);
        ld(w, S->level, S->off);
        em(w, ZL_ADD, 0, 0);
    }
    if (mode == 1) em(w, ZL_LOADA, 0, 0);
    return S->type;
}

static int push_level(aw_t *w, int kind, int store)
{
    if (set_level(w, w->level + 1, kind, store)) return st_of(w);
    w->level++;
    return ZL_OK;
}

/* Compile a thunk for a name argument and push its descriptor. */
static int thunk(aw_t *w, int32_t a, int is_addr, int ft)
{
    int t;
    if (w->nth >= A_THUNKS) return nfail(w, a, ZL_E_LIMIT, "too many name arguments");
    uint32_t id = w->nth++;
    int32_t j = em(w, ZL_JMP, 0, 0), entry = here(w);
    if (push_level(w, L_THUNK, -1)) return st_of(w);
    em(w, ZL_ENTER, 0, 0);
    t = is_addr ? elem(w, a, 0) : lw(w, a);
    w->level--;
    if (t < 0) return st_of(w);
    if (is_addr ? t != ft : !compat(ft, t))
        return nfail(w, a, ZL_E_SYNTAX, "the argument's type does not match the parameter");
    em(w, ZL_LEAVE, 0, 0);
    em(w, ZL_RET, 0, 0);
    patchto(w, j);
    w->th[id].entry = entry;
    w->th[id].addr = is_addr;
    w->th[id].line = w->pool[a].line;
    em(w, ZL_PUSH, -(int64_t) id - 1, 0);
    em(w, ZL_LINK, 0, 0);
    return st_of(w);
}

static int name_actual(aw_t *w, int32_t a, int ft)
{
    const anode_t *nd = &w->pool[a];
    if (nd->kind == N_ID) {
        int32_t s = resolve(w, a);
        if (s < 0) return st_of(w);
        const asym_t *S = &w->sym[s];
        switch (S->kind) {
        case S_VAR:
        case S_NAME:
            if (S->type != ft)
                return nfail(w, a, ZL_E_SYNTAX, "the argument's type does not match the parameter");
            if (S->kind == S_VAR) {
                ad(w, S->level, S->off);
                em(w, ZL_PUSH, 0, 0);
            } else {
                ld(w, S->level, S->off);
                ld(w, S->level, S->off + 1);
            }
            return st_of(w);
        case S_ARR:
            return nfail(w, a, ZL_E_SYNTAX, "an array passed where a simple parameter is expected");
        case S_LABEL:
            return nfail(w, a, ZL_E_UNSUPPORTED, "label parameters are not in this subset");
        case S_PROC:
            if (S->type != TY_NONE && S->nf == 0) break; /* a function call: a thunk */
            return nfail(w, a, ZL_E_UNSUPPORTED, "procedure parameters are not in this subset");
        default:
            if (S->res == STD_REAL) break;
            return nfail(w, a, ZL_E_UNSUPPORTED, "procedure parameters are not in this subset");
        }
    }
    if (nd->kind == N_STR)
        return nfail(w, a, ZL_E_UNSUPPORTED, "string parameters are not in this subset");
    return thunk(w, a, nd->kind == N_ELEM, ft);
}

static int array_actual(aw_t *w, int32_t a, int ft)
{
    const anode_t *nd = &w->pool[a];
    if (nd->kind != N_ID)
        return nfail(w, a, ZL_E_SYNTAX, "an array parameter needs an array identifier");
    int32_t s = resolve(w, a);
    if (s < 0) return st_of(w);
    const asym_t *S = &w->sym[s];
    if (S->kind != S_ARR)
        return nfail(w, a, ZL_E_SYNTAX, "an array parameter needs an array identifier");
    if (S->type != ft)
        return nfail(w, a, ZL_E_SYNTAX, "the array's type does not match the parameter");
    if (!S->dyn) {
        ad(w, S->level, S->off);
        em(w, ZL_PUSH, S->lo, 0);
        em(w, ZL_PUSH, S->n, 0);
    } else {
        ld(w, S->level, S->off);
        ld(w, S->level, S->off + 1);
        ld(w, S->level, S->off + 2);
    }
    return st_of(w);
}

static int call(aw_t *w, int32_t s, int32_t id)
{
    const anode_t *nd = &w->pool[id];
    const asym_t *S = &w->sym[s];
    int32_t a = nd->kind == N_CALL ? nd->l : -1;
    int na = 0, t;
    for (int32_t k = a; k >= 0; k = w->pool[k].nx) na++;
    if (na != S->nf) return tyerr(w, id, "wrong number of arguments");
    if (w->level < S->level) return tyerr(w, id, "a procedure of this block cannot be used here");
    em(w, ZL_LINK, w->level - S->level, 0);
    for (int k = 0; k < S->nf; k++, a = w->pool[a].nx) {
        const aformal_t *f = &w->fm[S->fbase + k];
        if (f->mode == F_VALUE) {
            if ((t = lw(w, a)) < 0) return -1;
            if (!compat(f->type, t))
                return tyerr(w, a, "the argument's type does not match the parameter");
        } else if (f->mode == F_NAME) {
            if (name_actual(w, a, f->type)) return -1;
        } else if (array_actual(w, a, f->type)) {
            return -1;
        }
    }
    w->c->line = nd->line;
    int32_t at = em(w, ZL_CALL, S->addr >= 0 ? S->addr : 0, 0);
    if (S->addr < 0 && addfix(w, at, s)) return -1;
    return S->type;
}

static int std_fn(aw_t *w, int32_t s, int32_t id)
{
    const anode_t *nd = &w->pool[id];
    int which = w->sym[s].res, t;
    if (which == STD_REAL) {
        nfail(w, id, ZL_E_UNSUPPORTED,
              "real-valued library procedures are not in the exact subset");
        return -1;
    }
    if (which <= STD_ININTEGER)
        return tyerr(w, id, "outinteger, outstring and ininteger are statements, not values");
    if (nd->kind != N_CALL || w->pool[nd->l].nx >= 0)
        return tyerr(w, id, "abs, iabs, sign and entier take one argument");
    if ((t = lw(w, nd->l)) < 0) return -1;
    if (!ARITH(t)) return tyerr(w, id, "abs, iabs, sign and entier need a number");
    w->c->line = nd->line;
    if (which == STD_ABS || which == STD_IABS) em(w, ZL_ABS, 0, 0);
    if (which == STD_SIGN) em(w, ZL_SGN, 0, 0);
    return which == STD_ABS ? TY_REAL : TY_INT; /* entier of an exact value is itself */
}

static int lw_name(aw_t *w, int32_t id)
{
    const anode_t *nd = &w->pool[id];
    int32_t s = resolve(w, id);
    if (s < 0) return -1;
    const asym_t *S = &w->sym[s];
    if (nd->kind == N_ID) {
        switch (S->kind) {
        case S_VAR:
            ld(w, S->level, S->off);
            return S->type;
        case S_NAME:
            name_read(w, S);
            return S->type;
        case S_ARR:
            return tyerr(w, id, "an array needs a subscript here");
        case S_LABEL:
            return tyerr(w, id, "a label is not a value");
        default:
            break; /* a procedure called without arguments */
        }
    }
    if (S->kind == S_STD) return std_fn(w, s, id);
    if (S->kind != S_PROC)
        return tyerr(w, id, "arguments given to something that is not a procedure");
    if (S->type == TY_NONE) return tyerr(w, id, "a procedure without a type has no value");
    return call(w, s, id);
}

static int lw_bin(aw_t *w, int32_t id)
{
    const anode_t *nd = &w->pool[id];
    int op = nd->op, lt, rt;
    int boolop = op == O_AND || op == O_OR || op == O_IMP || op == O_EQV;
    const char *bad = boolop ? "a Boolean operator needs Boolean operands"
                             : "arithmetic needs numbers, not Boolean values";
    if ((lt = lw(w, nd->l)) < 0) return -1;
    if (boolop ? lt != TY_BOOL : !ARITH(lt)) return tyerr(w, id, bad);
    if (op == O_IMP) em(w, ZL_NOT, 0, 0); /* a -> b is !a | b */
    if ((rt = lw(w, nd->r)) < 0) return -1;
    if (boolop ? rt != TY_BOOL : !ARITH(rt)) return tyerr(w, id, bad);
    w->c->line = nd->line;
    switch (op) {
    case O_AND:
        em(w, ZL_AND, 0, 0);
        return TY_BOOL;
    case O_OR:
    case O_IMP:
        em(w, ZL_OR, 0, 0);
        return TY_BOOL;
    case O_EQV:
        em(w, ZL_EQ, 0, 0);
        return TY_BOOL;
    case O_PLUS:
        em(w, ZL_ADD, 0, 0);
        return mix(lt, rt);
    case O_MINUS:
        em(w, ZL_SUB, 0, 0);
        return mix(lt, rt);
    case O_MUL:
        em(w, ZL_MUL, 0, 0);
        return mix(lt, rt);
    case O_SLASH:
        em(w, ZL_DIVX, 0, 0);
        return TY_REAL;
    case O_PCT:
        if (lt != TY_INT || rt != TY_INT)
            return tyerr(w, id, "% (integer division) needs integer operands");
        em(w, ZL_DIVT, 0, 0);
        return TY_INT;
    default: { /* O_POW */
        if (rt != TY_INT) {
            nfail(w, id, ZL_E_UNSUPPORTED, "a real exponent is not exact");
            return -1;
        }
        /* 0 ^ 0 and 0 ^ (-i) are undefined in the Report */
        em(w, ZL_OVER, 0, 0);
        em(w, ZL_PUSH, 0, 0);
        em(w, ZL_EQ, 0, 0);
        em(w, ZL_OVER, 0, 0);
        em(w, ZL_PUSH, 0, 0);
        em(w, ZL_LE, 0, 0);
        em(w, ZL_AND, 0, 0);
        int32_t ok = em(w, ZL_JZ, 0, 0);
        em(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        patchto(w, ok);
        em(w, ZL_POW, 1, 0);
        return lt;
    }
    }
}

static int relop(int t)
{
    switch (t) {
    case O_LT:
        return ZL_LT;
    case O_LE:
        return ZL_LE;
    case O_EQ:
        return ZL_EQ;
    case O_GE:
        return ZL_GE;
    case O_GT:
        return ZL_GT;
    default:
        return ZL_NE;
    }
}

/* Emit code that pushes the expression's value; returns its type or -1. */
static int lw(aw_t *w, int32_t id)
{
    const anode_t *nd = &w->pool[id];
    int lt, rt;
    if (w->c->failed) return -1;
    w->c->line = nd->line;
    switch (nd->kind) {
    case N_NUM:
        em(w, ZL_PUSH, nd->v, 0);
        return TY_INT;
    case N_BOOL:
        em(w, ZL_PUSH, nd->v, 0);
        return TY_BOOL;
    case N_STR:
        return tyerr(w, id, "a string is only an argument of outstring");
    case N_ID:
    case N_CALL:
        return lw_name(w, id);
    case N_ELEM:
        return elem(w, id, 1);
    case N_NEG:
        if ((lt = lw(w, nd->l)) < 0) return -1;
        if (!ARITH(lt)) return tyerr(w, id, "'-' needs a number");
        em(w, ZL_NEG, 0, 0);
        return lt;
    case N_NOT:
        if ((lt = lw(w, nd->l)) < 0) return -1;
        if (lt != TY_BOOL) return tyerr(w, id, "'!' needs a Boolean value");
        em(w, ZL_NOT, 0, 0);
        return TY_BOOL;
    case N_REL:
        if ((lt = lw(w, nd->l)) < 0 || (rt = lw(w, nd->r)) < 0) return -1;
        if (!ARITH(lt) || !ARITH(rt))
            return tyerr(w, id, "a relation compares numbers (== compares Boolean values)");
        w->c->line = nd->line;
        em(w, relop(nd->op), 0, 0);
        return TY_BOOL;
    case N_COND: {
        if ((lt = lw(w, nd->l)) < 0) return -1;
        if (lt != TY_BOOL) return tyerr(w, id, "the condition after if must be Boolean");
        int32_t jz = em(w, ZL_JZ, 0, 0);
        if ((lt = lw(w, nd->r)) < 0) return -1;
        int32_t je = em(w, ZL_JMP, 0, 0);
        patchto(w, jz);
        if ((rt = lw(w, nd->x)) < 0) return -1;
        patchto(w, je);
        if (lt == TY_BOOL && rt == TY_BOOL) return TY_BOOL;
        if (ARITH(lt) && ARITH(rt)) return mix(lt, rt);
        return tyerr(w, id, "the branches of a conditional expression differ in type");
    }
    default:
        return lw_bin(w, id);
    }
}

/* ------------------------------------------------------------ assignment */

static int target(aw_t *w, int32_t id, atg_t *t)
{
    const anode_t *nd = &w->pool[id];
    if (nd->kind != N_ID && nd->kind != N_ELEM)
        return tyerr(w, id, "only a variable can be assigned");
    int32_t s = resolve(w, id);
    if (s < 0) return -1;
    const asym_t *S = &w->sym[s];
    t->node = id;
    t->s = s;
    t->type = S->type;
    t->level = S->level;
    t->off = S->off;
    if (nd->kind == N_ELEM) {
        if (S->kind != S_ARR) return tyerr(w, id, "a subscript needs an array");
        t->kind = !S->dyn && S->level == 0 ? TG_SLOTX : TG_ADDR;
        return 0;
    }
    if (nd->kind != N_ID) return tyerr(w, id, "only a variable can be assigned");
    switch (S->kind) {
    case S_VAR:
        t->kind = TG_CELL;
        return 0;
    case S_NAME:
        t->kind = TG_ADDR;
        return 0;
    case S_PROC:
        if (S->type == TY_NONE || !S->busy)
            return tyerr(w, id, "only a typed procedure's own body may assign its name");
        t->kind = TG_CELL;
        t->level = S->level + 1;
        t->off = S->res;
        return 0;
    case S_ARR:
        return tyerr(w, id, "an array needs a subscript here");
    default:
        return tyerr(w, id, "this identifier cannot be assigned");
    }
}

static void target_addr(aw_t *w, const atg_t *t)
{
    if (t->kind == TG_CELL)
        ad(w, t->level, t->off);
    else if (w->pool[t->node].kind == N_ELEM)
        elem(w, t->node, 0);
    else
        name_addr(w, &w->sym[t->s]);
}

/* Assign rhs (or, with rhs < 0, the next input value) to nt left parts:
 * the left parts' subscripts first, then the value, as the Report says. */
static int assign(aw_t *w, const atg_t *tg, uint32_t nt, int32_t rhs)
{
    int t;
    for (uint32_t k = 1; k < nt; k++)
        if (tg[k].type != tg[0].type) return tyerr(w, tg[k].node, "the left parts differ in type");
    if (nt == 1) {
        if (tg[0].kind == TG_SLOTX)
            elem(w, tg[0].node, 2);
        else if (tg[0].kind == TG_ADDR)
            target_addr(w, &tg[0]);
    } else {
        for (uint32_t k = 0; k < nt; k++) target_addr(w, &tg[k]);
    }
    if (rhs >= 0) {
        if ((t = lw(w, rhs)) < 0) return st_of(w);
        if (!compat(tg[0].type, t))
            return nfail(w, rhs, ZL_E_SYNTAX, "the value's type does not match the variable");
    } else {
        em(w, ZL_IN, 0, 0);
    }
    if (nt == 1) {
        const asym_t *S = &w->sym[tg[0].s];
        if (tg[0].kind == TG_CELL)
            st(w, tg[0].level, tg[0].off);
        else if (tg[0].kind == TG_SLOTX)
            em(w, ZL_STOREX, S->off, (int32_t) S->n);
        else {
            em(w, ZL_SWAP, 0, 0);
            em(w, ZL_STOREA, 0, 0);
        }
        return st_of(w);
    }
    for (uint32_t k = nt; k-- > 0;) {
        if (k) { /* [... a v] -> [... v v a] */
            em(w, ZL_SWAP, 0, 0);
            em(w, ZL_OVER, 0, 0);
            em(w, ZL_SWAP, 0, 0);
        } else {
            em(w, ZL_SWAP, 0, 0);
        }
        em(w, ZL_STOREA, 0, 0);
    }
    return st_of(w);
}

/* ------------------------------------------------------------ statements */

static int is_decl(int t)
{
    return t == K_INTEGER || t == K_BOOLEAN || t == K_PROCEDURE || t == K_ARRAY || t == K_OWN ||
           t == K_REAL || t == K_SWITCH || t == K_STRING;
}

static int is_end(int t)
{
    return t == O_SEMI || t == K_END || t == K_ELSE || t == T_EOF;
}

/* The statement under then, else, do or a procedure heading: on the same
 * line when it is begin ... end, else on the next line, one step in. */
static int slot(aw_t *w, int ctx, int *comp, int *isfor, int own_line)
{
    alex_t save = w->L;
    int labels = 0, t;
    for (;;) {
        if (tk(w) != T_ID) {
            t = tk(w);
            break;
        }
        lex(w);
        if (tk(w) != O_COLON) {
            t = T_ID;
            break;
        }
        lex(w);
        labels = 1;
    }
    w->L = save;
    *comp = t == K_BEGIN;
    *isfor = t == K_FOR;
    if (*comp) {
        if (own_line)
            nl(w, w->ind);
        else
            ts(w, " ");
        return stmt(w, ctx);
    }
    if (!labels && is_end(t)) return stmt(w, ctx);
    nl(w, w->ind + 1);
    w->ind++;
    int s = stmt(w, ctx);
    w->ind--;
    return s;
}

static int st_if(aw_t *w)
{
    int32_t chain = -1;
    int comp = 0, isfor = 0;
    for (;;) {
        lex(w); /* past 'if' */
        int32_t b = pexpr(w), jz = -1;
        if (b < 0) return st_of(w);
        if (tk(w) != K_THEN) return afail(w, ZL_E_SYNTAX, "'then' expected");
        lex(w);
        ts(w, "if ");
        pe(w, b, 0);
        ts(w, " then");
        if (w->pass == 2) {
            int t = lw(w, b);
            if (t < 0) return st_of(w);
            if (t != TY_BOOL)
                return nfail(w, b, ZL_E_SYNTAX, "the condition after if must be Boolean");
            jz = em(w, ZL_JZ, 0, 0);
        }
        if (slot(w, C_THEN, &comp, &isfor, 0)) return st_of(w);
        if (tk(w) != K_ELSE) {
            patchto(w, jz);
            break;
        }
        if (isfor)
            return afail(w, ZL_E_SYNTAX, "a for statement after 'then' cannot take an 'else'");
        int32_t je = em(w, ZL_JMP, chain, 0);
        if (je >= 0) chain = je;
        patchto(w, jz);
        if (comp) {
            ts(w, " else");
        } else {
            nl(w, w->ind);
            ts(w, "else");
        }
        lex(w); /* past 'else' */
        if (tk(w) == K_IF) {
            ts(w, " ");
            continue;
        }
        if (slot(w, C_PLAIN, &comp, &isfor, 0)) return st_of(w);
        break;
    }
    while (w->pass == 2 && chain >= 0 && !w->c->failed) {
        int32_t next = (int32_t) w->c->p->code[chain].a;
        zl_patch(w->c, chain, here(w));
        chain = next;
    }
    return st_of(w);
}

/* (V - C) * sign(B) > 0, in that order, without forming V - C */
static int step_test(aw_t *w, int32_t vn, int32_t b, int32_t c)
{
    int t;
    if (lw(w, vn) < 0 || (t = lw(w, c)) < 0) return st_of(w);
    if (!ARITH(t)) return nfail(w, c, ZL_E_SYNTAX, "the limit after until must be a number");
    em(w, ZL_OVER, 0, 0);
    em(w, ZL_OVER, 0, 0);
    em(w, ZL_GT, 0, 0);
    em(w, ZL_ROT, 0, 0);
    em(w, ZL_ROT, 0, 0);
    em(w, ZL_LT, 0, 0);
    em(w, ZL_SUB, 0, 0);
    if ((t = lw(w, b)) < 0) return st_of(w);
    if (!ARITH(t)) return nfail(w, b, ZL_E_SYNTAX, "the step must be a number");
    em(w, ZL_SGN, 0, 0);
    em(w, ZL_MUL, 0, 0);
    em(w, ZL_PUSH, 0, 0);
    em(w, ZL_GT, 0, 0);
    return st_of(w);
}

/* The for statement as the Report expands it (4.6.4). One element runs its
 * statement in line; several share one copy, which returns to the element
 * that ran it through a selector cell. */
static int for_code(aw_t *w, uint32_t fb, uint32_t m, int32_t vn)
{
    atg_t V;
    int32_t sel = -1, chain = -1;
    int comp, isfor, t;
    zl_bzero(&V, sizeof V);
    if (w->pass == 2) {
        if (target(w, vn, &V)) return st_of(w);
        int k = w->sym[V.s].kind;
        if (V.type != TY_INT || (k != S_VAR && k != S_NAME))
            return nfail(w, vn, ZL_E_SYNTAX,
                         "the controlled variable must be a simple integer variable");
        if (m > 1) sel = alloc(w, w->lv[w->level].store, 1);
    }
    for (uint32_t k = 0; k < m && !w->c->failed; k++) {
        int kind = w->fel[fb + k].kind;
        int32_t a = w->fel[fb + k].a, b = w->fel[fb + k].b, c = w->fel[fb + k].c;
        int32_t top = here(w), jx = -1;
        if (w->pass == 2 && assign(w, &V, 1, a)) return st_of(w);
        if (kind == E_STEP) {
            top = here(w);
            if (w->pass == 2 && step_test(w, vn, b, c)) return st_of(w);
            jx = em(w, ZL_JNZ, 0, 0);
        } else if (kind == E_WHILE && w->pass == 2) {
            if ((t = lw(w, b)) < 0) return st_of(w);
            if (t != TY_BOOL)
                return nfail(w, b, ZL_E_SYNTAX, "the condition after while must be Boolean");
            jx = em(w, ZL_JZ, 0, 0);
        }
        if (m == 1) {
            if (slot(w, C_PLAIN, &comp, &isfor, 0)) return st_of(w);
        } else {
            em(w, ZL_PUSH, k + 1, 0);
            st(w, w->lv[w->level].store, sel);
            int32_t j = em(w, ZL_JMP, chain, 0);
            if (j >= 0) chain = j;
            w->fel[fb + k].cont = here(w);
        }
        if (kind == E_STEP && w->pass == 2) {
            int32_t inc = mknode(w, N_BIN, O_PLUS, w->pool[b].line, w->pool[b].col, vn, b, -1);
            if (inc < 0 || assign(w, &V, 1, inc)) return st_of(w);
        }
        if (kind != E_ARITH) em(w, ZL_JMP, top, 0);
        patchto(w, jx);
    }
    if (m > 1 && !w->c->failed) {
        int32_t jexit = em(w, ZL_JMP, 0, 0);
        while (w->pass == 2 && chain >= 0) {
            int32_t next = (int32_t) w->c->p->code[chain].a;
            zl_patch(w->c, chain, here(w));
            chain = next;
        }
        if (slot(w, C_PLAIN, &comp, &isfor, 0)) return st_of(w);
        for (uint32_t k = 0; k + 1 < m; k++) {
            ld(w, w->lv[w->level].store, sel);
            em(w, ZL_PUSH, k + 1, 0);
            em(w, ZL_EQ, 0, 0);
            em(w, ZL_JNZ, w->fel[fb + k].cont, 0);
        }
        em(w, ZL_JMP, w->fel[fb + m - 1].cont, 0);
        patchto(w, jexit);
    }
    return st_of(w);
}

static int st_for(aw_t *w)
{
    uint32_t fb = w->nfel, m = 0;
    lex(w); /* past 'for' */
    if (tk(w) != T_ID) return afail(w, ZL_E_SYNTAX, "controlled variable expected after for");
    alex_t v = w->L;
    lex(w);
    if (tk(w) == O_LB)
        return afail(w, ZL_E_UNSUPPORTED,
                     "a subscripted controlled variable is not in this subset");
    if (tk(w) != O_ASSIGN)
        return afail(w, ZL_E_SYNTAX, "':=' expected after the controlled variable");
    for (;;) {
        lex(w); /* past ':=' or ',' */
        if (w->nfel >= A_FEL) return afail(w, ZL_E_LIMIT, "for lists too long");
        int32_t a = pexpr(w), b = -1, c = -1;
        int kind = E_ARITH;
        if (a < 0) return st_of(w);
        if (tk(w) == K_STEP) {
            lex(w);
            if ((b = pexpr(w)) < 0) return st_of(w);
            if (tk(w) != K_UNTIL) return afail(w, ZL_E_SYNTAX, "'until' expected after step");
            lex(w);
            if ((c = pexpr(w)) < 0) return st_of(w);
            kind = E_STEP;
        } else if (tk(w) == K_WHILE) {
            lex(w);
            if ((b = pexpr(w)) < 0) return st_of(w);
            kind = E_WHILE;
        }
        w->fel[w->nfel].kind = kind;
        w->fel[w->nfel].a = a;
        w->fel[w->nfel].b = b;
        w->fel[w->nfel].c = c;
        w->nfel++;
        m++;
        if (tk(w) != O_COMMA) break;
    }
    if (tk(w) != K_DO) return afail(w, ZL_E_SYNTAX, "'do' expected");
    lex(w);
    ts(w, "for ");
    tn(w, v.off, v.len);
    ts(w, " :=");
    for (uint32_t k = 0; k < m; k++) {
        ts(w, k ? ", " : " ");
        pe(w, w->fel[fb + k].a, 0);
        if (w->fel[fb + k].kind == E_STEP) {
            ts(w, " step ");
            pe(w, w->fel[fb + k].b, 0);
            ts(w, " until ");
            pe(w, w->fel[fb + k].c, 0);
        } else if (w->fel[fb + k].kind == E_WHILE) {
            ts(w, " while ");
            pe(w, w->fel[fb + k].b, 0);
        }
    }
    ts(w, " do");
    int32_t vn = mknode(w, N_ID, 0, v.line, v.col, -1, -1, -1);
    if (vn < 0) return st_of(w);
    w->pool[vn].noff = v.off;
    w->pool[vn].nlen = (uint16_t) v.len;
    int s = for_code(w, fb, m, vn);
    w->nfel = fb;
    return s;
}

static int st_goto(aw_t *w)
{
    if (tk(w) == K_GO) {
        lex(w);
        if (tk(w) != T_ID || !nameq(w->src + w->L.off, w->L.len, "to", 2))
            return afail(w, ZL_E_SYNTAX, "'to' expected after 'go'");
    }
    lex(w);
    if (tk(w) == K_IF)
        return afail(w, ZL_E_UNSUPPORTED,
                     "conditional designational expressions are not in this subset");
    if (tk(w) == T_NUM) return afail(w, ZL_E_UNSUPPORTED, "numeric labels are not in this subset");
    if (tk(w) != T_ID) return afail(w, ZL_E_SYNTAX, "label expected after goto");
    alex_t at = w->L;
    lex(w);
    if (tk(w) == O_LB) return afail(w, ZL_E_UNSUPPORTED, "switches are not in this subset");
    ts(w, "goto ");
    tn(w, at.off, at.len);
    if (w->pass != 2) return st_of(w);
    int32_t s = lookup(w, w->src + at.off, at.len);
    if (s < 0) return pfail(w, at.line, at.col, ZL_E_UNDEFINED, "goto an undeclared label");
    const asym_t *S = &w->sym[s];
    if (S->kind != S_LABEL) return pfail(w, at.line, at.col, ZL_E_SYNTAX, "goto needs a label");
    if (S->level != w->level) {
        int proc = 0;
        for (int k = S->level + 1; k <= w->level; k++)
            if (w->lv[k].kind == L_PROC) proc = 1;
        return pfail(w, at.line, at.col, ZL_E_UNSUPPORTED,
                     proc ? "goto out of a procedure is not in this subset"
                          : "goto out of a block with dynamic arrays is not in this subset");
    }
    w->c->line = at.line;
    int32_t j = em(w, ZL_JMP, S->addr >= 0 ? S->addr : 0, 0);
    if (S->addr < 0) return addfix(w, j, s);
    return st_of(w);
}

static int unescape(aw_t *w, int32_t id, uint32_t *out_n)
{
    const anode_t *nd = &w->pool[id];
    const char *s = w->src + nd->noff;
    uint32_t n = 0;
    for (uint32_t i = 0; i < nd->nlen; i++) {
        char ch = s[i];
        if (ch == '\\') {
            char e = s[++i];
            ch = e == 'n' ? '\n' : e == 't' ? '\t' : e == '\\' ? '\\' : e == '"' ? '"' : 0;
            if (!ch)
                return nfail(w, id, ZL_E_SYNTAX, "unknown escape in a string (\\n \\t \\\\ \\\")");
        }
        if (n >= sizeof w->sbuf) return nfail(w, id, ZL_E_LIMIT, "string too long");
        w->sbuf[n++] = ch;
    }
    *out_n = n;
    return ZL_OK;
}

static int std_proc(aw_t *w, int which, int32_t id)
{
    const anode_t *nd = &w->pool[id];
    int32_t ch = nd->kind == N_CALL ? nd->l : -1, a = ch >= 0 ? w->pool[ch].nx : -1;
    int t;
    if (a < 0 || w->pool[a].nx >= 0)
        return nfail(w, id, ZL_E_SYNTAX,
                     "outinteger, outstring and ininteger take (channel, item)");
    const anode_t *cn = &w->pool[ch];
    if (cn->kind != N_NUM || cn->v != (which == STD_ININTEGER ? 0 : 1))
        return nfail(w, ch, ZL_E_UNSUPPORTED,
                     which == STD_ININTEGER ? "input is channel 0 only"
                                            : "output is channel 1 only");
    if (which == STD_OUTINTEGER) {
        if ((t = lw(w, a)) < 0) return st_of(w);
        if (!ARITH(t)) return nfail(w, a, ZL_E_SYNTAX, "outinteger prints a number");
        w->c->line = nd->line;
        em(w, ZL_OUTI, ZL_FMT_TRAIL, 0);
    } else if (which == STD_OUTSTRING) {
        uint32_t n = 0;
        if (w->pool[a].kind != N_STR)
            return nfail(w, a, ZL_E_SYNTAX, "outstring prints a string in double quotes");
        if (unescape(w, a, &n)) return st_of(w);
        int32_t k = zl_addstr(w->c, w->sbuf, n);
        if (k >= 0) em(w, ZL_OUTS, k, 0);
    } else {
        atg_t tg;
        if (target(w, a, &tg)) return st_of(w);
        if (tg.type != TY_INT)
            return nfail(w, a, ZL_E_SYNTAX, "ininteger reads into an integer variable");
        assign(w, &tg, 1, -1);
    }
    return st_of(w);
}

static int proc_stmt(aw_t *w, int32_t id)
{
    int32_t s = resolve(w, id);
    if (s < 0) return st_of(w);
    const asym_t *S = &w->sym[s];
    w->c->line = w->pool[id].line;
    if (S->kind == S_STD && S->res <= STD_ININTEGER) return std_proc(w, S->res, id);
    if (S->kind == S_STD) {
        if (lw(w, id) >= 0) em(w, ZL_DROP, 0, 0);
        return st_of(w);
    }
    if (S->kind != S_PROC)
        return nfail(w, id, ZL_E_SYNTAX, "a statement needs ':=' or a procedure");
    int t = call(w, s, id);
    if (t > TY_NONE) em(w, ZL_DROP, 0, 0);
    return st_of(w);
}

static int st_simple(aw_t *w)
{
    int32_t first = primary(w), tg[A_TARGETS], e = first;
    uint32_t nt = 0;
    if (first < 0) return st_of(w);
    if (tk(w) == O_ASSIGN) {
        for (;;) {
            int k = w->pool[e].kind;
            if (k != N_ID && k != N_ELEM)
                return nfail(w, e, ZL_E_SYNTAX, "only a variable can stand left of ':='");
            if (nt >= A_TARGETS) return afail(w, ZL_E_LIMIT, "too many left parts");
            tg[nt++] = e;
            lex(w); /* past ':=' */
            if ((e = pexpr(w)) < 0) return st_of(w);
            if (tk(w) != O_ASSIGN) break;
        }
        for (uint32_t k = 0; k < nt; k++) {
            pe(w, tg[k], 0);
            ts(w, " := ");
        }
        pe(w, e, 0);
        if (w->pass == 2) {
            atg_t t[A_TARGETS];
            for (uint32_t k = 0; k < nt; k++)
                if (target(w, tg[k], &t[k])) return st_of(w);
            return assign(w, t, nt, e);
        }
        return st_of(w);
    }
    if (w->pool[first].kind == N_ELEM) return afail(w, ZL_E_SYNTAX, "':=' expected");
    pe(w, first, 0);
    return w->pass == 2 ? proc_stmt(w, first) : st_of(w);
}

static int label_def(aw_t *w, const alex_t *at)
{
    if (w->pass == 1) {
        record(w, S_LABEL, TY_NONE, at);
        return st_of(w);
    }
    int32_t s = lookup_local(w, at->off, at->len);
    if (s < 0) return st_of(w);
    if (w->sym[s].kind != S_LABEL || w->sym[s].level != w->level)
        return pfail(w, at->line, at->col, ZL_E_VERIFY, "internal: label out of place");
    w->sym[s].addr = here(w);
    return ZL_OK;
}

static int decl(aw_t *w);

static int stmt(aw_t *w, int ctx)
{
    uint32_t mark = w->npool;
    int s = ZL_OK, labelled = 0, t;
    if (++w->sdepth > ZL_MAX_DEPTH) {
        s = afail(w, ZL_E_LIMIT, "statements nested too deeply");
        goto out;
    }
    while (tk(w) == T_ID) {
        alex_t save = w->L;
        lex(w);
        if (tk(w) != O_COLON) {
            w->L = save;
            break;
        }
        lex(w);
        if ((s = label_def(w, &save))) goto out;
        tn(w, save.off, save.len);
        ts(w, ":");
        labelled = 1;
    }
    if (tk(w) == T_NUM) {
        alex_t save = w->L;
        lex(w);
        int colon = tk(w) == O_COLON;
        w->L = save;
        if (colon) {
            s = afail(w, ZL_E_UNSUPPORTED, "numeric labels are not in this subset");
            goto out;
        }
    }
    t = tk(w);
    if (labelled && !is_end(t)) ts(w, " ");
    w->c->line = w->L.line;
    switch (t) {
    case K_BEGIN:
        lex(w);
        ts(w, "begin");
        s = block(w);
        break;
    case K_IF:
        if (ctx == C_THEN)
            s = afail(w, ZL_E_SYNTAX,
                      "a conditional statement after 'then' must be in begin ... end");
        else
            s = st_if(w);
        break;
    case K_FOR:
        s = st_for(w);
        break;
    case K_GOTO:
    case K_GO:
        s = st_goto(w);
        break;
    case T_ID:
        s = st_simple(w);
        break;
    case O_SEMI:
    case K_END:
    case K_ELSE:
    case T_EOF:
        break;
    case K_OWN:
    case K_REAL:
    case K_ARRAY:
    case K_SWITCH:
    case K_STRING:
        s = decl(w);
        break;
    case K_INTEGER:
    case K_BOOLEAN:
    case K_PROCEDURE:
        s = afail(w, ZL_E_SYNTAX, "declarations must come before the statements of a block");
        break;
    case K_CODE:
        s = afail(w, ZL_E_UNSUPPORTED,
                  "code bodies (procedures written elsewhere) are not in this subset");
        break;
    case K_COMMENT:
        s = afail(w, ZL_E_SYNTAX, "a comment must follow 'begin' or ';'");
        break;
    default:
        s = afail(w, ZL_E_SYNTAX, "statement expected");
        break;
    }
out:
    w->sdepth--;
    w->npool = mark;
    return w->c->failed ? w->c->d->status : s;
}

/* ------------------------------------------------------------ declarations */

static int var_decl(aw_t *w, int ty)
{
    ts(w, ty == TY_INT ? "integer" : "Boolean");
    for (int first = 1;; first = 0) {
        if (tk(w) != T_ID) return afail(w, ZL_E_SYNTAX, "variable name expected");
        alex_t at = w->L;
        ts(w, first ? " " : ", ");
        tn(w, at.off, at.len);
        record(w, S_VAR, ty, &at);
        lex(w);
        if (tk(w) == O_LB)
            return afail(w, ZL_E_SYNTAX, "an array is declared with 'integer array'");
        if (tk(w) != O_COMMA) break;
        lex(w);
    }
    return st_of(w);
}

/* Pass 2: evaluate a dynamic array's bounds into its descriptor. */
static int bound_code(aw_t *w, const asym_t *A, int32_t lo, int32_t hi)
{
    int t;
    w->bounds = 1;
    w->bound_base = w->open[w->nopen - 1].base;
    if ((t = lw(w, lo)) >= 0 && !ARITH(t)) tyerr(w, lo, "an array bound must be a number");
    st(w, A->level, A->off + 1);
    if ((t = lw(w, hi)) >= 0 && !ARITH(t)) tyerr(w, hi, "an array bound must be a number");
    ld(w, A->level, A->off + 1);
    em(w, ZL_SUB, 0, 0);
    em(w, ZL_PUSH, 1, 0);
    em(w, ZL_ADD, 0, 0);
    em(w, ZL_PUSH, 0, 0);
    em(w, ZL_MAX, 0, 0); /* u < l: no elements */
    st(w, A->level, A->off + 2);
    w->bounds = 0;
    return st_of(w);
}

static int arr_decl(aw_t *w, int ty)
{
    ts(w, ty == TY_INT ? "integer array" : "Boolean array");
    for (int firstseg = 1;; firstseg = 0) {
        uint32_t ns = 0;
        for (;;) {
            if (tk(w) != T_ID) return afail(w, ZL_E_SYNTAX, "array name expected");
            if (ns >= A_SEG) return afail(w, ZL_E_LIMIT, "too many arrays share one bound pair");
            w->seg[ns].off = w->L.off;
            w->seg[ns].len = w->L.len;
            w->seg[ns].line = w->L.line;
            w->seg[ns].col = w->L.col;
            ns++;
            ts(w, firstseg && ns == 1 ? " " : ", ");
            tn(w, w->L.off, w->L.len);
            lex(w);
            if (tk(w) == O_COMMA) {
                lex(w);
                continue;
            }
            if (tk(w) == O_LB) break;
            return afail(w, ZL_E_SYNTAX, "'[' and the bounds expected after the array names");
        }
        lex(w);
        int32_t lo = pexpr(w), hi = -1;
        if (lo < 0) return st_of(w);
        if (tk(w) != O_COLON) return afail(w, ZL_E_SYNTAX, "':' expected between the bounds");
        lex(w);
        if ((hi = pexpr(w)) < 0) return st_of(w);
        if (tk(w) == O_COMMA)
            return afail(w, ZL_E_UNSUPPORTED,
                         "arrays of more than one dimension are not in this subset");
        if (tk(w) != O_RB) return afail(w, ZL_E_SYNTAX, "']' expected");
        lex(w);
        ts(w, "[");
        pe(w, lo, 0);
        ts(w, ":");
        pe(w, hi, 0);
        ts(w, "]");
        int64_t l = 0, h = 0, n = 0;
        int cst = fold(w, lo, &l) && fold(w, hi, &h) && !__builtin_sub_overflow(h, l, &n) &&
                  !__builtin_add_overflow(n, 1, &n);
        if (cst && n < 1)
            return nfail(w, lo, ZL_E_UNSUPPORTED,
                         "an array with no elements (upper bound below lower)");
        if (cst && n > ZL_MAX_FRAMES) return nfail(w, lo, ZL_E_LIMIT, "array too large");
        for (uint32_t k = 0; k < ns && !w->c->failed; k++) {
            alex_t at = w->L;
            at.off = w->seg[k].off;
            at.len = w->seg[k].len;
            at.line = w->seg[k].line;
            at.col = w->seg[k].col;
            adecl_t *d = record(w, S_ARR, ty, &at);
            if (d) {
                d->dyn = (uint8_t) !cst;
                d->lo = l;
                d->n = n;
                if (!cst) w->sdyn[w->open[w->nopen - 1].id] = 1;
            }
            if (w->pass == 2 && !cst) {
                int32_t s = lookup_local(w, at.off, at.len);
                if (s >= 0 && bound_code(w, &w->sym[s], lo, hi)) return st_of(w);
            }
        }
        if (tk(w) != O_COMMA) break;
        lex(w);
    }
    return st_of(w);
}

static int head_comments(aw_t *w)
{
    while (tk(w) == K_COMMENT) {
        uint32_t o, n;
        if (comment_text(w, &o, &n)) return st_of(w);
        ts(w, " ");
        tw_comment(w, o, n);
    }
    return st_of(w);
}

static int32_t find_par(aw_t *w, uint32_t pb, uint32_t nf)
{
    for (uint32_t k = 0; k < nf; k++)
        if (nameq(w->src + w->par[pb + k].off, w->par[pb + k].len, w->src + w->L.off, w->L.len))
            return (int32_t) (pb + k);
    return -1;
}

/* After 'procedure' (and its type): the heading, then the body. */
static int proc_decl(aw_t *w, int ty)
{
    uint32_t pb = w->npar, nf = 0, pcells = 0;
    if (tk(w) != T_ID) return afail(w, ZL_E_SYNTAX, "procedure name expected");
    alex_t pn = w->L;
    lex(w);
    ts(w, ty == TY_INT    ? "integer procedure "
          : ty == TY_BOOL ? "Boolean procedure "
                          : "procedure ");
    tn(w, pn.off, pn.len);
    if (tk(w) == O_LP) {
        lex(w);
        ts(w, "(");
        for (;;) {
            if (tk(w) != T_ID) return afail(w, ZL_E_SYNTAX, "formal parameter name expected");
            if (find_par(w, pb, nf) >= 0)
                return afail(w, ZL_E_SYNTAX, "formal parameter named twice");
            if (w->npar >= A_PAR) return afail(w, ZL_E_LIMIT, "too many formal parameters");
            zl_bzero(&w->par[w->npar], sizeof w->par[0]);
            w->par[w->npar].off = w->L.off;
            w->par[w->npar].len = w->L.len;
            w->par[w->npar].line = w->L.line;
            w->par[w->npar].col = w->L.col;
            w->npar++;
            if (nf++) ts(w, ", ");
            tn(w, w->L.off, w->L.len);
            lex(w);
            if (tk(w) == O_COMMA) {
                lex(w);
                continue;
            }
            if (tk(w) == O_RP) break;
            return afail(w, ZL_E_SYNTAX, "',' or ')' expected in the formal parameters");
        }
        lex(w);
        ts(w, ")");
    }
    if (tk(w) != O_SEMI) return afail(w, ZL_E_SYNTAX, "';' expected after the procedure heading");
    lex(w);
    ts(w, ";");
    if (head_comments(w)) return st_of(w);
    if (tk(w) == K_VALUE) {
        lex(w);
        ts(w, " value");
        for (int first = 1;; first = 0) {
            int32_t k;
            if (tk(w) != T_ID)
                return afail(w, ZL_E_SYNTAX, "formal parameter expected in the value part");
            if ((k = find_par(w, pb, nf)) < 0)
                return afail(w, ZL_E_SYNTAX,
                             "the value part names something that is not a parameter");
            if (w->par[k].value)
                return afail(w, ZL_E_SYNTAX, "a parameter named twice in the value part");
            w->par[k].value = 1;
            ts(w, first ? " " : ", ");
            tn(w, w->L.off, w->L.len);
            lex(w);
            if (tk(w) != O_COMMA) break;
            lex(w);
        }
        if (tk(w) != O_SEMI) return afail(w, ZL_E_SYNTAX, "';' expected after the value part");
        lex(w);
        ts(w, ";");
        if (head_comments(w)) return st_of(w);
    }
    for (;;) {
        int t = tk(w), sty, arr = 0;
        if (t == K_INTEGER || t == K_BOOLEAN) {
            sty = t == K_INTEGER ? TY_INT : TY_BOOL;
            lex(w);
            if (tk(w) == K_ARRAY) {
                arr = 1;
                lex(w);
            } else if (tk(w) == K_PROCEDURE) {
                return afail(w, ZL_E_UNSUPPORTED, "procedure parameters are not in this subset");
            }
        } else if (t == K_REAL) {
            return afail(w, ZL_E_UNSUPPORTED, "real parameters are not in the exact subset");
        } else if (t == K_ARRAY) {
            return afail(w, ZL_E_UNSUPPORTED, "a plain array is real; specify integer array");
        } else if (t == K_LABEL) {
            return afail(w, ZL_E_UNSUPPORTED, "label parameters are not in this subset");
        } else if (t == K_SWITCH) {
            return afail(w, ZL_E_UNSUPPORTED, "switch parameters are not in this subset");
        } else if (t == K_PROCEDURE) {
            return afail(w, ZL_E_UNSUPPORTED, "procedure parameters are not in this subset");
        } else if (t == K_STRING) {
            return afail(w, ZL_E_UNSUPPORTED, "string parameters are not in this subset");
        } else {
            break;
        }
        ts(w, sty == TY_INT ? (arr ? " integer array" : " integer")
                            : (arr ? " Boolean array" : " Boolean"));
        for (int first = 1;; first = 0) {
            int32_t k;
            if (tk(w) != T_ID)
                return afail(w, ZL_E_SYNTAX, "formal parameter expected in the specification");
            if ((k = find_par(w, pb, nf)) < 0)
                return afail(w, ZL_E_SYNTAX,
                             "a specification of something that is not a parameter");
            if (w->par[k].spec) return afail(w, ZL_E_SYNTAX, "a parameter specified twice");
            w->par[k].spec = 1;
            w->par[k].type = (uint8_t) sty;
            w->par[k].arr = (uint8_t) arr;
            ts(w, first ? " " : ", ");
            tn(w, w->L.off, w->L.len);
            lex(w);
            if (tk(w) != O_COMMA) break;
            lex(w);
        }
        if (tk(w) != O_SEMI) return afail(w, ZL_E_SYNTAX, "';' expected after a specification");
        lex(w);
        ts(w, ";");
        if (head_comments(w)) return st_of(w);
    }
    for (uint32_t k = pb; k < pb + nf; k++) {
        if (!w->par[k].spec)
            return pfail(w, w->par[k].line, w->par[k].col, ZL_E_SYNTAX,
                         "a formal parameter without a specification");
        if (w->par[k].value && w->par[k].arr)
            return pfail(w, w->par[k].line, w->par[k].col, ZL_E_UNSUPPORTED,
                         "array value parameters (a copy of the array) are not in this subset");
        pcells += w->par[k].arr ? 3 : w->par[k].value ? 1 : 2;
    }
    if (pcells >= ZL_MAX_STACK) return afail(w, ZL_E_LIMIT, "too many parameters");
    int32_t s = -1;
    if (w->pass == 1) {
        adecl_t *d = record(w, S_PROC, ty, &pn);
        if (!d) return st_of(w);
        if (w->nfm + nf > A_FORMALS) return afail(w, ZL_E_LIMIT, "too many formal parameters");
        d->fbase = (int32_t) w->nfm;
        d->nf = (int32_t) nf;
        for (uint32_t k = pb; k < pb + nf; k++) {
            aformal_t *f = &w->fm[w->nfm++];
            f->nm = w->src + w->par[k].off;
            f->nlen = (uint16_t) w->par[k].len;
            f->mode = w->par[k].arr ? F_ARRAY : w->par[k].value ? F_VALUE : F_NAME;
            f->type = w->par[k].type;
            f->line = w->par[k].line;
            f->col = w->par[k].col;
        }
    } else if ((s = lookup_local(w, pn.off, pn.len)) < 0) {
        return st_of(w);
    }
    /* the body: one more level, whose frame holds the parameters first */
    int saved = w->level, B = (s >= 0 ? w->sym[s].level : w->level) + 1, comp, isfor;
    if (set_level(w, B, L_PROC, B)) return st_of(w);
    int32_t jover = em(w, ZL_JMP, 0, 0);
    if (s >= 0) w->sym[s].addr = here(w);
    int32_t enter = em(w, ZL_ENTER, 0, (int32_t) pcells);
    w->level = B;
    if (scope_open(w, B, B)) return st_of(w);
    if (s >= 0) {
        const asym_t *P = &w->sym[s];
        for (int32_t k = 0; k < P->nf && !w->c->failed; k++) {
            const aformal_t *f = &w->fm[P->fbase + k];
            int kind = f->mode == F_VALUE ? S_VAR : f->mode == F_NAME ? S_NAME : S_ARR;
            int32_t fs = add_sym(w, f->nm, f->nlen, kind, f->type, f->line, f->col);
            if (fs < 0) break;
            w->sym[fs].level = (int16_t) B;
            w->sym[fs].dyn = f->mode == F_ARRAY ? 2 : 0;
            w->sym[fs].off = alloc(w, B, f->mode == F_VALUE ? 1 : f->mode == F_NAME ? 2 : 3);
        }
        if (ty != TY_NONE) w->sym[s].res = alloc(w, B, 1);
        w->sym[s].busy = 1;
    }
    if (st_of(w) || slot(w, C_PLAIN, &comp, &isfor, 1)) return st_of(w);
    if (s >= 0) {
        w->sym[s].busy = 0;
        if (ty != TY_NONE) ld(w, B, w->sym[s].res);
        em(w, ZL_LEAVE, 0, 0);
        em(w, ZL_RET, 0, 0);
        zl_patch(w->c, enter, w->lv[B].max);
    }
    scope_close(w);
    w->level = saved;
    patchto(w, jover);
    w->npar = pb;
    return st_of(w);
}

static int decl(aw_t *w)
{
    int t = tk(w);
    switch (t) {
    case K_OWN:
        return afail(w, ZL_E_UNSUPPORTED, "own variables are not in this subset");
    case K_REAL:
        return afail(w, ZL_E_UNSUPPORTED,
                     "real (floating-point) variables are not in the exact subset");
    case K_ARRAY:
        return afail(w, ZL_E_UNSUPPORTED, "a plain array is real; declare integer array");
    case K_SWITCH:
        return afail(w, ZL_E_UNSUPPORTED, "switch declarations are not in this subset");
    case K_STRING:
        return afail(w, ZL_E_UNSUPPORTED, "string variables are not in this subset");
    case K_PROCEDURE:
        lex(w);
        return proc_decl(w, TY_NONE);
    default: {
        int ty = t == K_INTEGER ? TY_INT : TY_BOOL;
        lex(w);
        if (tk(w) == K_ARRAY) {
            lex(w);
            return arr_decl(w, ty);
        }
        if (tk(w) == K_PROCEDURE) {
            lex(w);
            return proc_decl(w, ty);
        }
        return var_decl(w, ty);
    }
    }
}

/* Entering the statements of a block with dynamic arrays: size them, then
 * enter frames of A_CHUNK cells until they fit, and run the statements in
 * the last one (one level deeper; the helper returns when the block ends). */
static int dyn_enter(aw_t *w, uint32_t base, int32_t dyna, int32_t *jafter)
{
    int P = w->level, S = w->lv[P].store, first = 1;
    for (uint32_t i = base; i < w->nsym; i++)
        if (w->sym[i].kind == S_ARR && w->sym[i].dyn == 1) {
            ld(w, S, w->sym[i].off + 2);
            if (!first) em(w, ZL_ADD, 0, 0);
            first = 0;
        }
    em(w, ZL_PUSH, A_CHUNK - 1, 0);
    em(w, ZL_ADD, 0, 0);
    em(w, ZL_PUSH, A_CHUNK, 0);
    em(w, ZL_DIVT, 0, 0);
    em(w, ZL_PUSH, 1, 0);
    em(w, ZL_MAX, 0, 0);
    em(w, ZL_PUSH, A_CHUNK, 0);
    em(w, ZL_MUL, 0, 0);
    em(w, ZL_DUP, 0, 0);
    st(w, S, dyna); /* the cells the frames will hold */
    em(w, ZL_LINK, 0, 0);
    int32_t call = em(w, ZL_CALL, 0, 0);
    *jafter = em(w, ZL_JMP, 0, 0);
    int32_t h = here(w);
    if (call >= 0) zl_patch(w->c, call, h);
    if (set_level(w, P + 1, L_DYN, S)) return st_of(w);
    w->level = P + 1;
    em(w, ZL_ENTER, A_CHUNK, 0); /* [cells still to enter] */
    em(w, ZL_PUSH, A_CHUNK, 0);
    em(w, ZL_SUB, 0, 0);
    em(w, ZL_DUP, 0, 0);
    int32_t last = em(w, ZL_JZ, 0, 0);
    em(w, ZL_LINK, 1, 0);
    em(w, ZL_CALL, h, 0);
    em(w, ZL_LEAVE, 0, 0);
    em(w, ZL_RET, 0, 0);
    patchto(w, last);
    em(w, ZL_DROP, 0, 0);
    em(w, ZL_ADDRF, 0, 0); /* the first cell is this frame's end minus the cells */
    em(w, ZL_PUSH, A_CHUNK, 0);
    em(w, ZL_ADD, 0, 0);
    ld(w, S, dyna);
    em(w, ZL_SUB, 0, 0);
    for (uint32_t i = base; i < w->nsym; i++)
        if (w->sym[i].kind == S_ARR && w->sym[i].dyn == 1) {
            em(w, ZL_DUP, 0, 0);
            st(w, S, w->sym[i].off);
            ld(w, S, w->sym[i].off + 2);
            em(w, ZL_ADD, 0, 0);
        }
    em(w, ZL_DROP, 0, 0);
    return st_of(w);
}

/* After 'begin' (already printed): a block or compound statement, through
 * its 'end'. Items go one per line, one step in from the 'begin' line. */
static int block(aw_t *w)
{
    alex_t save = w->L;
    uint32_t o, n, d = w->ind, symbase = w->nsym;
    int P = w->level, dyn = 0, need = 0, head, isblock;
    int32_t dyna = -1, jafter = -1;
    while (tk(w) == K_COMMENT && !w->c->failed) comment_text(w, &o, &n);
    isblock = is_decl(tk(w));
    w->L = save;
    head = isblock;
    if (isblock) {
        int S = w->lv[P].store;
        dyn = w->pass == 2 && w->nscope < A_SCOPES && w->sdyn[w->nscope];
        if (scope_open(w, dyn ? P + 1 : P, S)) return st_of(w);
        symbase = w->open[w->nopen - 1].base;
        if (dyn) dyna = alloc(w, S, 1);
    }
    for (;;) {
        if (w->c->failed) return st_of(w);
        if (tk(w) == K_COMMENT) {
            if (comment_text(w, &o, &n)) return st_of(w);
            if (need) ts(w, ";");
            nl(w, d + 1);
            tw_comment(w, o, n);
            need = 0;
            continue;
        }
        if (head && is_decl(tk(w))) {
            if (need) ts(w, ";");
            nl(w, d + 1);
            w->ind = d + 1;
            int s = decl(w);
            w->ind = d;
            if (s) return s;
            need = 1;
        } else {
            if (head) {
                head = 0;
                if (dyn && dyn_enter(w, symbase, dyna, &jafter)) return st_of(w);
            }
            if (!is_end(tk(w))) {
                if (need) ts(w, ";");
                nl(w, d + 1);
                need = 1;
            }
            w->ind = d + 1;
            int s = stmt(w, C_PLAIN);
            w->ind = d;
            if (s) return s;
        }
        if (tk(w) == O_SEMI) {
            lex(w);
            continue;
        }
        if (tk(w) == K_END) break;
        return afail(w, ZL_E_SYNTAX, "';' or 'end' expected");
    }
    if (head)
        return afail(w, ZL_E_SYNTAX, "a block needs ';' and a statement after its declarations");
    lex(w); /* past 'end' */
    if (!is_end(tk(w)))
        return afail(w, ZL_E_UNSUPPORTED,
                     "text after 'end' (an end comment) is not admitted; use comment");
    nl(w, d);
    ts(w, "end");
    if (dyn) {
        em(w, ZL_LEAVE, 0, 0);
        em(w, ZL_RET, 0, 0);
        patchto(w, jafter);
        w->level = P;
    }
    if (isblock) scope_close(w);
    return st_of(w);
}

/* ------------------------------------------------------------ program */

/* Name parameters bound to thunks: [env, -(thunk+1)] -> value (read) or
 * address (addr), found by binary search over the thunk numbers. */
static void disp_tree(aw_t *w, uint32_t lo, uint32_t hi, int addr)
{
    if (lo == hi) {
        w->c->line = w->th[lo].line;
        if (addr && !w->th[lo].addr) {
            em(w, ZL_TRAP, ZL_E_DOMAIN, 0); /* assignment to an expression */
            return;
        }
        em(w, ZL_DROP, 0, 0);
        em(w, ZL_CALL, w->th[lo].entry, 0);
        if (!addr && w->th[lo].addr) em(w, ZL_LOADA, 0, 0);
        em(w, ZL_RET, 0, 0);
        return;
    }
    uint32_t mid = lo + (hi - lo) / 2;
    em(w, ZL_DUP, 0, 0);
    em(w, ZL_PUSH, mid + 1, 0);
    em(w, ZL_LT, 0, 0);
    int32_t j = em(w, ZL_JZ, 0, 0);
    disp_tree(w, lo, mid, addr);
    patchto(w, j);
    disp_tree(w, mid + 1, hi, addr);
}

static void dispatcher(aw_t *w, int addr)
{
    if (!w->nth) {
        em(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        return;
    }
    em(w, ZL_NEG, 0, 0);
    em(w, ZL_PUSH, 1, 0);
    em(w, ZL_SUB, 0, 0);
    disp_tree(w, 0, w->nth - 1, addr);
}

static int program(aw_t *w)
{
    lex(w);
    if (w->c->failed) return st_of(w);
    if (tk(w) != K_BEGIN) return afail(w, ZL_E_SYNTAX, "an ALGOL 60 program starts with 'begin'");
    if (scope_open(w, 0, 0)) return st_of(w);
    w->c->line = w->L.line;
    lex(w);
    ts(w, "begin");
    if (block(w)) return st_of(w);
    if (tk(w) != T_EOF) return afail(w, ZL_E_SYNTAX, "text after the final 'end'");
    ts(w, "\n");
    scope_close(w);
    return st_of(w);
}

int zl_algol_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    aw_t *w = (aw_t *) work;
    if (len > 0x3fffffff) return zl_fail(c, ZL_E_LIMIT, "source too long");
    zl_bzero(w, sizeof *w);
    w->src = src;
    w->len = (uint32_t) len;
    w->c = c;
    for (int pass = 1; pass <= 2; pass++) {
        w->pass = pass;
        w->tw = pass == 2 ? tw : 0;
        w->nsym = w->nscope = w->nopen = w->npool = w->nfix = w->ndfix = w->nth = 0;
        w->nfel = w->npar = w->sdepth = w->edepth = w->ind = 0;
        w->level = 0;
        w->bounds = 0;
        zl_bzero(&w->L, sizeof w->L);
        w->L.pline = 1;
        w->L.pcol = 1;
        set_level(w, 0, L_MAIN, 0);
        for (size_t k = 0; k < sizeof stdtab / sizeof stdtab[0]; k++) {
            uint32_t n = 0;
            while (stdtab[k].nm[n]) n++;
            int32_t s = add_sym(w, stdtab[k].nm, n, S_STD, TY_NONE, 0, 0);
            if (s >= 0) w->sym[s].res = stdtab[k].which;
        }
        if (program(w)) return st_of(w);
    }
    em(w, ZL_HALT, 0, 0);
    if (w->ndfix) {
        int32_t rd = here(w);
        dispatcher(w, 0);
        int32_t adr = here(w);
        dispatcher(w, 1);
        for (uint32_t k = 0; k < w->ndfix; k++)
            zl_patch(c, w->dfix[k].at, w->dfix[k].addr ? adr : rd);
    }
    return st_of(w);
}
