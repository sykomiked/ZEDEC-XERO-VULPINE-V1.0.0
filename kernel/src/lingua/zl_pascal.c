/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_pascal.c — Pascal front-end: ISO 7185 Pascal (1983; Wirth's language
 * of 1970), integer subset, and its twin "Tutordrake" (made for teaching).
 *
 * Admitted: the program heading, with or without (input, output); const
 * (integer and Boolean constants, signed); var of integer, Boolean and
 * one-dimensional array[lo..hi] of integer or Boolean (constant bounds, any
 * lo); procedures and functions, nested to any depth up to 15, with value
 * and var parameters of integer or Boolean, and recursion; compound
 * statements, :=, if/then/else, while, repeat/until, for to/downto, case
 * with integer constant labels; + - * div mod, a sign at the start of an
 * expression, = <> < <= > >=, and or not; abs sqr odd succ pred ord maxint
 * true false; write and writeln of integers, Booleans and string literals
 * with constant field widths (x:5); read and readln of integer variables;
 * comments in braces or (* *), and the directive {$mode iso}.
 *
 * Semantics follow ISO 7185 as Free Pascal 3.2 runs it with -Miso (the
 * reference): integer is 32 bits (maxint 2147483647) and any result outside
 * 32 bits traps OVERFLOW (fpc computes in 64 bits and wraps on store; the
 * two agree whenever every result fits); div truncates; i mod j is never
 * negative and traps DIVZERO for j = 0 and DOMAIN for j < 0; and and or
 * short-circuit left to right, as fpc does; an integer is written in 11
 * columns and a Boolean in 5 unless a width is given, a Boolean or string
 * wider than its width is cut to it; an index outside its array traps
 * BOUNDS and a case selector without a matching label traps DOMAIN (fpc
 * checks neither without -Cr). read takes the next number of the caller's
 * input list; readln is read, and readln alone does nothing (the input is a
 * list of integers, not lines). Variables start at zero.
 *
 * Refused by name (ZL_E_UNSUPPORTED): real, char, string and text values
 * and the functions on them (chr trunc round sqrt sin cos exp ln arctan eof
 * eoln), real numbers and '/', type, record, set, file, packed, pointers and
 * nil, with, in, label and goto, forward, procedural and array parameters,
 * subrange types, multi-dimensional arrays, whole-array values, file
 * arguments to read and write, non-constant field widths, a case selector
 * of type Boolean, any other compiler directive, underscores in names.
 * Syntax and type errors are ZL_E_SYNTAX; unknown names ZL_E_UNDEFINED.
 *
 * Tutordrake (the twin): keywords and names in lower case; one statement
 * per line, two spaces of indentation per level; the block's const, var,
 * nested procedures and begin at the level of its heading, their items one
 * level in; each sub-statement of if, while, for and case on its own line
 * one level in, "else if" kept on one line; begin ... end with no empty
 * statements (an empty branch is written begin end); one space around
 * binary operators and :=, ", " between items, minimal parentheses, no
 * unary plus; a comment on a line of its own before the line it preceded,
 * with (* *) written as { } unless it holds a '}'. */
#include "zl_int.h"

#define P_SYMS     768
#define P_POOL     1024
#define P_PARAMS   1024
#define P_COMMENTS 64
#define P_NEST     15   /* procedures nested in procedures */
#define P_GROUP    64   /* names in one declaration group */
#define P_MAXPARAM 64   /* parameters of one procedure */
#define P_FRAME    4096 /* cells in one procedure frame */
#define P_LABELS   1024 /* case labels open at once */
#define P_NAMELEN  127
#define P_STRLEN   255
#define P_DEFWIDTH 11 /* fpc -Miso: an integer's default field width */
#define P_MAXINT   2147483647

enum { TK_EOF = 0, TK_ID, TK_NUM, TK_STR, TK_KW, TK_SYM };
enum {
    K_AND,
    K_ARRAY,
    K_BEGIN,
    K_CASE,
    K_CONST,
    K_DIV,
    K_DO,
    K_DOWNTO,
    K_ELSE,
    K_END,
    K_FILE,
    K_FOR,
    K_FUNCTION,
    K_GOTO,
    K_IF,
    K_IN,
    K_LABEL,
    K_MOD,
    K_NIL,
    K_NOT,
    K_OF,
    K_OR,
    K_PACKED,
    K_PROCEDURE,
    K_PROGRAM,
    K_RECORD,
    K_REPEAT,
    K_SET,
    K_THEN,
    K_TO,
    K_TYPE,
    K_UNTIL,
    K_VAR,
    K_WHILE,
    K_WITH,
    K__COUNT
};
static const char *const kwnames[K__COUNT] = {
    "AND", "ARRAY", "BEGIN", "CASE",     "CONST",  "DIV",       "DO",      "DOWNTO", "ELSE",
    "END", "FILE",  "FOR",   "FUNCTION", "GOTO",   "IF",        "IN",      "LABEL",  "MOD",
    "NIL", "NOT",   "OF",    "OR",       "PACKED", "PROCEDURE", "PROGRAM", "RECORD", "REPEAT",
    "SET", "THEN",  "TO",    "TYPE",     "UNTIL",  "VAR",       "WHILE",   "WITH"};

/* multi-character symbols; single ones are their character */
enum { SY_ASSIGN = 256, SY_LE, SY_GE, SY_NE, SY_DOTDOT };

enum { Y_CONST = 1, Y_VAR, Y_VPARAM, Y_RPARAM, Y_ARR, Y_PROC, Y_FUNC };
enum { TY_INT = 1, TY_BOOL = 2 };
enum { N_NUM = 1, N_CNAME, N_VAR, N_ELEM, N_CALL, N_FN, N_NEG, N_NOT, N_BIN };
enum { F_ABS = 1, F_SQR, F_ODD, F_SUCC, F_PRED, F_ORD };
enum { O_EQ = 1, O_NE, O_LT, O_LE, O_GT, O_GE, O_ADD, O_SUB, O_OR, O_MUL, O_DIV, O_MOD, O_AND };
enum { CTX_SEQ = 0, CTX_BRANCH, CTX_INLINE };

typedef struct {
    uint8_t kind;
    int16_t op; /* keyword index or symbol */
    uint32_t pos, len, line, col;
    int64_t v;
} ptok_t;

typedef struct {
    uint32_t pos;
    uint16_t len;
    uint8_t kind, type, level, isfor;
    uint16_t pfirst, np;
    int32_t addr;  /* slot (level 0), frame offset, or code address */
    int32_t lo, n; /* array: lower bound and length; function: result offset */
    int64_t v;     /* constant value */
} psym_t;

typedef struct {
    uint8_t kind, op, type, h; /* h: height of the tree below, for the depth limit */
    int32_t l, r, next, sym;   /* r: a call's arguments; next: the following argument */
    uint32_t line, pos, len;   /* pos, len: a constant's name in the source */
    int64_t v;
} pnode_t;

/* a constant as written: a sign, then a number or a constant's name */
typedef struct {
    int neg, named;
    uint32_t pos, len;
    int64_t v;
    int type;
} pconst_t;

typedef struct {
    const char *src;
    size_t n, i;
    uint32_t line, lstart;
    ptok_t t;
    char sbuf[P_STRLEN + 1]; /* the current string token, unescaped */
    uint32_t slen;
    struct {
        uint32_t pos, len;
        uint8_t paren;
    } com[P_COMMENTS];
    uint32_t ncom;
    psym_t sym[P_SYMS];
    uint32_t nsym;
    uint8_t pkind[P_PARAMS], ptype[P_PARAMS];
    uint32_t nparam;
    pnode_t pool[P_POOL];
    uint32_t npool;
    uint32_t level;                 /* the block being compiled: 0 is the program */
    uint32_t scope[P_NEST + 2];     /* first symbol of each open block */
    int32_t fsize[P_NEST + 2];      /* frame cells used by each open block */
    int32_t cur[P_NEST + 2];        /* the procedure whose block each level is */
    int32_t fors[ZL_MAX_DEPTH + 1]; /* control variables of the open for loops */
    uint32_t nfor;
    struct {
        int64_t v;
        int32_t addr;
    } lab[P_LABELS];
    uint32_t nlab;
    int32_t armjmp[P_LABELS];
    uint32_t narm;
    uint32_t depth;
    zl_cc_t *c;
    zl_out_t *tw;
} pw_t;

ZL_STATIC_ASSERT(sizeof(pw_t) <= sizeof(((zl_work_t *) 0)->fe), pw_fits);

/* ------------------------------------------------------------ errors */

static int pfail_at(pw_t *w, uint32_t line, uint32_t col, int st, const char *msg)
{
    if (!w->c->failed) {
        w->c->line = line;
        w->c->col = col;
    }
    return zl_fail(w->c, st, msg);
}

static int pfail(pw_t *w, int st, const char *msg)
{
    return pfail_at(w, w->t.line, w->t.col, st, msg);
}

static int status(pw_t *w)
{
    return w->c->failed ? w->c->d->status : ZL_OK;
}

#define TRY(x)                                                                                     \
    do {                                                                                           \
        int s_ = (x);                                                                              \
        if (s_) return s_;                                                                         \
    } while (0)

/* ------------------------------------------------------------ twin text */

static void tw_s(pw_t *w, const char *s)
{
    if (w->tw) zl_puts(w->tw, s);
}

static void tw_i(pw_t *w, int64_t v)
{
    if (w->tw) zl_puti(w->tw, v);
}

/* a name from the source, in lower case */
static void tw_name(pw_t *w, uint32_t pos, uint32_t len)
{
    if (!w->tw) return;
    for (uint32_t k = 0; k < len; k++) {
        char ch = w->src[pos + k];
        zl_putc(w->tw, (char) (ch >= 'A' && ch <= 'Z' ? ch + 32 : ch));
    }
}

static void tw_indent(pw_t *w, uint32_t level)
{
    for (uint32_t k = 0; k < level; k++) tw_s(w, "  ");
}

/* Start a line: the comments read since the last line go first, each on a
 * line of its own, then the indentation. */
static void bol(pw_t *w, uint32_t level)
{
    for (uint32_t k = 0; k < w->ncom; k++) {
        const char *s = w->src + w->com[k].pos;
        uint32_t n = w->com[k].len, brace = 0;
        tw_indent(w, level);
        if (w->com[k].paren) {
            brace = 1;
            for (uint32_t j = 2; j + 2 < n; j++)
                if (s[j] == '}') brace = 0;
        }
        if (w->tw) {
            if (brace) {
                zl_putc(w->tw, '{');
                zl_put(w->tw, s + 2, n - 4);
                zl_putc(w->tw, '}');
            } else {
                zl_put(w->tw, s, n);
            }
        }
        tw_s(w, "\n");
    }
    w->ncom = 0;
    tw_indent(w, level);
}

/* ------------------------------------------------------------ lexing */

static int lexfail(pw_t *w, size_t at, int st, const char *msg)
{
    return pfail_at(w, w->line, (uint32_t) (at - w->lstart) + 1, st, msg);
}

static int newline(pw_t *w, size_t at)
{
    if (at - w->lstart > ZL_MAX_LINE) return lexfail(w, at, ZL_E_LIMIT, "line too long");
    w->line++;
    w->lstart = (uint32_t) at + 1;
    return ZL_OK;
}

static int comment(pw_t *w)
{
    const char *s = w->src;
    size_t n = w->n, a = w->i, j;
    uint32_t line = w->line, col = (uint32_t) (a - w->lstart) + 1;
    int paren = s[a] == '(';
    j = a + (paren ? 2 : 1);
    for (;;) {
        if (j >= n) return pfail_at(w, line, col, ZL_E_SYNTAX, "unterminated comment");
        if (s[j] == '\n') TRY(newline(w, j));
        if (!paren && s[j] == '}') {
            j++;
            break;
        }
        if (paren && s[j] == '*' && j + 1 < n && s[j + 1] == ')') {
            j += 2;
            break;
        }
        if (s[j] == '{' || (s[j] == '(' && j + 1 < n && s[j + 1] == '*'))
            return lexfail(w, j, ZL_E_UNSUPPORTED, "a comment inside a comment");
        j++;
    }
    size_t in = a + (paren ? 2 : 1), inlen = j - in - (paren ? 2 : 1);
    if (inlen && s[in] == '$' && !zl_wordeq(s + in, inlen, "$MODE ISO"))
        return pfail_at(w, line, col, ZL_E_UNSUPPORTED,
                        "compiler directive (only {$mode iso} is accepted)");
    if (w->ncom >= P_COMMENTS)
        return pfail_at(w, line, col, ZL_E_LIMIT, "too many comments in one line");
    w->com[w->ncom].pos = (uint32_t) a;
    w->com[w->ncom].len = (uint32_t) (j - a);
    w->com[w->ncom].paren = (uint8_t) paren;
    w->ncom++;
    w->i = j;
    return ZL_OK;
}

/* Read the next token into w->t. */
static int lex(pw_t *w)
{
    const char *s = w->src;
    size_t n = w->n;
    if (w->c->failed) return status(w);
    for (;;) {
        if (w->i >= n) break;
        int ch = (unsigned char) s[w->i];
        if (ch == '\n') {
            TRY(newline(w, w->i));
            w->i++;
        } else if (zl_isspace(ch)) {
            w->i++;
        } else if (ch == '{' || (ch == '(' && w->i + 1 < n && s[w->i + 1] == '*')) {
            TRY(comment(w));
        } else {
            break;
        }
    }
    size_t a = w->i, j = a;
    ptok_t *t = &w->t;
    t->pos = (uint32_t) a;
    t->line = w->line;
    t->col = (uint32_t) (a - w->lstart) + 1;
    t->v = 0;
    t->op = 0;
    if (a >= n) {
        if (a - w->lstart > ZL_MAX_LINE) return lexfail(w, a, ZL_E_LIMIT, "line too long");
        t->kind = TK_EOF;
        t->len = 0;
        return ZL_OK;
    }
    int ch = (unsigned char) s[a];
    if (zl_isalpha(ch)) {
        while (j < n && (zl_isalnum((unsigned char) s[j]) || s[j] == '_')) {
            if (s[j] == '_')
                return lexfail(w, j, ZL_E_UNSUPPORTED, "an underscore in a name (not ISO 7185)");
            j++;
        }
        if (j - a > P_NAMELEN) return lexfail(w, a, ZL_E_LIMIT, "name too long");
        t->kind = TK_ID;
        for (int k = 0; k < K__COUNT; k++)
            if (zl_wordeq(s + a, j - a, kwnames[k])) {
                t->kind = TK_KW;
                t->op = (int16_t) k;
            }
    } else if (zl_isdigit(ch)) {
        int64_t v = 0;
        while (j < n && zl_isdigit((unsigned char) s[j])) {
            if (v <= P_MAXINT) v = v * 10 + (s[j] - '0');
            j++;
        }
        if (j < n && ((s[j] == '.' && j + 1 < n && zl_isdigit((unsigned char) s[j + 1])) ||
                      zl_upper((unsigned char) s[j]) == 'E'))
            return lexfail(w, a, ZL_E_UNSUPPORTED, "real number (integer subset)");
        if (v > P_MAXINT)
            return lexfail(w, a, ZL_E_UNSUPPORTED, "integer literal larger than maxint");
        t->kind = TK_NUM;
        t->v = v;
    } else if (ch == '\'') {
        w->slen = 0;
        for (j = a + 1;; j++) {
            if (j >= n || s[j] == '\n') return lexfail(w, a, ZL_E_SYNTAX, "unterminated string");
            if (s[j] == '\'') {
                if (j + 1 < n && s[j + 1] == '\'')
                    j++;
                else
                    break;
            }
            if (w->slen >= P_STRLEN) return lexfail(w, a, ZL_E_LIMIT, "string too long");
            w->sbuf[w->slen++] = s[j];
        }
        j++;
        t->kind = TK_STR;
    } else {
        int two = j + 1 < n ? (unsigned char) s[j + 1] : 0;
        t->kind = TK_SYM;
        j++;
        if (ch == ':' && two == '=')
            t->op = SY_ASSIGN;
        else if (ch == '<' && two == '=')
            t->op = SY_LE;
        else if (ch == '<' && two == '>')
            t->op = SY_NE;
        else if (ch == '>' && two == '=')
            t->op = SY_GE;
        else if (ch == '.' && two == '.')
            t->op = SY_DOTDOT;
        else
            t->op = (int16_t) ch;
        if (t->op >= 256) j++;
        if (t->op < 256 &&
            !(ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '=' || ch == '<' ||
              ch == '>' || ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == ',' ||
              ch == ';' || ch == ':' || ch == '.' || ch == '^' || ch == '@'))
            return lexfail(w, a, ch == '_' ? ZL_E_UNSUPPORTED : ZL_E_SYNTAX,
                           ch == '"'   ? "strings are written in single quotes"
                           : ch == '_' ? "an underscore in a name (not ISO 7185)"
                                       : "unexpected character");
    }
    t->len = (uint32_t) (j - a);
    w->i = j;
    return ZL_OK;
}

static int is_sym(pw_t *w, int op)
{
    return w->t.kind == TK_SYM && w->t.op == op;
}

static int is_kw(pw_t *w, int k)
{
    return w->t.kind == TK_KW && w->t.op == k;
}

static int expect_sym(pw_t *w, int op, const char *msg)
{
    if (!is_sym(w, op)) return pfail(w, ZL_E_SYNTAX, msg);
    return lex(w);
}

static int expect_kw(pw_t *w, int k, const char *msg)
{
    if (!is_kw(w, k)) return pfail(w, ZL_E_SYNTAX, msg);
    return lex(w);
}

static int tok_is(pw_t *w, const char *word)
{
    return w->t.kind == TK_ID && zl_wordeq(w->src + w->t.pos, w->t.len, word);
}

/* A keyword that ISO 7185 has and this subset does not. */
static int refuse_kw(pw_t *w)
{
    switch (w->t.op) {
    case K_FILE:
        return pfail(w, ZL_E_UNSUPPORTED, "file types");
    case K_GOTO:
    case K_LABEL:
        return pfail(w, ZL_E_UNSUPPORTED, "labels and goto");
    case K_IN:
    case K_SET:
        return pfail(w, ZL_E_UNSUPPORTED, "set types and in");
    case K_NIL:
        return pfail(w, ZL_E_UNSUPPORTED, "pointers and nil");
    case K_PACKED:
        return pfail(w, ZL_E_UNSUPPORTED, "packed types");
    case K_RECORD:
        return pfail(w, ZL_E_UNSUPPORTED, "record types");
    case K_TYPE:
        return pfail(w, ZL_E_UNSUPPORTED, "type declarations");
    case K_WITH:
        return pfail(w, ZL_E_UNSUPPORTED, "with statements");
    default:
        return pfail(w, ZL_E_SYNTAX, "unexpected keyword");
    }
}

/* Names of predefined things outside the subset. */
static int refuse_name(pw_t *w)
{
    static const char *const real[] = {"REAL", "TRUNC", "ROUND", "SQRT",   "SIN",
                                       "COS",  "EXP",   "LN",    "ARCTAN", 0};
    static const char *const chars[] = {"CHAR", "CHR", "STRING", 0};
    static const char *const files[] = {"TEXT", "EOF",  "EOLN",  "RESET",  "REWRITE", "GET",
                                        "PUT",  "PAGE", "INPUT", "OUTPUT", 0};
    static const char *const heap[] = {"NEW", "DISPOSE", "PACK", "UNPACK", 0};
    for (int k = 0; real[k]; k++)
        if (tok_is(w, real[k])) return pfail(w, ZL_E_UNSUPPORTED, "real values (integer subset)");
    for (int k = 0; chars[k]; k++)
        if (tok_is(w, chars[k])) return pfail(w, ZL_E_UNSUPPORTED, "char and string values");
    for (int k = 0; files[k]; k++)
        if (tok_is(w, files[k])) return pfail(w, ZL_E_UNSUPPORTED, "files and file functions");
    for (int k = 0; heap[k]; k++)
        if (tok_is(w, heap[k])) return pfail(w, ZL_E_UNSUPPORTED, "new, dispose, pack, unpack");
    return pfail(w, ZL_E_UNDEFINED, "unknown name");
}

/* ------------------------------------------------------------ symbols */

static int name_eq(pw_t *w, uint32_t p1, uint32_t l1, uint32_t p2, uint32_t l2)
{
    if (l1 != l2) return 0;
    for (uint32_t k = 0; k < l1; k++)
        if (zl_upper((unsigned char) w->src[p1 + k]) != zl_upper((unsigned char) w->src[p2 + k]))
            return 0;
    return 1;
}

/* The innermost declaration of the current identifier, or -1. */
static int32_t lookup(pw_t *w)
{
    for (uint32_t k = w->nsym; k-- > 0;)
        if (name_eq(w, w->sym[k].pos, w->sym[k].len, w->t.pos, w->t.len)) return (int32_t) k;
    return -1;
}

/* Declare the current identifier in the open block; returns its index. */
static int32_t declare(pw_t *w, int kind, int type)
{
    for (uint32_t k = w->scope[w->level]; k < w->nsym; k++)
        if (name_eq(w, w->sym[k].pos, w->sym[k].len, w->t.pos, w->t.len)) {
            pfail(w, ZL_E_SYNTAX, "name declared twice in one block");
            return -1;
        }
    if (w->nsym >= P_SYMS) {
        pfail(w, ZL_E_LIMIT, "too many names");
        return -1;
    }
    psym_t *y = &w->sym[w->nsym];
    zl_bzero(y, sizeof *y);
    y->pos = w->t.pos;
    y->len = (uint16_t) w->t.len;
    y->kind = (uint8_t) kind;
    y->type = (uint8_t) type;
    y->level = (uint8_t) w->level;
    return (int32_t) w->nsym++;
}

/* A frame cell (procedures) or a slot (the program) for n values. */
static int32_t cells(pw_t *w, int32_t n)
{
    if (w->level == 0) return zl_slots(w->c, (uint32_t) n);
    if (n > P_FRAME - w->fsize[w->level]) {
        pfail(w, ZL_E_LIMIT, "procedure frame too large");
        return -1;
    }
    w->fsize[w->level] += n;
    return w->fsize[w->level] - n;
}

static int is_active_for(pw_t *w, int32_t sym)
{
    for (uint32_t k = 0; k < w->nfor; k++)
        if (w->fors[k] == sym) return 1;
    return 0;
}

/* ------------------------------------------------------------ constants */

/* constant = [sign] (unsigned-number | constant-identifier); a name that
 * is not a constant is refused with status nc. */
static int p_constant_as(pw_t *w, pconst_t *k, int nc, const char *msg)
{
    zl_bzero(k, sizeof *k);
    if (is_sym(w, '+') || is_sym(w, '-')) {
        k->neg = is_sym(w, '-');
        TRY(lex(w));
    }
    if (w->t.kind == TK_NUM) {
        k->v = w->t.v;
        k->type = TY_INT;
    } else if (w->t.kind == TK_ID) {
        int32_t y = lookup(w);
        k->named = 1;
        k->pos = w->t.pos;
        k->len = w->t.len;
        if (y >= 0) {
            if (w->sym[y].kind != Y_CONST) return pfail(w, nc, msg);
            k->v = w->sym[y].v;
            k->type = w->sym[y].type;
        } else if (tok_is(w, "MAXINT")) {
            k->v = P_MAXINT;
            k->type = TY_INT;
        } else if (tok_is(w, "TRUE") || tok_is(w, "FALSE")) {
            k->v = tok_is(w, "TRUE");
            k->type = TY_BOOL;
        } else {
            return refuse_name(w);
        }
    } else if (w->t.kind == TK_STR) {
        return pfail(w, ZL_E_UNSUPPORTED, "char and string constants");
    } else {
        return pfail(w, ZL_E_SYNTAX, "a constant is expected");
    }
    if (k->neg) {
        if (k->type != TY_INT) return pfail(w, ZL_E_SYNTAX, "a sign before a Boolean constant");
        k->v = -k->v;
    }
    return lex(w);
}

static int p_constant(pw_t *w, pconst_t *k)
{
    return p_constant_as(w, k, ZL_E_SYNTAX, "a constant is expected");
}

static void tw_const(pw_t *w, const pconst_t *k)
{
    if (k->named) {
        if (k->neg) tw_s(w, "-");
        tw_name(w, k->pos, k->len);
    } else {
        tw_i(w, k->v);
    }
}

/* ------------------------------------------------------------ expressions */

static int32_t newnode(pw_t *w, int kind, int type, int32_t l, int32_t r)
{
    if (w->npool >= P_POOL) {
        pfail(w, ZL_E_LIMIT, "expression too large");
        return -1;
    }
    uint32_t h = 0;
    if (l >= 0 && w->pool[l].h > h) h = w->pool[l].h;
    if (r >= 0 && w->pool[r].h > h) h = w->pool[r].h;
    if (h + 1 > ZL_MAX_DEPTH) {
        pfail(w, ZL_E_LIMIT, "expression nested too deeply");
        return -1;
    }
    pnode_t *n = &w->pool[w->npool];
    zl_bzero(n, sizeof *n);
    n->kind = (uint8_t) kind;
    n->type = (uint8_t) type;
    n->h = (uint8_t) (h + 1);
    n->l = l;
    n->r = r;
    n->next = -1;
    n->sym = -1;
    n->line = w->t.line;
    return (int32_t) w->npool++;
}

static int32_t p_expr(pw_t *w);

static int32_t typed(pw_t *w, int32_t id, int type, const char *msg)
{
    if (id < 0) return -1;
    if (w->pool[id].type != type) {
        pfail_at(w, w->pool[id].line, 0, ZL_E_SYNTAX, msg);
        return -1;
    }
    return id;
}

/* A variable reference: name or name[index]. sym is already looked up and
 * is a variable, a parameter or an array. */
static int32_t p_varref(pw_t *w, int32_t y)
{
    psym_t *s = &w->sym[y];
    uint32_t line = w->t.line;
    int32_t n, idx = -1;
    if (lex(w)) return -1;
    if (s->kind == Y_ARR) {
        if (!is_sym(w, '[')) {
            pfail(w, ZL_E_UNSUPPORTED, "whole-array values (index the array)");
            return -1;
        }
        if (lex(w)) return -1;
        idx = typed(w, p_expr(w), TY_INT, "an array index must be an integer");
        if (idx < 0) return -1;
        if (is_sym(w, ',')) {
            pfail(w, ZL_E_UNSUPPORTED, "multi-dimensional arrays");
            return -1;
        }
        if (expect_sym(w, ']', "']' expected")) return -1;
    } else if (is_sym(w, '[')) {
        pfail(w, ZL_E_SYNTAX, "only an array takes an index");
        return -1;
    }
    n = newnode(w, idx >= 0 ? N_ELEM : N_VAR, s->type, idx, -1);
    if (n < 0) return -1;
    w->pool[n].sym = y;
    w->pool[n].line = line;
    return n;
}

/* The actual parameters of a call to procedure or function y. */
static int32_t p_args(pw_t *w, int32_t y, uint32_t line)
{
    psym_t *s = &w->sym[y];
    int32_t first = -1, last = -1;
    uint32_t k = 0;
    if (!is_sym(w, '(')) {
        if (s->np) {
            pfail_at(w, line, 0, ZL_E_SYNTAX, "too few arguments");
            return -2;
        }
        return -1;
    }
    if (!s->np) {
        pfail(w, ZL_E_SYNTAX, "too many arguments");
        return -2;
    }
    if (lex(w)) return -2;
    for (;; k++) {
        int32_t a;
        if (k >= s->np) {
            pfail(w, ZL_E_SYNTAX, "too many arguments");
            return -2;
        }
        int pk = w->pkind[s->pfirst + k], pt = w->ptype[s->pfirst + k];
        if (pk) { /* var parameter: a variable of the same type */
            int32_t v = w->t.kind == TK_ID ? lookup(w) : -1;
            if (v < 0 || (w->sym[v].kind != Y_VAR && w->sym[v].kind != Y_VPARAM &&
                          w->sym[v].kind != Y_RPARAM && w->sym[v].kind != Y_ARR)) {
                pfail(w, ZL_E_SYNTAX, "a var parameter needs a variable");
                return -2;
            }
            if (is_active_for(w, v)) {
                pfail(w, ZL_E_SYNTAX, "a for loop's control variable passed as var");
                return -2;
            }
            a = p_varref(w, v);
            if (a < 0) return -2;
            w->pool[a].op = 1; /* passed by address */
        } else {
            a = p_expr(w);
            if (a < 0) return -2;
        }
        if (w->pool[a].type != pt) {
            pfail(w, ZL_E_SYNTAX, "argument of the wrong type");
            return -2;
        }
        if (last >= 0)
            w->pool[last].next = a;
        else
            first = a;
        last = a;
        if (!is_sym(w, ',')) break;
        if (lex(w)) return -2;
    }
    if (k + 1 < s->np) {
        pfail(w, ZL_E_SYNTAX, "too few arguments");
        return -2;
    }
    if (expect_sym(w, ')', "')' expected")) return -2;
    return first;
}

static int32_t p_factor(pw_t *w)
{
    ptok_t t = w->t;
    int32_t n;
    if (t.kind == TK_NUM) {
        if ((n = newnode(w, N_NUM, TY_INT, -1, -1)) < 0) return -1;
        w->pool[n].v = t.v;
        return lex(w) ? -1 : n;
    }
    if (t.kind == TK_STR) {
        pfail(w, ZL_E_UNSUPPORTED, "char and string values (only write takes strings)");
        return -1;
    }
    if (is_sym(w, '(')) {
        if (lex(w)) return -1;
        n = p_expr(w);
        if (n < 0 || expect_sym(w, ')', "')' expected")) return -1;
        return n;
    }
    if (is_kw(w, K_NOT)) {
        if (lex(w)) return -1;
        int32_t a = typed(w, p_factor(w), TY_BOOL, "not needs a Boolean");
        return a < 0 ? -1 : newnode(w, N_NOT, TY_BOOL, a, -1);
    }
    if (is_sym(w, '+') || is_sym(w, '-')) {
        pfail(w, ZL_E_SYNTAX, "a sign only starts an expression: put it in parentheses");
        return -1;
    }
    if (is_sym(w, '/')) {
        pfail(w, ZL_E_UNSUPPORTED, "real division '/' (use div)");
        return -1;
    }
    if (is_sym(w, '^') || is_sym(w, '@')) {
        pfail(w, ZL_E_UNSUPPORTED, "pointers");
        return -1;
    }
    if (t.kind == TK_KW) {
        refuse_kw(w);
        if (w->c->d->status == ZL_E_SYNTAX) w->c->d->msg = "an expression is expected";
        return -1;
    }
    if (t.kind != TK_ID) {
        pfail(w, ZL_E_SYNTAX, "an expression is expected");
        return -1;
    }
    int32_t y = lookup(w);
    if (y >= 0) {
        psym_t *s = &w->sym[y];
        switch (s->kind) {
        case Y_CONST:
            if ((n = newnode(w, N_CNAME, s->type, -1, -1)) < 0) return -1;
            w->pool[n].pos = t.pos;
            w->pool[n].len = t.len;
            w->pool[n].v = s->v;
            return lex(w) ? -1 : n;
        case Y_VAR:
        case Y_VPARAM:
        case Y_RPARAM:
        case Y_ARR:
            return p_varref(w, y);
        case Y_FUNC: {
            if (lex(w)) return -1;
            int32_t a = p_args(w, y, t.line);
            if (a < -1) return -1;
            if ((n = newnode(w, N_CALL, s->type, -1, -1)) < 0) return -1;
            w->pool[n].sym = y;
            w->pool[n].r = a; /* the arguments */
            w->pool[n].line = t.line;
            for (int32_t k = a; k >= 0; k = w->pool[k].next)
                if (w->pool[k].h + 1 > w->pool[n].h) w->pool[n].h = (uint8_t) (w->pool[k].h + 1);
            if (w->pool[n].h > ZL_MAX_DEPTH) {
                pfail(w, ZL_E_LIMIT, "expression nested too deeply");
                return -1;
            }
            return n;
        }
        default:
            pfail(w, ZL_E_SYNTAX, "a procedure has no value");
            return -1;
        }
    }
    if (tok_is(w, "TRUE") || tok_is(w, "FALSE") || tok_is(w, "MAXINT")) {
        int m = tok_is(w, "MAXINT");
        if ((n = newnode(w, N_CNAME, m ? TY_INT : TY_BOOL, -1, -1)) < 0) return -1;
        w->pool[n].pos = t.pos;
        w->pool[n].len = t.len;
        w->pool[n].v = m ? P_MAXINT : tok_is(w, "TRUE");
        return lex(w) ? -1 : n;
    }
    static const char *const fns[] = {"ABS", "SQR", "ODD", "SUCC", "PRED", "ORD"};
    for (int k = 0; k < 6; k++) {
        if (!tok_is(w, fns[k])) continue;
        if (lex(w)) return -1;
        if (!is_sym(w, '(')) {
            pfail(w, ZL_E_SYNTAX, "'(' expected after a predefined function");
            return -1;
        }
        if (lex(w)) return -1;
        int32_t a = p_expr(w);
        if (a < 0) return -1;
        int op = F_ABS + k, at = w->pool[a].type;
        if (op != F_ORD && at != TY_INT) {
            if (op == F_SUCC || op == F_PRED)
                pfail(w, ZL_E_UNSUPPORTED, "succ and pred of a Boolean (not in this subset)");
            else
                pfail(w, ZL_E_SYNTAX, "this function needs an integer");
            return -1;
        }
        if (expect_sym(w, ')', "')' expected")) return -1;
        if ((n = newnode(w, N_FN, op == F_ODD ? TY_BOOL : TY_INT, a, -1)) < 0) return -1;
        w->pool[n].op = (uint8_t) op;
        w->pool[n].line = t.line;
        return n;
    }
    refuse_name(w);
    return -1;
}

static int mulop(pw_t *w)
{
    if (is_sym(w, '*')) return O_MUL;
    if (is_kw(w, K_DIV)) return O_DIV;
    if (is_kw(w, K_MOD)) return O_MOD;
    if (is_kw(w, K_AND)) return O_AND;
    return 0;
}

static int addop(pw_t *w)
{
    if (is_sym(w, '+')) return O_ADD;
    if (is_sym(w, '-')) return O_SUB;
    if (is_kw(w, K_OR)) return O_OR;
    return 0;
}

static int relop(pw_t *w)
{
    if (is_sym(w, '=')) return O_EQ;
    if (is_sym(w, SY_NE)) return O_NE;
    if (is_sym(w, '<')) return O_LT;
    if (is_sym(w, SY_LE)) return O_LE;
    if (is_sym(w, '>')) return O_GT;
    if (is_sym(w, SY_GE)) return O_GE;
    return 0;
}

static int32_t binary(pw_t *w, int op, int32_t l, int32_t r, uint32_t line)
{
    if (l < 0 || r < 0) return -1;
    int lt = w->pool[l].type, rt = w->pool[r].type, type = TY_INT;
    if (op <= O_GE) {
        if (lt != rt) {
            pfail(w, ZL_E_SYNTAX, "a comparison of an integer with a Boolean");
            return -1;
        }
        type = TY_BOOL;
    } else if (op == O_AND || op == O_OR) {
        if (lt != TY_BOOL || rt != TY_BOOL) {
            pfail(w, ZL_E_SYNTAX, "and/or need Booleans");
            return -1;
        }
        type = TY_BOOL;
    } else if (lt != TY_INT || rt != TY_INT) {
        pfail(w, ZL_E_SYNTAX, "arithmetic needs integers");
        return -1;
    }
    int32_t n = newnode(w, N_BIN, type, l, r);
    if (n < 0) return -1;
    w->pool[n].op = (uint8_t) op;
    w->pool[n].line = line;
    return n;
}

static int32_t p_term(pw_t *w)
{
    int32_t l = p_factor(w);
    int op;
    while (l >= 0 && (op = mulop(w)) != 0) {
        uint32_t line = w->t.line;
        if (lex(w)) return -1;
        l = binary(w, op, l, p_factor(w), line);
    }
    if (l >= 0 && is_sym(w, '/')) {
        pfail(w, ZL_E_UNSUPPORTED, "real division '/' (use div)");
        return -1;
    }
    return l;
}

static int32_t p_simple(pw_t *w)
{
    int32_t l;
    int op;
    if (is_sym(w, '+') || is_sym(w, '-')) {
        int neg = is_sym(w, '-');
        uint32_t line = w->t.line;
        if (lex(w)) return -1;
        l = typed(w, p_term(w), TY_INT, "a sign needs an integer");
        if (l >= 0 && neg) {
            l = newnode(w, N_NEG, TY_INT, l, -1);
            if (l >= 0) w->pool[l].line = line;
        }
    } else {
        l = p_term(w);
    }
    while (l >= 0 && (op = addop(w)) != 0) {
        uint32_t line = w->t.line;
        if (lex(w)) return -1;
        l = binary(w, op, l, p_term(w), line);
    }
    return l;
}

static int32_t p_expr(pw_t *w)
{
    int32_t l;
    int op;
    if (++w->depth > ZL_MAX_DEPTH) {
        pfail(w, ZL_E_LIMIT, "expression nested too deeply");
        return -1;
    }
    l = p_simple(w);
    if (l >= 0 && (op = relop(w)) != 0) {
        uint32_t line = w->t.line;
        if (lex(w)) return -1;
        l = binary(w, op, l, p_simple(w), line);
        if (l >= 0 && relop(w)) {
            pfail(w, ZL_E_SYNTAX, "comparisons do not chain: use parentheses");
            return -1;
        }
    }
    if (l >= 0 && is_kw(w, K_IN)) {
        pfail(w, ZL_E_UNSUPPORTED, "set types and in");
        return -1;
    }
    w->depth--;
    return l;
}

/* ------------------------------------------------------------ printing */

static int prec(const pnode_t *n)
{
    if (n->kind == N_BIN) return n->op <= O_GE ? 1 : n->op <= O_OR ? 2 : 3;
    if (n->kind == N_NEG) return 2;
    return 4;
}

static void pexpr(pw_t *w, int32_t id, int need, int start)
{
    static const char *const opname[] = {"",    " = ", " <> ", " < ", " <= ",  " > ",   " >= ",
                                         " + ", " - ", " or ", " * ", " div ", " mod ", " and "};
    static const char *const fname[] = {"", "abs(", "sqr(", "odd(", "succ(", "pred(", "ord("};
    const pnode_t *n = &w->pool[id];
    int p = prec(n), paren = p < need || (n->kind == N_NEG && !start);
    if (paren) {
        tw_s(w, "(");
        start = 1;
    }
    switch (n->kind) {
    case N_NUM:
        tw_i(w, n->v);
        break;
    case N_CNAME:
        tw_name(w, n->pos, n->len);
        break;
    case N_VAR:
        tw_name(w, w->sym[n->sym].pos, w->sym[n->sym].len);
        break;
    case N_ELEM:
        tw_name(w, w->sym[n->sym].pos, w->sym[n->sym].len);
        tw_s(w, "[");
        pexpr(w, n->l, 0, 1);
        tw_s(w, "]");
        break;
    case N_CALL:
        tw_name(w, w->sym[n->sym].pos, w->sym[n->sym].len);
        for (int32_t a = n->r, first = 1; a >= 0; a = w->pool[a].next, first = 0) {
            tw_s(w, first ? "(" : ", ");
            pexpr(w, a, 0, 1);
            if (w->pool[a].next < 0) tw_s(w, ")");
        }
        break;
    case N_FN:
        tw_s(w, fname[n->op]);
        pexpr(w, n->l, 0, 1);
        tw_s(w, ")");
        break;
    case N_NEG:
        tw_s(w, "-");
        pexpr(w, n->l, 3, 0);
        break;
    case N_NOT:
        tw_s(w, "not ");
        pexpr(w, n->l, 4, 0);
        break;
    default:
        if (p == 1) {
            pexpr(w, n->l, 2, 1);
            tw_s(w, opname[n->op]);
            pexpr(w, n->r, 2, 1);
        } else {
            pexpr(w, n->l, p, p == 2 ? start : 0);
            tw_s(w, opname[n->op]);
            pexpr(w, n->r, p + 1, 0);
        }
        break;
    }
    if (paren) tw_s(w, ")");
}

static void tw_expr(pw_t *w, int32_t id)
{
    if (w->tw) pexpr(w, id, 0, 1);
}

/* ------------------------------------------------------------ lowering */

static int32_t hops(pw_t *w, int32_t y)
{
    return (int32_t) (w->level - w->sym[y].level);
}

static int emit(pw_t *w, int op, int64_t a, int32_t b)
{
    return zl_emit(w->c, op, a, b) < 0 ? status(w) : ZL_OK;
}

static int lower(pw_t *w, int32_t id);

/* index - lo, checked against the array's length */
static int lower_index(pw_t *w, const psym_t *s, int32_t idx, int check)
{
    TRY(lower(w, idx));
    if (s->lo) {
        TRY(emit(w, ZL_PUSH, s->lo, 0));
        TRY(emit(w, ZL_SUB, 0, 0));
    }
    if (check) TRY(emit(w, ZL_BOUND, s->n, 0));
    return ZL_OK;
}

/* Push the address of a variable (for LOADA / STOREA and var arguments). */
static int lower_addr(pw_t *w, const pnode_t *n)
{
    const psym_t *s = &w->sym[n->sym];
    int32_t h = hops(w, n->sym);
    if (n->kind == N_ELEM) {
        TRY(lower_index(w, s, n->l, 1));
        if (s->level == 0)
            TRY(emit(w, ZL_PUSH, s->addr, 0));
        else
            TRY(emit(w, ZL_ADDRF, s->addr, h));
        return emit(w, ZL_ADD, 0, 0);
    }
    if (s->kind == Y_RPARAM) return emit(w, ZL_LOADF, s->addr, h);
    if (s->level == 0) return emit(w, ZL_PUSH, s->addr, 0);
    return emit(w, ZL_ADDRF, s->addr, h);
}

static int lower(pw_t *w, int32_t id)
{
    const pnode_t *n = &w->pool[id];
    const psym_t *s = n->sym >= 0 ? &w->sym[n->sym] : 0;
    w->c->line = n->line;
    switch (n->kind) {
    case N_NUM:
    case N_CNAME:
        return emit(w, ZL_PUSH, n->v, 0);
    case N_VAR:
    case N_ELEM:
        if (n->op == 1) return lower_addr(w, n); /* a var argument */
        if (n->kind == N_ELEM && s->level == 0) {
            TRY(lower_index(w, s, n->l, 0));
            w->c->line = n->line;
            return emit(w, ZL_LOADX, s->addr, s->n);
        }
        if (n->kind == N_ELEM || s->kind == Y_RPARAM) {
            TRY(lower_addr(w, n));
            w->c->line = n->line;
            return emit(w, ZL_LOADA, 0, 0);
        }
        if (s->level == 0) return emit(w, ZL_LOAD, s->addr, 0);
        return emit(w, ZL_LOADF, s->addr, hops(w, n->sym));
    case N_CALL:
        TRY(emit(w, ZL_LINK, hops(w, n->sym), 0));
        for (int32_t a = n->r; a >= 0; a = w->pool[a].next) TRY(lower(w, a));
        w->c->line = n->line;
        return emit(w, ZL_CALL, s->addr, 0);
    case N_FN:
        TRY(lower(w, n->l));
        w->c->line = n->line;
        switch (n->op) {
        case F_ABS:
            return emit(w, ZL_ABS, 0, 0);
        case F_SQR:
            TRY(emit(w, ZL_DUP, 0, 0));
            return emit(w, ZL_MUL, 0, 0);
        case F_ODD:
            TRY(emit(w, ZL_PUSH, 2, 0));
            return emit(w, ZL_MODF, 0, 0);
        case F_SUCC:
        case F_PRED:
            TRY(emit(w, ZL_PUSH, 1, 0));
            return emit(w, n->op == F_SUCC ? ZL_ADD : ZL_SUB, 0, 0);
        default:
            return ZL_OK; /* ord: integers and Booleans are already numbers */
        }
    case N_NEG:
        TRY(lower(w, n->l));
        w->c->line = n->line;
        return emit(w, ZL_NEG, 0, 0);
    case N_NOT:
        TRY(lower(w, n->l));
        w->c->line = n->line;
        return emit(w, ZL_NOT, 0, 0);
    default:
        break;
    }
    TRY(lower(w, n->l));
    w->c->line = n->line;
    if (n->op == O_AND || n->op == O_OR) { /* short-circuit, as fpc */
        TRY(emit(w, ZL_DUP, 0, 0));
        int32_t j = zl_emit(w->c, n->op == O_AND ? ZL_JZ : ZL_JNZ, 0, 0);
        TRY(emit(w, ZL_DROP, 0, 0));
        TRY(lower(w, n->r));
        zl_patch(w->c, j, (int64_t) w->c->p->ncode);
        return status(w);
    }
    TRY(lower(w, n->r));
    w->c->line = n->line;
    switch (n->op) {
    case O_EQ:
        return emit(w, ZL_EQ, 0, 0);
    case O_NE:
        return emit(w, ZL_NE, 0, 0);
    case O_LT:
        return emit(w, ZL_LT, 0, 0);
    case O_LE:
        return emit(w, ZL_LE, 0, 0);
    case O_GT:
        return emit(w, ZL_GT, 0, 0);
    case O_GE:
        return emit(w, ZL_GE, 0, 0);
    case O_ADD:
        return emit(w, ZL_ADD, 0, 0);
    case O_SUB:
        return emit(w, ZL_SUB, 0, 0);
    case O_MUL:
        return emit(w, ZL_MUL, 0, 0);
    case O_DIV:
        return emit(w, ZL_DIVT, 0, 0);
    default: { /* ISO mod: an error for a divisor below 1, never negative */
        TRY(emit(w, ZL_DUP, 0, 0));
        TRY(emit(w, ZL_PUSH, 0, 0));
        TRY(emit(w, ZL_LT, 0, 0));
        int32_t j = zl_emit(w->c, ZL_JZ, 0, 0);
        TRY(emit(w, ZL_TRAP, ZL_E_DOMAIN, 0));
        zl_patch(w->c, j, (int64_t) w->c->p->ncode);
        return emit(w, ZL_MODF, 0, 0);
    }
    }
}

/* ------------------------------------------------------------ targets */

/* An assignment or read target: a variable, an element, or a function's
 * result (sym is the function). */
typedef struct {
    int32_t node, func;
    int type;
} ptarget_t;

static int p_target(pw_t *w, ptarget_t *t, int32_t y, int allow_func)
{
    psym_t *s = &w->sym[y];
    t->node = -1;
    t->func = -1;
    if (s->kind == Y_FUNC) {
        if (!allow_func || (uint32_t) s->level + 1 > w->level || w->cur[s->level + 1] != y)
            return pfail(w, ZL_E_SYNTAX, "a function's result is set only inside it");
        t->func = y;
        t->type = s->type;
        return lex(w);
    }
    if (s->kind == Y_CONST) return pfail(w, ZL_E_SYNTAX, "a constant cannot be assigned");
    if (s->kind == Y_PROC) return pfail(w, ZL_E_SYNTAX, "a procedure cannot be assigned");
    if (is_active_for(w, y))
        return pfail(w, ZL_E_SYNTAX, "a for loop's control variable is assigned in its loop");
    t->node = p_varref(w, y);
    if (t->node < 0) return status(w);
    t->type = w->pool[t->node].type;
    return ZL_OK;
}

static void tw_target(pw_t *w, const ptarget_t *t)
{
    if (t->func >= 0)
        tw_name(w, w->sym[t->func].pos, w->sym[t->func].len);
    else
        tw_expr(w, t->node);
}

/* Code before the value: the index or address part of the store. */
static int store_pre(pw_t *w, const ptarget_t *t)
{
    if (t->func >= 0) return ZL_OK;
    const pnode_t *n = &w->pool[t->node];
    const psym_t *s = &w->sym[n->sym];
    if (n->kind != N_ELEM) return ZL_OK;
    if (s->level == 0) return lower_index(w, s, n->l, 0);
    return lower_addr(w, n);
}

static int store_post(pw_t *w, const ptarget_t *t, uint32_t line)
{
    w->c->line = line;
    if (t->func >= 0) return emit(w, ZL_STOREF, w->sym[t->func].n, hops(w, t->func) - 1);
    const pnode_t *n = &w->pool[t->node];
    const psym_t *s = &w->sym[n->sym];
    if (n->kind == N_ELEM) {
        if (s->level == 0) return emit(w, ZL_STOREX, s->addr, s->n);
        TRY(emit(w, ZL_SWAP, 0, 0));
        return emit(w, ZL_STOREA, 0, 0);
    }
    if (s->kind == Y_RPARAM) {
        TRY(emit(w, ZL_LOADF, s->addr, hops(w, n->sym)));
        return emit(w, ZL_STOREA, 0, 0);
    }
    if (s->level == 0) return emit(w, ZL_STORE, s->addr, 0);
    return emit(w, ZL_STOREF, s->addr, hops(w, n->sym));
}

/* ------------------------------------------------------------ statements */

static int p_statement(pw_t *w, uint32_t L, int ctx);

static int empty_start(pw_t *w)
{
    return is_sym(w, ';') || is_kw(w, K_END) || is_kw(w, K_UNTIL) || is_kw(w, K_ELSE) ||
           w->t.kind == TK_EOF;
}

/* statement { ';' statement } up to the token that ends the sequence;
 * empty statements are dropped from the twin. */
static int p_sequence(pw_t *w, uint32_t L)
{
    int first = 1;
    for (;;) {
        if (!empty_start(w)) {
            tw_s(w, first ? "\n" : ";\n");
            first = 0;
            TRY(p_statement(w, L, CTX_SEQ));
        }
        if (!is_sym(w, ';')) break;
        TRY(lex(w));
    }
    return ZL_OK;
}

/* begin ... end; the caller has started the line */
static int p_compound(pw_t *w, uint32_t L)
{
    tw_s(w, "begin");
    TRY(expect_kw(w, K_BEGIN, "begin expected"));
    TRY(p_sequence(w, L + 1));
    tw_s(w, "\n");
    bol(w, L);
    tw_s(w, "end");
    return expect_kw(w, K_END, "end expected (or a ';' is missing)");
}

static int p_assign(pw_t *w, int32_t y)
{
    ptarget_t t;
    uint32_t line = w->t.line;
    TRY(p_target(w, &t, y, 1));
    if (is_sym(w, '=')) return pfail(w, ZL_E_SYNTAX, "':=' expected (not '=')");
    TRY(expect_sym(w, SY_ASSIGN, "':=' expected"));
    int32_t e = p_expr(w);
    if (e < 0) return status(w);
    if (w->pool[e].type != t.type)
        return pfail_at(w, line, 0, ZL_E_SYNTAX, "assignment of the wrong type");
    tw_target(w, &t);
    tw_s(w, " := ");
    tw_expr(w, e);
    w->c->line = line;
    TRY(store_pre(w, &t));
    TRY(lower(w, e));
    return store_post(w, &t, line);
}

static int p_call(pw_t *w, int32_t y)
{
    uint32_t line = w->t.line;
    TRY(lex(w));
    int32_t a = p_args(w, y, line);
    if (a < -1) return status(w);
    tw_name(w, w->sym[y].pos, w->sym[y].len);
    for (int32_t k = a, first = 1; k >= 0; k = w->pool[k].next, first = 0) {
        tw_s(w, first ? "(" : ", ");
        tw_expr(w, k);
        if (w->pool[k].next < 0) tw_s(w, ")");
    }
    w->c->line = line;
    TRY(emit(w, ZL_LINK, hops(w, y), 0));
    for (int32_t k = a; k >= 0; k = w->pool[k].next) TRY(lower(w, k));
    w->c->line = line;
    return emit(w, ZL_CALL, w->sym[y].addr, 0);
}

/* A field width: a constant from 0 to 64. */
static int p_width(pw_t *w, pconst_t *k)
{
    int32_t y = w->t.kind == TK_ID ? lookup(w) : -1;
    if ((w->t.kind != TK_NUM && w->t.kind != TK_ID && !is_sym(w, '-') && !is_sym(w, '+')) ||
        (y >= 0 && w->sym[y].kind != Y_CONST))
        return pfail(w, ZL_E_UNSUPPORTED, "a field width that is not a constant");
    TRY(p_constant_as(w, k, ZL_E_UNSUPPORTED, "a field width that is not a constant"));
    if (!is_sym(w, ',') && !is_sym(w, ')') && !is_sym(w, ':'))
        return pfail(w, ZL_E_UNSUPPORTED, "a field width that is not a constant");
    if (k->type != TY_INT) return pfail(w, ZL_E_SYNTAX, "a field width is an integer");
    if (k->v < 0) return pfail(w, ZL_E_SYNTAX, "a negative field width");
    if (k->v > 64) return pfail(w, ZL_E_UNSUPPORTED, "a field width over 64");
    if (is_sym(w, ':')) return pfail(w, ZL_E_UNSUPPORTED, "a second width (real formatting)");
    return ZL_OK;
}

/* text padded on the left to width, or cut to it */
static int32_t fitted(pw_t *w, const char *s, uint32_t n, int64_t width)
{
    char b[P_STRLEN + 64 + 1];
    uint32_t k = 0;
    if (width >= 0 && (uint32_t) width < n) n = (uint32_t) width;
    while (width >= 0 && k + n < (uint32_t) width) b[k++] = ' ';
    for (uint32_t j = 0; j < n; j++) b[k++] = s[j];
    return zl_addstr(w->c, b, k);
}

static int p_write(pw_t *w, int ln)
{
    uint32_t line = w->t.line;
    tw_s(w, ln ? "writeln" : "write");
    TRY(lex(w));
    if (!is_sym(w, '(')) {
        if (!ln) return pfail_at(w, line, 0, ZL_E_SYNTAX, "write needs at least one item");
        w->c->line = line;
        return emit(w, ZL_NL, 0, 0);
    }
    TRY(lex(w));
    for (int first = 1;; first = 0) {
        tw_s(w, first ? "(" : ", ");
        w->npool = 0;
        if (w->t.kind == TK_STR) {
            char str[P_STRLEN];
            uint32_t n = w->slen, sline = w->t.line, spos = w->t.pos, slen = w->t.len;
            pconst_t k;
            int64_t width = -1;
            zl_bcopy(str, w->sbuf, n);
            if (w->tw) zl_put(w->tw, w->src + spos, slen);
            TRY(lex(w));
            if (is_sym(w, ':')) {
                TRY(lex(w));
                TRY(p_width(w, &k));
                tw_s(w, ":");
                tw_const(w, &k);
                width = k.v;
            }
            int32_t si = fitted(w, str, n, width);
            if (si < 0) return status(w);
            w->c->line = sline;
            TRY(emit(w, ZL_OUTS, si, 0));
        } else {
            if (first && w->t.kind == TK_ID && lookup(w) < 0 &&
                (tok_is(w, "OUTPUT") || tok_is(w, "INPUT")))
                return pfail(w, ZL_E_UNSUPPORTED, "a file argument to write");
            int32_t e = p_expr(w);
            pconst_t k;
            int has_w = 0;
            if (e < 0) return status(w);
            tw_expr(w, e);
            if (is_sym(w, ':')) {
                TRY(lex(w));
                TRY(p_width(w, &k));
                tw_s(w, ":");
                tw_const(w, &k);
                has_w = 1;
            }
            TRY(lower(w, e));
            w->c->line = w->pool[e].line;
            if (w->pool[e].type == TY_INT) {
                TRY(emit(w, ZL_OUTI, 0, has_w ? (int32_t) k.v : P_DEFWIDTH));
            } else {
                int64_t width = has_w ? k.v : 5;
                int32_t st = fitted(w, "true", 4, width), sf = fitted(w, "false", 5, width);
                if (st < 0 || sf < 0) return status(w);
                int32_t jf = zl_emit(w->c, ZL_JZ, 0, 0);
                TRY(emit(w, ZL_OUTS, st, 0));
                int32_t je = zl_emit(w->c, ZL_JMP, 0, 0);
                zl_patch(w->c, jf, (int64_t) w->c->p->ncode);
                TRY(emit(w, ZL_OUTS, sf, 0));
                zl_patch(w->c, je, (int64_t) w->c->p->ncode);
            }
        }
        if (!is_sym(w, ',')) break;
        TRY(lex(w));
    }
    tw_s(w, ")");
    TRY(expect_sym(w, ')', "')' expected"));
    if (ln) {
        w->c->line = line;
        TRY(emit(w, ZL_NL, 0, 0));
    }
    return ZL_OK;
}

static int p_read(pw_t *w, int ln)
{
    uint32_t rline = w->t.line;
    tw_s(w, ln ? "readln" : "read");
    TRY(lex(w));
    if (!is_sym(w, '(')) {
        if (!ln) return pfail_at(w, rline, 0, ZL_E_SYNTAX, "read needs at least one variable");
        return ZL_OK; /* readln alone: the input has no lines to skip */
    }
    TRY(lex(w));
    for (int first = 1;; first = 0) {
        ptarget_t t;
        uint32_t line = w->t.line;
        w->npool = 0;
        tw_s(w, first ? "(" : ", ");
        if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a variable is expected");
        int32_t y = lookup(w);
        if (y < 0) {
            if (first && (tok_is(w, "INPUT") || tok_is(w, "OUTPUT")))
                return pfail(w, ZL_E_UNSUPPORTED, "a file argument to read");
            return refuse_name(w);
        }
        TRY(p_target(w, &t, y, 0));
        if (t.type != TY_INT) return pfail(w, ZL_E_SYNTAX, "read takes integer variables");
        tw_target(w, &t);
        TRY(store_pre(w, &t));
        w->c->line = line;
        TRY(emit(w, ZL_IN, 0, 0));
        TRY(store_post(w, &t, line));
        if (!is_sym(w, ',')) break;
        TRY(lex(w));
    }
    tw_s(w, ")");
    return expect_sym(w, ')', "')' expected");
}

static int32_t p_cond(pw_t *w, const char *msg)
{
    return typed(w, p_expr(w), TY_BOOL, msg);
}

static int p_if(pw_t *w, uint32_t L)
{
    uint32_t line = w->t.line;
    tw_s(w, "if ");
    TRY(lex(w));
    int32_t e = p_cond(w, "if needs a Boolean condition");
    if (e < 0) return status(w);
    tw_expr(w, e);
    tw_s(w, " then\n");
    TRY(lower(w, e));
    w->c->line = line;
    int32_t jf = zl_emit(w->c, ZL_JZ, 0, 0);
    TRY(expect_kw(w, K_THEN, "then expected"));
    TRY(p_statement(w, L + 1, CTX_BRANCH));
    if (!is_kw(w, K_ELSE)) {
        zl_patch(w->c, jf, (int64_t) w->c->p->ncode);
        return status(w);
    }
    w->c->line = w->t.line;
    int32_t je = zl_emit(w->c, ZL_JMP, 0, 0);
    zl_patch(w->c, jf, (int64_t) w->c->p->ncode);
    tw_s(w, "\n");
    bol(w, L);
    tw_s(w, "else");
    TRY(lex(w));
    if (is_kw(w, K_IF)) {
        tw_s(w, " ");
        TRY(p_statement(w, L, CTX_INLINE));
    } else {
        tw_s(w, "\n");
        TRY(p_statement(w, L + 1, CTX_BRANCH));
    }
    zl_patch(w->c, je, (int64_t) w->c->p->ncode);
    return status(w);
}

static int p_while(pw_t *w, uint32_t L)
{
    uint32_t line = w->t.line;
    tw_s(w, "while ");
    TRY(lex(w));
    int32_t e = p_cond(w, "while needs a Boolean condition");
    if (e < 0) return status(w);
    tw_expr(w, e);
    tw_s(w, " do\n");
    int64_t top = w->c->p->ncode;
    TRY(lower(w, e));
    w->c->line = line;
    int32_t jx = zl_emit(w->c, ZL_JZ, 0, 0);
    TRY(expect_kw(w, K_DO, "do expected"));
    TRY(p_statement(w, L + 1, CTX_BRANCH));
    w->c->line = line;
    TRY(emit(w, ZL_JMP, top, 0));
    zl_patch(w->c, jx, (int64_t) w->c->p->ncode);
    return status(w);
}

static int p_repeat(pw_t *w, uint32_t L)
{
    int64_t top = w->c->p->ncode;
    tw_s(w, "repeat");
    TRY(lex(w));
    TRY(p_sequence(w, L + 1));
    tw_s(w, "\n");
    bol(w, L);
    tw_s(w, "until ");
    uint32_t line = w->t.line;
    TRY(expect_kw(w, K_UNTIL, "until expected (or a ';' is missing)"));
    w->npool = 0;
    int32_t e = p_cond(w, "until needs a Boolean condition");
    if (e < 0) return status(w);
    tw_expr(w, e);
    TRY(lower(w, e));
    w->c->line = line;
    return emit(w, ZL_JZ, top, 0);
}

static int p_for(pw_t *w, uint32_t L)
{
    uint32_t line = w->t.line;
    tw_s(w, "for ");
    TRY(lex(w));
    if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a control variable is expected");
    int32_t y = lookup(w);
    if (y < 0) return refuse_name(w);
    psym_t *s = &w->sym[y];
    if (s->kind != Y_VAR || s->level != w->level)
        return pfail(w, ZL_E_SYNTAX, "the control variable must be a variable of this block");
    if (s->type != TY_INT)
        return pfail(w, ZL_E_UNSUPPORTED, "a Boolean control variable (integer subset)");
    if (is_active_for(w, y)) return pfail(w, ZL_E_SYNTAX, "the control variable of an open loop");
    TRY(lex(w));
    TRY(expect_sym(w, SY_ASSIGN, "':=' expected"));
    int32_t e1 = typed(w, p_expr(w), TY_INT, "a for loop runs over integers");
    if (e1 < 0) return status(w);
    int down = is_kw(w, K_DOWNTO);
    if (!down && !is_kw(w, K_TO)) return pfail(w, ZL_E_SYNTAX, "to or downto expected");
    TRY(lex(w));
    int32_t e2 = typed(w, p_expr(w), TY_INT, "a for loop runs over integers");
    if (e2 < 0) return status(w);
    tw_name(w, s->pos, s->len);
    tw_s(w, " := ");
    tw_expr(w, e1);
    tw_s(w, down ? " downto " : " to ");
    tw_expr(w, e2);
    tw_s(w, " do\n");
    TRY(expect_kw(w, K_DO, "do expected"));
    int32_t lim = cells(w, 1);
    if (lim < 0) return status(w);
    int g = w->level == 0;
    /* v := e1; lim := e2; if v <= lim: loop body; exit when v = lim, else step.
     * The variable never steps past the limit, so maxint is a valid limit. */
    TRY(lower(w, e1));
    TRY(lower(w, e2));
    w->c->line = line;
    TRY(emit(w, g ? ZL_STORE : ZL_STOREF, lim, 0));
    TRY(emit(w, g ? ZL_STORE : ZL_STOREF, s->addr, 0));
    TRY(emit(w, g ? ZL_LOAD : ZL_LOADF, s->addr, 0));
    TRY(emit(w, g ? ZL_LOAD : ZL_LOADF, lim, 0));
    TRY(emit(w, down ? ZL_LT : ZL_GT, 0, 0));
    int32_t jx = zl_emit(w->c, ZL_JNZ, 0, 0);
    int64_t top = w->c->p->ncode;
    w->fors[w->nfor++] = y;
    TRY(p_statement(w, L + 1, CTX_BRANCH));
    w->nfor--;
    w->c->line = line;
    TRY(emit(w, g ? ZL_LOAD : ZL_LOADF, s->addr, 0));
    TRY(emit(w, g ? ZL_LOAD : ZL_LOADF, lim, 0));
    TRY(emit(w, ZL_EQ, 0, 0));
    int32_t jd = zl_emit(w->c, ZL_JNZ, 0, 0);
    TRY(emit(w, g ? ZL_LOAD : ZL_LOADF, s->addr, 0));
    TRY(emit(w, ZL_PUSH, 1, 0));
    TRY(emit(w, down ? ZL_SUB : ZL_ADD, 0, 0));
    TRY(emit(w, g ? ZL_STORE : ZL_STOREF, s->addr, 0));
    TRY(emit(w, ZL_JMP, top, 0));
    zl_patch(w->c, jx, (int64_t) w->c->p->ncode);
    zl_patch(w->c, jd, (int64_t) w->c->p->ncode);
    return status(w);
}

static int p_case(pw_t *w, uint32_t L)
{
    uint32_t line = w->t.line, lab0 = w->nlab, arm0 = w->narm;
    tw_s(w, "case ");
    TRY(lex(w));
    int32_t e = p_expr(w);
    if (e < 0) return status(w);
    if (w->pool[e].type != TY_INT)
        return pfail(w, ZL_E_UNSUPPORTED, "a Boolean case selector (integer labels only)");
    tw_expr(w, e);
    tw_s(w, " of");
    TRY(lower(w, e));
    w->c->line = line;
    int32_t jd = zl_emit(w->c, ZL_JMP, 0, 0);
    TRY(expect_kw(w, K_OF, "of expected"));
    for (int first = 1;; first = 0) {
        if (is_kw(w, K_ELSE)) return pfail(w, ZL_E_UNSUPPORTED, "case ... else (not ISO 7185)");
        tw_s(w, first ? "\n" : ";\n");
        bol(w, L + 1);
        int64_t arm = w->c->p->ncode;
        for (int lfirst = 1;; lfirst = 0) {
            pconst_t k;
            TRY(p_constant(w, &k));
            if (k.type != TY_INT) return pfail(w, ZL_E_SYNTAX, "case labels are integers");
            for (uint32_t j = lab0; j < w->nlab; j++)
                if (w->lab[j].v == k.v) return pfail(w, ZL_E_SYNTAX, "a case label used twice");
            if (w->nlab >= P_LABELS) return pfail(w, ZL_E_LIMIT, "too many case labels");
            w->lab[w->nlab].v = k.v;
            w->lab[w->nlab].addr = (int32_t) arm;
            w->nlab++;
            if (!lfirst) tw_s(w, ", ");
            tw_const(w, &k);
            if (!is_sym(w, ',')) break;
            TRY(lex(w));
        }
        tw_s(w, ":\n");
        TRY(expect_sym(w, ':', "':' expected after case labels"));
        w->c->line = line;
        TRY(emit(w, ZL_DROP, 0, 0));
        TRY(p_statement(w, L + 2, CTX_BRANCH));
        if (w->narm >= P_LABELS) return pfail(w, ZL_E_LIMIT, "too many case arms");
        w->c->line = line;
        w->armjmp[w->narm++] = zl_emit(w->c, ZL_JMP, 0, 0);
        if (is_sym(w, ';')) {
            TRY(lex(w));
            if (is_kw(w, K_END)) break;
        } else if (is_kw(w, K_END)) {
            break;
        } else {
            return pfail(w, ZL_E_SYNTAX, "';' or end expected after a case arm");
        }
    }
    tw_s(w, "\n");
    bol(w, L);
    tw_s(w, "end");
    TRY(lex(w));
    /* dispatch: compare the selector with each label in turn */
    zl_patch(w->c, jd, (int64_t) w->c->p->ncode);
    w->c->line = line;
    for (uint32_t j = lab0; j < w->nlab; j++) {
        TRY(emit(w, ZL_DUP, 0, 0));
        TRY(emit(w, ZL_PUSH, w->lab[j].v, 0));
        TRY(emit(w, ZL_EQ, 0, 0));
        TRY(emit(w, ZL_JNZ, w->lab[j].addr, 0));
    }
    TRY(emit(w, ZL_TRAP, ZL_E_DOMAIN, 0)); /* no label matched: an error in ISO 7185 */
    for (uint32_t j = arm0; j < w->narm; j++)
        zl_patch(w->c, w->armjmp[j], (int64_t) w->c->p->ncode);
    w->nlab = lab0;
    w->narm = arm0;
    return status(w);
}

static int p_statement(pw_t *w, uint32_t L, int ctx)
{
    int s;
    if (++w->depth > ZL_MAX_DEPTH) return pfail(w, ZL_E_LIMIT, "statements nested too deeply");
    w->npool = 0;
    w->c->line = w->t.line;
    if (empty_start(w)) { /* an empty branch is written begin end */
        bol(w, L);
        tw_s(w, "begin\n");
        bol(w, L);
        tw_s(w, "end");
        w->depth--;
        return ZL_OK;
    }
    if (ctx != CTX_INLINE) bol(w, L);
    if (w->t.kind == TK_NUM)
        s = pfail(w, ZL_E_UNSUPPORTED, "labels and goto");
    else if (w->t.kind == TK_KW) {
        switch (w->t.op) {
        case K_BEGIN:
            s = p_compound(w, L);
            break;
        case K_IF:
            s = p_if(w, L);
            break;
        case K_WHILE:
            s = p_while(w, L);
            break;
        case K_REPEAT:
            s = p_repeat(w, L);
            break;
        case K_FOR:
            s = p_for(w, L);
            break;
        case K_CASE:
            s = p_case(w, L);
            break;
        default:
            s = refuse_kw(w);
            if (s == ZL_E_SYNTAX) w->c->d->msg = "a statement is expected";
            break;
        }
    } else if (w->t.kind == TK_ID) {
        int32_t y = lookup(w);
        if (y >= 0) {
            int k = w->sym[y].kind;
            if (k == Y_PROC)
                s = p_call(w, y);
            else if (k == Y_FUNC &&
                     !(w->level > w->sym[y].level && w->cur[w->sym[y].level + 1] == y)) {
                s = pfail(w, ZL_E_SYNTAX, "a function call is not a statement");
            } else
                s = p_assign(w, y);
        } else if (tok_is(w, "WRITE") || tok_is(w, "WRITELN")) {
            s = p_write(w, tok_is(w, "WRITELN"));
        } else if (tok_is(w, "READ") || tok_is(w, "READLN")) {
            s = p_read(w, tok_is(w, "READLN"));
        } else {
            s = refuse_name(w);
        }
    } else {
        s = pfail(w, ZL_E_SYNTAX, "a statement is expected");
    }
    w->depth--;
    return s ? s : status(w);
}

/* ------------------------------------------------------------ declarations */

static int p_typename(pw_t *w, int *type)
{
    if (w->t.kind == TK_ID && lookup(w) < 0) {
        if (tok_is(w, "INTEGER") || tok_is(w, "BOOLEAN")) {
            *type = tok_is(w, "INTEGER") ? TY_INT : TY_BOOL;
            tw_s(w, *type == TY_INT ? "integer" : "boolean");
            return lex(w);
        }
        if (tok_is(w, "REAL")) return pfail(w, ZL_E_UNSUPPORTED, "real values (integer subset)");
        if (tok_is(w, "CHAR") || tok_is(w, "STRING"))
            return pfail(w, ZL_E_UNSUPPORTED, "char and string values");
        if (tok_is(w, "TEXT")) return pfail(w, ZL_E_UNSUPPORTED, "files and file functions");
    }
    if (w->t.kind == TK_KW) {
        if (is_kw(w, K_ARRAY))
            return pfail(w, ZL_E_UNSUPPORTED, "array parameters and arrays of arrays");
        if (is_kw(w, K_PROCEDURE) || is_kw(w, K_FUNCTION))
            return pfail(w, ZL_E_UNSUPPORTED, "procedural parameters");
        return refuse_kw(w);
    }
    if (is_sym(w, '^')) return pfail(w, ZL_E_UNSUPPORTED, "pointers");
    if (is_sym(w, '(')) return pfail(w, ZL_E_UNSUPPORTED, "enumerated types");
    if (w->t.kind == TK_NUM || is_sym(w, '-') || is_sym(w, '+'))
        return pfail(w, ZL_E_UNSUPPORTED, "subrange types");
    if (w->t.kind == TK_ID && lookup(w) >= 0 && w->sym[lookup(w)].kind == Y_CONST)
        return pfail(w, ZL_E_UNSUPPORTED, "subrange types");
    if (w->t.kind == TK_ID)
        return pfail(w, ZL_E_UNSUPPORTED, "types other than integer and Boolean");
    return pfail(w, ZL_E_SYNTAX, "a type is expected");
}

/* name {, name} ':' -- the names are left for the caller to declare */
static int p_names(pw_t *w, ptok_t *names, uint32_t *n)
{
    *n = 0;
    for (;;) {
        if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a name is expected");
        if (*n >= P_GROUP) return pfail(w, ZL_E_LIMIT, "too many names in one group");
        for (uint32_t k = 0; k < *n; k++)
            if (name_eq(w, names[k].pos, names[k].len, w->t.pos, w->t.len))
                return pfail(w, ZL_E_SYNTAX, "name declared twice in one block");
        names[(*n)++] = w->t;
        if (*n > 1) tw_s(w, ", ");
        tw_name(w, w->t.pos, w->t.len);
        TRY(lex(w));
        if (!is_sym(w, ',')) break;
        TRY(lex(w));
    }
    tw_s(w, ": ");
    return expect_sym(w, ':', "':' expected");
}

static int declare_names(pw_t *w, const ptok_t *names, uint32_t n, int kind, int type, int32_t lo,
                         int32_t len)
{
    ptok_t save = w->t;
    for (uint32_t k = 0; k < n; k++) {
        w->t = names[k];
        int32_t y = declare(w, kind, type);
        if (y < 0) {
            w->t = save;
            return status(w);
        }
        if (kind == Y_ARR) {
            w->sym[y].lo = lo;
            w->sym[y].n = len;
        }
        w->sym[y].addr =
            kind == Y_VPARAM || kind == Y_RPARAM ? -1 : cells(w, kind == Y_ARR ? len : 1);
        if (w->sym[y].addr < 0 && kind != Y_VPARAM && kind != Y_RPARAM) {
            w->t = save;
            return status(w);
        }
    }
    w->t = save;
    return ZL_OK;
}

static int p_const_part(pw_t *w, uint32_t B)
{
    bol(w, B);
    tw_s(w, "const\n");
    TRY(lex(w));
    do {
        pconst_t k;
        if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a constant's name is expected");
        ptok_t name = w->t;
        bol(w, B + 1);
        tw_name(w, name.pos, name.len);
        tw_s(w, " = ");
        TRY(lex(w));
        TRY(expect_sym(w, '=', "'=' expected"));
        TRY(p_constant(w, &k));
        tw_const(w, &k);
        tw_s(w, ";\n");
        ptok_t save = w->t;
        w->t = name;
        int32_t y = declare(w, Y_CONST, k.type);
        w->t = save;
        if (y < 0) return status(w);
        w->sym[y].v = k.v;
        TRY(expect_sym(w, ';', "';' expected"));
    } while (w->t.kind == TK_ID);
    return ZL_OK;
}

static int p_var_part(pw_t *w, uint32_t B)
{
    bol(w, B);
    tw_s(w, "var\n");
    TRY(lex(w));
    do {
        ptok_t names[P_GROUP];
        uint32_t n;
        bol(w, B + 1);
        TRY(p_names(w, names, &n));
        if (is_kw(w, K_ARRAY)) {
            pconst_t lo, hi;
            int type;
            TRY(lex(w));
            tw_s(w, "array[");
            TRY(expect_sym(w, '[', "'[' expected"));
            if (w->t.kind == TK_ID && lookup(w) < 0 &&
                (tok_is(w, "INTEGER") || tok_is(w, "BOOLEAN") || tok_is(w, "CHAR")))
                return pfail(w, ZL_E_UNSUPPORTED, "an index type other than a subrange");
            TRY(p_constant(w, &lo));
            tw_const(w, &lo);
            tw_s(w, "..");
            TRY(expect_sym(w, SY_DOTDOT, "'..' expected"));
            TRY(p_constant(w, &hi));
            tw_const(w, &hi);
            if (lo.type != TY_INT || hi.type != TY_INT)
                return pfail(w, ZL_E_UNSUPPORTED, "an index type other than integers");
            if (is_sym(w, ',')) return pfail(w, ZL_E_UNSUPPORTED, "multi-dimensional arrays");
            tw_s(w, "] of ");
            TRY(expect_sym(w, ']', "']' expected"));
            TRY(expect_kw(w, K_OF, "of expected"));
            TRY(p_typename(w, &type));
            if (hi.v < lo.v)
                return pfail(w, ZL_E_SYNTAX, "an array's upper bound is below its lower");
            if (hi.v - lo.v + 1 > (w->level ? P_FRAME : ZL_MAX_SLOTS))
                return pfail(w, ZL_E_LIMIT, "array too large");
            TRY(declare_names(w, names, n, Y_ARR, type, (int32_t) lo.v,
                              (int32_t) (hi.v - lo.v + 1)));
        } else {
            int type;
            TRY(p_typename(w, &type));
            TRY(declare_names(w, names, n, Y_VAR, type, 0, 0));
        }
        tw_s(w, ";\n");
        TRY(expect_sym(w, ';', "';' expected"));
    } while (w->t.kind == TK_ID);
    return ZL_OK;
}

static int p_block(pw_t *w);

static int p_proc(pw_t *w, int func)
{
    uint32_t B = w->level, line = w->t.line;
    bol(w, B);
    tw_s(w, func ? "function " : "procedure ");
    TRY(lex(w));
    if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a name is expected");
    int32_t y = declare(w, func ? Y_FUNC : Y_PROC, 0);
    if (y < 0) return status(w);
    tw_name(w, w->t.pos, w->t.len);
    TRY(lex(w));
    if (w->level >= P_NEST) return pfail(w, ZL_E_LIMIT, "procedures nested too deeply");
    w->level++;
    w->scope[w->level] = w->nsym;
    w->fsize[w->level] = 0;
    w->cur[w->level] = y;
    w->sym[y].pfirst = (uint16_t) w->nparam;
    if (is_sym(w, '(')) {
        TRY(lex(w));
        tw_s(w, "(");
        for (int first = 1;; first = 0) {
            ptok_t names[P_GROUP];
            uint32_t n;
            int type, var = is_kw(w, K_VAR);
            if (!first) tw_s(w, "; ");
            if (is_kw(w, K_PROCEDURE) || is_kw(w, K_FUNCTION))
                return pfail(w, ZL_E_UNSUPPORTED, "procedural parameters");
            if (var) {
                tw_s(w, "var ");
                TRY(lex(w));
            }
            TRY(p_names(w, names, &n));
            TRY(p_typename(w, &type));
            if (w->sym[y].np + n > P_MAXPARAM || w->nparam + n > P_PARAMS)
                return pfail(w, ZL_E_LIMIT, "too many parameters");
            TRY(declare_names(w, names, n, var ? Y_RPARAM : Y_VPARAM, type, 0, 0));
            for (uint32_t k = 0; k < n; k++) {
                psym_t *p = &w->sym[w->nsym - n + k];
                p->addr = w->fsize[w->level]++;
                w->pkind[w->nparam] = (uint8_t) var;
                w->ptype[w->nparam] = (uint8_t) type;
                w->nparam++;
            }
            w->sym[y].np = (uint16_t) (w->sym[y].np + n);
            if (!is_sym(w, ';')) break;
            TRY(lex(w));
        }
        tw_s(w, ")");
        TRY(expect_sym(w, ')', "')' expected"));
    }
    if (func) {
        int type;
        tw_s(w, ": ");
        if (!is_sym(w, ':')) return pfail(w, ZL_E_SYNTAX, "a function needs ': type'");
        TRY(lex(w));
        TRY(p_typename(w, &type));
        w->sym[y].type = (uint8_t) type;
        w->sym[y].n = w->fsize[w->level]++;
    } else if (is_sym(w, ':')) {
        return pfail(w, ZL_E_SYNTAX, "a procedure has no result type");
    }
    tw_s(w, ";\n");
    TRY(expect_sym(w, ';', "';' expected"));
    if (tok_is(w, "FORWARD") || tok_is(w, "EXTERNAL"))
        return pfail(w, ZL_E_UNSUPPORTED, "forward and external declarations");
    w->c->line = line;
    w->sym[y].addr = (int32_t) w->c->p->ncode;
    int32_t enter = zl_emit(w->c, ZL_ENTER, 0, w->sym[y].np);
    if (enter < 0) return status(w);
    TRY(p_block(w));
    w->c->line = w->t.line;
    if (func) TRY(emit(w, ZL_LOADF, w->sym[y].n, 0));
    TRY(emit(w, ZL_LEAVE, 0, 0));
    TRY(emit(w, ZL_RET, 0, 0));
    w->c->p->code[enter].a = w->fsize[w->level];
    w->nsym = w->scope[w->level];
    w->level--;
    tw_s(w, ";\n");
    return expect_sym(w, ';', "';' expected after a procedure's end");
}

/* declarations, then the body; the twin's block ends after "end" */
static int p_block(pw_t *w)
{
    uint32_t B = w->level ? w->level - 1 : 0;
    int32_t jover = -1;
    if (is_kw(w, K_LABEL)) return refuse_kw(w);
    if (is_kw(w, K_CONST)) TRY(p_const_part(w, B));
    if (is_kw(w, K_TYPE)) return refuse_kw(w);
    if (is_kw(w, K_VAR)) TRY(p_var_part(w, B));
    while (is_kw(w, K_PROCEDURE) || is_kw(w, K_FUNCTION)) {
        if (jover < 0 && (jover = zl_emit(w->c, ZL_JMP, 0, 0)) < 0) return status(w);
        TRY(p_proc(w, is_kw(w, K_FUNCTION)));
    }
    if (is_kw(w, K_CONST) || is_kw(w, K_VAR) || is_kw(w, K_LABEL) || is_kw(w, K_TYPE))
        return pfail(w, ZL_E_SYNTAX,
                     "declarations out of order (label, const, type, var, "
                     "procedures)");
    if (jover >= 0) zl_patch(w->c, jover, (int64_t) w->c->p->ncode);
    if (!is_kw(w, K_BEGIN)) return pfail(w, ZL_E_SYNTAX, "begin expected");
    bol(w, B);
    return p_compound(w, B);
}

int zl_pascal_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    pw_t *w = (pw_t *) work;
    zl_bzero(w, sizeof *w);
    w->c = c;
    w->tw = tw;
    w->src = src;
    w->n = len;
    w->line = 1;
    w->cur[0] = -1;
    c->p->word_bits = 32; /* integer under fpc -Miso: maxint = 2147483647 */
    TRY(lex(w));
    bol(w, 0);
    tw_s(w, "program ");
    TRY(expect_kw(w, K_PROGRAM, "program expected"));
    if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "the program's name is expected");
    tw_name(w, w->t.pos, w->t.len);
    TRY(lex(w));
    if (is_sym(w, '(')) {
        int seen_in = 0, seen_out = 0;
        tw_s(w, "(");
        TRY(lex(w));
        for (int first = 1;; first = 0) {
            if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a program parameter is expected");
            int in = tok_is(w, "INPUT"), out = tok_is(w, "OUTPUT");
            if (!in && !out)
                return pfail(w, ZL_E_UNSUPPORTED, "program parameters other than input and output");
            if ((in && seen_in) || (out && seen_out))
                return pfail(w, ZL_E_SYNTAX, "a program parameter listed twice");
            seen_in |= in;
            seen_out |= out;
            if (!first) tw_s(w, ", ");
            tw_s(w, in ? "input" : "output");
            TRY(lex(w));
            if (!is_sym(w, ',')) break;
            TRY(lex(w));
        }
        tw_s(w, ")");
        TRY(expect_sym(w, ')', "')' expected"));
    }
    tw_s(w, ";\n");
    TRY(expect_sym(w, ';', "';' expected after the program heading"));
    TRY(p_block(w));
    if (!is_sym(w, '.')) return pfail(w, ZL_E_SYNTAX, "'.' expected after the program's end");
    tw_s(w, ".\n");
    TRY(lex(w));
    if (w->t.kind != TK_EOF) return pfail(w, ZL_E_SYNTAX, "text after the program's end");
    if (w->ncom) bol(w, 0); /* trailing comments; no indentation at level 0 */
    w->c->line = w->t.line;
    TRY(emit(w, ZL_HALT, 0, 0));
    return status(w);
}
