/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_fortran.c — FORTRAN front-end: FORTRAN 77 fixed form (the 1957
 * language as ANSI X3.9-1978 fixed it), INTEGER subset, and its twin,
 * "Reckonwyrm" (it reckons, like the Trebago pillar it feeds).
 *
 * Source form: comment lines start with C, c, * or ! in column 1 (blank
 * lines are comments too); columns 1-5 hold a label, a character other than
 * blank or 0 in column 6 continues the previous line, the statement is in
 * columns 7-72 and anything past column 72 is ignored. A tab in columns 1-6
 * starts the statement (the DEC convention). Blanks are not significant
 * outside character constants, so DO10I=1.10 is the classic assignment, and
 * '!' outside a constant starts a comment, as gfortran allows.
 *
 * Admitted: PROGRAM, IMPLICIT NONE, IMPLICIT INTEGER (letter ranges),
 * INTEGER, DIMENSION (one dimension, lower:upper bounds), PARAMETER;
 * assignment; DO label / DO ... END DO / DO WHILE; logical, arithmetic and
 * block IF (ELSE IF, ELSE, END IF); GOTO, computed GOTO; CONTINUE; STOP;
 * END; PRINT * and WRITE (*,*) or (6,*); READ * and READ (*,*); the
 * operators + - * / ** .EQ. .NE. .LT. .LE. .GT. .GE. (and == /= < <= > >=)
 * .AND. .OR. .NOT.; MOD, ABS, IABS, MAX, MAX0, MIN, MIN0, ISIGN.
 *
 * INTEGER means 32 bits, as gfortran's default kind: a result outside it
 * traps instead of wrapping. Division truncates toward zero and a negative
 * power truncates (2**(-1) is 0), as the standard says for integers. PRINT *
 * output matches gfortran's list-directed layout for integers and
 * character constants. Refused by name: REAL, DOUBLE PRECISION, COMPLEX,
 * CHARACTER and LOGICAL variables, an implicitly REAL name, real
 * constants and intrinsics, SUBROUTINE, FUNCTION, CALL, COMMON,
 * EQUIVALENCE, FORMAT, DATA, implied DO lists, multi-dimensional arrays.
 *
 * Reckonwyrm (the twin): fixed form, label left-justified in
 * columns 1-5, statement from column 7, keywords and names upper case, one
 * space around '=' and binary operators, ', ' between list items, the
 * dotted relational forms, character constants in apostrophes, minimal
 * parentheses, comment lines as 'C' plus their text, statements over 66
 * columns continued with '&' in column 6. */
#include "zl_int.h"

enum { E_NUM = 1, E_VAR, E_ARR, E_NEG, E_BIN, E_REL, E_NOT, E_AND, E_OR, E_FN };
enum { T_INT = 1, T_LOG = 2 };
enum { FN_MOD = 1, FN_ABS, FN_MAX, FN_MIN, FN_ISIGN };
enum { S_SCALAR = 1, S_ARRAY, S_PARAM };

typedef struct {
    uint8_t kind, op, type, fn;
    int32_t sym, l, r, next; /* next: argument list */
    int64_t v;
} fnode_t;

typedef struct {
    char name[32];
    uint8_t namelen, kind, declared;
    int32_t slot;
    int64_t lo, len, val;
} fsym_t;

#define F_POOL   1024
#define F_SYMS   512
#define F_LABELS 2048
#define F_FIX    4096
#define F_CMT    256
#define F_TEXT   2048

typedef struct {
    uint8_t kind; /* 1 DO-label, 2 DO-END, 3 DO-WHILE, 4 IF block */
    uint32_t label;
    int32_t var, cnt, step, top, exitj;
    int32_t ends[32];
    uint32_t nends;
    int32_t pending; /* IF: the JZ to the next branch, or -1 */
    int else_seen;
} fctl_t;

typedef struct {
    fnode_t pool[F_POOL];
    uint32_t npool;
    fsym_t sym[F_SYMS];
    uint32_t nsym;
    uint32_t lab[F_LABELS];
    int32_t labaddr[F_LABELS];
    uint32_t nlab;
    struct {
        int32_t at;
        uint32_t label, srcline;
    } fix[F_FIX];
    uint32_t nfix;
    fctl_t ctl[ZL_MAX_DEPTH];
    uint32_t nctl;
    uint8_t intletter[26]; /* IMPLICIT INTEGER letters */
    int implicit_none, executable, ended, program_seen;
    uint32_t indent; /* twin indentation: open blocks, two columns each */
    int32_t tmp_slot;
    uint32_t depth;
    /* the statement being parsed: squeezed text */
    char t[F_TEXT];
    size_t tn, ti;
    /* twin line being built, and comments queued behind it */
    char tl[F_TEXT * 2];
    zl_out_t lo;
    struct {
        uint32_t off, len;
    } cmt[F_CMT];
    uint32_t ncmt;
    const char *src;
    zl_cc_t *c;
    zl_out_t *tw;
} fw_t;

ZL_STATIC_ASSERT(sizeof(fw_t) <= sizeof(((zl_work_t *) 0)->fe), fw_fits);

static int ffail(fw_t *w, int st, const char *msg)
{
    return zl_fail(w->c, st, msg);
}

/* ------------------------------------------------------------ twin text */

static void ts(fw_t *w, const char *s)
{
    if (w->tw) zl_puts(&w->lo, s);
}
static void ti(fw_t *w, int64_t v)
{
    if (w->tw) zl_puti(&w->lo, v);
}
static void tname(fw_t *w, int32_t s)
{
    if (w->tw) zl_put(&w->lo, w->sym[s].name, w->sym[s].namelen);
}

/* ------------------------------------------------------------ lexing */

static int at(fw_t *w)
{
    return w->ti < w->tn ? (unsigned char) w->t[w->ti] : 0;
}
static int eat(fw_t *w, int ch)
{
    if (at(w) == ch) {
        w->ti++;
        return 1;
    }
    return 0;
}
static int need(fw_t *w, int ch, const char *msg)
{
    return eat(w, ch) ? ZL_OK : ffail(w, ZL_E_SYNTAX, msg);
}

static int kwp(fw_t *w, const char *k)
{
    size_t j = 0;
    while (k[j] && w->ti + j < w->tn && w->t[w->ti + j] == k[j]) j++;
    if (k[j]) return 0;
    w->ti += j;
    return 1;
}

static int dotop(fw_t *w, const char *op) /* ".EQ." etc. */
{
    return kwp(w, op);
}

static int is_dotop_here(fw_t *w, size_t pos)
{
    static const char *const ops[] = {".EQ.",    ".NE.",  ".LT.",   ".LE.",  ".GT.",
                                      ".GE.",    ".AND.", ".OR.",   ".NOT.", ".TRUE.",
                                      ".FALSE.", ".EQV.", ".NEQV.", 0};
    for (int k = 0; ops[k]; k++) {
        size_t j = 0;
        while (ops[k][j] && pos + j < w->tn && w->t[pos + j] == ops[k][j]) j++;
        if (!ops[k][j]) return 1;
    }
    return 0;
}

static int name(fw_t *w, char *out, uint8_t *outlen)
{
    size_t n = 0;
    if (!zl_isalpha(at(w))) return 0;
    while (zl_isalnum(at(w)) || at(w) == '_') {
        if (n >= 31) return -1;
        out[n++] = (char) w->t[w->ti++];
    }
    *outlen = (uint8_t) n;
    return 1;
}

static int intconst(fw_t *w, int64_t *v)
{
    int64_t x = 0;
    if (!zl_isdigit(at(w))) return ffail(w, ZL_E_SYNTAX, "integer constant expected");
    while (zl_isdigit(at(w))) {
        x = x * 10 + (w->t[w->ti++] - '0');
        if (x > 2147483648ll)
            return ffail(w, ZL_E_UNSUPPORTED, "integer constant wider than 32 bits");
    }
    if (at(w) == '.' && !is_dotop_here(w, w->ti))
        return ffail(w, ZL_E_UNSUPPORTED, "real constant (exact INTEGER subset)");
    if ((at(w) == 'E' || at(w) == 'D') &&
        (zl_isdigit(w->ti + 1 < w->tn ? w->t[w->ti + 1] : 0) ||
         (w->ti + 1 < w->tn && (w->t[w->ti + 1] == '+' || w->t[w->ti + 1] == '-'))))
        return ffail(w, ZL_E_UNSUPPORTED, "real constant (exact INTEGER subset)");
    *v = x;
    return ZL_OK;
}

static int labelref(fw_t *w, uint32_t *l)
{
    int64_t v = 0;
    int n = 0;
    while (zl_isdigit(at(w))) {
        v = v * 10 + (w->t[w->ti++] - '0');
        if (++n > 5) break;
    }
    if (n == 0 || n > 5 || v == 0)
        return ffail(w, ZL_E_SYNTAX, "statement label (1 to 99999) expected");
    *l = (uint32_t) v;
    return ZL_OK;
}

/* ------------------------------------------------------------ symbols */

static int32_t sym_find(fw_t *w, const char *n, uint8_t len)
{
    for (uint32_t i = 0; i < w->nsym; i++) {
        if (w->sym[i].namelen != len) continue;
        uint8_t k = 0;
        while (k < len && w->sym[i].name[k] == n[k]) k++;
        if (k == len) return (int32_t) i;
    }
    return -1;
}

static int32_t sym_add(fw_t *w, const char *n, uint8_t len)
{
    if (w->nsym >= F_SYMS) {
        ffail(w, ZL_E_LIMIT, "too many names");
        return -1;
    }
    fsym_t *s = &w->sym[w->nsym];
    zl_bzero(s, sizeof *s);
    zl_bcopy(s->name, n, len);
    s->namelen = len;
    s->slot = -1;
    return (int32_t) w->nsym++;
}

/* A name used in an expression or as a target: find or create a scalar. */
static int32_t sym_use(fw_t *w, const char *n, uint8_t len)
{
    int32_t s = sym_find(w, n, len);
    if (s >= 0 && (w->sym[s].declared || w->sym[s].kind)) return s;
    if (s < 0) {
        int letter = n[0] - 'A';
        if (w->implicit_none) {
            ffail(w, ZL_E_UNDEFINED, "name not declared (IMPLICIT NONE)");
            return -1;
        }
        if (!((letter >= 'I' - 'A' && letter <= 'N' - 'A') || w->intletter[letter])) {
            ffail(w, ZL_E_UNSUPPORTED, "implicitly REAL name (exact subset: declare it INTEGER)");
            return -1;
        }
        if ((s = sym_add(w, n, len)) < 0) return -1;
    }
    if (!w->sym[s].kind) {
        w->sym[s].kind = S_SCALAR;
        w->sym[s].len = 1;
    }
    return s;
}

static int sym_slot(fw_t *w, int32_t s)
{
    fsym_t *y = &w->sym[s];
    if (y->slot < 0) {
        y->slot = zl_slots(w->c, (uint32_t) (y->kind == S_ARRAY ? y->len : 1));
        if (y->slot < 0) return ZL_E_LIMIT;
    }
    return ZL_OK;
}

/* ------------------------------------------------------------ expressions */

static int32_t node(fw_t *w, int kind, int type)
{
    if (w->npool >= F_POOL) {
        ffail(w, ZL_E_LIMIT, "expression too large");
        return -1;
    }
    fnode_t *n = &w->pool[w->npool];
    zl_bzero(n, sizeof *n);
    n->kind = (uint8_t) kind;
    n->type = (uint8_t) type;
    n->l = n->r = n->next = n->sym = -1;
    return (int32_t) w->npool++;
}

static int32_t e_or(fw_t *w);

static int32_t e_arith(fw_t *w);

static int32_t e_primary(fw_t *w)
{
    int32_t n;
    int ch = at(w);
    if (ch == '(') {
        w->ti++;
        if (++w->depth > ZL_MAX_DEPTH) {
            ffail(w, ZL_E_LIMIT, "expression nested too deeply");
            return -1;
        }
        n = e_or(w);
        w->depth--;
        if (n < 0 || need(w, ')', "')' expected")) return -1;
        return n;
    }
    if (zl_isdigit(ch)) {
        int64_t v;
        if (intconst(w, &v)) return -1;
        if (v > 2147483647ll) {
            ffail(w, ZL_E_UNSUPPORTED, "integer constant wider than 32 bits");
            return -1;
        }
        if ((n = node(w, E_NUM, T_INT)) < 0) return -1;
        w->pool[n].v = v;
        return n;
    }
    if (ch == '.') {
        if (kwp(w, ".TRUE.") || kwp(w, ".FALSE.")) {
            ffail(w, ZL_E_UNSUPPORTED, "LOGICAL constants are not in this subset");
            return -1;
        }
        ffail(w, ZL_E_UNSUPPORTED, "real constant (exact INTEGER subset)");
        return -1;
    }
    if (ch == '\'' || ch == '"') {
        ffail(w, ZL_E_UNSUPPORTED, "a character constant is not an INTEGER");
        return -1;
    }
    char nm[32];
    uint8_t nl;
    int r = name(w, nm, &nl);
    if (r < 0) {
        ffail(w, ZL_E_SYNTAX, "name longer than 31 characters");
        return -1;
    }
    if (r == 0) {
        ffail(w, ZL_E_SYNTAX, "expression expected");
        return -1;
    }
    int32_t s = sym_find(w, nm, nl);
    if (at(w) == '(' && (s < 0 || w->sym[s].kind != S_ARRAY)) {
        static const struct {
            const char *n;
            uint8_t fn;
            uint8_t min, max;
        } fns[] = {{"MOD", FN_MOD, 2, 2},   {"ABS", FN_ABS, 1, 1},     {"IABS", FN_ABS, 1, 1},
                   {"MAX", FN_MAX, 2, 16},  {"MAX0", FN_MAX, 2, 16},   {"MIN", FN_MIN, 2, 16},
                   {"MIN0", FN_MIN, 2, 16}, {"ISIGN", FN_ISIGN, 2, 2}, {0, 0, 0, 0}};
        static const char *const reals[] = {
            "SQRT",  "SIN",    "COS",   "TAN",   "ATAN", "EXP",  "LOG",  "ALOG", "FLOAT",
            "REAL",  "DBLE",   "AMOD",  "ATAN2", "SINH", "COSH", "TANH", "ASIN", "ACOS",
            "LOG10", "ALOG10", "DSQRT", "NINT",  "INT",  "IFIX", 0};
        for (int k = 0; reals[k]; k++)
            if (zl_wordeq(nm, nl, reals[k])) {
                ffail(w, ZL_E_UNSUPPORTED, "real-valued intrinsic (exact INTEGER subset)");
                return -1;
            }
        for (int k = 0; fns[k].n; k++) {
            if (!zl_wordeq(nm, nl, fns[k].n)) continue;
            w->ti++;
            if ((n = node(w, E_FN, T_INT)) < 0) return -1;
            w->pool[n].fn = fns[k].fn;
            w->pool[n].v = k; /* which spelling, for the twin */
            int32_t prev = -1;
            unsigned argc = 0;
            do {
                int32_t a = e_arith(w);
                if (a < 0) return -1;
                if (w->pool[a].type != T_INT) {
                    ffail(w, ZL_E_SYNTAX, "intrinsic argument must be INTEGER");
                    return -1;
                }
                if (prev < 0)
                    w->pool[n].l = a;
                else
                    w->pool[prev].next = a;
                prev = a;
                argc++;
            } while (eat(w, ','));
            if (need(w, ')', "')' expected")) return -1;
            if (argc < fns[k].min || argc > fns[k].max) {
                ffail(w, ZL_E_SYNTAX, "wrong number of intrinsic arguments");
                return -1;
            }
            return n;
        }
        ffail(w, ZL_E_UNSUPPORTED, "function references are not in this subset");
        return -1;
    }
    if ((s = sym_use(w, nm, nl)) < 0) return -1;
    if (w->sym[s].kind == S_ARRAY) {
        if (need(w, '(', "an array needs a subscript")) return -1;
        int32_t ix = e_arith(w);
        if (ix < 0) return -1;
        if (at(w) == ',') {
            ffail(w, ZL_E_UNSUPPORTED, "multi-dimensional arrays are not in this subset");
            return -1;
        }
        if (w->pool[ix].type != T_INT || need(w, ')', "')' expected")) return -1;
        if ((n = node(w, E_ARR, T_INT)) < 0) return -1;
        w->pool[n].sym = s;
        w->pool[n].l = ix;
        return n;
    }
    if ((n = node(w, w->sym[s].kind == S_PARAM ? E_VAR : E_VAR, T_INT)) < 0) return -1;
    w->pool[n].sym = s;
    return n;
}

static int32_t mkbin(fw_t *w, int kind, int op, int type, int32_t l, int32_t r)
{
    int32_t n;
    if (l < 0 || r < 0) return -1;
    if ((n = node(w, kind, type)) < 0) return -1;
    w->pool[n].op = (uint8_t) op;
    w->pool[n].l = l;
    w->pool[n].r = r;
    return n;
}

static int intpair(fw_t *w, int32_t l, int32_t r)
{
    if (l < 0 || r < 0) return 0;
    if (w->pool[l].type != T_INT || w->pool[r].type != T_INT) {
        ffail(w, ZL_E_SYNTAX, "arithmetic on a LOGICAL value");
        return 0;
    }
    return 1;
}

static int32_t e_factor(fw_t *w)
{
    int32_t l = e_primary(w);
    if (l >= 0 && at(w) == '*' && w->ti + 1 < w->tn && w->t[w->ti + 1] == '*') {
        w->ti += 2;
        if (++w->depth > ZL_MAX_DEPTH) {
            ffail(w, ZL_E_LIMIT, "expression nested too deeply");
            return -1;
        }
        int32_t r = e_factor(w); /* '**' associates right */
        w->depth--;
        if (!intpair(w, l, r)) return -1;
        return mkbin(w, E_BIN, 'P', T_INT, l, r);
    }
    return l;
}

static int32_t e_term(fw_t *w)
{
    int32_t l = e_factor(w);
    for (;;) {
        if (l < 0) return -1;
        int ch = at(w);
        if (ch == '/' && !(w->ti + 1 < w->tn && w->t[w->ti + 1] == '=')) {
            w->ti++;
        } else if (ch == '*' && !(w->ti + 1 < w->tn && w->t[w->ti + 1] == '*')) {
            w->ti++;
        } else {
            return l;
        }
        int32_t r = e_factor(w);
        if (!intpair(w, l, r)) return -1;
        l = mkbin(w, E_BIN, ch, T_INT, l, r);
    }
}

static int32_t e_arith(fw_t *w)
{
    int32_t l;
    int ch = at(w);
    if (ch == '+' || ch == '-') {
        w->ti++;
        l = e_term(w);
        if (l >= 0 && w->pool[l].type != T_INT) {
            ffail(w, ZL_E_SYNTAX, "arithmetic on a LOGICAL value");
            return -1;
        }
        if (l >= 0 && ch == '-') {
            int32_t n = node(w, E_NEG, T_INT);
            if (n < 0) return -1;
            w->pool[n].l = l;
            l = n;
        }
    } else {
        l = e_term(w);
    }
    while (l >= 0 && ((ch = at(w)) == '+' || ch == '-')) {
        w->ti++;
        int32_t r = e_term(w);
        if (!intpair(w, l, r)) return -1;
        l = mkbin(w, E_BIN, ch, T_INT, l, r);
    }
    return l;
}

static int32_t e_rel(fw_t *w)
{
    int32_t l = e_arith(w);
    int op = 0;
    if (l < 0) return -1;
    if (dotop(w, ".EQ.") || kwp(w, "=="))
        op = ZL_EQ;
    else if (dotop(w, ".NE.") || kwp(w, "/="))
        op = ZL_NE;
    else if (dotop(w, ".LE.") || kwp(w, "<="))
        op = ZL_LE;
    else if (dotop(w, ".LT.") || kwp(w, "<"))
        op = ZL_LT;
    else if (dotop(w, ".GE.") || kwp(w, ">="))
        op = ZL_GE;
    else if (dotop(w, ".GT.") || kwp(w, ">"))
        op = ZL_GT;
    else
        return l;
    int32_t r = e_arith(w);
    if (!intpair(w, l, r)) return -1;
    return mkbin(w, E_REL, op, T_LOG, l, r);
}

static int32_t e_not(fw_t *w)
{
    if (dotop(w, ".NOT.")) {
        int32_t a = e_rel(w), n;
        if (a < 0) return -1;
        if (w->pool[a].type != T_LOG) {
            ffail(w, ZL_E_SYNTAX, ".NOT. needs a LOGICAL operand");
            return -1;
        }
        if ((n = node(w, E_NOT, T_LOG)) < 0) return -1;
        w->pool[n].l = a;
        return n;
    }
    return e_rel(w);
}

static int logpair(fw_t *w, int32_t l, int32_t r)
{
    if (l < 0 || r < 0) return 0;
    if (w->pool[l].type != T_LOG || w->pool[r].type != T_LOG) {
        ffail(w, ZL_E_SYNTAX, ".AND./.OR. need LOGICAL operands");
        return 0;
    }
    return 1;
}

static int32_t e_and(fw_t *w)
{
    int32_t l = e_not(w);
    while (l >= 0 && dotop(w, ".AND.")) {
        int32_t r = e_not(w);
        if (!logpair(w, l, r)) return -1;
        l = mkbin(w, E_AND, 0, T_LOG, l, r);
    }
    return l;
}

static int32_t e_or(fw_t *w)
{
    int32_t l = e_and(w);
    if (l >= 0 && (kwp(w, ".EQV.") || kwp(w, ".NEQV."))) {
        ffail(w, ZL_E_UNSUPPORTED, ".EQV./.NEQV. are not in this subset");
        return -1;
    }
    while (l >= 0 && dotop(w, ".OR.")) {
        int32_t r = e_and(w);
        if (!logpair(w, l, r)) return -1;
        l = mkbin(w, E_OR, 0, T_LOG, l, r);
    }
    return l;
}

/* ------------------------------------------------------------ printing */

static int fprec(const fnode_t *n)
{
    switch (n->kind) {
    case E_OR:
        return 1;
    case E_AND:
        return 2;
    case E_NOT:
        return 3;
    case E_REL:
        return 4;
    case E_NEG:
        return 5;
    case E_BIN:
        return n->op == '+' || n->op == '-' ? 5 : n->op == 'P' ? 7 : 6;
    default:
        return 8;
    }
}

static void fpe(fw_t *w, int32_t id, int need_p)
{
    zl_out_t *o = &w->lo;
    const fnode_t *n = &w->pool[id];
    int p = fprec(n), paren = p < need_p;
    if (paren) zl_putc(o, '(');
    switch (n->kind) {
    case E_NUM:
        zl_puti(o, n->v);
        break;
    case E_VAR:
        zl_put(o, w->sym[n->sym].name, w->sym[n->sym].namelen);
        break;
    case E_ARR:
        zl_put(o, w->sym[n->sym].name, w->sym[n->sym].namelen);
        zl_putc(o, '(');
        fpe(w, n->l, 0);
        zl_putc(o, ')');
        break;
    case E_FN: {
        static const char *const nm[] = {"MOD",  "ABS", "IABS", "MAX",
                                         "MAX0", "MIN", "MIN0", "ISIGN"};
        zl_puts(o, nm[n->v]);
        zl_putc(o, '(');
        for (int32_t a = n->l; a >= 0; a = w->pool[a].next) {
            fpe(w, a, 0);
            if (w->pool[a].next >= 0) zl_puts(o, ", ");
        }
        zl_putc(o, ')');
        break;
    }
    case E_NEG:
        zl_putc(o, '-');
        fpe(w, n->l, 6);
        break;
    case E_NOT:
        zl_puts(o, ".NOT. ");
        fpe(w, n->l, 4);
        break;
    case E_BIN:
    case E_REL:
    case E_AND:
    case E_OR: {
        const char *op;
        int lp = p, rp = p + 1;
        if (n->kind == E_AND)
            op = " .AND. ";
        else if (n->kind == E_OR)
            op = " .OR. ";
        else if (n->kind == E_REL) {
            op = n->op == ZL_EQ   ? " .EQ. "
                 : n->op == ZL_NE ? " .NE. "
                 : n->op == ZL_LT ? " .LT. "
                 : n->op == ZL_LE ? " .LE. "
                 : n->op == ZL_GT ? " .GT. "
                                  : " .GE. ";
            lp = 5;
            rp = 5;
        } else {
            op = n->op == '+'   ? " + "
                 : n->op == '-' ? " - "
                 : n->op == '*' ? " * "
                 : n->op == '/' ? " / "
                                : " ** ";
            if (n->op == 'P') {
                lp = 8;
                rp = 7;
            }
        }
        fpe(w, n->l, lp);
        zl_puts(o, op);
        fpe(w, n->r, rp);
        break;
    }
    }
    if (paren) zl_putc(o, ')');
}

static void te(fw_t *w, int32_t id)
{
    if (w->tw) fpe(w, id, 0);
}

/* ------------------------------------------------------------ lowering */

static int lower(fw_t *w, int32_t id, uint32_t depth)
{
    zl_cc_t *c = w->c;
    const fnode_t *n = &w->pool[id];
    int s;
    if (depth > 2 * ZL_MAX_DEPTH) return ffail(w, ZL_E_LIMIT, "expression nested too deeply");
    switch (n->kind) {
    case E_NUM:
        zl_emit(c, ZL_PUSH, n->v, 0);
        break;
    case E_VAR:
        if (w->sym[n->sym].kind == S_PARAM) {
            zl_emit(c, ZL_PUSH, w->sym[n->sym].val, 0);
            break;
        }
        if ((s = sym_slot(w, n->sym))) return s;
        zl_emit(c, ZL_LOAD, w->sym[n->sym].slot, 0);
        break;
    case E_ARR: {
        fsym_t *y = &w->sym[n->sym];
        if ((s = sym_slot(w, n->sym))) return s;
        if ((s = lower(w, n->l, depth + 1))) return s;
        if (y->lo) {
            zl_emit(c, ZL_PUSH, y->lo, 0);
            zl_emit(c, ZL_SUB, 0, 0);
        }
        zl_emit(c, ZL_LOADX, y->slot, (int32_t) y->len);
        break;
    }
    case E_NEG:
    case E_NOT:
        if ((s = lower(w, n->l, depth + 1))) return s;
        zl_emit(c, n->kind == E_NEG ? ZL_NEG : ZL_NOT, 0, 0);
        break;
    case E_FN: {
        int32_t a = n->l;
        if ((s = lower(w, a, depth + 1))) return s;
        if (n->fn == FN_ABS) {
            zl_emit(c, ZL_ABS, 0, 0);
            break;
        }
        if (n->fn == FN_ISIGN) {
            /* |a| * (b < 0 ? -1 : 1) */
            zl_emit(c, ZL_ABS, 0, 0);
            if ((s = lower(w, w->pool[a].next, depth + 1))) return s;
            zl_emit(c, ZL_PUSH, 0, 0);
            zl_emit(c, ZL_LT, 0, 0);
            zl_emit(c, ZL_PUSH, -2, 0);
            zl_emit(c, ZL_MUL, 0, 0);
            zl_emit(c, ZL_PUSH, 1, 0);
            zl_emit(c, ZL_ADD, 0, 0);
            zl_emit(c, ZL_MUL, 0, 0);
            break;
        }
        for (a = w->pool[a].next; a >= 0; a = w->pool[a].next) {
            if ((s = lower(w, a, depth + 1))) return s;
            zl_emit(c, n->fn == FN_MOD ? ZL_MODT : n->fn == FN_MAX ? ZL_MAX : ZL_MIN, 0, 0);
        }
        break;
    }
    default:
        if ((s = lower(w, n->l, depth + 1))) return s;
        if ((s = lower(w, n->r, depth + 1))) return s;
        if (n->kind == E_REL)
            zl_emit(c, n->op, 0, 0);
        else if (n->kind == E_AND)
            zl_emit(c, ZL_AND, 0, 0);
        else if (n->kind == E_OR)
            zl_emit(c, ZL_OR, 0, 0);
        else
            switch (n->op) {
            case '+':
                zl_emit(c, ZL_ADD, 0, 0);
                break;
            case '-':
                zl_emit(c, ZL_SUB, 0, 0);
                break;
            case '*':
                zl_emit(c, ZL_MUL, 0, 0);
                break;
            case '/':
                zl_emit(c, ZL_DIVT, 0, 0);
                break;
            default:
                zl_emit(c, ZL_POW, 0, 0);
                break;
            }
        break;
    }
    return c->failed ? c->d->status : ZL_OK;
}

/* Constant value of a PARAMETER expression. */
static int cfold(fw_t *w, int32_t id, int64_t *v, uint32_t depth)
{
    const fnode_t *n = &w->pool[id];
    int64_t a, b;
    int s;
    if (depth > ZL_MAX_DEPTH) return ffail(w, ZL_E_LIMIT, "expression nested too deeply");
    switch (n->kind) {
    case E_NUM:
        *v = n->v;
        return ZL_OK;
    case E_VAR:
        if (w->sym[n->sym].kind != S_PARAM)
            return ffail(w, ZL_E_SYNTAX, "PARAMETER value must be constant");
        *v = w->sym[n->sym].val;
        return ZL_OK;
    case E_NEG:
        if ((s = cfold(w, n->l, &a, depth + 1))) return s;
        *v = -a;
        break;
    case E_BIN:
        if ((s = cfold(w, n->l, &a, depth + 1)) || (s = cfold(w, n->r, &b, depth + 1))) return s;
        if (n->op == '+')
            *v = a + b;
        else if (n->op == '-')
            *v = a - b;
        else if (n->op == '*')
            *v = a * b;
        else if (n->op == '/') {
            if (b == 0) return ffail(w, ZL_E_DIVZERO, "division by zero in PARAMETER");
            *v = a / b;
        } else {
            if (b < 0 || b > 62) return ffail(w, ZL_E_UNSUPPORTED, "PARAMETER power out of range");
            int64_t r = 1;
            for (int64_t k = 0; k < b; k++) {
                r *= a;
                if (r > INT32_MAX || r < INT32_MIN) break;
            }
            *v = r;
        }
        break;
    default:
        return ffail(w, ZL_E_SYNTAX, "PARAMETER value must be a constant INTEGER expression");
    }
    if (*v > INT32_MAX || *v < INT32_MIN)
        return ffail(w, ZL_E_OVERFLOW, "PARAMETER value wider than 32 bits");
    return ZL_OK;
}

/* ------------------------------------------------------------ statements */

static int add_fix(fw_t *w, int32_t at_, uint32_t label)
{
    if (at_ < 0) return ZL_E_LIMIT;
    if (w->nfix >= F_FIX) return ffail(w, ZL_E_LIMIT, "too many jumps");
    w->fix[w->nfix].at = at_;
    w->fix[w->nfix].label = label;
    w->fix[w->nfix].srcline = w->c->line;
    w->nfix++;
    return ZL_OK;
}

static int32_t tmp(fw_t *w)
{
    if (w->tmp_slot < 0) w->tmp_slot = zl_slots(w->c, 1);
    return w->tmp_slot;
}

typedef struct {
    int32_t sym, idx;
} ftarget_t;

static int target(fw_t *w, ftarget_t *t)
{
    char nm[32];
    uint8_t nl;
    int r = name(w, nm, &nl);
    if (r <= 0) return ffail(w, ZL_E_SYNTAX, "variable expected");
    t->sym = sym_use(w, nm, nl);
    t->idx = -1;
    if (t->sym < 0) return w->c->d->status;
    fsym_t *y = &w->sym[t->sym];
    if (y->kind == S_PARAM) return ffail(w, ZL_E_SYNTAX, "cannot assign to a PARAMETER");
    if (y->kind == S_ARRAY) {
        if (need(w, '(', "an array needs a subscript")) return ZL_E_SYNTAX;
        t->idx = e_arith(w);
        if (t->idx < 0) return w->c->d->status;
        if (at(w) == ',')
            return ffail(w, ZL_E_UNSUPPORTED, "multi-dimensional arrays are not in this subset");
        if (w->pool[t->idx].type != T_INT)
            return ffail(w, ZL_E_SYNTAX, "subscript must be INTEGER");
        if (need(w, ')', "')' expected")) return ZL_E_SYNTAX;
    } else if (at(w) == '(') {
        return ffail(w, ZL_E_SYNTAX, "subscript on a name that is not an array");
    }
    return ZL_OK;
}

static void ttarget(fw_t *w, const ftarget_t *t)
{
    if (!w->tw) return;
    tname(w, t->sym);
    if (t->idx >= 0) {
        ts(w, "(");
        fpe(w, t->idx, 0);
        ts(w, ")");
    }
}

static int store_pre(fw_t *w, const ftarget_t *t)
{
    int s;
    if ((s = sym_slot(w, t->sym))) return s;
    if (t->idx < 0) return ZL_OK;
    if ((s = lower(w, t->idx, 0))) return s;
    if (w->sym[t->sym].lo) {
        zl_emit(w->c, ZL_PUSH, w->sym[t->sym].lo, 0);
        zl_emit(w->c, ZL_SUB, 0, 0);
    }
    return w->c->failed ? w->c->d->status : ZL_OK;
}

static int store_post(fw_t *w, const ftarget_t *t)
{
    fsym_t *y = &w->sym[t->sym];
    if (t->idx >= 0)
        zl_emit(w->c, ZL_STOREX, y->slot, (int32_t) y->len);
    else
        zl_emit(w->c, ZL_STORE, y->slot, 0);
    return w->c->failed ? w->c->d->status : ZL_OK;
}

static int32_t cond(fw_t *w)
{
    if (need(w, '(', "'(' expected")) return -1;
    int32_t e = e_or(w);
    if (e < 0 || need(w, ')', "')' expected")) return -1;
    return e;
}

static int st_assign(fw_t *w)
{
    ftarget_t t;
    int s;
    if ((s = target(w, &t))) return s;
    if (need(w, '=', "'=' expected")) return ZL_E_SYNTAX;
    int32_t e = e_or(w);
    if (e < 0) return w->c->d->status;
    if (w->pool[e].type != T_INT)
        return ffail(w, ZL_E_SYNTAX, "a LOGICAL value cannot be stored in an INTEGER");
    ttarget(w, &t);
    ts(w, " = ");
    te(w, e);
    if ((s = store_pre(w, &t)) || (s = lower(w, e, 0))) return s;
    return store_post(w, &t);
}

/* PRINT / WRITE item list, laid out as gfortran's list-directed output. */
static int io_list_out(fw_t *w, int empty_ok)
{
    zl_cc_t *c = w->c;
    int prev = 0; /* 0 none, 1 integer, 2 character */
    int first = 1;
    if (at(w) == 0) {
        if (!empty_ok) return ffail(w, ZL_E_SYNTAX, "output list expected");
        zl_emit(c, ZL_NL, 0, 0);
        return ZL_OK;
    }
    do {
        ts(w, first ? " " : ", ");
        int q = at(w);
        /* gfortran starts a record with a blank; an integer's 12-column
         * field already begins with one */
        if (first && (q == '\'' || q == '"')) {
            int32_t k = zl_addstr(c, " ", 1);
            zl_emit(c, ZL_OUTS, k, 0);
        }
        first = 0;
        if (q == '\'' || q == '"') {
            char buf[F_TEXT];
            size_t n = 0;
            w->ti++;
            for (;;) {
                if (w->ti >= w->tn) return ffail(w, ZL_E_SYNTAX, "unterminated character constant");
                if (w->t[w->ti] == q) {
                    if (w->ti + 1 < w->tn && w->t[w->ti + 1] == q) {
                        buf[n++] = (char) q;
                        w->ti += 2;
                        continue;
                    }
                    w->ti++;
                    break;
                }
                buf[n++] = w->t[w->ti++];
            }
            if (prev == 1) {
                int32_t k = zl_addstr(c, " ", 1);
                zl_emit(c, ZL_OUTS, k, 0);
            }
            int32_t k = zl_addstr(c, buf, n);
            if (k < 0) return ZL_E_LIMIT;
            zl_emit(c, ZL_OUTS, k, 0);
            if (w->tw) {
                zl_putc(&w->lo, '\'');
                for (size_t i = 0; i < n; i++) {
                    if (buf[i] == '\'') zl_putc(&w->lo, '\'');
                    zl_putc(&w->lo, buf[i]);
                }
                zl_putc(&w->lo, '\'');
            }
            prev = 2;
        } else if (q == '(') {
            /* an implied DO list starts with '(' and has '=' inside */
            size_t k = w->ti;
            int depth = 0;
            for (; k < w->tn; k++) {
                if (w->t[k] == '(')
                    depth++;
                else if (w->t[k] == ')') {
                    if (--depth == 0) break;
                } else if (w->t[k] == '=' && depth == 1)
                    return ffail(w, ZL_E_UNSUPPORTED, "implied DO lists are not in this subset");
            }
            goto expr_item;
        } else {
        expr_item:;
            int32_t e = e_or(w);
            if (e < 0) return c->d->status;
            if (w->pool[e].type != T_INT)
                return ffail(w, ZL_E_UNSUPPORTED, "printing LOGICAL values is not in this subset");
            te(w, e);
            int s = lower(w, e, 0);
            if (s) return s;
            zl_emit(c, ZL_OUTI, 0, 12);
            prev = 1;
        }
    } while (eat(w, ','));
    zl_emit(c, ZL_NL, 0, 0);
    return c->failed ? c->d->status : ZL_OK;
}

static int io_list_in(fw_t *w)
{
    int first = 1;
    do {
        ftarget_t t;
        int s;
        if (at(w) == '(')
            return ffail(w, ZL_E_UNSUPPORTED, "implied DO lists are not in this subset");
        if ((s = target(w, &t))) return s;
        ts(w, first ? " " : ", ");
        first = 0;
        ttarget(w, &t);
        if ((s = store_pre(w, &t))) return s;
        zl_emit(w->c, ZL_IN, 0, 0);
        if ((s = store_post(w, &t))) return s;
    } while (eat(w, ','));
    return w->c->failed ? w->c->d->status : ZL_OK;
}

/* Close every DO whose terminal statement carries this label. */
static int close_do(fw_t *w, fctl_t *d)
{
    zl_cc_t *c = w->c;
    if (d->kind == 3) {
        zl_emit(c, ZL_JMP, d->top, 0);
        zl_patch(c, d->exitj, (int64_t) c->p->ncode);
        return c->failed ? c->d->status : ZL_OK;
    }
    zl_emit(c, ZL_LOAD, d->var, 0);
    zl_emit(c, ZL_LOAD, d->step, 0);
    zl_emit(c, ZL_ADD, 0, 0);
    zl_emit(c, ZL_STORE, d->var, 0);
    zl_emit(c, ZL_LOAD, d->cnt, 0);
    zl_emit(c, ZL_PUSH, 1, 0);
    zl_emit(c, ZL_SUB, 0, 0);
    zl_emit(c, ZL_STORE, d->cnt, 0);
    zl_emit(c, ZL_JMP, d->top, 0);
    zl_patch(c, d->exitj, (int64_t) c->p->ncode);
    return c->failed ? c->d->status : ZL_OK;
}

static int st_do(fw_t *w, uint32_t label)
{
    zl_cc_t *c = w->c;
    ftarget_t t;
    int s;
    if (w->nctl >= ZL_MAX_DEPTH) return ffail(w, ZL_E_LIMIT, "DO and IF blocks nested too deeply");
    fctl_t *d = &w->ctl[w->nctl];
    zl_bzero(d, sizeof *d);
    ts(w, "DO");
    if (label) {
        ts(w, " ");
        ti(w, label);
    }
    if (kwp(w, "WHILE")) {
        if (label) return ffail(w, ZL_E_UNSUPPORTED, "a labelled DO WHILE is not in this subset");
        int32_t e = cond(w);
        if (e < 0) return c->d->status;
        if (w->pool[e].type != T_LOG)
            return ffail(w, ZL_E_SYNTAX, "DO WHILE needs a LOGICAL condition");
        ts(w, " WHILE (");
        te(w, e);
        ts(w, ")");
        d->kind = 3;
        d->top = (int32_t) c->p->ncode;
        if ((s = lower(w, e, 0))) return s;
        d->exitj = zl_emit(c, ZL_JZ, 0, 0);
        w->nctl++;
        return c->failed ? c->d->status : ZL_OK;
    }
    if ((s = target(w, &t))) return s;
    if (t.idx >= 0) return ffail(w, ZL_E_SYNTAX, "the DO variable must be a scalar");
    if (need(w, '=', "'=' expected")) return ZL_E_SYNTAX;
    int32_t e1 = e_arith(w), e2 = -1, e3 = -1;
    if (e1 < 0 || need(w, ',', "',' expected")) return c->d->status;
    if ((e2 = e_arith(w)) < 0) return c->d->status;
    if (eat(w, ',') && (e3 = e_arith(w)) < 0) return c->d->status;
    if (w->pool[e1].type != T_INT || w->pool[e2].type != T_INT ||
        (e3 >= 0 && w->pool[e3].type != T_INT))
        return ffail(w, ZL_E_SYNTAX, "DO bounds must be INTEGER");
    ts(w, " ");
    ttarget(w, &t);
    ts(w, " = ");
    te(w, e1);
    ts(w, ", ");
    te(w, e2);
    if (e3 >= 0) {
        ts(w, ", ");
        te(w, e3);
    }
    if ((s = sym_slot(w, t.sym))) return s;
    d->kind = label ? 1 : 2;
    d->label = label;
    d->var = w->sym[t.sym].slot;
    d->cnt = zl_slots(c, 1);
    d->step = zl_slots(c, 1);
    if (d->cnt < 0 || d->step < 0) return c->d->status;
    /* e1 e2 e3 first, then the trip count, then the variable (ANSI 11.10.3) */
    if ((s = lower(w, e1, 0)) || (s = lower(w, e2, 0))) return s;
    if (e3 >= 0) {
        if ((s = lower(w, e3, 0))) return s;
    } else
        zl_emit(c, ZL_PUSH, 1, 0);
    zl_emit(c, ZL_DUP, 0, 0);
    int32_t ok = zl_emit(c, ZL_JNZ, 0, 0);
    zl_emit(c, ZL_TRAP, ZL_E_DOMAIN, 0);
    zl_patch(c, ok, (int64_t) c->p->ncode);
    zl_emit(c, ZL_STORE, d->step, 0);
    zl_emit(c, ZL_OVER, 0, 0);
    zl_emit(c, ZL_SUB, 0, 0);
    zl_emit(c, ZL_LOAD, d->step, 0);
    zl_emit(c, ZL_ADD, 0, 0);
    zl_emit(c, ZL_LOAD, d->step, 0);
    zl_emit(c, ZL_DIVT, 0, 0);
    zl_emit(c, ZL_STORE, d->cnt, 0);
    zl_emit(c, ZL_STORE, d->var, 0);
    d->top = zl_emit(c, ZL_LOAD, d->cnt, 0);
    zl_emit(c, ZL_PUSH, 0, 0);
    zl_emit(c, ZL_GT, 0, 0);
    d->exitj = zl_emit(c, ZL_JZ, 0, 0);
    w->nctl++;
    return c->failed ? c->d->status : ZL_OK;
}

static int simple_statement(fw_t *w, uint32_t label, int in_logical_if);

static int st_if(fw_t *w, uint32_t label)
{
    zl_cc_t *c = w->c;
    int s;
    int32_t e = cond(w);
    if (e < 0) return c->d->status;
    if (zl_isdigit(at(w))) { /* arithmetic IF */
        uint32_t l1, l2, l3;
        if (w->pool[e].type != T_INT)
            return ffail(w, ZL_E_SYNTAX, "arithmetic IF needs an INTEGER expression");
        if ((s = labelref(w, &l1)) || need(w, ',', "',' expected") || (s = labelref(w, &l2)) ||
            need(w, ',', "',' expected") || (s = labelref(w, &l3)))
            return c->d->status;
        ts(w, "IF (");
        te(w, e);
        ts(w, ") ");
        ti(w, l1);
        ts(w, ", ");
        ti(w, l2);
        ts(w, ", ");
        ti(w, l3);
        int32_t t = tmp(w);
        if (t < 0 || (s = lower(w, e, 0))) return c->d->status;
        zl_emit(c, ZL_STORE, t, 0);
        zl_emit(c, ZL_LOAD, t, 0);
        zl_emit(c, ZL_PUSH, 0, 0);
        zl_emit(c, ZL_LT, 0, 0);
        if ((s = add_fix(w, zl_emit(c, ZL_JNZ, 0, 0), l1))) return s;
        zl_emit(c, ZL_LOAD, t, 0);
        if ((s = add_fix(w, zl_emit(c, ZL_JZ, 0, 0), l2))) return s;
        return add_fix(w, zl_emit(c, ZL_JMP, 0, 0), l3);
    }
    if (w->pool[e].type != T_LOG) return ffail(w, ZL_E_SYNTAX, "IF needs a LOGICAL condition");
    if (kwp(w, "THEN")) {
        if (at(w)) return ffail(w, ZL_E_SYNTAX, "unexpected text after THEN");
        if (w->nctl >= ZL_MAX_DEPTH)
            return ffail(w, ZL_E_LIMIT, "DO and IF blocks nested too deeply");
        fctl_t *d = &w->ctl[w->nctl++];
        zl_bzero(d, sizeof *d);
        d->kind = 4;
        ts(w, "IF (");
        te(w, e);
        ts(w, ") THEN");
        if ((s = lower(w, e, 0))) return s;
        d->pending = zl_emit(c, ZL_JZ, 0, 0);
        return c->failed ? c->d->status : ZL_OK;
    }
    ts(w, "IF (");
    te(w, e);
    ts(w, ") ");
    if ((s = lower(w, e, 0))) return s;
    int32_t skip = zl_emit(c, ZL_JZ, 0, 0);
    if ((s = simple_statement(w, label, 1))) return s;
    zl_patch(c, skip, (int64_t) c->p->ncode);
    return c->failed ? c->d->status : ZL_OK;
}

static int st_else(fw_t *w, int elseif)
{
    zl_cc_t *c = w->c;
    int s;
    if (w->nctl == 0 || w->ctl[w->nctl - 1].kind != 4)
        return ffail(w, ZL_E_SYNTAX, "ELSE without IF ... THEN");
    fctl_t *d = &w->ctl[w->nctl - 1];
    if (d->else_seen) return ffail(w, ZL_E_SYNTAX, "ELSE IF or ELSE after ELSE");
    if (d->nends >= 32) return ffail(w, ZL_E_LIMIT, "too many ELSE IF branches");
    d->ends[d->nends++] = zl_emit(c, ZL_JMP, 0, 0);
    zl_patch(c, d->pending, (int64_t) c->p->ncode);
    d->pending = -1;
    if (!elseif) {
        d->else_seen = 1;
        if (at(w)) return ffail(w, ZL_E_SYNTAX, "unexpected text after ELSE");
        ts(w, "ELSE");
        return c->failed ? c->d->status : ZL_OK;
    }
    int32_t e = cond(w);
    if (e < 0) return c->d->status;
    if (w->pool[e].type != T_LOG) return ffail(w, ZL_E_SYNTAX, "ELSE IF needs a LOGICAL condition");
    if (!kwp(w, "THEN") || at(w)) return ffail(w, ZL_E_SYNTAX, "THEN expected");
    ts(w, "ELSE IF (");
    te(w, e);
    ts(w, ") THEN");
    if ((s = lower(w, e, 0))) return s;
    d->pending = zl_emit(c, ZL_JZ, 0, 0);
    return c->failed ? c->d->status : ZL_OK;
}

static int st_goto(fw_t *w)
{
    zl_cc_t *c = w->c;
    int s;
    uint32_t l;
    if (eat(w, '(')) { /* computed GOTO */
        uint32_t ls[64];
        unsigned n = 0;
        do {
            if (n >= 64) return ffail(w, ZL_E_LIMIT, "too many labels in a computed GOTO");
            if ((s = labelref(w, &ls[n++]))) return s;
        } while (eat(w, ','));
        if (need(w, ')', "')' expected")) return ZL_E_SYNTAX;
        eat(w, ',');
        int32_t e = e_arith(w);
        if (e < 0) return c->d->status;
        if (w->pool[e].type != T_INT)
            return ffail(w, ZL_E_SYNTAX, "computed GOTO needs an INTEGER");
        ts(w, "GOTO (");
        for (unsigned k = 0; k < n; k++) {
            if (k) ts(w, ", ");
            ti(w, ls[k]);
        }
        ts(w, "), ");
        te(w, e);
        int32_t t = tmp(w);
        if (t < 0 || (s = lower(w, e, 0))) return c->d->status;
        zl_emit(c, ZL_STORE, t, 0);
        for (unsigned k = 0; k < n; k++) {
            zl_emit(c, ZL_LOAD, t, 0);
            zl_emit(c, ZL_PUSH, k + 1, 0);
            zl_emit(c, ZL_EQ, 0, 0);
            if ((s = add_fix(w, zl_emit(c, ZL_JNZ, 0, 0), ls[k]))) return s;
        }
        return c->failed ? c->d->status : ZL_OK; /* out of range: fall through */
    }
    if ((s = labelref(w, &l))) return s;
    ts(w, "GOTO ");
    ti(w, l);
    return add_fix(w, zl_emit(c, ZL_JMP, 0, 0), l);
}

static int unit_star(fw_t *w) /* "(*,*)" or "(6,*)" after WRITE / READ */
{
    if (!eat(w, '(')) return 0;
    if (!eat(w, '*') && !kwp(w, "6") && !kwp(w, "5")) return -1;
    if (!eat(w, ',') || !eat(w, '*') || !eat(w, ')')) return -1;
    return 1;
}

/* Assignment: a top-level '=' (not part of == /= <= >=) preceded by
 * exactly a name or a name(...). FORTRAN has no reserved words, so this is
 * tested before any keyword. */
static int is_assignment(fw_t *w)
{
    size_t k, eq = 0;
    int depth = 0, q = 0, found = 0;
    for (k = w->ti; k < w->tn; k++) {
        char ch = w->t[k];
        if (q) {
            if (ch == q) q = 0;
            continue;
        }
        if (ch == '\'' || ch == '"')
            q = ch;
        else if (ch == '(')
            depth++;
        else if (ch == ')')
            depth--;
        else if (ch == '=' && depth == 0) {
            char p = k > w->ti ? w->t[k - 1] : 0;
            char n = k + 1 < w->tn ? w->t[k + 1] : 0;
            if (p != '/' && p != '<' && p != '>' && p != '=' && n != '=') {
                eq = k;
                found = 1;
                break;
            }
        }
    }
    if (!found) return 0;
    size_t j = w->ti;
    if (j >= eq || !zl_isalpha((unsigned char) w->t[j])) return 0;
    while (j < eq && (zl_isalnum((unsigned char) w->t[j]) || w->t[j] == '_')) j++;
    if (j < eq && w->t[j] == '(') {
        int d = 0;
        for (; j < eq; j++) {
            if (w->t[j] == '(')
                d++;
            else if (w->t[j] == ')' && --d == 0) {
                j++;
                break;
            }
        }
    }
    return j == eq;
}

/* Statements that may also follow a logical IF. */
static int simple_statement(fw_t *w, uint32_t label, int in_lif)
{
    zl_cc_t *c = w->c;
    if (is_assignment(w)) return st_assign(w);
    if (kwp(w, "GOTO")) return st_goto(w); /* GO TO squeezes to GOTO */
    if (kwp(w, "CONTINUE")) {
        ts(w, "CONTINUE");
        return at(w) ? ffail(w, ZL_E_SYNTAX, "unexpected text after CONTINUE") : ZL_OK;
    }
    if (kwp(w, "STOP")) {
        if (at(w))
            return ffail(w, ZL_E_UNSUPPORTED, "STOP with a code or message is not in this subset");
        ts(w, "STOP");
        zl_emit(c, ZL_HALT, 0, 0);
        return c->failed ? c->d->status : ZL_OK;
    }
    if (kwp(w, "PRINT")) {
        if (!eat(w, '*'))
            return ffail(w, ZL_E_UNSUPPORTED,
                         "PRINT with a FORMAT is not in this subset (use PRINT *)");
        ts(w, "PRINT *");
        if (at(w) == 0) {
            zl_emit(c, ZL_NL, 0, 0);
            return c->failed ? c->d->status : ZL_OK;
        }
        if (!eat(w, ',')) return ffail(w, ZL_E_SYNTAX, "',' expected after PRINT *");
        ts(w, ",");
        return io_list_out(w, 0);
    }
    if (kwp(w, "WRITE")) {
        size_t save = w->ti;
        int u = unit_star(w);
        if (u <= 0)
            return ffail(w, ZL_E_UNSUPPORTED,
                         "WRITE with a unit or FORMAT other than (*,*) is not in this subset");
        ts(w, "WRITE ");
        if (w->tw) zl_put(&w->lo, w->t + save, 1);
        if (w->tw) {
            zl_put(&w->lo, w->t + save + 1, 1);
            ts(w, ", *)");
        }
        if (at(w) == 0) {
            zl_emit(c, ZL_NL, 0, 0);
            return c->failed ? c->d->status : ZL_OK;
        }
        return io_list_out(w, 0);
    }
    if (kwp(w, "READ")) {
        if (eat(w, '*')) {
            if (!eat(w, ',')) return ffail(w, ZL_E_SYNTAX, "',' expected after READ *");
            ts(w, "READ *,");
        } else {
            size_t save = w->ti;
            int u = unit_star(w);
            if (u <= 0)
                return ffail(w, ZL_E_UNSUPPORTED,
                             "READ with a unit or FORMAT other than (*,*) is not in this subset");
            ts(w, "READ ");
            if (w->tw) {
                zl_put(&w->lo, w->t + save, 2);
                ts(w, ", *)");
            }
        }
        return io_list_in(w);
    }
    if (!in_lif && kwp(w, "IF")) return st_if(w, label);
    static const char *const unsup[] = {"CALL",    "RETURN",  "ASSIGN", "PAUSE",
                                        "OPEN",    "CLOSE",   "REWIND", "BACKSPACE",
                                        "ENDFILE", "INQUIRE", 0};
    for (int k = 0; unsup[k]; k++)
        if (kwp(w, unsup[k]))
            return ffail(w, ZL_E_UNSUPPORTED, "statement not in this subset (subprograms, files)");
    return ffail(w, ZL_E_SYNTAX,
                 in_lif ? "statement not allowed after a logical IF" : "unknown statement");
}

static int letter_ranges(fw_t *w, uint8_t *set)
{
    if (need(w, '(', "'(' expected")) return ZL_E_SYNTAX;
    ts(w, " (");
    int first = 1;
    do {
        int a = at(w), b;
        if (!zl_isalpha(a)) return ffail(w, ZL_E_SYNTAX, "letter expected");
        w->ti++;
        b = a;
        if (eat(w, '-')) {
            b = at(w);
            if (!zl_isalpha(b) || b < a) return ffail(w, ZL_E_SYNTAX, "letter range expected");
            w->ti++;
        }
        for (int k = a; k <= b; k++) set[k - 'A'] = 1;
        if (!first) ts(w, ", ");
        first = 0;
        if (w->tw) {
            zl_putc(&w->lo, (char) a);
            if (b != a) {
                zl_putc(&w->lo, '-');
                zl_putc(&w->lo, (char) b);
            }
        }
    } while (eat(w, ','));
    ts(w, ")");
    return need(w, ')', "')' expected");
}

static int declarator(fw_t *w, int declare_int, int need_dims)
{
    char nm[32];
    uint8_t nl;
    int r = name(w, nm, &nl);
    if (r <= 0) return ffail(w, ZL_E_SYNTAX, "name expected");
    int32_t s = sym_find(w, nm, nl);
    if (s < 0 && (s = sym_add(w, nm, nl)) < 0) return ZL_E_LIMIT;
    fsym_t *y = &w->sym[s];
    if (declare_int) {
        if (y->declared) return ffail(w, ZL_E_SYNTAX, "name declared INTEGER twice");
        y->declared = 1;
    } else {
        int letter = nm[0] - 'A';
        if (!y->declared &&
            !((letter >= 'I' - 'A' && letter <= 'N' - 'A') || w->intletter[letter])) {
            if (w->implicit_none)
                return ffail(w, ZL_E_UNDEFINED, "name not declared (IMPLICIT NONE)");
            return ffail(w, ZL_E_UNSUPPORTED,
                         "implicitly REAL array (exact subset: declare it INTEGER)");
        }
    }
    tname(w, s);
    if (eat(w, '(')) {
        int64_t lo = 1, hi;
        int neg = eat(w, '-');
        int st;
        if (y->kind == S_ARRAY) return ffail(w, ZL_E_SYNTAX, "array dimensioned twice");
        if (zl_isalpha(at(w))) { /* a PARAMETER bound */
            char pn[32];
            uint8_t pl;
            if (name(w, pn, &pl) <= 0) return ffail(w, ZL_E_SYNTAX, "bound expected");
            int32_t ps = sym_find(w, pn, pl);
            if (ps < 0 || w->sym[ps].kind != S_PARAM)
                return ffail(w, ZL_E_UNSUPPORTED, "array bounds must be constants or PARAMETERs");
            hi = w->sym[ps].val;
            ts(w, "(");
            if (neg) ts(w, "-");
            tname(w, ps);
        } else {
            if ((st = intconst(w, &hi))) return st;
            ts(w, "(");
            if (neg) ts(w, "-");
            ti(w, hi);
        }
        if (neg) hi = -hi;
        if (eat(w, ':')) {
            lo = hi;
            neg = eat(w, '-');
            if ((st = intconst(w, &hi))) return st;
            if (neg) hi = -hi;
            ts(w, ":");
            ti(w, hi);
        }
        ts(w, ")");
        if (at(w) == ',' && w->ti + 1 < w->tn && zl_isdigit((unsigned char) w->t[w->ti + 1]))
            return ffail(w, ZL_E_UNSUPPORTED, "multi-dimensional arrays are not in this subset");
        if (need(w, ')', "')' expected")) return ZL_E_SYNTAX;
        if (hi < lo || hi - lo + 1 > ZL_MAX_SLOTS)
            return ffail(w, ZL_E_LIMIT, "array bounds out of range");
        if (y->slot >= 0) return ffail(w, ZL_E_SYNTAX, "a scalar cannot become an array");
        y->kind = S_ARRAY;
        y->lo = lo;
        y->len = hi - lo + 1;
    } else if (need_dims) {
        return ffail(w, ZL_E_SYNTAX, "DIMENSION needs bounds");
    }
    return ZL_OK;
}

static int spec_statement(fw_t *w, int *handled)
{
    *handled = 1;
    if (kwp(w, "IMPLICITNONE")) {
        if (at(w)) return ffail(w, ZL_E_SYNTAX, "unexpected text after IMPLICIT NONE");
        w->implicit_none = 1;
        ts(w, "IMPLICIT NONE");
        return ZL_OK;
    }
    if (kwp(w, "IMPLICITINTEGER")) {
        ts(w, "IMPLICIT INTEGER");
        return letter_ranges(w, w->intletter);
    }
    if (kwp(w, "IMPLICIT"))
        return ffail(w, ZL_E_UNSUPPORTED,
                     "only IMPLICIT NONE and IMPLICIT INTEGER are in this subset");
    if (kwp(w, "INTEGER")) {
        if (at(w) == '*')
            return ffail(w, ZL_E_UNSUPPORTED, "INTEGER*n kinds are not in this subset");
        ts(w, "INTEGER ");
        int first = 1;
        do {
            if (!first) ts(w, ", ");
            first = 0;
            int s = declarator(w, 1, 0);
            if (s) return s;
        } while (eat(w, ','));
        return at(w) ? ffail(w, ZL_E_SYNTAX, "unexpected text in INTEGER") : ZL_OK;
    }
    if (kwp(w, "DIMENSION")) {
        ts(w, "DIMENSION ");
        int first = 1;
        do {
            if (!first) ts(w, ", ");
            first = 0;
            int s = declarator(w, 0, 1);
            if (s) return s;
        } while (eat(w, ','));
        return at(w) ? ffail(w, ZL_E_SYNTAX, "unexpected text in DIMENSION") : ZL_OK;
    }
    if (kwp(w, "PARAMETER")) {
        if (need(w, '(', "'(' expected")) return ZL_E_SYNTAX;
        ts(w, "PARAMETER (");
        int first = 1;
        do {
            char nm[32];
            uint8_t nl;
            if (name(w, nm, &nl) <= 0) return ffail(w, ZL_E_SYNTAX, "name expected");
            int32_t s = sym_find(w, nm, nl);
            if (s >= 0 && (w->sym[s].kind || w->sym[s].slot >= 0))
                return ffail(w, ZL_E_SYNTAX, "PARAMETER name already in use");
            int letter = nm[0] - 'A';
            if (s < 0) {
                if (w->implicit_none)
                    return ffail(w, ZL_E_UNDEFINED, "name not declared (IMPLICIT NONE)");
                if (!((letter >= 'I' - 'A' && letter <= 'N' - 'A') || w->intletter[letter]))
                    return ffail(w, ZL_E_UNSUPPORTED,
                                 "implicitly REAL PARAMETER (declare it INTEGER)");
                if ((s = sym_add(w, nm, nl)) < 0) return ZL_E_LIMIT;
            }
            if (need(w, '=', "'=' expected")) return ZL_E_SYNTAX;
            int32_t e = e_arith(w);
            int64_t v;
            if (e < 0) return w->c->d->status;
            int st = cfold(w, e, &v, 0);
            if (st) return st;
            if (!first) ts(w, ", ");
            first = 0;
            tname(w, s);
            ts(w, " = ");
            te(w, e);
            w->sym[s].kind = S_PARAM;
            w->sym[s].val = v;
        } while (eat(w, ','));
        ts(w, ")");
        if (need(w, ')', "')' expected")) return ZL_E_SYNTAX;
        return at(w) ? ffail(w, ZL_E_SYNTAX, "unexpected text after PARAMETER") : ZL_OK;
    }
    static const char *const unsup[] = {"REAL",     "DOUBLEPRECISION", "COMPLEX",     "CHARACTER",
                                        "LOGICAL",  "COMMON",          "EQUIVALENCE", "DATA",
                                        "SAVE",     "EXTERNAL",        "INTRINSIC",   "SUBROUTINE",
                                        "FUNCTION", "BLOCKDATA",       "FORMAT",      "ENTRY",
                                        0};
    for (int k = 0; unsup[k]; k++)
        if (kwp(w, unsup[k]))
            return ffail(
                w, ZL_E_UNSUPPORTED,
                "statement not in this subset (non-INTEGER types, subprograms, FORMAT, DATA)");
    *handled = 0;
    return ZL_OK;
}

static int is_do(fw_t *w)
{
    /* DO [label] name = e1, e2 ...  (a top-level comma after '=') or DO WHILE / bare DO */
    size_t k = w->ti;
    if (!(k + 1 < w->tn && w->t[k] == 'D' && w->t[k + 1] == 'O')) return 0;
    k += 2;
    while (k < w->tn && zl_isdigit((unsigned char) w->t[k])) k++;
    if (k + 5 <= w->tn && w->t[k] == 'W' && w->t[k + 1] == 'H' && w->t[k + 2] == 'I' &&
        w->t[k + 3] == 'L' && w->t[k + 4] == 'E' && k + 5 < w->tn && w->t[k + 5] == '(')
        return 1;
    if (!(k < w->tn && zl_isalpha((unsigned char) w->t[k]))) return 0;
    int depth = 0, seen_eq = 0;
    for (; k < w->tn; k++) {
        char ch = w->t[k];
        if (ch == '(')
            depth++;
        else if (ch == ')')
            depth--;
        else if (ch == '=' && depth == 0)
            seen_eq = 1;
        else if (ch == ',' && depth == 0 && seen_eq)
            return 1;
    }
    return 0;
}

static int statement(fw_t *w, uint32_t label)
{
    zl_cc_t *c = w->c;
    int s, handled;
    w->indent = w->nctl;
    if (label) {
        for (uint32_t k = 0; k < w->nlab; k++)
            if (w->lab[k] == label) return ffail(w, ZL_E_SYNTAX, "label used twice");
        if (w->nlab >= F_LABELS) return ffail(w, ZL_E_LIMIT, "too many labels");
        w->lab[w->nlab] = label;
        w->labaddr[w->nlab] = (int32_t) c->p->ncode;
        w->nlab++;
    }
    if (w->ended) return ffail(w, ZL_E_SYNTAX, "statement after END");
    if (kwp(w, "PROGRAM")) {
        char nm[32];
        uint8_t nl;
        if (w->program_seen || w->executable || w->nsym)
            return ffail(w, ZL_E_SYNTAX, "PROGRAM must be the first statement");
        if (name(w, nm, &nl) <= 0 || at(w)) return ffail(w, ZL_E_SYNTAX, "PROGRAM name expected");
        w->program_seen = 1;
        ts(w, "PROGRAM ");
        if (w->tw) zl_put(&w->lo, nm, nl);
        return ZL_OK;
    }
    if (is_do(w)) {
        uint32_t dl = 0;
        w->executable = 1;
        w->ti += 2;
        if (zl_isdigit(at(w)) && (s = labelref(w, &dl))) return s;
        eat(w, ',');
        return st_do(w, dl);
    }
    if (is_assignment(w)) {
        w->executable = 1;
        return st_assign(w);
    }
    {
        size_t save = w->ti;
        s = spec_statement(w, &handled);
        if (s) return s;
        if (handled) {
            if (w->executable)
                return ffail(w, ZL_E_SYNTAX,
                             "specification statement after an executable statement");
            if (label)
                return ffail(w, ZL_E_SYNTAX, "a specification statement cannot carry a label");
            return ZL_OK;
        }
        w->ti = save;
    }
    w->executable = 1;
    if (kwp(w, "ENDDO")) {
        if (w->indent) w->indent--;
        if (at(w)) return ffail(w, ZL_E_SYNTAX, "unexpected text after END DO");
        if (w->nctl == 0 || (w->ctl[w->nctl - 1].kind != 2 && w->ctl[w->nctl - 1].kind != 3))
            return ffail(w, ZL_E_SYNTAX, "END DO without DO");
        ts(w, "END DO");
        return close_do(w, &w->ctl[--w->nctl]);
    }
    if (kwp(w, "ENDIF")) {
        if (w->indent) w->indent--;
        if (at(w)) return ffail(w, ZL_E_SYNTAX, "unexpected text after END IF");
        if (w->nctl == 0 || w->ctl[w->nctl - 1].kind != 4)
            return ffail(w, ZL_E_SYNTAX, "END IF without IF ... THEN");
        fctl_t *d = &w->ctl[--w->nctl];
        ts(w, "END IF");
        if (d->pending >= 0) zl_patch(c, d->pending, (int64_t) c->p->ncode);
        for (uint32_t k = 0; k < d->nends; k++) zl_patch(c, d->ends[k], (int64_t) c->p->ncode);
        return ZL_OK;
    }
    if (kwp(w, "ELSEIF")) {
        if (w->indent) w->indent--;
        return st_else(w, 1);
    }
    if (kwp(w, "ELSE")) {
        if (w->indent) w->indent--;
        return st_else(w, 0);
    }
    if (kwp(w, "END")) {
        if (at(w)) {
            /* END PROGRAM [name] */
            if (!kwp(w, "PROGRAM")) return ffail(w, ZL_E_SYNTAX, "unexpected text after END");
            while (zl_isalnum(at(w)) || at(w) == '_') w->ti++;
            if (at(w)) return ffail(w, ZL_E_SYNTAX, "unexpected text after END PROGRAM");
            return ffail(w, ZL_E_UNSUPPORTED,
                         "END PROGRAM is Fortran 90; this subset is FORTRAN 77 (use END)");
        }
        if (w->nctl) return ffail(w, ZL_E_SYNTAX, "END inside an open DO or IF block");
        ts(w, "END");
        zl_emit(c, ZL_HALT, 0, 0);
        w->ended = 1;
        return c->failed ? c->d->status : ZL_OK;
    }
    return simple_statement(w, label, 0);
}

/* After a labelled statement: close the DO loops it terminates. */
static int terminals(fw_t *w, uint32_t label)
{
    int s;
    while (label && w->nctl && w->ctl[w->nctl - 1].kind == 1 &&
           w->ctl[w->nctl - 1].label == label) {
        if ((s = close_do(w, &w->ctl[--w->nctl]))) return s;
    }
    for (uint32_t k = 0; label && k < w->nctl; k++)
        if (w->ctl[k].kind == 1 && w->ctl[k].label == label)
            return ffail(w, ZL_E_SYNTAX, "DO loops are not properly nested");
    return ZL_OK;
}

static int is_do(fw_t *w);
static int bad_terminal(fw_t *w, uint32_t label, size_t start)
{
    int is_term = 0;
    for (uint32_t k = 0; label && k < w->nctl; k++)
        if (w->ctl[k].kind == 1 && w->ctl[k].label == label) is_term = 1;
    if (!is_term) return 0;
    w->ti = start;
    if (is_assignment(w)) return 0;
    static const char *const bad[] = {"GOTO", "STOP", "END", "ELSE", "RETURN", 0};
    for (int k = 0; bad[k]; k++) {
        size_t j = 0;
        while (bad[k][j] && start + j < w->tn && w->t[start + j] == bad[k][j]) j++;
        if (!bad[k][j]) return 1;
    }
    if (is_do(w)) return 1;
    if (start + 2 < w->tn && w->t[start] == 'I' && w->t[start + 1] == 'F' &&
        w->t[start + 2] == '(') {
        /* arithmetic or block IF cannot end a DO; a logical IF can */
        size_t k = start + 2;
        int d = 0;
        for (; k < w->tn; k++) {
            if (w->t[k] == '(')
                d++;
            else if (w->t[k] == ')' && --d == 0) {
                k++;
                break;
            }
        }
        if (k < w->tn &&
            (zl_isdigit((unsigned char) w->t[k]) || (w->tn - k == 4 && w->t[k] == 'T')))
            return 1;
    }
    return 0;
}

/* Emit one twin line: label field, statement, continuation lines. */
static void flush_twin(fw_t *w, uint32_t label)
{
    if (!w->tw) return;
    char field[8];
    size_t n = 0;
    if (label) n = zl_itoa(label, field);
    while (n < 6) field[n++] = ' ';
    zl_put(w->tw, field, 6);
    /* the statement field, indented two columns per open DO or IF block */
    size_t ind = 2 * (size_t) w->indent, len, off = 0;
    if (ind > 40) ind = 40;
    if (w->lo.len + ind >= sizeof w->tl) ind = 0;
    for (size_t k = w->lo.len; k > 0; k--) w->tl[k - 1 + ind] = w->tl[k - 1];
    for (size_t k = 0; k < ind; k++) w->tl[k] = ' ';
    len = w->lo.len + ind;
    for (;;) {
        size_t take = len - off > 66 ? 66 : len - off;
        zl_put(w->tw, w->tl + off, take);
        zl_putc(w->tw, '\n');
        off += take;
        if (off >= len) break;
        zl_puts(w->tw, "     &");
    }
    for (uint32_t k = 0; k < w->ncmt; k++) {
        zl_putc(w->tw, 'C');
        zl_put(w->tw, w->src + w->cmt[k].off, w->cmt[k].len);
        zl_putc(w->tw, '\n');
    }
    w->ncmt = 0;
}

static int queue_comment(fw_t *w, size_t off, size_t len)
{
    while (len && zl_isspace((unsigned char) w->src[off + len - 1])) len--;
    if (!w->tw) return ZL_OK;
    if (w->ncmt >= F_CMT) return ffail(w, ZL_E_LIMIT, "too many comment lines in a row");
    w->cmt[w->ncmt].off = (uint32_t) off;
    w->cmt[w->ncmt].len = (uint32_t) len;
    w->ncmt++;
    return ZL_OK;
}

typedef struct {
    int q; /* open quote character, or 0 */
} squeeze_t;

/* Append one line's statement field to w->t, dropping blanks outside
 * character constants and upper-casing, stopping at a '!' comment. */
static int squeeze(fw_t *w, squeeze_t *z, const char *src, size_t a, size_t b, int pad_to)
{
    size_t used = 0;
    for (size_t k = a; k < b; k++) {
        char ch = src[k];
        used++;
        if (z->q) {
            if (w->tn >= F_TEXT) return ffail(w, ZL_E_LIMIT, "statement too long");
            w->t[w->tn++] = ch;
            if (ch == z->q) z->q = 0; /* a doubled quote reopens on the next char */
            continue;
        }
        if (ch == '!') return queue_comment(w, k + 1, b - k - 1);
        if (ch == ' ' || ch == '\t') continue;
        if (ch == '\'' || ch == '"') z->q = ch;
        if (w->tn >= F_TEXT) return ffail(w, ZL_E_LIMIT, "statement too long");
        w->t[w->tn++] = (char) zl_upper((unsigned char) ch);
    }
    if (z->q && pad_to) /* a constant continued onto the next line: columns up to 72 are blanks */
        for (; used < (size_t) pad_to; used++) {
            if (w->tn >= F_TEXT) return ffail(w, ZL_E_LIMIT, "statement too long");
            w->t[w->tn++] = ' ';
        }
    return ZL_OK;
}

typedef struct {
    size_t a, b; /* statement field */
    uint32_t label;
    uint32_t line;
    int cont; /* continuation line */
    int comment;
    int blank;
} fline_t;

static int split_line(fw_t *w, const char *src, size_t a, size_t e, fline_t *L)
{
    zl_bzero(L, sizeof *L);
    if (e > a && src[e - 1] == '\r') e--;
    size_t n = e - a;
    size_t k;
    for (k = a; k < e && src[k] == ' '; k++) {
    }
    if (k == e) {
        L->blank = 1;
        return ZL_OK;
    }
    char c0 = src[a];
    if (c0 == 'C' || c0 == 'c' || c0 == '*' || c0 == '!') {
        L->comment = 1;
        L->a = a + 1;
        L->b = e;
        return ZL_OK;
    }
    /* label field: columns 1-5, or up to a tab */
    size_t col = 0, p = a;
    uint32_t label = 0;
    int digits = 0;
    while (p < e && col < 5 && src[p] != '\t') {
        char ch = src[p];
        if (zl_isdigit((unsigned char) ch)) {
            label = label * 10 + (uint32_t) (ch - '0');
            digits++;
        } else if (ch != ' ')
            return ffail(w, ZL_E_SYNTAX, "columns 1-5 hold only a statement label");
        p++;
        col++;
    }
    if (p < e && src[p] == '\t') {
        p++;
        L->a = p;
    } else if (col == 5) {
        if (p < e && src[p] != ' ' && src[p] != '0') {
            if (digits) return ffail(w, ZL_E_SYNTAX, "a continuation line cannot carry a label");
            L->cont = 1;
        }
        L->a = p < e ? p + 1 : e;
    } else {
        L->a = e; /* a short line: label only */
    }
    if (n > 72) e = a + 72;
    L->b = e < L->a ? L->a : e;
    if (digits && label == 0) return ffail(w, ZL_E_SYNTAX, "label 0 is not allowed");
    L->label = label;
    return ZL_OK;
}

int zl_fortran_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    fw_t *w = (fw_t *) work;
    zl_bzero(w, sizeof *w);
    w->c = c;
    w->tw = tw;
    w->src = src;
    w->tmp_slot = -1;
    c->p->word_bits = 32;
    int have = 0, s;
    uint32_t cur_label = 0, cur_line = 0;
    squeeze_t z = {0};
    size_t pos = 0;
    uint32_t line = 0;
    for (;;) {
        fline_t L;
        int eof = pos >= len;
        if (!eof) {
            size_t e = pos;
            while (e < len && src[e] != '\n') e++;
            line++;
            c->line = line;
            c->col = 0;
            if (e - pos > ZL_MAX_LINE) return ffail(w, ZL_E_LIMIT, "line too long");
            if ((s = split_line(w, src, pos, e, &L))) return s;
            pos = e < len ? e + 1 : e;
            if (L.blank) continue;
            if (L.comment) {
                if (!have && tw) {
                    size_t b = L.b;
                    zl_putc(tw, 'C');
                    while (b > L.a && zl_isspace((unsigned char) src[b - 1])) b--;
                    zl_put(tw, src + L.a, b - L.a);
                    zl_putc(tw, '\n');
                } else if ((s = queue_comment(w, L.a, L.b - L.a))) {
                    return s;
                }
                continue;
            }
            if (L.cont) {
                if (!have)
                    return ffail(w, ZL_E_SYNTAX, "continuation line with nothing to continue");
                if ((s = squeeze(w, &z, src, L.a, L.b, 66))) return s;
                continue;
            }
        }
        /* a new statement starts (or the file ended): finish the pending one */
        if (have) {
            if (z.q) {
                c->line = cur_line;
                return ffail(w, ZL_E_SYNTAX, "unterminated character constant");
            }
            uint32_t save_line = c->line;
            c->line = cur_line;
            w->ti = 0;
            w->npool = 0;
            w->depth = 0;
            w->lo.p = w->tl;
            w->lo.cap = sizeof w->tl;
            w->lo.len = 0;
            w->lo.over = 0;
            if (w->tn == 0) {
                if (cur_label) return ffail(w, ZL_E_SYNTAX, "a label with no statement");
            } else {
                if (bad_terminal(w, cur_label, 0))
                    return ffail(w, ZL_E_SYNTAX, "this statement cannot end a DO loop");
                w->ti = 0;
                if ((s = statement(w, cur_label))) return s;
                if (c->failed) return c->d->status;
                if (w->ti != w->tn)
                    return ffail(w, ZL_E_SYNTAX, "unexpected text after the statement");
                if (w->lo.over) return ffail(w, ZL_E_LIMIT, "statement too long for its twin line");
                if ((s = terminals(w, cur_label))) return s;
                flush_twin(w, cur_label);
            }
            c->line = save_line;
            have = 0;
        }
        if (eof) break;
        have = 1;
        cur_label = L.label;
        cur_line = line;
        w->tn = 0;
        z.q = 0;
        if ((s = squeeze(w, &z, src, L.a, L.b, 66))) return s;
    }
    if (w->tw && w->ncmt) { /* trailing comments */
        for (uint32_t k = 0; k < w->ncmt; k++) {
            zl_putc(w->tw, 'C');
            zl_put(w->tw, src + w->cmt[k].off, w->cmt[k].len);
            zl_putc(w->tw, '\n');
        }
    }
    c->line = line;
    if (!w->ended) return ffail(w, ZL_E_SYNTAX, "END expected at the end of the program");
    for (uint32_t k = 0; k < w->nfix; k++) {
        uint32_t j = 0;
        while (j < w->nlab && w->lab[j] != w->fix[k].label) j++;
        if (j == w->nlab) {
            c->line = w->fix[k].srcline;
            return zl_fail(c, ZL_E_UNDEFINED, "jump to a label that does not exist");
        }
        zl_patch(c, w->fix[k].at, w->labaddr[j]);
    }
    if (c->p->ncode == 0) zl_emit(c, ZL_HALT, 0, 0);
    return c->failed ? c->d->status : ZL_OK;
}
