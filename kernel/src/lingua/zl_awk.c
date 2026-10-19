/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_awk.c — AWK front-end: the integer subset of POSIX awk (One True Awk
 * as BSD and Linux ship it), and its twin "Fielddrake". See LINGUA_SPEC.md.
 *
 * Admitted: BEGIN { ... } and END { ... } blocks (any number, run in
 * order); integers; variables (they start at 0); = += -= *= %= and ++ --;
 * + - * %; unary - and !; == != < <= > >=; && || !; the ternary ?:;
 * if/else, while, for (init; cond; incr), break, continue, exit; print and
 * printf of numbers, string literals and their adjacency concatenation;
 * int(x); % (truncating, as awk), ^ and / refused (they give floats).
 *
 * A comparison in a print or printf argument must be parenthesised — the
 * reference grammar refuses it bare, so Lingua does too. Assignment is a
 * statement, not a value. Strings live only in print and printf arguments.
 *
 * Refused by name: the record model ($n, NF, NR, RS/FS/OFS/ORS, getline,
 * next, patterns over records), regex (~ and !~), arrays and for (i in a),
 * function definitions, length/substr/sprintf and the string functions,
 * do-while (not in POSIX awk), and floats.
 *
 * [ref differs] awk's numbers are arbitrary precision and it wraps on
 * integer overflow in practice; Lingua traps ZL_E_OVERFLOW past 63 bits.
 *
 * Fielddrake (the twin) is the same program in one spelling: one statement
 * per line, two spaces of indentation per block, ';' dropped, one space
 * around binary operators and after ',', none inside parentheses (kept as
 * written), integers in decimal, strings in double quotes (escapes \\ \"
 * \n \t), and every single-statement body braced. '#' comments keep their
 * text; a run of blank lines becomes one. */
#include "zl_int.h"

/* ------------------------------------------------------------ tokens */

enum {
    K_EOF,
    K_NL,
    K_NAME,
    K_INT,
    K_STR,
    K_IF,
    K_ELSE,
    K_WHILE,
    K_FOR,
    K_BREAK,
    K_CONTINUE,
    K_EXIT,
    K_PRINT,
    K_PRINTF,
    K_BEGIN,
    K_END,
    K_IN,
    K_FUNCTION,
    K_PLUS,
    K_MINUS,
    K_STAR,
    K_PERCENT,
    K_SLASH,
    K_POW,
    K_BANG,
    K_TILDE,
    K_BANGTILDE,
    K_LT,
    K_LE,
    K_EQ,
    K_NE,
    K_GE,
    K_GT,
    K_ANDAND,
    K_OROR,
    K_QUEST,
    K_COLON,
    K_ASSIGN,
    K_PLUSEQ,
    K_MINUSEQ,
    K_STAREQ,
    K_PERCENTEQ,
    K_INCR,
    K_DECR,
    K_LP,
    K_RP,
    K_LB,
    K_RB,
    K_LBRACK,
    K_RBRACK,
    K_COMMA,
    K_SEMI,
    K_DOLLAR,
    K__COUNT
};

static const char *const kspell[K__COUNT] = {
    "<eof>", "<newline>", "<name>", "<number>", "<string>", "if",     "else",   "while", "for",
    "break", "continue",  "exit",   "print",   "printf",  "BEGIN",   "END",    "in",    "function",
    "+",     "-",         "*",      "%",       "/",       "^",       "!",      "~",     "!~",
    "<",     "<=",        "==",     "!=",      ">=",      ">",       "&&",     "||",    "?",
    ":",     "=",         "+=",     "-=",      "*=",      "%=",      "++",     "--",    "(",
    ")",     "{",         "}",      "[",       "]",       ",",       ";",     "$",
};

typedef struct {
    uint8_t k, blank;
    uint16_t pad;
    uint32_t pos, len;
    uint32_t line, col;
    int64_t v; /* K_INT: the value; K_STR: offset of the decoded bytes in sbuf */
} atok_t;

typedef struct {
    uint32_t pos, len;
    uint32_t tok;
    uint32_t line;
    uint8_t trailing, blank;
    uint16_t pad;
} acom_t;

/* ------------------------------------------------------------ the compiler */

enum { P_STR = 1, P_INT, P_TAB, P_NL };

#define AT_MAX  4096
#define AC_MAX  512
#define AS_MAX  8192
#define AV_MAX  256
#define AX_MAX  512
#define AB_MAX  256
#define AP_MAX  128
#define AM_MAX  64

typedef struct {
    zl_cc_t *c;
    zl_out_t *tw;
    const char *s;
    size_t n;
    atok_t tok[AT_MAX];
    uint32_t ntok, t;
    acom_t com[AC_MAX];
    uint32_t ncom, cnext;
    char sbuf[AS_MAX];
    uint32_t nsbuf;
    struct {
        uint32_t pos, len;
        int32_t cell; /* slot (>=0) or frame offset (-1 - off); frames unused */
    } var[AV_MAX];
    uint32_t nvar;
    struct {
        int32_t at;
        int32_t cell;
        int op; /* 0 jz, 1 jmp */
    } fix[AX_MAX];
    uint32_t nfix;
    int32_t brk[AB_MAX];
    uint32_t nbrk;
    int32_t brk0, cnt0;
    struct {
        uint8_t kind;
        int32_t v;
    } pc[AP_MAX];
    uint32_t npc;
    int32_t tmp[AM_MAX];
    uint32_t ntmp, tmpn;
    int strok, nocmp, unknown, tw_any, blockdepth, loopdepth;
    uint32_t depth, nest;
} aw_t;

ZL_STATIC_ASSERT(sizeof(aw_t) <= sizeof(((zl_work_t *) 0)->fe), aw_fits);

#define TRY(x)                                                                                     \
    do {                                                                                           \
        int s_ = (x);                                                                              \
        if (s_ != ZL_OK) return s_;                                                                \
    } while (0)

static int status(aw_t *w)
{
    return w->c->failed ? w->c->d->status : ZL_E_LIMIT;
}

static int lfail_at(aw_t *w, uint32_t t, int st, const char *msg)
{
    w->c->line = w->tok[t].line;
    w->c->col = w->tok[t].col;
    return zl_fail(w->c, st, msg);
}

static int lfail(aw_t *w, int st, const char *msg)
{
    return lfail_at(w, w->t, st, msg);
}

static int em(aw_t *w, int op, int64_t a, int32_t b)
{
    return zl_emit(w->c, op, a, b) < 0 ? status(w) : ZL_OK;
}

static int32_t here_pc(aw_t *w)
{
    return (int32_t) w->c->p->ncode;
}

static void ln(aw_t *w, uint32_t t)
{
    w->c->line = w->tok[t].line;
}

/* ------------------------------------------------------------ lexing */

static int lex_fail(aw_t *w, uint32_t line, size_t at, size_t bol, int st, const char *msg)
{
    w->c->line = line;
    w->c->col = (uint32_t) (at - bol) + 1;
    return zl_fail(w->c, st, msg);
}

static int lex(aw_t *w)
{
    const char *s = w->s;
    size_t n = w->n, i = 0, bol = 0;
    uint32_t line = 1, last_line = 0;
    for (;;) {
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\f' || s[i] == '\v'))
            i++;
        if (i < n && s[i] == '\n') {
            if (w->ntok >= AT_MAX - 1) return lex_fail(w, line, i, bol, ZL_E_LIMIT, "program too long");
            /* newline ends a statement, like ';' does */
            atok_t *t = &w->tok[w->ntok];
            t->pos = (uint32_t) i;
            t->line = line;
            t->col = (uint32_t) (i - bol) + 1;
            t->blank = 0;
            t->k = K_NL;
            t->len = 1;
            w->ntok++;
            line++;
            bol = i + 1;
            i++;
            continue;
        }
        if (i >= n) break;
        if (s[i] == '#') {
            size_t st = i + 1, e = st;
            while (e < n && s[e] != '\n') e++;
            size_t te = e;
            while (te > st && (s[te - 1] == ' ' || s[te - 1] == '\t' || s[te - 1] == '\r')) te--;
            if (w->ncom >= AC_MAX) return lex_fail(w, line, i, bol, ZL_E_LIMIT, "too many comments");
            acom_t *m = &w->com[w->ncom++];
            m->pos = (uint32_t) st;
            m->len = (uint32_t) (te - st);
            m->tok = w->ntok;
            m->line = line;
            m->trailing = w->ntok && w->tok[w->ntok - 1].line == line;
            m->blank = last_line && line > last_line + 1;
            last_line = line;
            i = e;
            continue;
        }
        if (w->ntok >= AT_MAX - 1) return lex_fail(w, line, i, bol, ZL_E_LIMIT, "program too long");
        atok_t *t = &w->tok[w->ntok];
        t->pos = (uint32_t) i;
        t->line = line;
        t->col = (uint32_t) (i - bol) + 1;
        t->blank = last_line && line > last_line + 1;
        int ch = (unsigned char) s[i];
        if (zl_isdigit(ch)) {
            uint64_t v = 0;
            size_t st = i;
            while (i < n && zl_isdigit((unsigned char) s[i])) {
                uint64_t d = (uint64_t) (s[i] - '0');
                if (v > (UINT64_C(0x7fffffffffffffff) - d) / 10)
                    return lex_fail(w, line, st, bol, ZL_E_UNSUPPORTED, "an integer past 2^63-1");
                v = v * 10 + d;
                i++;
            }
            if (i < n && s[i] == '.')
                return lex_fail(w, line, st, bol, ZL_E_UNSUPPORTED, "a fraction is a float; outside the subset");
            if (i < n && (zl_isalpha((unsigned char) s[i]) || s[i] == '_'))
                return lex_fail(w, line, st, bol, ZL_E_SYNTAX, "malformed number");
            t->k = K_INT;
            t->v = (int64_t) v;
            t->len = (uint32_t) (i - st);
        } else if (zl_isalpha(ch) || ch == '_') {
            size_t st = i;
            while (i < n && (zl_isalnum((unsigned char) s[i]) || s[i] == '_')) i++;
            t->k = K_NAME;
            t->len = (uint32_t) (i - st);
            for (int k = K_IF; k <= K_FUNCTION; k++) {
                const char *kw = kspell[k];
                size_t j = 0;
                while (j < t->len && kw[j] && kw[j] == s[st + j]) j++;
                if (j == t->len && kw[j] == 0) {
                    t->k = (uint8_t) k;
                    break;
                }
            }
        } else if (ch == '"') {
            size_t st = i;
            uint32_t off = w->nsbuf;
            i++;
            for (;;) {
                if (i >= n || s[i] == '\n')
                    return lex_fail(w, line, st, bol, ZL_E_SYNTAX, "unfinished string");
                int b = (unsigned char) s[i];
                if (b == '"') {
                    i++;
                    break;
                }
                if (b == '\\') {
                    int e = i + 1 < n ? (unsigned char) s[i + 1] : 0;
                    b = e == 'n' ? '\n' : e == 't' ? '\t' : e == '\\' ? '\\' : e == '"' ? '"' : -1;
                    if (b < 0)
                        return lex_fail(w, line, i, bol, ZL_E_UNSUPPORTED,
                                        "only the escapes \\n \\t \\\\ \\\" are in this subset");
                    i += 2;
                } else {
                    if ((b < 0x20 && b != '\t') || b == 0x7f)
                        return lex_fail(w, line, i, bol, ZL_E_SYNTAX, "a control character in a string");
                    i++;
                }
                if (w->nsbuf >= AS_MAX) return lex_fail(w, line, st, bol, ZL_E_LIMIT, "too much string text");
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
                {"&&", K_ANDAND}, {"||", K_OROR},  {"++", K_INCR},   {"--", K_DECR},
                {"+=", K_PLUSEQ}, {"-=", K_MINUSEQ}, {"*=", K_STAREQ}, {"%=", K_PERCENTEQ},
                {"<=", K_LE},     {">=", K_GE},     {"==", K_EQ},    {"!=", K_NE},
                {"!~", K_BANGTILDE}, {"+", K_PLUS}, {"-", K_MINUS},  {"*", K_STAR},
                {"%", K_PERCENT}, {"/", K_SLASH},   {"^", K_POW},    {"!", K_BANG},
                {"~", K_TILDE},   {"<", K_LT},      {">", K_GT},     {"?", K_QUEST},
                {":", K_COLON},   {"=", K_ASSIGN},  {"(", K_LP},     {")", K_RP},
                {"{", K_LB},      {"}", K_RB},      {"[", K_LBRACK}, {"]", K_RBRACK},
                {",", K_COMMA},   {";", K_SEMI},   {"$", K_DOLLAR},
            };
            size_t k = 0, m = 0;
            for (; k < sizeof sym / sizeof sym[0]; k++) {
                m = 0;
                while (sym[k].sp[m] && i + m < n && s[i + m] == sym[k].sp[m]) m++;
                if (!sym[k].sp[m]) break;
            }
            if (k == sizeof sym / sizeof sym[0])
                return lex_fail(w, line, i, bol, ZL_E_SYNTAX, "a character awk does not use here");
            t->k = sym[k].k;
            t->len = (uint32_t) m;
            i += m;
        }
        w->ntok++;
        last_line = line;
    }
    atok_t *e = &w->tok[w->ntok];
    e->k = K_EOF;
    e->pos = (uint32_t) n;
    e->line = line;
    e->col = (uint32_t) (i - bol) + 1;
    w->ntok++;
    return ZL_OK;
}

/* ------------------------------------------------------------ helpers */

static int at(aw_t *w, int k)
{
    return w->tok[w->t].k == k;
}

static void next(aw_t *w)
{
    if (w->tok[w->t].k != K_EOF) w->t++;
}

static int name_is(aw_t *w, uint32_t t, const char *lit)
{
    const atok_t *k = &w->tok[t];
    if (k->k != K_NAME) return 0;
    uint32_t j = 0;
    while (j < k->len && lit[j] && w->s[k->pos + j] == lit[j]) j++;
    return j == k->len && lit[j] == 0;
}

static int same_name(aw_t *w, uint32_t pa, uint32_t la, uint32_t pb, uint32_t lb)
{
    if (la != lb) return 0;
    for (uint32_t j = 0; j < la; j++)
        if (w->s[pa + j] != w->s[pb + j]) return 0;
    return 1;
}

static int expect(aw_t *w, int k, const char *msg)
{
    if (!at(w, k)) return lfail(w, ZL_E_SYNTAX, msg);
    next(w);
    return ZL_OK;
}

static int nest_in(aw_t *w)
{
    if (++w->nest > ZL_MAX_DEPTH) return lfail(w, ZL_E_LIMIT, "nested too deeply");
    return ZL_OK;
}

/* ------------------------------------------------------------ the twin */

static void tws(aw_t *w, const char *s)
{
    if (w->tw) zl_puts(w->tw, s);
}

static void tw_text(aw_t *w, uint32_t pos, uint32_t len)
{
    if (w->tw) zl_put(w->tw, w->s + pos, len);
}

static void tw_name(aw_t *w, uint32_t t)
{
    tw_text(w, w->tok[t].pos, w->tok[t].len);
}

static void tw_str(aw_t *w, uint32_t t)
{
    if (!w->tw) return;
    const atok_t *k = &w->tok[t];
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

static void tw_indent(aw_t *w)
{
    for (uint32_t d = 0; d < w->depth; d++) tws(w, "  ");
}

static void flush_comments(aw_t *w, uint32_t upto)
{
    while (w->cnext < w->ncom && w->com[w->cnext].tok <= upto) {
        const acom_t *m = &w->com[w->cnext++];
        if (m->blank && w->tw_any) tws(w, "\n");
        tw_indent(w);
        tws(w, "#");
        tw_text(w, m->pos, m->len);
        tws(w, "\n");
        w->tw_any = 1;
    }
}

static void bol(aw_t *w)
{
    flush_comments(w, w->t);
    if (w->tok[w->t].blank && w->tw_any) tws(w, "\n");
    tw_indent(w);
    w->tw_any = 1;
}

/* The line's trailing '#' comment, if any, after `last` was consumed. A
 * statement must end with ';', a newline, '}' or the end of the program;
 * '{' opens a body instead (its line ends with the first statement). */
static int eol(aw_t *w, uint32_t last)
{
    if (w->tok[last].k != K_LB && !at(w, K_SEMI) && !at(w, K_NL) && !at(w, K_RB) && !at(w, K_EOF) &&
        !at(w, K_BEGIN) && !at(w, K_END))
        return lfail(w, ZL_E_SYNTAX, "a ';' or the end of the line expected");
    if (w->cnext < w->ncom && w->com[w->cnext].tok <= last) {
        w->c->line = w->com[w->cnext].line;
        w->c->col = 0;
        return zl_fail(w->c, ZL_E_UNSUPPORTED, "a comment inside a statement is outside the subset");
    }
    if (w->cnext < w->ncom && w->com[w->cnext].tok == last + 1 && w->com[w->cnext].trailing) {
        const acom_t *m = &w->com[w->cnext++];
        tws(w, " #");
        tw_text(w, m->pos, m->len);
    }
    tws(w, "\n");
    return ZL_OK;
}

/* ------------------------------------------------------------ cells */

static int new_cell(aw_t *w, int32_t *out)
{
    int32_t s = zl_slots(w->c, 1);
    if (s < 0) return status(w);
    *out = s;
    return ZL_OK;
}

static int tmp_cell(aw_t *w, int32_t *out)
{
    if (w->tmpn == w->ntmp) {
        if (w->ntmp >= AM_MAX) return lfail(w, ZL_E_LIMIT, "too many values in one statement");
        int32_t s = zl_slots(w->c, 1);
        if (s < 0) return status(w);
        w->tmp[w->ntmp++] = s;
    }
    *out = w->tmp[w->tmpn++];
    return ZL_OK;
}

static int piece(aw_t *w, int kind, int32_t v)
{
    if (w->npc >= AP_MAX) return lfail(w, ZL_E_LIMIT, "too many pieces in one print");
    w->pc[w->npc].kind = (uint8_t) kind;
    w->pc[w->npc].v = v;
    w->npc++;
    return ZL_OK;
}

static int stash(aw_t *w, int kind)
{
    int32_t c;
    TRY(tmp_cell(w, &c));
    TRY(em(w, ZL_STORE, c, 0));
    return piece(w, kind, c);
}

/* ------------------------------------------------------------ variables */

static int var_find(aw_t *w, uint32_t t)
{
    for (uint32_t i = w->nvar; i > 0; i--)
        if (same_name(w, w->var[i - 1].pos, w->var[i - 1].len, w->tok[t].pos, w->tok[t].len))
            return (int) i - 1;
    return -1;
}

static int var_cell(aw_t *w, uint32_t t, int32_t *cell, int write)
{
    int vi = var_find(w, t);
    if (vi >= 0) {
        *cell = w->var[vi].cell;
        return ZL_OK;
    }
    if (!write) { /* awk variables start at 0 */
        TRY(new_cell(w, cell));
        if (w->nvar >= AV_MAX) return lfail_at(w, t, ZL_E_LIMIT, "too many variables");
        w->var[w->nvar].pos = w->tok[t].pos;
        w->var[w->nvar].len = w->tok[t].len;
        w->var[w->nvar].cell = *cell;
        w->nvar++;
        return ZL_OK;
    }
    TRY(new_cell(w, cell));
    if (w->nvar >= AV_MAX) return lfail_at(w, t, ZL_E_LIMIT, "too many variables");
    w->var[w->nvar].pos = w->tok[t].pos;
    w->var[w->nvar].len = w->tok[t].len;
    w->var[w->nvar].cell = *cell;
    w->nvar++;
    return ZL_OK;
}

/* ------------------------------------------------------------ expressions */

static int expr(aw_t *w);
static int assign_stmt(aw_t *w, int want_val);

static int primary(aw_t *w)
{
    uint32_t t0 = w->t;
    const atok_t *k = &w->tok[t0];
    ln(w, t0);
    switch (k->k) {
    case K_INT:
        if (w->tw) zl_puti(w->tw, k->v);
        next(w);
        return em(w, ZL_PUSH, k->v, 0);
    case K_STR:
        if (!w->strok) return lfail(w, ZL_E_UNSUPPORTED, "a string outside print and printf");
        if (w->nocmp) return lfail(w, ZL_E_UNSUPPORTED, "a string in an expression");
        return lfail(w, ZL_E_UNSUPPORTED, "a string outside print and printf");
    case K_NAME:
        if (name_is(w, t0, "int") && w->tok[w->t + 1].k == K_LP) {
            tw_name(w, t0);
            tws(w, "(");
            w->t += 2;
            {
                int save = w->nocmp;
                w->nocmp = 0; /* int( ) admits a comparison */
                TRY(expr(w));
                w->nocmp = save;
            }
            TRY(expect(w, K_RP, "')' expected"));
            tws(w, ")");
            return ZL_OK; /* int of an integer is the integer */
        }
        if (w->tok[w->t + 1].k == K_LP) return lfail_at(w, t0, ZL_E_UNSUPPORTED, "this function is outside the subset");
        if (name_is(w, t0, "length") || name_is(w, t0, "substr") || name_is(w, t0, "sprintf") ||
            name_is(w, t0, "index") || name_is(w, t0, "match") || name_is(w, t0, "split") ||
            name_is(w, t0, "sub") || name_is(w, t0, "gsub") || name_is(w, t0, "tolower") ||
            name_is(w, t0, "toupper"))
            return lfail_at(w, t0, ZL_E_UNSUPPORTED, "the string functions are outside the subset");
        if (w->tok[w->t + 1].k == K_LBRACK)
            return lfail_at(w, t0, ZL_E_UNSUPPORTED, "arrays are outside the subset");
        {
            int32_t cell;
            tw_name(w, t0);
            next(w);
            TRY(var_cell(w, t0, &cell, 0));
            return em(w, ZL_LOAD, cell, 0);
        }
    case K_LP:
        tws(w, "(");
        next(w);
        {
            int save = w->nocmp;
            w->nocmp = 0; /* parentheses admit a comparison */
            TRY(expr(w));
            w->nocmp = save;
        }
        TRY(expect(w, K_RP, "')' expected"));
        tws(w, ")");
        return ZL_OK;
    case K_INCR:
    case K_DECR: {
        int op = k->k == K_INCR ? ZL_ADD : ZL_SUB;
        tws(w, kspell[k->k]);
        next(w);
        if (!at(w, K_NAME)) return lfail(w, ZL_E_SYNTAX, "a variable expected after ++ or --");
        int32_t cell;
        uint32_t vt = w->t;
        tw_name(w, vt);
        next(w);
        TRY(var_cell(w, vt, &cell, 1));
        TRY(em(w, ZL_LOAD, cell, 0));
        TRY(em(w, ZL_PUSH, 1, 0));
        TRY(em(w, op, 0, 0));
        TRY(em(w, ZL_STORE, cell, 0));
        return ZL_OK;
    }
    case K_LBRACK:
        return lfail(w, ZL_E_UNSUPPORTED, "arrays are outside the subset");
    case K_DOLLAR:
        return lfail(w, ZL_E_UNSUPPORTED, "the record model ($1, NF, NR) is outside the subset");
    case K_TILDE:
    case K_BANGTILDE:
        return lfail(w, ZL_E_UNSUPPORTED, "regex match is outside the subset");
    case K_NL:
        return lfail(w, ZL_E_SYNTAX, "an expression expected");
    default:
        return lfail(w, ZL_E_SYNTAX, "an expression expected");
    }
}

/* a binary level above unary: * % + - ; then a primary with postfix ++/-- */
static int add(aw_t *w);
static int mul(aw_t *w);

static int unary(aw_t *w)
{
    uint32_t t0 = w->t;
    if (at(w, K_MINUS) || at(w, K_BANG)) {
        int op = at(w, K_MINUS) ? ZL_NEG : ZL_NOT;
        if (at(w, K_MINUS) && w->tok[w->t + 1].k == K_MINUS) tws(w, "- ");
        else tws(w, kspell[w->tok[w->t].k]);
        next(w);
        TRY(unary(w));
        ln(w, t0);
        return em(w, op, 0, 0);
    }
    uint32_t vt = w->t;
    TRY(primary(w));
    /* postfix ++ / -- */
    if (at(w, K_INCR) || at(w, K_DECR)) {
        if (w->tok[vt].k != K_NAME) return lfail(w, ZL_E_UNSUPPORTED, "++ and -- need a variable");
        int32_t cell;
        TRY(var_cell(w, vt, &cell, 1));
        ln(w, vt);
        TRY(em(w, ZL_DUP, 0, 0)); /* keep the old value on the stack */
        TRY(em(w, ZL_PUSH, 1, 0));
        TRY(em(w, at(w, K_INCR) ? ZL_ADD : ZL_SUB, 0, 0));
        TRY(em(w, ZL_STORE, cell, 0));
        tws(w, kspell[w->tok[w->t].k]);
        next(w);
    }
    return ZL_OK;
}

static int mul(aw_t *w)
{
    TRY(unary(w));
    while (at(w, K_STAR) || at(w, K_PERCENT)) {
        uint32_t ot = w->t;
        tws(w, " ");
        tws(w, kspell[w->tok[w->t].k]);
        tws(w, " ");
        next(w);
        TRY(unary(w));
        ln(w, ot);
        TRY(em(w, w->tok[ot].k == K_STAR ? ZL_MUL : ZL_MODT, 0, 0));
    }
    if (at(w, K_SLASH) || at(w, K_POW))
        return lfail(w, ZL_E_UNSUPPORTED, "'/' and '^' give floats; the subset keeps integers");
    return ZL_OK;
}

static int add(aw_t *w)
{
    TRY(mul(w));
    while (at(w, K_PLUS) || at(w, K_MINUS)) {
        uint32_t ot = w->t;
        tws(w, " ");
        tws(w, kspell[w->tok[w->t].k]);
        tws(w, " ");
        next(w);
        TRY(mul(w));
        ln(w, ot);
        TRY(em(w, w->tok[ot].k == K_PLUS ? ZL_ADD : ZL_SUB, 0, 0));
    }
    return ZL_OK;
}

/* concatenation is implicit adjacency: only in print and printf arguments,
 * where the operands flatten into print pieces in order. It lives below +
 * and -, above the comparisons awk puts there. Outside print the strings
 * cannot appear at all, so the level is a pass-through. */
static int concat(aw_t *w)
{
    int pending = 0; /* a parsed operand is still on the stack */
    /* pieces only at the top level of a print or printf argument, where
     * nocmp still stands: inside ( ) and int( ) the flag drops and the
     * level is a pass-through so the nested expression keeps its value. */
    if (w->strok && w->nocmp && at(w, K_STR)) {
        const atok_t *st = &w->tok[w->t];
        int32_t si = zl_addstr(w->c, w->sbuf + st->v, st->len);
        if (si < 0) return status(w);
        tw_str(w, w->t);
        next(w);
        TRY(piece(w, P_STR, si));
    } else {
        TRY(add(w));
        pending = 1;
    }
    if (!w->strok || !w->nocmp) return ZL_OK;
    for (;;) {
        int k = w->tok[w->t].k;
        if (k != K_INT && k != K_STR && k != K_NAME && k != K_LP && k != K_INCR && k != K_DECR &&
            k != K_MINUS && k != K_BANG)
            break;
        tws(w, " ");
        if (pending) {
            TRY(stash(w, P_INT));
            pending = 0;
        }
        if (k == K_STR) {
            const atok_t *st = &w->tok[w->t];
            int32_t si = zl_addstr(w->c, w->sbuf + st->v, st->len);
            if (si < 0) return status(w);
            tw_str(w, w->t);
            next(w);
            TRY(piece(w, P_STR, si));
        } else {
            TRY(add(w));
            pending = 1;
        }
    }
    if (pending) TRY(stash(w, P_INT));
    return ZL_OK;
}

static int cmp(aw_t *w)
{
    TRY(concat(w));
    while (at(w, K_LT) || at(w, K_LE) || at(w, K_EQ) || at(w, K_NE) || at(w, K_GE) || at(w, K_GT)) {
        if (w->nocmp)
            return lfail(w, ZL_E_UNSUPPORTED, "write a comparison in print and printf inside parentheses");
        uint32_t ot = w->t;
        tws(w, " ");
        tws(w, kspell[w->tok[w->t].k]);
        tws(w, " ");
        next(w);
        TRY(concat(w));
        ln(w, ot);
        switch (w->tok[ot].k) {
        case K_LT: TRY(em(w, ZL_LT, 0, 0)); break;
        case K_LE: TRY(em(w, ZL_LE, 0, 0)); break;
        case K_EQ: TRY(em(w, ZL_EQ, 0, 0)); break;
        case K_NE: TRY(em(w, ZL_NE, 0, 0)); break;
        case K_GE: TRY(em(w, ZL_GE, 0, 0)); break;
        default: TRY(em(w, ZL_GT, 0, 0)); break;
        }
    }
    return ZL_OK;
}

/* && and || are the same shape: each operand is normalised to 0/1 before
 * the branch tests it, so a chain yields a truth value like awk's. Every
 * short-circuit exit patches to the end of the whole chain. */
static int logic_chain(aw_t *w, int (*operand)(aw_t *), int op, int jump)
{
    TRY(operand(w));
    int32_t end[64];
    uint32_t nj = 0;
    while (at(w, op)) {
        uint32_t ot = w->t;
        tws(w, " ");
        tws(w, kspell[op]);
        tws(w, " ");
        next(w);
        TRY(em(w, ZL_NOT, 0, 0));
        TRY(em(w, ZL_NOT, 0, 0));
        TRY(em(w, ZL_DUP, 0, 0));
        int32_t j = zl_emit(w->c, jump, 0, 0);
        if (j < 0) return status(w);
        if (nj >= sizeof end / sizeof end[0]) return lfail(w, ZL_E_LIMIT, "a && or || chain too long");
        end[nj++] = j;
        TRY(em(w, ZL_DROP, 0, 0));
        TRY(operand(w));
        ln(w, ot);
    }
    if (nj) {
        TRY(em(w, ZL_NOT, 0, 0));
        TRY(em(w, ZL_NOT, 0, 0));
        for (uint32_t i = 0; i < nj; i++) zl_patch(w->c, end[i], here_pc(w));
    }
    return ZL_OK;
}

static int andexpr(aw_t *w)
{
    return logic_chain(w, cmp, K_ANDAND, ZL_JZ);
}

static int orexpr(aw_t *w)
{
    return logic_chain(w, andexpr, K_OROR, ZL_JNZ);
}

/* ternary is right-associative; the condition and arms are full expressions */
static int expr(aw_t *w)
{
    TRY(nest_in(w));
    TRY(orexpr(w));
    if (at(w, K_QUEST)) {
        tws(w, " ? ");
        next(w);
        int32_t jz = zl_emit(w->c, ZL_JZ, 0, 0);
        if (jz < 0) return status(w);
        int save = w->nocmp;
        w->nocmp = 0;
        TRY(expr(w));
        w->nocmp = save;
        TRY(expect(w, K_COLON, "':' expected"));
        tws(w, " : ");
        int32_t jmp = zl_emit(w->c, ZL_JMP, 0, 0);
        if (jmp < 0) return status(w);
        zl_patch(w->c, jz, here_pc(w));
        TRY(expr(w));
        zl_patch(w->c, jmp, here_pc(w));
    }
    w->nest--;
    return ZL_OK;
}

/* ------------------------------------------------------------ statements */

static int block(aw_t *w);
static int statement(aw_t *w, int *always);

static void skip_seps(aw_t *w)
{
    while (at(w, K_SEMI) || at(w, K_NL)) next(w);
}

static void patch_here(aw_t *w, int32_t j)
{
    if (j >= 0) zl_patch(w->c, j, here_pc(w));
}

static int32_t loop_begin(aw_t *w)
{
    int32_t b = w->brk0, c = w->cnt0;
    w->brk0 = (int32_t) w->nbrk;
    return (int32_t) (((uint32_t) (uint16_t) b << 16) | (uint32_t) (uint16_t) c);
}

static void loop_end(aw_t *w, int32_t saved)
{
    int32_t c0 = (int32_t) (int16_t) ((uint16_t) saved);
    for (uint32_t i = (uint32_t) w->brk0; i < w->nbrk; i++) zl_patch(w->c, w->brk[i], here_pc(w));
    w->nbrk = (uint32_t) w->brk0;
    w->brk0 = (int32_t) (int16_t) ((uint32_t) saved >> 16);
    w->cnt0 = c0;
}

/* The closing '}' of a braced body, written at the depth it closes. */
static int rbrace(aw_t *w)
{
    bol(w);
    tws(w, "}");
    return expect(w, K_RB, "'}' expected");
}

static int if_stmt(aw_t *w, int *always)
{
    bol(w);
    tws(w, "if (");
    next(w);
    TRY(expect(w, K_LP, "'(' expected"));
    TRY(expr(w));
    TRY(expect(w, K_RP, "')' expected"));
    tws(w, ") {");
    TRY(expect(w, K_LB, "a braced { } body expected"));
    uint32_t last = w->t ? w->t - 1 : 0;
    TRY(eol(w, last));
    int32_t jz = zl_emit(w->c, ZL_JZ, 0, 0);
    if (jz < 0) return status(w);
    w->depth++;
    TRY(block(w));
    w->depth--;
    if (at(w, K_RB) && w->tok[w->t + 1].k == K_ELSE) {
        int32_t jmp = zl_emit(w->c, ZL_JMP, 0, 0);
        if (jmp < 0) return status(w);
        patch_here(w, jz);
        jz = -1;
        bol(w);
        tws(w, "} else {");
        next(w); /* '}' */
        next(w); /* 'else' */
        TRY(expect(w, K_LB, "a braced { } body expected"));
        last = w->t ? w->t - 1 : 0;
        TRY(eol(w, last));
        w->depth++;
        TRY(block(w));
        w->depth--;
        patch_here(w, jmp);
    }
    patch_here(w, jz);
    TRY(rbrace(w));
    last = w->t ? w->t - 1 : 0;
    TRY(eol(w, last));
    *always = 0; /* awk has no statically-never-ending if */
    return ZL_OK;
}

static int while_stmt(aw_t *w)
{
    bol(w);
    tws(w, "while (");
    next(w);
    TRY(expect(w, K_LP, "'(' expected"));
    int32_t top = here_pc(w);
    TRY(expr(w));
    TRY(expect(w, K_RP, "')' expected"));
    tws(w, ") {");
    TRY(expect(w, K_LB, "a braced { } body expected"));
    uint32_t last = w->t ? w->t - 1 : 0;
    TRY(eol(w, last));
    int32_t jz = zl_emit(w->c, ZL_JZ, 0, 0);
    if (jz < 0) return status(w);
    int32_t saved = loop_begin(w);
    w->cnt0 = top; /* a continue re-enters at the condition */
    w->loopdepth++;
    w->depth++;
    TRY(block(w));
    w->depth--;
    w->loopdepth--;
    TRY(em(w, ZL_JMP, top, 0));
    patch_here(w, jz);
    loop_end(w, saved);
    TRY(rbrace(w));
    last = w->t ? w->t - 1 : 0;
    return eol(w, last);
}

/* An init or increment clause of a for header: an assignment emits its
 * store and nothing on the stack; anything else is an expression whose
 * value drops (x++ leaves the old value). */
static int for_clause(aw_t *w)
{
    int k1 = w->tok[w->t + 1].k;
    if (at(w, K_NAME) &&
        (k1 == K_ASSIGN || k1 == K_PLUSEQ || k1 == K_MINUSEQ || k1 == K_STAREQ || k1 == K_PERCENTEQ))
        return assign_stmt(w, 0);
    TRY(expr(w));
    return em(w, ZL_DROP, 0, 0);
}

static int for_stmt(aw_t *w)
{
    bol(w);
    tws(w, "for (");
    next(w);
    TRY(expect(w, K_LP, "'(' expected"));
    if (at(w, K_NAME) && w->tok[w->t + 1].k == K_IN) return lfail(w, ZL_E_UNSUPPORTED, "for (i in a) is arrays; outside the subset");
    if (!at(w, K_SEMI)) TRY(for_clause(w));
    tws(w, "; ");
    TRY(expect(w, K_SEMI, "';' expected"));
    int32_t cond = -1, top = here_pc(w);
    if (!at(w, K_SEMI)) {
        TRY(expr(w));
        cond = zl_emit(w->c, ZL_JZ, 0, 0);
        if (cond < 0) return status(w);
    }
    tws(w, "; ");
    TRY(expect(w, K_SEMI, "';' expected"));
    int32_t jmpb = -1, isec = -1;
    if (!at(w, K_RP)) {
        /* init; top: cond JZ end; JMP body; isec: incr; JMP top;
         * body: ...; JMP isec; end: */
        jmpb = zl_emit(w->c, ZL_JMP, 0, 0);
        if (jmpb < 0) return status(w);
        isec = here_pc(w);
        TRY(for_clause(w));
        TRY(em(w, ZL_JMP, top, 0));
        patch_here(w, jmpb);
    }
    tws(w, ") {");
    TRY(expect(w, K_RP, "')' expected"));
    TRY(expect(w, K_LB, "a braced { } body expected"));
    uint32_t last = w->t ? w->t - 1 : 0;
    TRY(eol(w, last));
    int32_t saved = loop_begin(w);
    w->cnt0 = isec >= 0 ? isec : top; /* a continue re-enters at the increment */
    w->loopdepth++;
    w->depth++;
    TRY(block(w));
    w->depth--;
    w->loopdepth--;
    if (isec >= 0) TRY(em(w, ZL_JMP, isec, 0));
    else TRY(em(w, ZL_JMP, top, 0));
    patch_here(w, cond);
    loop_end(w, saved);
    TRY(rbrace(w));
    last = w->t ? w->t - 1 : 0;
    return eol(w, last);
}

/* One piece of a print or printf argument list: a literal string, a tab
 * for print's comma, a stashed integer or, in a printf format walk, a
 * hole where the next argument's value lands. */
static int emit_piece(aw_t *w, uint8_t kind, int32_t v)
{
    if (kind == P_STR)
        return em(w, ZL_OUTS, v, 0);
    if (kind == P_TAB) {
        TRY(em(w, ZL_PUSH, '\t', 0));
        return em(w, ZL_OUTC, 0, 0);
    }
    TRY(em(w, ZL_LOAD, v, 0));
    return em(w, ZL_OUTI, 0, 0);
}

static int print_stmt(aw_t *w, int isprintf)
{
    uint32_t t0 = w->t, tmpn = w->tmpn;
    /* printf: the format becomes literal runs and holes; each later
     * argument's pieces land at the next hole when the list is emitted */
    int32_t fseg[96]; /* a string index, or -1 for a hole */
    uint32_t nfseg = 0, nholes = 0;
    uint32_t astart[64], acount[64];
    uint32_t narg = 0;
    bol(w);
    tws(w, isprintf ? "printf " : "print ");
    next(w);
    /* printf("fmt", args) is the same call parenthesised; for print a
     * leading '(' belongs to the first argument and primary takes it. */
    if (isprintf && at(w, K_LP) && w->tok[w->t + 1].k == K_STR) next(w);
    w->npc = 0;
    w->strok = 1;
    w->nocmp = 1;
    uint32_t nargs = 0;
    while (!at(w, K_RP) && !at(w, K_RB) && !at(w, K_NL) && !at(w, K_EOF) && !at(w, K_SEMI)) {
        if (nargs) {
            TRY(expect(w, K_COMMA, "',' expected"));
            tws(w, ", ");
            if (!isprintf) TRY(piece(w, P_TAB, 0));
        }
        if (isprintf && nargs == 0) {
            if (!at(w, K_STR)) return lfail(w, ZL_E_UNSUPPORTED, "printf's first argument is a format string");
            const atok_t *fmt = &w->tok[w->t];
            tw_str(w, w->t);
            next(w);
            /* parse the format: literal runs and %d %i %s %% holes */
            uint32_t j = 0;
            while (j < fmt->len) {
                char c = w->sbuf[fmt->v + j];
                if (c != '%') {
                    uint32_t st = j;
                    while (j < fmt->len && w->sbuf[fmt->v + j] != '%') j++;
                    int32_t si = zl_addstr(w->c, w->sbuf + fmt->v + st, j - st);
                    if (si < 0) return status(w);
                    if (nfseg >= sizeof fseg / sizeof fseg[0])
                        return lfail_at(w, t0, ZL_E_LIMIT, "a printf format too long");
                    fseg[nfseg++] = si;
                    continue;
                }
                j++;
                if (j >= fmt->len) return lfail_at(w, t0, ZL_E_SYNTAX, "a lone % in a format");
                char f = w->sbuf[fmt->v + j++];
                if (f == '%') {
                    int32_t si = zl_addstr(w->c, "%", 1);
                    if (si < 0) return status(w);
                    if (nfseg >= sizeof fseg / sizeof fseg[0])
                        return lfail_at(w, t0, ZL_E_LIMIT, "a printf format too long");
                    fseg[nfseg++] = si;
                } else if (f == 'd' || f == 'i' || f == 's') {
                    if (nfseg >= sizeof fseg / sizeof fseg[0])
                        return lfail_at(w, t0, ZL_E_LIMIT, "a printf format too long");
                    fseg[nfseg++] = -1; /* a hole for the next argument */
                    nholes++;
                } else {
                    return lfail_at(w, t0, ZL_E_UNSUPPORTED, "only %d, %i, %s and %% in printf");
                }
            }
            nargs = 1;
            continue;
        }
        {
            uint32_t pc0 = w->npc;
            TRY(concat(w)); /* the arg flattened into pieces in order */
            if (isprintf) {
                if (narg >= sizeof astart / sizeof astart[0])
                    return lfail_at(w, t0, ZL_E_LIMIT, "too many printf arguments");
                astart[narg] = pc0;
                acount[narg] = w->npc - pc0;
                narg++;
            }
        }
        nargs++;
    }
    w->strok = 0;
    w->nocmp = 0;
    if (at(w, K_RP)) next(w);
    if (isprintf) {
        if (narg < nholes) return lfail_at(w, t0, ZL_E_SYNTAX, "fewer printf arguments than conversions");
        if (narg > nholes) return lfail_at(w, t0, ZL_E_UNSUPPORTED, "printf arguments beyond the conversions");
    }
    ln(w, t0);
    /* emit: strings and numbers in order, tabs for print's comma */
    uint32_t ai = 0;
    if (isprintf) {
        for (uint32_t i = 0; i < nfseg; i++) {
            if (fseg[i] >= 0) {
                TRY(emit_piece(w, P_STR, fseg[i]));
            } else {
                for (uint32_t j = 0; j < acount[ai]; j++)
                    TRY(emit_piece(w, w->pc[astart[ai] + j].kind, w->pc[astart[ai] + j].v));
                ai++;
            }
        }
    } else {
        for (uint32_t i = 0; i < w->npc; i++) TRY(emit_piece(w, w->pc[i].kind, w->pc[i].v));
        TRY(em(w, ZL_NL, 0, 0));
    }
    w->npc = 0;
    w->tmpn = tmpn;
    {
        uint32_t last = w->t ? w->t - 1 : 0;
        return eol(w, last);
    }
}

static int assign_stmt(aw_t *w, int want_val)
{
    if (!at(w, K_NAME)) return lfail(w, ZL_E_SYNTAX, "a variable expected");
    int32_t cell;
    uint32_t vt = w->t;
    if (w->tok[w->t + 1].k == K_LBRACK) return lfail(w, ZL_E_UNSUPPORTED, "arrays are outside the subset");
    tw_name(w, vt);
    next(w);
    TRY(var_cell(w, vt, &cell, 1));
    int op = 0;
    if (at(w, K_PLUSEQ)) op = ZL_ADD;
    else if (at(w, K_MINUSEQ)) op = ZL_SUB;
    else if (at(w, K_STAREQ)) op = ZL_MUL;
    else if (at(w, K_PERCENTEQ)) op = ZL_MODT;
    if (!op && !at(w, K_ASSIGN)) return lfail_at(w, vt, ZL_E_SYNTAX, "'=' expected");
    tws(w, " ");
    tws(w, kspell[w->tok[w->t].k]);
    tws(w, " ");
    next(w);
    if (op) {
        TRY(em(w, ZL_LOAD, cell, 0));
        TRY(expr(w));
        ln(w, vt);
        TRY(em(w, op, 0, 0));
    } else {
        TRY(expr(w));
        ln(w, vt);
    }
    TRY(em(w, ZL_STORE, cell, 0));
    if (want_val) TRY(em(w, ZL_LOAD, cell, 0));
    return ZL_OK;
}

static int statement(aw_t *w, int *always)
{
    *always = 0;
    w->tmpn = 0;
    int k = w->tok[w->t].k;
    if (k == K_SEMI || k == K_NL) return ZL_OK;
    if (k == K_IF) return if_stmt(w, always);
    if (k == K_WHILE) return while_stmt(w);
    if (k == K_FOR) return for_stmt(w);
    if (k == K_PRINT || k == K_PRINTF) return print_stmt(w, k == K_PRINTF);
    if (k == K_BREAK || k == K_CONTINUE) {
        if (w->loopdepth == 0) return lfail(w, ZL_E_SYNTAX, "break and continue are inside a loop");
        bol(w);
        tws(w, kspell[k]);
        next(w);
        uint32_t last = w->t ? w->t - 1 : 0;
        TRY(eol(w, last));
        int32_t j = zl_emit(w->c, k == K_BREAK ? ZL_JMP : ZL_JMP, 0, 0);
        if (j < 0) return status(w);
        if (k == K_BREAK) {
            if (w->nbrk >= AB_MAX) return lfail(w, ZL_E_LIMIT, "too many breaks");
            w->brk[w->nbrk++] = j;
        } else {
            if (w->nfix >= AX_MAX) return lfail(w, ZL_E_LIMIT, "too many continues");
            w->fix[w->nfix].at = j;
            w->fix[w->nfix].cell = (int32_t) w->cnt0;
            w->fix[w->nfix].op = 0;
            w->nfix++;
        }
        return ZL_OK;
    }
    if (k == K_EXIT) {
        bol(w);
        tws(w, "exit");
        next(w);
        if (!at(w, K_SEMI) && !at(w, K_NL) && !at(w, K_RB) && !at(w, K_EOF)) {
            tws(w, " ");
            TRY(expr(w));
            TRY(em(w, ZL_DROP, 0, 0));
        }
        TRY(em(w, ZL_HALT, 0, 0));
        *always = 1;
        uint32_t last = w->t ? w->t - 1 : 0;
        return eol(w, last);
    }
    if (k == K_LB) {
        bol(w);
        tws(w, "{");
        next(w);
        uint32_t last = w->t ? w->t - 1 : 0;
        TRY(eol(w, last));
        w->depth++;
        TRY(block(w));
        w->depth--;
        TRY(rbrace(w));
        last = w->t ? w->t - 1 : 0;
        return eol(w, last);
    }
    if (k == K_NAME) {
        uint32_t t0 = w->t;
        int k1 = w->tok[w->t + 1].k;
        if (k1 == K_ASSIGN || k1 == K_PLUSEQ || k1 == K_MINUSEQ || k1 == K_STAREQ || k1 == K_PERCENTEQ) {
            bol(w);
            TRY(assign_stmt(w, 0));
            uint32_t last = w->t ? w->t - 1 : 0;
            return eol(w, last);
        }
        if (k1 == K_INCR || k1 == K_DECR) {
            bol(w);
            TRY(expr(w));
            TRY(em(w, ZL_DROP, 0, 0));
            uint32_t last = w->t ? w->t - 1 : 0;
            return eol(w, last);
        }
        if (k1 == K_LP) {
            bol(w);
            TRY(expr(w));
            TRY(em(w, ZL_DROP, 0, 0));
            uint32_t last = w->t ? w->t - 1 : 0;
            return eol(w, last);
        }
        if (k1 == K_LBRACK)
            return lfail_at(w, t0, ZL_E_UNSUPPORTED, "arrays are outside the subset");
        if (name_is(w, t0, "getline") || name_is(w, t0, "next") || name_is(w, t0, "nextfile"))
            return lfail_at(w, t0, ZL_E_UNSUPPORTED, "the record model is outside the subset");
        if (name_is(w, t0, "do"))
            return lfail_at(w, t0, ZL_E_UNSUPPORTED, "do-while is not in POSIX awk");
        return lfail_at(w, t0, ZL_E_SYNTAX, "a statement expected");
    }
    if (k == K_FUNCTION) return lfail(w, ZL_E_UNSUPPORTED, "function definitions are outside the subset");
    return lfail(w, ZL_E_SYNTAX, "a statement expected");
}

static int block(aw_t *w)
{
    TRY(nest_in(w));
    w->blockdepth++;
    for (;;) {
        skip_seps(w);
        if (at(w, K_RB) || at(w, K_EOF)) break;
        int a;
        TRY(statement(w, &a));
        (void) a;
    }
    w->blockdepth--;
    w->nest--;
    return ZL_OK;
}

/* A BEGIN or END block: keyword { statements } */
static int special(aw_t *w)
{
    bol(w);
    tws(w, at(w, K_BEGIN) ? "BEGIN {" : "END {");
    next(w);
    TRY(expect(w, K_LB, "'{' expected"));
    uint32_t last = w->t ? w->t - 1 : 0;
    TRY(eol(w, last));
    w->depth++;
    TRY(block(w));
    w->depth--;
    TRY(rbrace(w));
    last = w->t ? w->t - 1 : 0;
    return eol(w, last);
}

static int chunk(aw_t *w)
{
    for (;;) {
        skip_seps(w);
        if (at(w, K_EOF)) break;
        if (at(w, K_BEGIN) || at(w, K_END)) {
            TRY(special(w));
            continue;
        }
        if (at(w, K_FUNCTION))
            return lfail(w, ZL_E_UNSUPPORTED, "function definitions are outside the subset");
        /* anything else at the top level is a pattern or a bare action —
         * the record model, which is outside the subset */
        return lfail(w, ZL_E_UNSUPPORTED,
                     "rules over records are outside the subset; only BEGIN { } and END { } run here");
    }
    flush_comments(w, w->t);
    w->c->line = w->tok[w->t].line;
    TRY(em(w, ZL_HALT, 0, 0));
    /* patch continues: a continue jumps to the loop's increment or top */
    for (uint32_t i = 0; i < w->nfix; i++) zl_patch(w->c, w->fix[i].at, w->fix[i].cell);
    return ZL_OK;
}

static int pass(aw_t *w, zl_out_t *tw)
{
    zl_prog_t *p = w->c->p;
    int lang = p->lang;
    zl_bzero(p, sizeof *p);
    zl_cc_init(w->c, p, w->c->d, lang, 64);
    w->tw = tw;
    w->t = w->cnext = w->nvar = w->nfix = w->nbrk = w->npc = w->ntmp = w->tmpn = 0;
    w->brk0 = w->cnt0 = -1;
    w->strok = w->nocmp = w->unknown = w->tw_any = w->blockdepth = w->loopdepth = 0;
    w->depth = w->nest = 0;
    return chunk(w);
}

int zl_awk_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    aw_t *w = (aw_t *) work;
    zl_bzero(w, sizeof *w);
    w->c = c;
    w->s = src;
    w->n = len;
    TRY(lex(w));
    return pass(w, tw);
}