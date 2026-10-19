/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_lua.c — Lua front-end: the integer and boolean subset of Lua 5.4 as
 * the reference interpreter (lua 5.4 and later) runs it, and its twin
 * "Moonwyrm". See LINGUA_SPEC.md for what every front-end owes.
 *
 * Admitted: integers (decimal, and hexadecimal that fits 63 bits), true and
 * false; local declarations with values; assignment, several names at once
 * (all values are evaluated first); + - * // % (floored, as Lua), unary
 * minus, & | ~ and unary ~; == ~= < <= > >=; and, or and not on booleans;
 * if / elseif / else, while, repeat ... until (the condition sees the
 * block's locals), numeric for with an optional step (Lua 5.4's iteration
 * count: the loop never steps past its limit), do ... end, break; function
 * and local function at the top of the chunk, with parameters, recursion
 * and return; print and io.write of numbers, booleans (print only), string
 * literals and .. chains of them; io.read("n"); math.abs, math.max,
 * math.min, math.maxinteger, math.mininteger. A number used as a condition
 * is true, as in Lua.
 *
 * Static types: Lua is dynamically typed, so the front-end infers one type
 * (number or boolean) for every variable, parameter and function result,
 * from declarations, assignments, call sites and return statements, over
 * as many passes as forward references need. A name that would hold both,
 * a function that returns both a value and nothing, or a value-returning
 * function that can end without returning is refused: Lua would carry a
 * nil there. Reading a global before any assignment ran, or calling a
 * global function before its definition ran, traps ZL_E_DOMAIN where Lua
 * raises an error.
 *
 * Refused by name: floats, / and ^ (they give floats), << and >>, nil,
 * strings outside print and io.write, tables, fields and methods, #,
 * anonymous and nested functions, varargs, goto and labels, the generic
 * for, local attributes, assigning to a for loop's variable (an error in
 * Lua 5.5), long strings and long comments, and the standard library
 * beyond the calls above.
 *
 * [ref differs] Lua wraps on integer overflow (and on mininteger // -1);
 * Lingua traps ZL_E_OVERFLOW. A for loop whose range is wider than 2^63
 * traps instead of running. Calls nest at most ZL_MAX_CALLS deep.
 *
 * Moonwyrm (the twin) is the same program in one spelling: one statement
 * per line, two spaces of indentation per block, ';' dropped, one space
 * around binary operators and after commas, none inside parentheses (which
 * are kept as written), integers in decimal, strings in double quotes
 * (escapes \\ \" \n \t), io.read("n"). Comments keep their text: a comment
 * on its own line stays on its own line at the indentation of the line
 * after it, a comment after code stays after that line's code; a run of
 * blank lines becomes one, and none starts the file. */
#include "zl_int.h"

/* ------------------------------------------------------------ tokens */

enum {
    K_EOF,
    K_NAME,
    K_INT,
    K_STR,
    K_AND, /* keywords, in alphabetical order */
    K_BREAK,
    K_DO,
    K_ELSE,
    K_ELSEIF,
    K_END,
    K_FALSE,
    K_FOR,
    K_FUNCTION,
    K_GOTO,
    K_IF,
    K_IN,
    K_LOCAL,
    K_NIL,
    K_NOT,
    K_OR,
    K_REPEAT,
    K_RETURN,
    K_THEN,
    K_TRUE,
    K_UNTIL,
    K_WHILE,
    K_PLUS, /* symbols */
    K_MINUS,
    K_STAR,
    K_SLASH,
    K_IDIV,
    K_MOD,
    K_POW,
    K_LEN,
    K_BAND,
    K_BXOR,
    K_BOR,
    K_SHL,
    K_SHR,
    K_CONCAT,
    K_EQ,
    K_NE,
    K_LE,
    K_GE,
    K_LT,
    K_GT,
    K_ASSIGN,
    K_LP,
    K_RP,
    K_LBRACE,
    K_RBRACE,
    K_LBRACK,
    K_RBRACK,
    K_DCOLON,
    K_SEMI,
    K_COLON,
    K_COMMA,
    K_DOT,
    K_DOTS,
    K__COUNT
};

static const char *const kspell[K__COUNT] = {
    "<eof>",  "<name>", "<number>", "<string>", "and",   "break", "do",    "else", "elseif", "end",
    "false",  "for",    "function", "goto",     "if",    "in",    "local", "nil",  "not",    "or",
    "repeat", "return", "then",     "true",     "until", "while", "+",     "-",    "*",      "/",
    "//",     "%",      "^",        "#",        "&",     "~",     "|",     "<<",   ">>",     "..",
    "==",     "~=",     "<=",       ">=",       "<",     ">",     "=",     "(",    ")",      "{",
    "}",      "[",      "]",        "::",       ";",     ":",     ",",     ".",    "...",
};

typedef struct {
    uint8_t k, blank; /* blank: an empty line before the line it starts */
    uint16_t pad;
    uint32_t pos, len; /* source bytes; for K_STR, len is the decoded length */
    uint32_t line, col;
    int64_t v; /* K_INT: the value; K_STR: offset of the decoded bytes in sbuf */
} ltok_t;

typedef struct {
    uint32_t pos, len; /* the text after "--", trailing blanks cut */
    uint32_t tok;      /* index of the token that follows it */
    uint32_t line;
    uint8_t trailing, blank;
    uint16_t pad;
} lcom_t;

/* ------------------------------------------------------------ the compiler */

enum { TY_UNK = 0, TY_INT, TY_BOOL, TY_STR, TY_NONE };
enum { V_SLOT = 1, V_FRAME, V_FUNC };
enum { P_STR = 1, P_INT, P_BOOL, P_TAB };

#define LT_MAX     4096 /* tokens */
#define LC_MAX     512  /* comments */
#define LS_MAX     8192 /* decoded string bytes */
#define LV_MAX     256  /* names in scope */
#define LG_MAX     128  /* globals */
#define LF_MAX     64   /* functions */
#define LX_MAX     512  /* calls waiting for a function's address */
#define LB_MAX     256  /* pending breaks */
#define LP_MAX     128  /* output pieces of one print */
#define LA_MAX     32   /* parameters, names in one statement */
#define LM_MAX     64   /* reusable temporaries of the main chunk */
#define LE_MAX     64   /* elseif arms of one if */
#define L_PASSES   8    /* passes allowed to settle the types */
#define NP_UNKNOWN 255

typedef struct {
    uint32_t pos, len;
    uint8_t kind, type, ro, pad;
    int32_t idx; /* V_SLOT: slot; V_FRAME: frame offset; V_FUNC: function */
} lvar_t;

typedef struct {
    uint32_t pos, len;
    uint8_t isfn, isvar, type, assigned;
    int32_t fn;         /* isfn: the function */
    int32_t slot, flag; /* this pass: the value and the "assigned yet" flag */
    uint32_t use_line;  /* this pass: first read or call */
} lglob_t;

typedef struct {
    uint32_t pos; /* the name token's offset (UINT32_MAX until defined) */
    uint32_t len;
    uint8_t np, type, defined, pad;
    uint8_t ptype[LA_MAX];
    int32_t entry; /* this pass */
    uint32_t ncall;
} lfn_t;

typedef struct {
    zl_cc_t *c;
    zl_out_t *tw; /* the twin, on the last pass only */
    const char *s;
    size_t n;
    ltok_t tok[LT_MAX];
    uint32_t ntok, t;
    lcom_t com[LC_MAX];
    uint32_t ncom, cnext;
    char sbuf[LS_MAX];
    uint32_t nsbuf;
    lvar_t var[LV_MAX];
    uint32_t nvar;
    lglob_t glob[LG_MAX];
    uint32_t nglob;
    lfn_t fn[LF_MAX];
    uint32_t nfn;
    struct {
        int32_t at;
        int32_t fn;
        uint32_t line;
    } fix[LX_MAX];
    uint32_t nfix;
    int32_t brk[LB_MAX];
    uint32_t nbrk;
    int32_t brk0; /* first break of the innermost loop; -1 outside loops */
    struct {
        uint8_t kind;
        int32_t v;
    } pc[LP_MAX];
    uint32_t npc;
    int32_t tmp[LM_MAX];
    uint32_t ntmp, tmpn;
    int infn, strok, unknown, tw_any, blockdepth;
    int ret_val, ret_none;
    int32_t cur_fn;
    uint32_t foff, fmax, depth, nest;
} lw_t;

ZL_STATIC_ASSERT(sizeof(lw_t) <= sizeof(((zl_work_t *) 0)->fe), lw_fits);

#define TRY(x)                                                                                     \
    do {                                                                                           \
        int s_ = (x);                                                                              \
        if (s_ != ZL_OK) return s_;                                                                \
    } while (0)

static int status(lw_t *w)
{
    return w->c->failed ? w->c->d->status : ZL_E_LIMIT;
}

static int lfail_at(lw_t *w, uint32_t t, int st, const char *msg)
{
    w->c->line = w->tok[t].line;
    w->c->col = w->tok[t].col;
    return zl_fail(w->c, st, msg);
}

static int lfail(lw_t *w, int st, const char *msg)
{
    return lfail_at(w, w->t, st, msg);
}

static int em(lw_t *w, int op, int64_t a, int32_t b)
{
    return zl_emit(w->c, op, a, b) < 0 ? status(w) : ZL_OK;
}

static int32_t here_pc(lw_t *w)
{
    return (int32_t) w->c->p->ncode;
}

static void ln(lw_t *w, uint32_t t)
{
    w->c->line = w->tok[t].line;
}

/* ------------------------------------------------------------ lexing */

static int is_name_start(int ch)
{
    return zl_isalpha(ch) || ch == '_';
}

static int is_name_char(int ch)
{
    return zl_isalnum(ch) || ch == '_';
}

static int hexval(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int lex_fail(lw_t *w, uint32_t line, size_t at, size_t bol, int st, const char *msg)
{
    w->c->line = line;
    w->c->col = (uint32_t) (at - bol) + 1;
    return zl_fail(w->c, st, msg);
}

static int lex(lw_t *w)
{
    const char *s = w->s;
    size_t n = w->n, i = 0, bol = 0;
    uint32_t line = 1, last_line = 0, tok_line = 0;
    if (n && s[0] == '#')
        return lex_fail(w, 1, 0, 0, ZL_E_UNSUPPORTED,
                        "a first line starting with # is outside the subset");
    for (;;) {
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' ||
                         s[i] == '\f' || s[i] == '\v')) {
            if (s[i] == '\n') {
                line++;
                bol = i + 1;
            }
            i++;
        }
        if (i >= n) break;
        int ch = (unsigned char) s[i];
        if (ch == '-' && i + 1 < n && s[i + 1] == '-') {
            size_t st = i + 2, e = st;
            if (st < n && s[st] == '[') {
                size_t k = st + 1;
                while (k < n && s[k] == '=') k++;
                if (k < n && s[k] == '[')
                    return lex_fail(w, line, i, bol, ZL_E_UNSUPPORTED,
                                    "long comments --[[ ]] are outside the subset");
            }
            while (e < n && s[e] != '\n') {
                int b = (unsigned char) s[e];
                if (b < 0x20 && b != '\t' && b != '\r')
                    return lex_fail(w, line, e, bol, ZL_E_SYNTAX,
                                    "a control character in a comment");
                e++;
            }
            size_t te = e;
            while (te > st && (s[te - 1] == ' ' || s[te - 1] == '\t' || s[te - 1] == '\r')) te--;
            if (w->ncom >= LC_MAX)
                return lex_fail(w, line, i, bol, ZL_E_LIMIT, "too many comments");
            lcom_t *m = &w->com[w->ncom++];
            m->pos = (uint32_t) st;
            m->len = (uint32_t) (te - st);
            m->tok = w->ntok;
            m->line = line;
            m->trailing = tok_line == line;
            m->blank = last_line && line > last_line + 1;
            last_line = line;
            i = e;
            continue;
        }
        if (w->ntok >= LT_MAX - 1) return lex_fail(w, line, i, bol, ZL_E_LIMIT, "program too long");
        ltok_t *t = &w->tok[w->ntok];
        t->pos = (uint32_t) i;
        t->line = line;
        t->col = (uint32_t) (i - bol) + 1;
        t->blank = last_line && line > last_line + 1;
        if (zl_isdigit(ch) || (ch == '.' && i + 1 < n && zl_isdigit((unsigned char) s[i + 1]))) {
            uint64_t v = 0;
            size_t st = i;
            int hex = ch == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X');
            if (hex) {
                size_t nd = 0;
                i += 2;
                while (i < n && hexval((unsigned char) s[i]) >= 0) {
                    if (v >> 59)
                        return lex_fail(
                            w, line, st, bol, ZL_E_UNSUPPORTED,
                            "a hexadecimal number past 63 bits wraps in Lua; outside the subset");
                    v = v * 16 + (uint64_t) hexval((unsigned char) s[i]);
                    nd++;
                    i++;
                }
                if (!nd) return lex_fail(w, line, st, bol, ZL_E_SYNTAX, "malformed number");
                if (i < n && (s[i] == '.' || s[i] == 'p' || s[i] == 'P'))
                    return lex_fail(
                        w, line, st, bol, ZL_E_UNSUPPORTED,
                        "a number with a fraction or exponent is a float; outside the subset");
            } else {
                while (i < n && zl_isdigit((unsigned char) s[i])) {
                    uint64_t d = (uint64_t) (s[i] - '0');
                    if (v > (UINT64_C(0x7fffffffffffffff) - d) / 10)
                        return lex_fail(
                            w, line, st, bol, ZL_E_UNSUPPORTED,
                            "an integer past 2^63-1 is a float in Lua; outside the subset");
                    v = v * 10 + d;
                    i++;
                }
                if (i < n && (s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
                    return lex_fail(
                        w, line, st, bol, ZL_E_UNSUPPORTED,
                        "a number with a fraction or exponent is a float; outside the subset");
            }
            if (i < n && is_name_char((unsigned char) s[i]))
                return lex_fail(w, line, st, bol, ZL_E_SYNTAX, "malformed number");
            t->k = K_INT;
            t->v = (int64_t) v;
            t->len = (uint32_t) (i - st);
        } else if (is_name_start(ch)) {
            size_t st = i;
            while (i < n && is_name_char((unsigned char) s[i])) i++;
            t->k = K_NAME;
            t->len = (uint32_t) (i - st);
            for (int k = K_AND; k <= K_WHILE; k++) {
                const char *kw = kspell[k];
                size_t j = 0;
                while (j < t->len && kw[j] && kw[j] == s[st + j]) j++;
                if (j == t->len && kw[j] == 0) {
                    t->k = (uint8_t) k;
                    break;
                }
            }
        } else if (ch == '"' || ch == '\'') {
            size_t st = i;
            uint32_t off = w->nsbuf;
            i++;
            for (;;) {
                if (i >= n || s[i] == '\n')
                    return lex_fail(w, line, st, bol, ZL_E_SYNTAX, "unfinished string");
                int b = (unsigned char) s[i];
                if (b == ch) {
                    i++;
                    break;
                }
                if (b == '\\') {
                    int e = i + 1 < n ? (unsigned char) s[i + 1] : 0;
                    b = e == 'n'    ? '\n'
                        : e == 't'  ? '\t'
                        : e == '\\' ? '\\'
                        : e == '"'  ? '"'
                        : e == '\'' ? '\''
                                    : -1;
                    if (b < 0)
                        return lex_fail(
                            w, line, i, bol, ZL_E_UNSUPPORTED,
                            "only the escapes \\n \\t \\\\ \\\" \\' are in this subset");
                    i += 2;
                } else {
                    if ((b < 0x20 && b != '\t') || b == 0x7f)
                        return lex_fail(w, line, i, bol, ZL_E_SYNTAX,
                                        "a control character in a string");
                    i++;
                }
                if (w->nsbuf >= LS_MAX)
                    return lex_fail(w, line, st, bol, ZL_E_LIMIT, "too much string text");
                w->sbuf[w->nsbuf++] = (char) b;
            }
            t->k = K_STR;
            t->v = off;
            t->len = w->nsbuf - off;
        } else {
            static const struct {
                const char *sp;
                uint8_t k;
            } sym[] = {
                {"...", K_DOTS}, {"..", K_CONCAT}, {"//", K_IDIV},  {"==", K_EQ},
                {"~=", K_NE},    {"<=", K_LE},     {">=", K_GE},    {"<<", K_SHL},
                {">>", K_SHR},   {"::", K_DCOLON}, {"+", K_PLUS},   {"-", K_MINUS},
                {"*", K_STAR},   {"/", K_SLASH},   {"%", K_MOD},    {"^", K_POW},
                {"#", K_LEN},    {"&", K_BAND},    {"~", K_BXOR},   {"|", K_BOR},
                {"<", K_LT},     {">", K_GT},      {"=", K_ASSIGN}, {"(", K_LP},
                {")", K_RP},     {"{", K_LBRACE},  {"}", K_RBRACE}, {"[", K_LBRACK},
                {"]", K_RBRACK}, {";", K_SEMI},    {":", K_COLON},  {",", K_COMMA},
                {".", K_DOT},
            };
            size_t k = 0, m = 0;
            for (; k < sizeof sym / sizeof sym[0]; k++) {
                m = 0;
                while (sym[k].sp[m] && i + m < n && s[i + m] == sym[k].sp[m]) m++;
                if (!sym[k].sp[m]) break;
            }
            if (k == sizeof sym / sizeof sym[0])
                return lex_fail(w, line, i, bol, ZL_E_SYNTAX, "a character Lua does not use here");
            if (sym[k].k == K_LBRACK && i + 1 < n && (s[i + 1] == '[' || s[i + 1] == '='))
                return lex_fail(w, line, i, bol, ZL_E_UNSUPPORTED,
                                "long strings [[ ]] are outside the subset");
            t->k = sym[k].k;
            t->len = (uint32_t) m;
            i += m;
        }
        w->ntok++;
        last_line = line;
        tok_line = line;
    }
    ltok_t *e = &w->tok[w->ntok];
    e->k = K_EOF;
    e->pos = (uint32_t) n;
    e->len = 0;
    e->line = line;
    e->col = (uint32_t) (i - bol) + 1;
    e->blank = 0;
    w->ntok++;
    return ZL_OK;
}

/* ------------------------------------------------------------ helpers */

static int at(lw_t *w, int k)
{
    return w->tok[w->t].k == k;
}

static int peek_k(lw_t *w, uint32_t ahead)
{
    uint32_t i = w->t + ahead;
    return i < w->ntok ? w->tok[i].k : K_EOF;
}

static void next(lw_t *w)
{
    if (w->tok[w->t].k != K_EOF) w->t++;
}

static int name_is(lw_t *w, uint32_t t, const char *lit)
{
    const ltok_t *k = &w->tok[t];
    if (k->k != K_NAME) return 0;
    uint32_t j = 0;
    while (j < k->len && lit[j] && w->s[k->pos + j] == lit[j]) j++;
    return j == k->len && lit[j] == 0;
}

static int same_name(lw_t *w, uint32_t pa, uint32_t la, uint32_t pb, uint32_t lb)
{
    if (la != lb) return 0;
    for (uint32_t j = 0; j < la; j++)
        if (w->s[pa + j] != w->s[pb + j]) return 0;
    return 1;
}

static int expect(lw_t *w, int k, const char *msg)
{
    if (!at(w, k)) return lfail(w, ZL_E_SYNTAX, msg);
    next(w);
    return ZL_OK;
}

static int ty_mark(lw_t *w, int ty)
{
    if (ty == TY_UNK) w->unknown = 1;
    return ty;
}

static int nest_in(lw_t *w)
{
    if (++w->nest > ZL_MAX_DEPTH) return lfail(w, ZL_E_LIMIT, "nested too deeply");
    return ZL_OK;
}

/* ------------------------------------------------------------ the twin */

static void tws(lw_t *w, const char *s)
{
    if (w->tw) zl_puts(w->tw, s);
}

static void tw_text(lw_t *w, uint32_t pos, uint32_t len)
{
    if (w->tw) zl_put(w->tw, w->s + pos, len);
}

static void tw_name(lw_t *w, uint32_t t)
{
    tw_text(w, w->tok[t].pos, w->tok[t].len);
}

static void tw_str(lw_t *w, uint32_t t)
{
    if (!w->tw) return;
    const ltok_t *k = &w->tok[t];
    zl_putc(w->tw, '"');
    for (uint32_t j = 0; j < k->len; j++) {
        char ch = w->sbuf[k->v + j];
        if (ch == '\n')
            zl_puts(w->tw, "\\n");
        else if (ch == '\t')
            zl_puts(w->tw, "\\t");
        else if (ch == '"' || ch == '\\') {
            zl_putc(w->tw, '\\');
            zl_putc(w->tw, ch);
        } else
            zl_putc(w->tw, ch);
    }
    zl_putc(w->tw, '"');
}

static void tw_indent(lw_t *w)
{
    for (uint32_t d = 0; d < w->depth; d++) tws(w, "  ");
}

/* Every comment before token `upto` goes out on a line of its own. */
static void flush_comments(lw_t *w, uint32_t upto)
{
    while (w->cnext < w->ncom && w->com[w->cnext].tok <= upto) {
        const lcom_t *m = &w->com[w->cnext++];
        if (m->blank && w->tw_any) tws(w, "\n");
        tw_indent(w);
        tws(w, "--");
        tw_text(w, m->pos, m->len);
        tws(w, "\n");
        w->tw_any = 1;
    }
}

/* Start a twin line at the current token. */
static void bol(lw_t *w)
{
    flush_comments(w, w->t);
    if (w->tok[w->t].blank && w->tw_any) tws(w, "\n");
    tw_indent(w);
    w->tw_any = 1;
}

/* End a twin line after the last token taken: semicolons that follow are
 * dropped, a comment right after it stays on the line, and a comment
 * anywhere inside the line is refused. */
static int eol(lw_t *w)
{
    while (at(w, K_SEMI) && !(w->cnext < w->ncom && w->com[w->cnext].tok <= w->t)) next(w);
    uint32_t last = w->t ? w->t - 1 : 0;
    if (w->cnext < w->ncom && w->com[w->cnext].tok <= last) {
        w->c->line = w->com[w->cnext].line;
        w->c->col = 0;
        return zl_fail(w->c, ZL_E_UNSUPPORTED,
                       "a comment inside a statement is outside the subset");
    }
    if (w->cnext < w->ncom && w->com[w->cnext].tok == last + 1 && w->com[w->cnext].trailing) {
        const lcom_t *m = &w->com[w->cnext++];
        tws(w, " --");
        tw_text(w, m->pos, m->len);
    }
    tws(w, "\n");
    return ZL_OK;
}

/* ------------------------------------------------------------ names */

static const char *const libnames[] = {
    "assert",    "collectgarbage", "dofile", "error",   "getmetatable", "ipairs",
    "load",      "loadfile",       "next",   "pairs",   "pcall",        "rawequal",
    "rawget",    "rawlen",         "rawset", "require", "select",       "setmetatable",
    "tonumber",  "tostring",       "type",   "xpcall",  "_G",           "_VERSION",
    "coroutine", "debug",          "os",     "package", "string",       "table",
    "utf8",
};

static int is_builtin(lw_t *w, uint32_t t)
{
    return name_is(w, t, "print") || name_is(w, t, "io") || name_is(w, t, "math");
}

static int is_lib(lw_t *w, uint32_t t)
{
    for (size_t i = 0; i < sizeof libnames / sizeof libnames[0]; i++)
        if (name_is(w, t, libnames[i])) return 1;
    return is_builtin(w, t);
}

static int var_find(lw_t *w, uint32_t t)
{
    for (uint32_t i = w->nvar; i > 0; i--)
        if (same_name(w, w->var[i - 1].pos, w->var[i - 1].len, w->tok[t].pos, w->tok[t].len))
            return (int) i - 1;
    return -1;
}

static int declare(lw_t *w, uint32_t t, int kind, int32_t idx, int type, int ro)
{
    if (is_builtin(w, t))
        return lfail_at(w, t, ZL_E_UNSUPPORTED, "print, io and math cannot be redefined here");
    if (w->nvar >= LV_MAX) return lfail_at(w, t, ZL_E_LIMIT, "too many names in scope");
    lvar_t *v = &w->var[w->nvar++];
    v->pos = w->tok[t].pos;
    v->len = w->tok[t].len;
    v->kind = (uint8_t) kind;
    v->idx = idx;
    v->type = (uint8_t) type;
    v->ro = (uint8_t) ro;
    return ZL_OK;
}

static int glob_get(lw_t *w, uint32_t t, lglob_t **out)
{
    if (is_lib(w, t))
        return lfail_at(w, t, ZL_E_UNSUPPORTED, "a Lua library name outside the subset");
    for (uint32_t i = 0; i < w->nglob; i++)
        if (same_name(w, w->glob[i].pos, w->glob[i].len, w->tok[t].pos, w->tok[t].len)) {
            *out = &w->glob[i];
            return ZL_OK;
        }
    if (w->nglob >= LG_MAX) return lfail_at(w, t, ZL_E_LIMIT, "too many globals");
    lglob_t *g = &w->glob[w->nglob++];
    g->pos = w->tok[t].pos;
    g->len = w->tok[t].len;
    g->fn = -1;
    g->slot = g->flag = -1;
    *out = g;
    return ZL_OK;
}

static int glob_cells(lw_t *w, lglob_t *g)
{
    if (g->flag < 0) {
        g->flag = zl_slots(w->c, 1);
        if (g->flag < 0) return status(w);
    }
    if (!g->isfn && g->slot < 0) {
        g->slot = zl_slots(w->c, 1);
        if (g->slot < 0) return status(w);
    }
    return ZL_OK;
}

static int fn_new(lw_t *w, uint32_t pos, uint32_t len, int32_t *out)
{
    if (w->nfn >= LF_MAX) return lfail(w, ZL_E_LIMIT, "too many functions");
    lfn_t *f = &w->fn[w->nfn];
    f->pos = pos;
    f->len = len;
    f->np = NP_UNKNOWN;
    f->type = TY_UNK;
    f->entry = -1;
    *out = (int32_t) w->nfn++;
    return ZL_OK;
}

static int fn_by_pos(lw_t *w, uint32_t t, int32_t *out)
{
    for (uint32_t i = 0; i < w->nfn; i++)
        if (w->fn[i].pos == w->tok[t].pos) {
            *out = (int32_t) i;
            return ZL_OK;
        }
    return fn_new(w, w->tok[t].pos, w->tok[t].len, out);
}

/* ------------------------------------------------------------ cells */

/* A cell is a slot (>= 0) in the main chunk, a frame offset (-1 - off) in
 * a function. */
static int ld_cell(lw_t *w, int32_t c)
{
    return c >= 0 ? em(w, ZL_LOAD, c, 0) : em(w, ZL_LOADF, -1 - c, 0);
}

static int st_cell(lw_t *w, int32_t c)
{
    return c >= 0 ? em(w, ZL_STORE, c, 0) : em(w, ZL_STOREF, -1 - c, 0);
}

static int new_cell(lw_t *w, int32_t *out)
{
    if (w->infn) {
        if (w->foff >= ZL_MAX_FRAMES)
            return lfail(w, ZL_E_LIMIT, "too many locals in one function");
        *out = -1 - (int32_t) w->foff++;
        if (w->foff > w->fmax) w->fmax = w->foff;
        return ZL_OK;
    }
    int32_t s = zl_slots(w->c, 1);
    if (s < 0) return status(w);
    *out = s;
    return ZL_OK;
}

/* A temporary that lives for one statement. */
static int tmp_cell(lw_t *w, int32_t *out)
{
    if (w->infn) return new_cell(w, out);
    if (w->tmpn == w->ntmp) {
        if (w->ntmp >= LM_MAX) return lfail(w, ZL_E_LIMIT, "too many values in one statement");
        int32_t s = zl_slots(w->c, 1);
        if (s < 0) return status(w);
        w->tmp[w->ntmp++] = s;
    }
    *out = w->tmp[w->tmpn++];
    return ZL_OK;
}

static int piece(lw_t *w, int kind, int32_t v)
{
    if (w->npc >= LP_MAX) return lfail(w, ZL_E_LIMIT, "too many pieces in one print");
    w->pc[w->npc].kind = (uint8_t) kind;
    w->pc[w->npc].v = v;
    w->npc++;
    return ZL_OK;
}

/* The value on the stack becomes the next output piece. */
static int stash(lw_t *w, int kind)
{
    int32_t c;
    TRY(tmp_cell(w, &c));
    TRY(st_cell(w, c));
    return piece(w, kind, c);
}

/* Trap unless the flag slot says the global was set. */
static int guard(lw_t *w, int32_t flag)
{
    TRY(em(w, ZL_LOAD, flag, 0));
    int32_t j = zl_emit(w->c, ZL_JNZ, 0, 0);
    if (j < 0) return status(w);
    TRY(em(w, ZL_TRAP, ZL_E_DOMAIN, 0));
    zl_patch(w->c, j, here_pc(w));
    return ZL_OK;
}

/* ------------------------------------------------------------ expressions */

static int subexpr(lw_t *w, int limit, int *ty);

static int need_num(lw_t *w, uint32_t t, int ty)
{
    if (ty == TY_BOOL)
        return lfail_at(w, t, ZL_E_UNSUPPORTED, "arithmetic on a boolean is an error in Lua");
    if (ty == TY_STR)
        return lfail_at(w, t, ZL_E_UNSUPPORTED, "a string in an expression is outside the subset");
    if (ty == TY_NONE) return lfail_at(w, t, ZL_E_UNSUPPORTED, "the function returns no value");
    return ZL_OK;
}

static int need_value(lw_t *w, uint32_t t, int ty)
{
    if (ty == TY_STR)
        return lfail_at(w, t, ZL_E_UNSUPPORTED, "a string value is outside the subset");
    if (ty == TY_NONE) return lfail_at(w, t, ZL_E_UNSUPPORTED, "the function returns no value");
    return ZL_OK;
}

static int call(lw_t *w, int *ty, int want);

/* math.abs/max/min/maxinteger/mininteger and io.read("n") */
static int lib_expr(lw_t *w, int *ty, int want)
{
    uint32_t t0 = w->t, f = w->t + 2;
    int io = name_is(w, t0, "io");
    if (peek_k(w, 2) != K_NAME) return lfail_at(w, f, ZL_E_SYNTAX, "a name expected after '.'");
    ln(w, t0);
    tw_name(w, t0);
    tws(w, ".");
    tw_name(w, f);
    w->t += 3;
    if (!io && (name_is(w, f, "maxinteger") || name_is(w, f, "mininteger"))) {
        *ty = TY_INT;
        return em(w, ZL_PUSH, name_is(w, f, "maxinteger") ? INT64_MAX : INT64_MIN, 0);
    }
    if (io && name_is(w, f, "read")) {
        if (!at(w, K_LP) || peek_k(w, 1) != K_STR || peek_k(w, 2) != K_RP)
            return lfail(w, ZL_E_UNSUPPORTED, "only io.read(\"n\") is in this subset");
        const ltok_t *a = &w->tok[w->t + 1];
        const char *v = w->sbuf + a->v;
        if (!((a->len == 1 && v[0] == 'n') || (a->len == 2 && v[0] == '*' && v[1] == 'n')))
            return lfail(w, ZL_E_UNSUPPORTED, "only io.read(\"n\") is in this subset");
        w->t += 3;
        tws(w, "(\"n\")");
        *ty = TY_INT;
        return em(w, ZL_IN, 0, 0);
    }
    int op = io                     ? 0
             : name_is(w, f, "abs") ? ZL_ABS
             : name_is(w, f, "max") ? ZL_MAX
             : name_is(w, f, "min") ? ZL_MIN
                                    : 0;
    if (!op) return lfail_at(w, f, ZL_E_UNSUPPORTED, "this library function is outside the subset");
    if (!at(w, K_LP)) return lfail(w, ZL_E_SYNTAX, "'(' expected");
    tws(w, "(");
    next(w);
    int save = w->strok, nargs = 0;
    w->strok = 0;
    while (!at(w, K_RP)) {
        if (nargs) {
            TRY(expect(w, K_COMMA, "',' or ')' expected"));
            tws(w, ", ");
        }
        uint32_t at0 = w->t;
        int a;
        TRY(subexpr(w, 0, &a));
        TRY(need_num(w, at0, a));
        ln(w, t0);
        if (nargs && op != ZL_ABS) TRY(em(w, op, 0, 0));
        nargs++;
    }
    next(w);
    tws(w, ")");
    w->strok = save;
    if (op == ZL_ABS && nargs != 1)
        return lfail_at(w, t0, ZL_E_UNSUPPORTED, "math.abs takes one number");
    if (nargs < 1) return lfail_at(w, t0, ZL_E_UNSUPPORTED, "math.max and math.min take numbers");
    if (op == ZL_ABS) TRY(em(w, ZL_ABS, 0, 0));
    (void) want;
    *ty = TY_INT;
    return ZL_OK;
}

static int name_expr(lw_t *w, int *ty)
{
    uint32_t t0 = w->t;
    int k1 = peek_k(w, 1);
    if ((name_is(w, t0, "io") || name_is(w, t0, "math")) && k1 == K_DOT) return lib_expr(w, ty, 1);
    if (k1 == K_LP) return call(w, ty, 1);
    if (k1 == K_DOT || k1 == K_COLON || k1 == K_LBRACK)
        return lfail_at(w, t0 + 1, ZL_E_UNSUPPORTED,
                        "tables, fields and methods are outside the subset");
    if (k1 == K_STR || k1 == K_LBRACE)
        return lfail_at(w, t0 + 1, ZL_E_UNSUPPORTED,
                        "a call without parentheses is outside the subset");
    ln(w, t0);
    tw_name(w, t0);
    next(w);
    int vi = var_find(w, t0);
    if (vi >= 0) {
        const lvar_t *v = &w->var[vi];
        if (v->kind == V_FUNC)
            return lfail_at(w, t0, ZL_E_UNSUPPORTED,
                            "a function used as a value is outside the subset");
        *ty = ty_mark(w, v->type);
        return ld_cell(w, v->kind == V_SLOT ? v->idx : -1 - v->idx);
    }
    lglob_t *g;
    TRY(glob_get(w, t0, &g));
    if (g->isfn)
        return lfail_at(w, t0, ZL_E_UNSUPPORTED,
                        "a function used as a value is outside the subset");
    g->isvar = 1;
    if (!g->use_line) g->use_line = w->tok[t0].line;
    TRY(glob_cells(w, g));
    TRY(guard(w, g->flag));
    *ty = ty_mark(w, g->type);
    return em(w, ZL_LOAD, g->slot, 0);
}

static int simpleexp(lw_t *w, int *ty)
{
    uint32_t t0 = w->t;
    const ltok_t *k = &w->tok[t0];
    ln(w, t0);
    switch (k->k) {
    case K_INT:
        if (w->tw) zl_puti(w->tw, k->v);
        next(w);
        *ty = TY_INT;
        return em(w, ZL_PUSH, k->v, 0);
    case K_TRUE:
    case K_FALSE:
        tws(w, kspell[k->k]);
        next(w);
        *ty = TY_BOOL;
        return em(w, ZL_PUSH, k->k == K_TRUE, 0);
    case K_NIL:
        return lfail(w, ZL_E_UNSUPPORTED, "nil is outside the subset");
    case K_STR: {
        if (!w->strok)
            return lfail(w, ZL_E_UNSUPPORTED,
                         "a string outside print and io.write is outside the subset");
        int32_t si = zl_addstr(w->c, w->sbuf + k->v, k->len);
        if (si < 0) return status(w);
        tw_str(w, t0);
        next(w);
        *ty = TY_STR;
        return piece(w, P_STR, si);
    }
    case K_DOTS:
        return lfail(w, ZL_E_UNSUPPORTED, "varargs are outside the subset");
    case K_FUNCTION:
        return lfail(w, ZL_E_UNSUPPORTED, "an anonymous function is outside the subset");
    case K_LBRACE:
        return lfail(w, ZL_E_UNSUPPORTED, "tables are outside the subset");
    case K_LP:
        tws(w, "(");
        next(w);
        TRY(subexpr(w, 0, ty));
        TRY(expect(w, K_RP, "')' expected"));
        tws(w, ")");
        if (at(w, K_LP) || at(w, K_STR) || at(w, K_LBRACE) || at(w, K_DOT) || at(w, K_COLON) ||
            at(w, K_LBRACK))
            return lfail(w, ZL_E_UNSUPPORTED,
                         "calling or indexing a parenthesised value is outside the subset");
        return ZL_OK;
    case K_NAME:
        return name_expr(w, ty);
    default:
        return lfail(w, ZL_E_SYNTAX, "an expression expected");
    }
}

/* Lua's binary priorities (lparser.c): left, right. */
static int binprio(int k, int *l, int *r)
{
    switch (k) {
    case K_OR:
        *l = *r = 1;
        return 1;
    case K_AND:
        *l = *r = 2;
        return 1;
    case K_LT:
    case K_GT:
    case K_LE:
    case K_GE:
    case K_NE:
    case K_EQ:
        *l = *r = 3;
        return 1;
    case K_BOR:
        *l = *r = 4;
        return 1;
    case K_BXOR:
        *l = *r = 5;
        return 1;
    case K_BAND:
        *l = *r = 6;
        return 1;
    case K_SHL:
    case K_SHR:
        *l = *r = 7;
        return 1;
    case K_CONCAT:
        *l = 9;
        *r = 8;
        return 1;
    case K_PLUS:
    case K_MINUS:
        *l = *r = 10;
        return 1;
    case K_STAR:
    case K_SLASH:
    case K_IDIV:
    case K_MOD:
        *l = *r = 11;
        return 1;
    case K_POW:
        *l = 14;
        *r = 13;
        return 1;
    default:
        return 0;
    }
}
#define UNARY_PRIO 12

static int arith_op(int k)
{
    switch (k) {
    case K_PLUS:
        return ZL_ADD;
    case K_MINUS:
        return ZL_SUB;
    case K_STAR:
        return ZL_MUL;
    case K_IDIV:
        return ZL_DIVF;
    case K_MOD:
        return ZL_MODF;
    case K_BAND:
        return ZL_AND;
    case K_BOR:
        return ZL_OR;
    case K_BXOR:
        return ZL_XOR;
    case K_LT:
        return ZL_LT;
    case K_LE:
        return ZL_LE;
    case K_GT:
        return ZL_GT;
    case K_GE:
        return ZL_GE;
    case K_EQ:
        return ZL_EQ;
    default:
        return ZL_NE;
    }
}

static int subexpr(lw_t *w, int limit, int *ty)
{
    TRY(nest_in(w));
    uint32_t t0 = w->t;
    int k = w->tok[t0].k;
    if (k == K_LEN) return lfail(w, ZL_E_UNSUPPORTED, "# (length) is outside the subset");
    if (k == K_NOT || k == K_MINUS || k == K_BXOR) {
        int t1;
        tws(w, k == K_NOT ? "not " : k == K_MINUS && peek_k(w, 1) == K_MINUS ? "- " : kspell[k]);
        next(w);
        TRY(subexpr(w, UNARY_PRIO, &t1));
        ln(w, t0);
        if (k == K_NOT) {
            TRY(need_value(w, t0, t1));
            if (t1 == TY_INT) {
                TRY(em(w, ZL_DROP, 0, 0)); /* a number is true: not n is false */
                TRY(em(w, ZL_PUSH, 0, 0));
            } else {
                TRY(em(w, ZL_NOT, 0, 0));
            }
            *ty = TY_BOOL;
        } else {
            TRY(need_num(w, t0, t1));
            TRY(em(w, k == K_MINUS ? ZL_NEG : ZL_INVERT, 0, 0));
            *ty = TY_INT;
        }
    } else {
        TRY(simpleexp(w, ty));
    }
    for (;;) {
        int op = w->tok[w->t].k, l, r;
        if (!binprio(op, &l, &r) || l <= limit) break;
        uint32_t ot = w->t;
        if (op == K_SLASH) return lfail(w, ZL_E_UNSUPPORTED, "/ gives a float in Lua; use //");
        if (op == K_POW)
            return lfail(w, ZL_E_UNSUPPORTED, "^ gives a float in Lua; outside the subset");
        if (op == K_SHL || op == K_SHR)
            return lfail(w, ZL_E_UNSUPPORTED, "<< and >> are outside the subset");
        int lt = *ty, rt;
        if (op == K_CONCAT) {
            if (!w->strok)
                return lfail(w, ZL_E_UNSUPPORTED,
                             "strings outside print and io.write are outside the subset");
            if (lt == TY_BOOL || lt == TY_NONE)
                return lfail(w, ZL_E_UNSUPPORTED,
                             "concatenating a boolean or nothing is an error in Lua");
            if (lt != TY_STR) TRY(stash(w, P_INT));
        } else if (lt == TY_STR) {
            return lfail(w, ZL_E_UNSUPPORTED, "a string in an expression is outside the subset");
        } else if (lt == TY_NONE) {
            return lfail(w, ZL_E_UNSUPPORTED, "the function returns no value");
        }
        tws(w, " ");
        tws(w, kspell[op]);
        tws(w, " ");
        next(w);
        if (op == K_AND || op == K_OR) {
            if (lt == TY_INT)
                return lfail_at(w, ot, ZL_E_UNSUPPORTED,
                                "and/or on a number gives the number in Lua; outside the subset");
            ln(w, ot);
            TRY(em(w, ZL_DUP, 0, 0));
            int32_t j = zl_emit(w->c, op == K_AND ? ZL_JZ : ZL_JNZ, 0, 0);
            if (j < 0) return status(w);
            TRY(em(w, ZL_DROP, 0, 0));
            TRY(subexpr(w, r, &rt));
            TRY(need_value(w, ot, rt));
            if (rt == TY_INT)
                return lfail_at(w, ot, ZL_E_UNSUPPORTED,
                                "and/or on a number gives the number in Lua; outside the subset");
            zl_patch(w->c, j, here_pc(w));
            *ty = TY_BOOL;
            continue;
        }
        TRY(subexpr(w, r, &rt));
        ln(w, ot);
        if (op == K_CONCAT) {
            if (rt == TY_BOOL || rt == TY_NONE)
                return lfail_at(w, ot, ZL_E_UNSUPPORTED,
                                "concatenating a boolean or nothing is an error in Lua");
            if (rt != TY_STR) TRY(stash(w, P_INT));
            *ty = TY_STR;
            continue;
        }
        if (rt == TY_STR)
            return lfail_at(w, ot, ZL_E_UNSUPPORTED,
                            "a string in an expression is outside the subset");
        if (rt == TY_NONE)
            return lfail_at(w, ot, ZL_E_UNSUPPORTED, "the function returns no value");
        if (op == K_EQ || op == K_NE) {
            if (lt != TY_UNK && rt != TY_UNK && lt != rt) { /* a number never equals a boolean */
                TRY(em(w, ZL_DROP, 0, 0));
                TRY(em(w, ZL_DROP, 0, 0));
                TRY(em(w, ZL_PUSH, op == K_NE, 0));
            } else {
                TRY(em(w, arith_op(op), 0, 0));
            }
            *ty = TY_BOOL;
            continue;
        }
        if (lt == TY_BOOL || rt == TY_BOOL)
            return lfail_at(w, ot, ZL_E_UNSUPPORTED,
                            op == K_LT || op == K_LE || op == K_GT || op == K_GE
                                ? "ordering booleans is an error in Lua"
                                : "arithmetic on a boolean is an error in Lua");
        TRY(em(w, arith_op(op), 0, 0));
        *ty = op == K_LT || op == K_LE || op == K_GT || op == K_GE ? TY_BOOL : TY_INT;
    }
    w->nest--;
    return ZL_OK;
}

/* An expression whose value is kept: a number or a boolean. */
static int value(lw_t *w, int *ty)
{
    uint32_t t0 = w->t;
    int save = w->strok;
    w->strok = 0;
    TRY(subexpr(w, 0, ty));
    w->strok = save;
    return need_value(w, t0, *ty);
}

static int set_ptype(lw_t *w, lfn_t *f, uint32_t i, int ty, uint32_t t)
{
    if (i >= LA_MAX) return lfail_at(w, t, ZL_E_LIMIT, "too many arguments");
    if (ty == TY_UNK) return ZL_OK;
    if (f->ptype[i] == TY_UNK)
        f->ptype[i] = (uint8_t) ty;
    else if (f->ptype[i] != ty)
        return lfail_at(w, t, ZL_E_UNSUPPORTED,
                        "a parameter that takes both numbers and booleans is outside the subset");
    return ZL_OK;
}

/* A call of a function of the program; want: the value is used. */
static int call(lw_t *w, int *ty, int want)
{
    uint32_t t0 = w->t;
    int32_t fi;
    lglob_t *g = 0;
    int vi = var_find(w, t0);
    if (vi >= 0) {
        if (w->var[vi].kind != V_FUNC)
            return lfail_at(w, t0, ZL_E_UNSUPPORTED, "calling a variable is outside the subset");
        fi = w->var[vi].idx;
    } else {
        if (name_is(w, t0, "print"))
            return lfail_at(w, t0, ZL_E_UNSUPPORTED, "print returns no value");
        TRY(glob_get(w, t0, &g));
        if (g->isvar)
            return lfail_at(w, t0, ZL_E_UNSUPPORTED, "calling a variable is outside the subset");
        if (g->fn < 0) {
            TRY(fn_new(w, UINT32_MAX, w->tok[t0].len, &fi));
            g->fn = fi;
        }
        g->isfn = 1;
        fi = g->fn;
        if (!g->use_line) g->use_line = w->tok[t0].line;
        TRY(glob_cells(w, g));
    }
    lfn_t *f = &w->fn[fi];
    ln(w, t0);
    tw_name(w, t0);
    tws(w, "(");
    w->t += 2;
    TRY(em(w, ZL_LINK, w->infn ? 1 : 0, 0));
    uint32_t nargs = 0;
    while (!at(w, K_RP)) {
        if (nargs) {
            TRY(expect(w, K_COMMA, "',' or ')' expected"));
            tws(w, ", ");
        }
        uint32_t a0 = w->t;
        int a;
        TRY(value(w, &a));
        ty_mark(w, a);
        TRY(set_ptype(w, f, nargs, a, a0));
        nargs++;
    }
    next(w);
    tws(w, ")");
    ln(w, t0);
    if (f->np == NP_UNKNOWN)
        w->unknown = 1; /* defined further on: the next pass checks it */
    else if (nargs != f->np)
        return lfail_at(
            w, t0, ZL_E_UNSUPPORTED,
            "a call with a different number of arguments than parameters is outside the subset");
    f->ncall++;
    if (g) TRY(guard(w, g->flag));
    int32_t c = zl_emit(w->c, ZL_CALL, f->entry >= 0 ? f->entry : 0, 0);
    if (c < 0) return status(w);
    if (f->entry < 0) {
        if (w->nfix >= LX_MAX)
            return lfail_at(w, t0, ZL_E_LIMIT, "too many calls before definitions");
        w->fix[w->nfix].at = c;
        w->fix[w->nfix].fn = fi;
        w->fix[w->nfix].line = w->tok[t0].line;
        w->nfix++;
    }
    *ty = ty_mark(w, f->type);
    if (want && *ty == TY_NONE)
        return lfail_at(w, t0, ZL_E_UNSUPPORTED, "the function returns no value");
    return ZL_OK;
}

/* ------------------------------------------------------------ statements */

static int block(lw_t *w, int *always, int keep);
static int statement(lw_t *w, int *always);

static int block_end(int k)
{
    return k == K_END || k == K_ELSE || k == K_ELSEIF || k == K_UNTIL || k == K_EOF;
}

/* A condition: a boolean jumps when false (*jz is the jump to patch); a
 * number is always true and only runs for its effects (*jz = -1). */
static int cond(lw_t *w, int32_t *jz)
{
    uint32_t t0 = w->t;
    int ty;
    TRY(value(w, &ty));
    ln(w, t0);
    if (ty == TY_INT) {
        *jz = -1;
        return em(w, ZL_DROP, 0, 0);
    }
    *jz = zl_emit(w->c, ZL_JZ, 0, 0);
    return *jz < 0 ? status(w) : ZL_OK;
}

static void patch_here(lw_t *w, int32_t j)
{
    if (j >= 0) zl_patch(w->c, j, here_pc(w));
}

static int end_kw(lw_t *w, int k, const char *msg)
{
    bol(w);
    if (!at(w, k)) return lfail(w, ZL_E_SYNTAX, msg);
    tws(w, kspell[k]);
    next(w);
    return ZL_OK;
}

static int loop_begin(lw_t *w)
{
    int32_t b = w->brk0;
    w->brk0 = (int32_t) w->nbrk;
    return b;
}

static void loop_end(lw_t *w, int32_t saved)
{
    for (uint32_t i = (uint32_t) w->brk0; i < w->nbrk; i++) zl_patch(w->c, w->brk[i], here_pc(w));
    w->nbrk = (uint32_t) w->brk0;
    w->brk0 = saved;
}

static int if_stmt(lw_t *w, int *always)
{
    int32_t ends[LE_MAX], jz;
    uint32_t nends = 0;
    int a, all, has_else = 0;
    bol(w);
    tws(w, "if ");
    next(w);
    TRY(cond(w, &jz));
    TRY(expect(w, K_THEN, "'then' expected"));
    tws(w, " then");
    TRY(eol(w));
    w->depth++;
    TRY(block(w, &a, 0));
    w->depth--;
    all = a;
    while (at(w, K_ELSEIF) || at(w, K_ELSE)) {
        if (nends >= LE_MAX) return lfail(w, ZL_E_LIMIT, "too many elseif arms");
        ln(w, w->t);
        ends[nends] = zl_emit(w->c, ZL_JMP, 0, 0);
        if (ends[nends++] < 0) return status(w);
        patch_here(w, jz);
        jz = -1;
        bol(w);
        if (at(w, K_ELSE)) {
            tws(w, "else");
            next(w);
            TRY(eol(w));
            has_else = 1;
        } else {
            tws(w, "elseif ");
            next(w);
            TRY(cond(w, &jz));
            TRY(expect(w, K_THEN, "'then' expected"));
            tws(w, " then");
            TRY(eol(w));
        }
        w->depth++;
        TRY(block(w, &a, 0));
        w->depth--;
        all = all && a;
        if (has_else) break;
    }
    patch_here(w, jz);
    TRY(end_kw(w, K_END, "'end' expected"));
    TRY(eol(w));
    for (uint32_t i = 0; i < nends; i++) patch_here(w, ends[i]);
    *always = has_else && all;
    return ZL_OK;
}

static int while_stmt(lw_t *w)
{
    int32_t jz, top = here_pc(w);
    int a;
    bol(w);
    tws(w, "while ");
    next(w);
    TRY(cond(w, &jz));
    TRY(expect(w, K_DO, "'do' expected"));
    tws(w, " do");
    TRY(eol(w));
    int32_t saved = loop_begin(w);
    w->depth++;
    TRY(block(w, &a, 0));
    w->depth--;
    TRY(em(w, ZL_JMP, top, 0));
    patch_here(w, jz);
    loop_end(w, saved);
    TRY(end_kw(w, K_END, "'end' expected"));
    return eol(w);
}

static int repeat_stmt(lw_t *w)
{
    uint32_t mark = w->nvar, foff = w->foff;
    int32_t top = here_pc(w);
    int a, ty;
    bol(w);
    tws(w, "repeat");
    next(w);
    TRY(eol(w));
    int32_t saved = loop_begin(w);
    w->depth++;
    TRY(block(w, &a, 1));
    w->depth--;
    TRY(end_kw(w, K_UNTIL, "'until' expected"));
    tws(w, " ");
    uint32_t t0 = w->t;
    TRY(value(w, &ty));
    ln(w, t0);
    TRY(em(w, ty == TY_INT ? ZL_DROP : ZL_JZ, ty == TY_INT ? 0 : top, 0));
    TRY(eol(w));
    w->nvar = mark;
    w->foff = foff;
    loop_end(w, saved);
    return ZL_OK;
}

static int do_stmt(lw_t *w, int *always)
{
    bol(w);
    tws(w, "do");
    next(w);
    TRY(eol(w));
    w->depth++;
    TRY(block(w, always, 0));
    w->depth--;
    TRY(end_kw(w, K_END, "'end' expected"));
    return eol(w);
}

/* for v = a, b[, s] do ... end, with Lua 5.4's count: the loop runs while
 * the next value would not pass the limit, so it never steps past it. */
static int for_stmt(lw_t *w)
{
    uint32_t mark = w->nvar, foff = w->foff, t0 = w->t;
    int32_t cur, lim, stp = 0, ex[4];
    uint32_t nex = 0;
    int64_t step = 1;
    int dyn = 0, has_step = 0, ty, a;
    bol(w);
    tws(w, "for ");
    next(w);
    uint32_t vt = w->t;
    if (!at(w, K_NAME)) return lfail(w, ZL_E_SYNTAX, "a name expected after 'for'");
    if (peek_k(w, 1) == K_COMMA || peek_k(w, 1) == K_IN)
        return lfail(w, ZL_E_UNSUPPORTED, "the generic for (in) is outside the subset");
    tw_name(w, vt);
    next(w);
    TRY(expect(w, K_ASSIGN, "'=' expected"));
    tws(w, " = ");
    uint32_t e0 = w->t;
    TRY(value(w, &ty));
    if (ty == TY_BOOL) return lfail_at(w, e0, ZL_E_UNSUPPORTED, "a for loop's bounds are numbers");
    TRY(expect(w, K_COMMA, "',' expected"));
    tws(w, ", ");
    e0 = w->t;
    TRY(value(w, &ty));
    if (ty == TY_BOOL) return lfail_at(w, e0, ZL_E_UNSUPPORTED, "a for loop's bounds are numbers");
    if (at(w, K_COMMA)) {
        next(w);
        tws(w, ", ");
        e0 = w->t;
        int32_t before = here_pc(w);
        TRY(value(w, &ty));
        if (ty == TY_BOOL)
            return lfail_at(w, e0, ZL_E_UNSUPPORTED, "a for loop's step is a number");
        const zl_ins_t *code = w->c->p->code;
        int32_t after = here_pc(w);
        if (after == before + 1 && code[before].op == ZL_PUSH)
            step = code[before].a;
        else if (after == before + 2 && code[before].op == ZL_PUSH &&
                 code[before + 1].op == ZL_NEG && code[before].a != INT64_MIN)
            step = -code[before].a;
        else
            dyn = 1;
        has_step = 1;
    }
    TRY(expect(w, K_DO, "'do' expected"));
    tws(w, " do");
    TRY(eol(w));
    ln(w, t0);
    TRY(new_cell(w, &cur));
    TRY(new_cell(w, &lim));
    if (has_step) {
        TRY(new_cell(w, &stp));
        TRY(st_cell(w, stp));
    }
    TRY(st_cell(w, lim));
    TRY(st_cell(w, cur));
    if (dyn) {
        TRY(ld_cell(w, stp));
        int32_t j = zl_emit(w->c, ZL_JNZ, 0, 0);
        if (j < 0) return status(w);
        TRY(em(w, ZL_TRAP, ZL_E_DOMAIN, 0)); /* 'for' step is zero */
        patch_here(w, j);
    } else if (step == 0) {
        TRY(em(w, ZL_TRAP, ZL_E_DOMAIN, 0));
    }
    /* the first test: start past the limit runs nothing */
    int32_t jneg = -1, jbody = -1;
    if (dyn) {
        TRY(ld_cell(w, stp));
        TRY(em(w, ZL_PUSH, 0, 0));
        TRY(em(w, ZL_GT, 0, 0));
        jneg = zl_emit(w->c, ZL_JZ, 0, 0);
        if (jneg < 0) return status(w);
    }
    if (dyn || step > 0) {
        TRY(ld_cell(w, cur));
        TRY(ld_cell(w, lim));
        TRY(em(w, ZL_GT, 0, 0));
        ex[nex] = zl_emit(w->c, ZL_JNZ, 0, 0);
        if (ex[nex++] < 0) return status(w);
    }
    if (dyn) {
        jbody = zl_emit(w->c, ZL_JMP, 0, 0);
        if (jbody < 0) return status(w);
        patch_here(w, jneg);
    }
    if (dyn || step < 0) {
        TRY(ld_cell(w, cur));
        TRY(ld_cell(w, lim));
        TRY(em(w, ZL_LT, 0, 0));
        ex[nex] = zl_emit(w->c, ZL_JNZ, 0, 0);
        if (ex[nex++] < 0) return status(w);
    }
    patch_here(w, jbody);
    int32_t body = here_pc(w);
    TRY(declare(w, vt, w->infn ? V_FRAME : V_SLOT, cur >= 0 ? cur : -1 - cur, TY_INT, 1));
    int32_t saved = loop_begin(w);
    w->depth++;
    TRY(block(w, &a, 0));
    w->depth--;
    ln(w, t0);
    /* the next test: (lim - cur) - step < 0 going up, (cur - lim) + step < 0 going down */
    jneg = jbody = -1;
    if (dyn) {
        TRY(ld_cell(w, stp));
        TRY(em(w, ZL_PUSH, 0, 0));
        TRY(em(w, ZL_GT, 0, 0));
        jneg = zl_emit(w->c, ZL_JZ, 0, 0);
        if (jneg < 0) return status(w);
    }
    for (int down = 0; down < 2; down++) {
        if (!(dyn || (down ? step < 0 : step > 0))) continue;
        if (down && dyn) patch_here(w, jneg);
        TRY(ld_cell(w, down ? cur : lim));
        TRY(ld_cell(w, down ? lim : cur));
        TRY(em(w, ZL_SUB, 0, 0));
        TRY(has_step ? ld_cell(w, stp) : em(w, ZL_PUSH, step, 0));
        TRY(em(w, down ? ZL_ADD : ZL_SUB, 0, 0));
        TRY(em(w, ZL_PUSH, 0, 0));
        TRY(em(w, ZL_LT, 0, 0));
        ex[nex] = zl_emit(w->c, ZL_JNZ, 0, 0);
        if (ex[nex++] < 0) return status(w);
        if (!down && dyn) {
            jbody = zl_emit(w->c, ZL_JMP, 0, 0);
            if (jbody < 0) return status(w);
        }
    }
    patch_here(w, jbody);
    TRY(ld_cell(w, cur));
    TRY(has_step ? ld_cell(w, stp) : em(w, ZL_PUSH, step, 0));
    TRY(em(w, ZL_ADD, 0, 0));
    TRY(st_cell(w, cur));
    TRY(em(w, ZL_JMP, body, 0));
    for (uint32_t i = 0; i < nex; i++) patch_here(w, ex[i]);
    loop_end(w, saved);
    w->nvar = mark;
    w->foff = foff;
    TRY(end_kw(w, K_END, "'end' expected"));
    return eol(w);
}

static int names_list(lw_t *w, uint32_t *names, uint32_t *nn, int local)
{
    *nn = 0;
    for (;;) {
        if (!at(w, K_NAME)) return lfail(w, ZL_E_SYNTAX, "a name expected");
        if (*nn >= LA_MAX) return lfail(w, ZL_E_LIMIT, "too many names in one statement");
        int k1 = peek_k(w, 1);
        if (!local && (k1 == K_DOT || k1 == K_LBRACK || k1 == K_COLON))
            return lfail_at(w, w->t + 1, ZL_E_UNSUPPORTED,
                            "tables and fields are outside the subset");
        if (local && k1 == K_LT)
            return lfail_at(w, w->t + 1, ZL_E_UNSUPPORTED,
                            "local attributes are outside the subset");
        if (*nn) tws(w, ", ");
        tw_name(w, w->t);
        names[(*nn)++] = w->t;
        next(w);
        if (!at(w, K_COMMA)) return ZL_OK;
        next(w);
    }
}

static int values_list(lw_t *w, uint8_t *types, uint32_t want, uint32_t t0)
{
    uint32_t n = 0;
    for (;;) {
        int ty;
        if (n >= LA_MAX) return lfail(w, ZL_E_LIMIT, "too many values in one statement");
        if (n) tws(w, ", ");
        TRY(value(w, &ty));
        types[n++] = (uint8_t) ty_mark(w, ty);
        if (!at(w, K_COMMA)) break;
        next(w);
    }
    if (n != want)
        return lfail_at(w, t0, ZL_E_UNSUPPORTED,
                        "as many values as names: Lua fills or drops with nil");
    return ZL_OK;
}

static int check_type(lw_t *w, uint32_t t, uint8_t *slot, int ty)
{
    if (ty == TY_UNK) return ZL_OK;
    if (*slot == TY_UNK)
        *slot = (uint8_t) ty;
    else if (*slot != ty)
        return lfail_at(w, t, ZL_E_UNSUPPORTED,
                        "a name that holds both numbers and booleans is outside the subset");
    return ZL_OK;
}

static int local_stmt(lw_t *w)
{
    uint32_t names[LA_MAX], nn, t0 = w->t;
    uint8_t types[LA_MAX];
    bol(w);
    tws(w, "local ");
    next(w);
    TRY(names_list(w, names, &nn, 1));
    if (!at(w, K_ASSIGN))
        return lfail_at(w, names[nn - 1], ZL_E_UNSUPPORTED,
                        "a local without a value is nil; give it a value");
    tws(w, " = ");
    next(w);
    TRY(values_list(w, types, nn, t0));
    ln(w, t0);
    int32_t cells[LA_MAX];
    for (uint32_t i = 0; i < nn; i++) TRY(new_cell(w, &cells[i]));
    for (uint32_t i = nn; i > 0; i--) TRY(st_cell(w, cells[i - 1]));
    for (uint32_t i = 0; i < nn; i++)
        TRY(declare(w, names[i], w->infn ? V_FRAME : V_SLOT,
                    cells[i] >= 0 ? cells[i] : -1 - cells[i], types[i], 0));
    return eol(w);
}

static int assign_stmt(lw_t *w)
{
    uint32_t names[LA_MAX], nn, t0 = w->t;
    uint8_t types[LA_MAX];
    bol(w);
    TRY(names_list(w, names, &nn, 0));
    TRY(expect(w, K_ASSIGN, "'=' expected"));
    tws(w, " = ");
    TRY(values_list(w, types, nn, t0));
    ln(w, t0);
    for (uint32_t i = nn; i > 0; i--) {
        uint32_t t = names[i - 1];
        int vi = var_find(w, t);
        if (vi >= 0) {
            lvar_t *v = &w->var[vi];
            if (v->kind == V_FUNC)
                return lfail_at(w, t, ZL_E_UNSUPPORTED,
                                "assigning to a function is outside the subset");
            if (v->ro)
                return lfail_at(w, t, ZL_E_UNSUPPORTED,
                                "assigning to a for loop's variable is outside the subset");
            TRY(check_type(w, t, &v->type, types[i - 1]));
            TRY(st_cell(w, v->kind == V_SLOT ? v->idx : -1 - v->idx));
            continue;
        }
        lglob_t *g;
        TRY(glob_get(w, t, &g));
        if (g->isfn)
            return lfail_at(w, t, ZL_E_UNSUPPORTED,
                            "assigning to a function is outside the subset");
        g->isvar = 1;
        g->assigned = 1;
        TRY(check_type(w, t, &g->type, types[i - 1]));
        TRY(glob_cells(w, g));
        TRY(em(w, ZL_STORE, g->slot, 0));
        TRY(em(w, ZL_PUSH, 1, 0));
        TRY(em(w, ZL_STORE, g->flag, 0));
    }
    return eol(w);
}

/* print(...) and io.write(...): every value is evaluated first, then the
 * output is written, as Lua evaluates the arguments before the call. */
static int out_stmt(lw_t *w, int isprint)
{
    uint32_t t0 = w->t, tmpn = w->tmpn, foff = w->foff;
    bol(w);
    tws(w, isprint ? "print(" : "io.write(");
    w->t += isprint ? 2 : 4;
    w->npc = 0;
    w->strok = 1;
    uint32_t nargs = 0;
    while (!at(w, K_RP)) {
        if (nargs) {
            TRY(expect(w, K_COMMA, "',' or ')' expected"));
            tws(w, ", ");
            if (isprint) TRY(piece(w, P_TAB, 0));
        }
        uint32_t a0 = w->t;
        int ty;
        TRY(subexpr(w, 0, &ty));
        ln(w, a0);
        if (ty == TY_NONE)
            return lfail_at(w, a0, ZL_E_UNSUPPORTED, "the function returns no value");
        if (ty == TY_BOOL && !isprint)
            return lfail_at(w, a0, ZL_E_UNSUPPORTED, "io.write takes numbers and strings");
        if (ty != TY_STR) TRY(stash(w, ty == TY_BOOL ? P_BOOL : P_INT));
        nargs++;
    }
    next(w);
    tws(w, ")");
    w->strok = 0;
    ln(w, t0);
    for (uint32_t i = 0; i < w->npc; i++) {
        int32_t v = w->pc[i].v;
        switch (w->pc[i].kind) {
        case P_STR:
            TRY(em(w, ZL_OUTS, v, 0));
            break;
        case P_TAB:
            TRY(em(w, ZL_PUSH, '\t', 0));
            TRY(em(w, ZL_OUTC, 0, 0));
            break;
        case P_INT:
            TRY(ld_cell(w, v));
            TRY(em(w, ZL_OUTI, 0, 0));
            break;
        default: {
            int32_t st = zl_addstr(w->c, "true", 4), sf = zl_addstr(w->c, "false", 5);
            if (st < 0 || sf < 0) return status(w);
            TRY(ld_cell(w, v));
            int32_t jf = zl_emit(w->c, ZL_JZ, 0, 0);
            if (jf < 0) return status(w);
            TRY(em(w, ZL_OUTS, st, 0));
            int32_t je = zl_emit(w->c, ZL_JMP, 0, 0);
            if (je < 0) return status(w);
            patch_here(w, jf);
            TRY(em(w, ZL_OUTS, sf, 0));
            patch_here(w, je);
            break;
        }
        }
    }
    if (isprint) TRY(em(w, ZL_NL, 0, 0));
    w->npc = 0;
    w->tmpn = tmpn;
    w->foff = foff;
    return eol(w);
}

static int call_stmt(lw_t *w)
{
    uint32_t t0 = w->t;
    int ty;
    bol(w);
    if (name_is(w, t0, "io") || name_is(w, t0, "math"))
        TRY(lib_expr(w, &ty, 0));
    else
        TRY(call(w, &ty, 0));
    ln(w, t0);
    if (ty != TY_NONE) TRY(em(w, ZL_DROP, 0, 0));
    return eol(w);
}

static int funcdef(lw_t *w, int local)
{
    uint32_t t0 = w->t;
    if (w->infn || w->blockdepth > 0)
        return lfail(w, ZL_E_UNSUPPORTED, "functions are defined at the top level of the chunk");
    bol(w);
    tws(w, local ? "local function " : "function ");
    w->t += local ? 2 : 1;
    uint32_t nt = w->t;
    if (!at(w, K_NAME)) return lfail(w, ZL_E_SYNTAX, "a function name expected");
    if (peek_k(w, 1) == K_DOT || peek_k(w, 1) == K_COLON)
        return lfail_at(w, nt + 1, ZL_E_UNSUPPORTED, "a function in a table is outside the subset");
    if (is_builtin(w, nt) || is_lib(w, nt))
        return lfail(w, ZL_E_UNSUPPORTED, "a Lua library name outside the subset");
    int32_t fi;
    lglob_t *g = 0;
    if (local) {
        TRY(fn_by_pos(w, nt, &fi));
    } else {
        TRY(glob_get(w, nt, &g));
        if (g->isvar)
            return lfail(w, ZL_E_UNSUPPORTED, "a name that is both a variable and a function");
        if (g->fn >= 0 && w->fn[g->fn].pos == UINT32_MAX) w->fn[g->fn].pos = w->tok[nt].pos;
        if (g->fn >= 0 && w->fn[g->fn].pos != w->tok[nt].pos)
            return lfail(w, ZL_E_UNSUPPORTED, "a function defined twice is outside the subset");
        if (g->fn < 0) TRY(fn_by_pos(w, nt, &g->fn));
        g->isfn = 1;
        fi = g->fn;
        TRY(glob_cells(w, g));
    }
    lfn_t *f = &w->fn[fi];
    if (f->defined)
        return lfail(w, ZL_E_UNSUPPORTED, "a function defined twice is outside the subset");
    tw_name(w, nt);
    next(w);
    TRY(expect(w, K_LP, "'(' expected"));
    tws(w, "(");
    uint32_t params[LA_MAX], np = 0;
    while (!at(w, K_RP)) {
        if (np) {
            TRY(expect(w, K_COMMA, "',' or ')' expected"));
            tws(w, ", ");
        }
        if (at(w, K_DOTS)) return lfail(w, ZL_E_UNSUPPORTED, "varargs are outside the subset");
        if (!at(w, K_NAME)) return lfail(w, ZL_E_SYNTAX, "a parameter name expected");
        if (np >= LA_MAX) return lfail(w, ZL_E_LIMIT, "too many parameters");
        for (uint32_t j = 0; j < np; j++)
            if (same_name(w, w->tok[params[j]].pos, w->tok[params[j]].len, w->tok[w->t].pos,
                          w->tok[w->t].len))
                return lfail(w, ZL_E_UNSUPPORTED, "two parameters with one name");
        tw_name(w, w->t);
        params[np++] = w->t;
        next(w);
    }
    next(w);
    tws(w, ")");
    TRY(eol(w));
    if (f->np != NP_UNKNOWN && f->np != np)
        return lfail_at(
            w, nt, ZL_E_UNSUPPORTED,
            "a call with a different number of arguments than parameters is outside the subset");
    f->np = (uint8_t) np;
    ln(w, t0);
    int32_t jover = zl_emit(w->c, ZL_JMP, 0, 0);
    if (jover < 0) return status(w);
    f->entry = here_pc(w);
    f->defined = 1;
    if (local) TRY(declare(w, nt, V_FUNC, fi, TY_UNK, 1));
    uint32_t mark = w->nvar;
    w->infn = 1;
    w->cur_fn = fi;
    w->foff = w->fmax = np;
    w->ret_val = w->ret_none = 0;
    for (uint32_t i = 0; i < np; i++)
        TRY(declare(w, params[i], V_FRAME, (int32_t) i, ty_mark(w, f->ptype[i]), 0));
    int32_t enter = zl_emit(w->c, ZL_ENTER, 0, (int32_t) np);
    if (enter < 0) return status(w);
    int always;
    w->depth++;
    TRY(block(w, &always, 0));
    w->depth--;
    uint32_t et = w->t;
    TRY(end_kw(w, K_END, "'end' expected"));
    TRY(eol(w));
    ln(w, et);
    if (w->ret_val) {
        if (!always)
            return lfail_at(
                w, et, ZL_E_UNSUPPORTED,
                "a function that returns a value must return one on every path (Lua gives nil)");
        TRY(em(w, ZL_TRAP, ZL_E_DOMAIN, 0)); /* not reached */
    } else {
        TRY(em(w, ZL_LEAVE, 0, 0));
        TRY(em(w, ZL_RET, 0, 0));
        if (f->type == TY_UNK) f->type = TY_NONE;
    }
    w->c->p->code[enter].a = w->fmax;
    w->nvar = mark;
    w->infn = 0;
    w->foff = w->fmax = 0;
    patch_here(w, jover);
    if (g) {
        TRY(em(w, ZL_PUSH, 1, 0));
        TRY(em(w, ZL_STORE, g->flag, 0));
    }
    return ZL_OK;
}

static int ret_stmt(lw_t *w, int *always)
{
    uint32_t t0 = w->t;
    bol(w);
    tws(w, "return");
    next(w);
    ln(w, t0);
    if (block_end(w->tok[w->t].k) || at(w, K_SEMI)) {
        if (w->infn) {
            lfn_t *f = &w->fn[w->cur_fn];
            if (w->ret_val || f->type == TY_INT || f->type == TY_BOOL)
                return lfail_at(w, t0, ZL_E_UNSUPPORTED,
                                "a function that returns both a value and nothing");
            w->ret_none = 1;
            TRY(em(w, ZL_LEAVE, 0, 0));
            TRY(em(w, ZL_RET, 0, 0));
        } else {
            TRY(em(w, ZL_HALT, 0, 0));
        }
    } else {
        int ty;
        tws(w, " ");
        TRY(value(w, &ty));
        if (at(w, K_COMMA))
            return lfail(w, ZL_E_UNSUPPORTED, "a return of several values is outside the subset");
        ln(w, t0);
        if (w->infn) {
            lfn_t *f = &w->fn[w->cur_fn];
            if (w->ret_none || f->type == TY_NONE)
                return lfail_at(w, t0, ZL_E_UNSUPPORTED,
                                "a function that returns both a value and nothing");
            TRY(check_type(w, t0, &f->type, ty_mark(w, ty)));
            w->ret_val = 1;
            TRY(em(w, ZL_LEAVE, 0, 0));
            TRY(em(w, ZL_RET, 0, 0));
        } else {
            TRY(em(w, ZL_DROP, 0, 0));
            TRY(em(w, ZL_HALT, 0, 0));
        }
    }
    TRY(eol(w));
    if (!block_end(w->tok[w->t].k))
        return lfail(w, ZL_E_SYNTAX, "return must be the last statement of a block");
    *always = 1;
    return ZL_OK;
}

static int statement(lw_t *w, int *always)
{
    *always = 0;
    w->tmpn = 0;
    switch (w->tok[w->t].k) {
    case K_SEMI:
        next(w);
        return ZL_OK;
    case K_IF:
        return if_stmt(w, always);
    case K_WHILE:
        return while_stmt(w);
    case K_DO:
        return do_stmt(w, always);
    case K_FOR:
        return for_stmt(w);
    case K_REPEAT:
        return repeat_stmt(w);
    case K_FUNCTION:
        return funcdef(w, 0);
    case K_LOCAL:
        return peek_k(w, 1) == K_FUNCTION ? funcdef(w, 1) : local_stmt(w);
    case K_RETURN:
        return ret_stmt(w, always);
    case K_BREAK:
        if (w->brk0 < 0) return lfail(w, ZL_E_SYNTAX, "break outside a loop");
        if (w->nbrk >= LB_MAX) return lfail(w, ZL_E_LIMIT, "too many breaks");
        bol(w);
        tws(w, "break");
        ln(w, w->t);
        next(w);
        w->brk[w->nbrk] = zl_emit(w->c, ZL_JMP, 0, 0);
        if (w->brk[w->nbrk++] < 0) return status(w);
        return eol(w);
    case K_GOTO:
    case K_DCOLON:
        return lfail(w, ZL_E_UNSUPPORTED, "goto and labels are outside the subset");
    case K_NAME: {
        uint32_t t0 = w->t;
        int k1 = peek_k(w, 1);
        if (name_is(w, t0, "print") && k1 == K_LP) return out_stmt(w, 1);
        if (name_is(w, t0, "io") && k1 == K_DOT && peek_k(w, 3) == K_LP &&
            name_is(w, t0 + 2, "write"))
            return out_stmt(w, 0);
        if ((name_is(w, t0, "io") || name_is(w, t0, "math")) && k1 == K_DOT)
            return peek_k(w, 3) == K_LP ? call_stmt(w)
                                        : lfail_at(w, t0, ZL_E_SYNTAX, "a call expected");
        if (k1 == K_LP) return call_stmt(w);
        if (k1 == K_ASSIGN || k1 == K_COMMA) return assign_stmt(w);
        if (k1 == K_DOT || k1 == K_LBRACK || k1 == K_COLON)
            return lfail_at(w, t0 + 1, ZL_E_UNSUPPORTED,
                            "tables, fields and methods are outside the subset");
        if (k1 == K_STR || k1 == K_LBRACE)
            return lfail_at(w, t0 + 1, ZL_E_UNSUPPORTED,
                            "a call without parentheses is outside the subset");
        return lfail_at(w, t0 + 1, ZL_E_SYNTAX, "'=' or a call expected");
    }
    default:
        return lfail(w, ZL_E_SYNTAX, "a statement expected");
    }
}

static int block(lw_t *w, int *always, int keep)
{
    uint32_t mark = w->nvar, foff = w->foff;
    TRY(nest_in(w));
    w->blockdepth++;
    *always = 0;
    while (!block_end(w->tok[w->t].k)) {
        int a;
        TRY(statement(w, &a));
        if (a) *always = 1;
    }
    w->blockdepth--;
    w->nest--;
    if (!keep) {
        w->nvar = mark;
        w->foff = foff;
    }
    return ZL_OK;
}

static int chunk(lw_t *w)
{
    while (!at(w, K_EOF)) {
        int a;
        if (block_end(w->tok[w->t].k)) return lfail(w, ZL_E_SYNTAX, "'<eof>' expected");
        TRY(statement(w, &a));
    }
    flush_comments(w, w->t);
    w->c->line = w->tok[w->t].line;
    TRY(em(w, ZL_HALT, 0, 0));
    for (uint32_t i = 0; i < w->nglob; i++) {
        const lglob_t *g = &w->glob[i];
        if (g->isfn && g->fn >= 0 && !w->fn[g->fn].defined && g->use_line) {
            w->c->line = g->use_line;
            w->c->col = 0;
            return zl_fail(w->c, ZL_E_UNDEFINED, "a call to a function the program never defines");
        }
        if (g->isvar && !g->assigned && g->use_line) {
            w->c->line = g->use_line;
            w->c->col = 0;
            return zl_fail(w->c, ZL_E_UNDEFINED,
                           "a global that is read but never assigned (it would be nil)");
        }
    }
    for (uint32_t i = 0; i < w->nfix; i++) {
        const lfn_t *f = &w->fn[w->fix[i].fn];
        if (f->entry < 0) {
            w->c->line = w->fix[i].line;
            w->c->col = 0;
            return zl_fail(w->c, ZL_E_UNDEFINED, "a call to a function the program never defines");
        }
        zl_patch(w->c, w->fix[i].at, f->entry);
    }
    return ZL_OK;
}

/* One pass over the tokens; the tables of types carry over between passes. */
static int pass(lw_t *w, zl_out_t *tw)
{
    zl_prog_t *p = w->c->p;
    int lang = p->lang;
    zl_bzero(p, sizeof *p);
    zl_cc_init(w->c, p, w->c->d, lang, 64);
    for (uint32_t i = 0; i < w->nfn; i++) {
        w->fn[i].entry = -1;
        w->fn[i].defined = 0;
        w->fn[i].ncall = 0;
    }
    for (uint32_t i = 0; i < w->nglob; i++) {
        w->glob[i].slot = w->glob[i].flag = -1;
        w->glob[i].assigned = 0;
        w->glob[i].use_line = 0;
    }
    w->tw = tw;
    w->t = w->cnext = w->nvar = w->nfix = w->nbrk = w->npc = w->ntmp = w->tmpn = 0;
    w->brk0 = -1;
    w->infn = w->strok = w->unknown = w->tw_any = w->blockdepth = 0;
    w->foff = w->fmax = w->depth = w->nest = 0;
    return chunk(w);
}

int zl_lua_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    lw_t *w = (lw_t *) work;
    zl_bzero(w, sizeof *w);
    w->c = c;
    w->s = src;
    w->n = len;
    TRY(lex(w));
    for (int k = 0;; k++) {
        TRY(pass(w, 0));
        if (!w->unknown) break;
        if (k + 1 == L_PASSES)
            return lfail_at(
                w, 0, ZL_E_UNSUPPORTED,
                "a type that depends only on itself (a function that never returns a known value)");
        /* a parameter no call ever passes is a number */
        for (uint32_t i = 0; i < w->nfn; i++)
            if (!w->fn[i].ncall)
                for (uint32_t j = 0; j < LA_MAX; j++)
                    if (w->fn[i].ptype[j] == TY_UNK) w->fn[i].ptype[j] = TY_INT;
    }
    return tw ? pass(w, tw) : ZL_OK;
}
