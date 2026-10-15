/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_pascal.c — the Wirth-family front-end: ISO 7185 Pascal, Delphi,
 * Modula-2 and Oberon, integer subsets, sharing one lexer, one expression
 * parser and one lowering, with the twins "Tutordrake" (Pascal, made for
 * teaching), "Formwyrm" (Delphi), "Moduledrake" (Modula-2) and "Faydrake"
 * (Oberon). c->p->lang says which dialect. Integers are 32 bits in all
 * four, and any integer result outside 32 bits traps OVERFLOW.
 *
 * ---- ISO 7185 Pascal (1983; Wirth's language of 1970)
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
 * with (* *) written as { } unless it holds a '}'.
 *
 * ---- Delphi (Object Pascal, 1995), integer subset
 * What ISO Pascal admits above, and: // comments; underscores in names;
 * $hex literals; Integer and LongInt (both 32 bits) and Boolean;
 * declarations (const, var, procedures) in any order and repeated; Result
 * in functions (also from procedures nested in them), Exit and Exit(value),
 * Break and Continue in for, while and repeat, Inc(v[, n]) and Dec(v[, n]),
 * case labels with ranges (a..b) and case ... else with a statement list;
 * calls written with empty parentheses; xor; and, or, xor and not on
 * integers are bitwise; a sign may start any factor (2 * -3); mod
 * truncates (its sign is the dividend's, a negative divisor is fine) and a
 * case selector without a matching label and no else does nothing;
 * write/writeln print an integer in as few columns as it needs and
 * Booleans as TRUE and FALSE, and pad but never cut to a width. Directives
 * {$APPTYPE CONSOLE}, {$MODE DELPHI}, {$Q+}, {$R+}, {$OVERFLOWCHECKS ON},
 * {$RANGECHECKS ON}. Semantics are those of Delphi with overflow and range
 * checks on ({$Q+} {$R+}): an integer result outside 32 bits traps
 * OVERFLOW where Delphi raises EIntOverflow (Delphi's default {$Q-} wraps,
 * which is not modelled: such programs stop rather than print a guess),
 * an index outside its array traps BOUNDS. The reference is fpc -Mdelphi
 * -Co -Cr, which evaluates intermediate results in 64 bits, so the
 * generator keeps every intermediate result inside 32 bits.
 * Refused by name, besides what ISO Pascal refuses: units and uses,
 * classes and objects, exceptions, strings and chars (#n character codes
 * too), shl and shr, const and out parameters, initialised variables,
 * default parameters and calling directives, a function called as a
 * statement, other integer types (Int64, Cardinal, Byte, ...), High, Low
 * and SizeOf, Halt and other directives ({$Q-} and {$R-} among them).
 * Formwyrm (the twin) is Tutordrake's layout; // comments stay //, a
 * case's else follows a ';'.
 *
 * ---- Modula-2 (PIM4, Wirth 1988), integer subset, as gm2 -fpim4
 * -fsoft-check-all -fno-wholevalue runs it (gm2 wraps on overflow; here it
 * traps).
 * MODULE Name; FROM InOut IMPORT WriteInt, WriteString, WriteLn (only
 * these); CONST, VAR (INTEGER, BOOLEAN, ARRAY [lo..hi] OF either) and
 * PROCEDURE declarations in any order and repeated; procedures nested to
 * depth 15 with value and VAR parameters, function procedures with RETURN
 * anywhere (falling off the end of one traps DOMAIN, as gm2's -freturn);
 * :=, IF/ELSIF/ELSE, WHILE, REPEAT, FOR ... TO ... [BY constant] (the
 * limit is evaluated once; the variable never steps past it), LOOP and
 * EXIT, CASE with label lists and ranges and ELSE (no match and no ELSE
 * traps DOMAIN), RETURN, INC and DEC; + - * DIV MOD, = # <> < <= > >=,
 * AND & OR NOT ~ (AND and OR short-circuit); ABS ODD MAX(INTEGER)
 * MIN(INTEGER) TRUE FALSE; WriteInt(e, width) with a constant width,
 * WriteString of a string literal, WriteLn; nested (* *) comments. Names
 * and keywords are case-sensitive. DIV rounds down and MOD is never
 * negative; a divisor of zero traps DIVZERO and a negative one DOMAIN (PIM
 * defines DIV and MOD for positive divisors); indexes are checked
 * (BOUNDS). Refused by name: other
 * modules and imports, CARDINAL, LONGINT, CHAR, REAL and other types,
 * TYPE, RECORD, SET, POINTER, WITH, IN, procedure values, open array
 * parameters, constant expressions, hexadecimal, octal and character
 * literals, underscores, RETURN in the module body, the other standard
 * procedures (HALT, CAP, CHR, ORD, VAL, SIZE, HIGH, INCL, EXCL, NEW, ...).
 * Moduledrake (the twin): keywords and standard names in upper case,
 * names as declared; CONST and VAR at the level of their block with their
 * items one level in; a procedure's heading, BEGIN and END Name at one
 * level, its declarations and statements one level in; one statement per
 * line, IF/ELSIF/ELSE/END and the CASE labels at the statement's level and
 * their statements one in; "#" for inequality, NOT and AND spelled out;
 * strings in double quotes unless they hold one; BY 1 omitted; empty
 * statements dropped; comments on lines of their own as written.
 *
 * ---- Oberon (Oberon-07, revised 2016), integer subset, as OBNC runs it
 * MODULE Name; IMPORT Out; CONST, VAR (INTEGER, BOOLEAN, ARRAY n OF
 * either), procedures (value and VAR parameters, a function's body ends
 * with RETURN e); :=, IF/ELSIF/ELSE, WHILE with ELSIF arms, REPEAT, FOR
 * ... TO ... [BY constant] exactly as the report defines it (v := beg;
 * WHILE v <= end DO S; v := v + inc END: the limit is evaluated every
 * time and the variable may be assigned), CASE with label lists and
 * ranges (no match traps DOMAIN), INC and DEC; + - * DIV MOD, = # < <= >
 * >=, & OR ~; ABS ODD TRUE FALSE; Out.Int(e, width) with a constant width,
 * Out.String and Out.Char of literals, Out.Ln. A procedure sees its own
 * and the module's variables, not those of the procedures around it.
 * DIV and MOD as in Modula-2 (a divisor of zero or below traps). OBNC
 * wraps on overflow and divides by a negative divisor; both trap here
 * (the report defines DIV and MOD for positive divisors only). Refused by
 * name: other modules, aliases, export marks, CHAR, REAL, BYTE, SET and
 * other types, TYPE, RECORD, POINTER, IS, IN, procedure values, open
 * arrays, CASE ... ELSE, constant expressions, other standard procedures
 * (ORD, CHR, LSL, ASSERT, NEW, LEN, ...). Faydrake (the twin) is
 * Moduledrake's layout with "&" and "~".
 */
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
#define P_JUMPS    512  /* break, continue and EXIT jumps waiting for their loop's end */
#define P_NAMELEN  127
#define P_STRLEN   255
#define P_DEFWIDTH 11 /* fpc -Miso: an integer's default field width */
#define P_MAXINT   2147483647
#define P_MININT   (-P_MAXINT - 1)

enum { D_ISO = 0, D_DELPHI, D_M2, D_OBERON };
#define DM_ISO 1u
#define DM_DEL 2u
#define DM_M2  4u
#define DM_OB  8u
#define DM_PAS (DM_ISO | DM_DEL)
#define DM_MOD (DM_M2 | DM_OB)
#define DM_ALL 15u

enum { TK_EOF = 0, TK_ID, TK_NUM, TK_STR, TK_KW, TK_SYM };
enum {
    K_AND,
    K_ARRAY,
    K_BEGIN,
    K_BY,
    K_CASE,
    K_CONST,
    K_DIV,
    K_DO,
    K_DOWNTO,
    K_ELSE,
    K_ELSIF,
    K_END,
    K_EXIT,
    K_FILE,
    K_FOR,
    K_FROM,
    K_FUNCTION,
    K_GOTO,
    K_IF,
    K_IMPORT,
    K_IN,
    K_IS,
    K_LABEL,
    K_LOOP,
    K_MOD,
    K_MODULE,
    K_NIL,
    K_NOT,
    K_OF,
    K_OR,
    K_PACKED,
    K_POINTER,
    K_PROCEDURE,
    K_PROGRAM,
    K_RECORD,
    K_REPEAT,
    K_RETURN,
    K_SET,
    K_THEN,
    K_TO,
    K_TYPE,
    K_UNTIL,
    K_VAR,
    K_WHILE,
    K_WITH,
    K_XOR,
    K_TRUE,
    K_FALSE,
    K_M2MOD,  /* DEFINITION IMPLEMENTATION EXPORT QUALIFIED */
    K_DUNIT,  /* Delphi: units and libraries */
    K_DCLASS, /* Delphi: classes and objects */
    K_DEXC,   /* Delphi: exceptions */
    K_DSTR,   /* Delphi: string */
    K_DSHIFT, /* Delphi: shl shr */
    K_DOTHER, /* Delphi: asm inline out threadvar */
    K__COUNT
};
static const struct {
    const char *name;
    uint8_t k, dm;
} kwtab[] = {
    {"AND", K_AND, DM_PAS | DM_M2},
    {"ARRAY", K_ARRAY, DM_ALL},
    {"BEGIN", K_BEGIN, DM_ALL},
    {"BY", K_BY, DM_MOD},
    {"CASE", K_CASE, DM_ALL},
    {"CONST", K_CONST, DM_ALL},
    {"DIV", K_DIV, DM_ALL},
    {"DO", K_DO, DM_ALL},
    {"DOWNTO", K_DOWNTO, DM_PAS},
    {"ELSE", K_ELSE, DM_ALL},
    {"ELSIF", K_ELSIF, DM_MOD},
    {"END", K_END, DM_ALL},
    {"EXIT", K_EXIT, DM_M2},
    {"FILE", K_FILE, DM_PAS},
    {"FOR", K_FOR, DM_ALL},
    {"FROM", K_FROM, DM_M2},
    {"FUNCTION", K_FUNCTION, DM_PAS},
    {"GOTO", K_GOTO, DM_PAS},
    {"IF", K_IF, DM_ALL},
    {"IMPORT", K_IMPORT, DM_MOD},
    {"IN", K_IN, DM_ALL},
    {"IS", K_IS, DM_OB},
    {"LABEL", K_LABEL, DM_PAS},
    {"LOOP", K_LOOP, DM_M2},
    {"MOD", K_MOD, DM_ALL},
    {"MODULE", K_MODULE, DM_MOD},
    {"NIL", K_NIL, DM_PAS | DM_OB},
    {"NOT", K_NOT, DM_PAS | DM_M2},
    {"OF", K_OF, DM_ALL},
    {"OR", K_OR, DM_ALL},
    {"PACKED", K_PACKED, DM_PAS},
    {"POINTER", K_POINTER, DM_MOD},
    {"PROCEDURE", K_PROCEDURE, DM_ALL},
    {"PROGRAM", K_PROGRAM, DM_PAS},
    {"RECORD", K_RECORD, DM_ALL},
    {"REPEAT", K_REPEAT, DM_ALL},
    {"RETURN", K_RETURN, DM_MOD},
    {"SET", K_SET, DM_PAS | DM_M2},
    {"THEN", K_THEN, DM_ALL},
    {"TO", K_TO, DM_ALL},
    {"TYPE", K_TYPE, DM_ALL},
    {"UNTIL", K_UNTIL, DM_ALL},
    {"VAR", K_VAR, DM_ALL},
    {"WHILE", K_WHILE, DM_ALL},
    {"WITH", K_WITH, DM_PAS | DM_M2},
    {"XOR", K_XOR, DM_DEL},
    {"TRUE", K_TRUE, DM_OB},
    {"FALSE", K_FALSE, DM_OB},
    {"DEFINITION", K_M2MOD, DM_M2},
    {"IMPLEMENTATION", K_M2MOD, DM_M2},
    {"EXPORT", K_M2MOD, DM_M2},
    {"QUALIFIED", K_M2MOD, DM_M2},
    {"UNIT", K_DUNIT, DM_DEL},
    {"USES", K_DUNIT, DM_DEL},
    {"INTERFACE", K_DUNIT, DM_DEL},
    {"IMPLEMENTATION", K_DUNIT, DM_DEL},
    {"INITIALIZATION", K_DUNIT, DM_DEL},
    {"FINALIZATION", K_DUNIT, DM_DEL},
    {"LIBRARY", K_DUNIT, DM_DEL},
    {"EXPORTS", K_DUNIT, DM_DEL},
    {"CLASS", K_DCLASS, DM_DEL},
    {"OBJECT", K_DCLASS, DM_DEL},
    {"CONSTRUCTOR", K_DCLASS, DM_DEL},
    {"DESTRUCTOR", K_DCLASS, DM_DEL},
    {"INHERITED", K_DCLASS, DM_DEL},
    {"PROPERTY", K_DCLASS, DM_DEL},
    {"DISPINTERFACE", K_DCLASS, DM_DEL},
    {"AS", K_DCLASS, DM_DEL},
    {"IS", K_DCLASS, DM_DEL},
    {"TRY", K_DEXC, DM_DEL},
    {"EXCEPT", K_DEXC, DM_DEL},
    {"FINALLY", K_DEXC, DM_DEL},
    {"RAISE", K_DEXC, DM_DEL},
    {"STRING", K_DSTR, DM_DEL},
    {"RESOURCESTRING", K_DSTR, DM_DEL},
    {"SHL", K_DSHIFT, DM_DEL},
    {"SHR", K_DSHIFT, DM_DEL},
    {"ASM", K_DOTHER, DM_DEL},
    {"INLINE", K_DOTHER, DM_DEL},
    {"OUT", K_DOTHER, DM_DEL},
    {"THREADVAR", K_DOTHER, DM_DEL},
};

/* multi-character symbols; single ones are their character */
enum { SY_ASSIGN = 256, SY_LE, SY_GE, SY_NE, SY_DOTDOT };

enum { Y_CONST = 1, Y_VAR, Y_VPARAM, Y_RPARAM, Y_ARR, Y_PROC, Y_FUNC };
enum { TY_INT = 1, TY_BOOL = 2 };
enum { N_NUM = 1, N_CNAME, N_VAR, N_ELEM, N_CALL, N_FN, N_NEG, N_NOT, N_BIN };
enum { F_ABS = 1, F_SQR, F_ODD, F_SUCC, F_PRED, F_ORD, F_MAXI, F_MINI };
enum {
    O_EQ = 1,
    O_NE,
    O_LT,
    O_LE,
    O_GT,
    O_GE,
    O_ADD,
    O_SUB,
    O_OR,
    O_XOR,
    O_MUL,
    O_DIV,
    O_MOD,
    O_AND
};
enum { CTX_SEQ = 0, CTX_BRANCH, CTX_INLINE };
/* imports (Modula-2 and Oberon) */
enum { IMP_WRITEINT = 1, IMP_WRITESTRING = 2, IMP_WRITELN = 4, IMP_OUT = 8 };

typedef struct {
    uint8_t kind;
    int16_t op; /* keyword index or symbol */
    uint32_t pos, len, line, col;
    int64_t v;
} ptok_t;

typedef struct {
    uint32_t pos;
    uint16_t len;
    uint8_t kind, type, level, isfor, res; /* res: Delphi's Result */
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
    int dia, cs; /* dialect; cs: names and keywords are case-sensitive */
    ptok_t t;
    char sbuf[P_STRLEN + 1]; /* the current string token, unescaped */
    uint32_t slen;
    struct {
        uint32_t pos, len;
        uint8_t kind; /* 0 { }, 1 (* *), 2 // */
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
        int64_t v, hi;
        int32_t addr;
    } lab[P_LABELS];
    uint32_t nlab;
    int32_t armjmp[P_LABELS];
    uint32_t narm;
    uint32_t loops[ZL_MAX_DEPTH + 1]; /* open loops: the first of their jumps */
    uint32_t nloop;
    struct {
        int32_t at;
        uint8_t cont;
    } jl[P_JUMPS];
    uint32_t njl;
    uint32_t imports;
    uint32_t depth;
    zl_cc_t *c;
    zl_out_t *tw;
} pw_t;

ZL_STATIC_ASSERT(sizeof(pw_t) <= sizeof(((zl_work_t *) 0)->fe), pw_fits);

static int pas(const pw_t *w)
{
    return w->dia <= D_DELPHI;
}

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

/* A keyword: lower case in the Pascal twins, upper case in the others. */
static void tw_k(pw_t *w, const char *lower, const char *upper)
{
    tw_s(w, pas(w) ? lower : upper);
}

/* a name from the source: in lower case for Pascal and Delphi, as written
 * for Modula-2 and Oberon */
static void tw_name(pw_t *w, uint32_t pos, uint32_t len)
{
    if (!w->tw) return;
    if (w->cs) {
        zl_put(w->tw, w->src + pos, len);
        return;
    }
    for (uint32_t k = 0; k < len; k++) {
        char ch = w->src[pos + k];
        zl_putc(w->tw, (char) (ch >= 'A' && ch <= 'Z' ? ch + 32 : ch));
    }
}

static void tw_sym(pw_t *w, int32_t y)
{
    if (w->sym[y].res)
        tw_s(w, "result");
    else
        tw_name(w, w->sym[y].pos, w->sym[y].len);
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
        if (w->com[k].kind == 1 && pas(w)) {
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

/* Is n bytes at s, past a leading '$', one of Delphi's accepted directives? */
static int delphi_directive(const char *s, size_t n)
{
    static const char *const ok[] = {"$APPTYPE CONSOLE", "$MODE DELPHI", "$Q+", "$R+",
                                     "$OVERFLOWCHECKS ON", "$RANGECHECKS ON", 0};
    for (int k = 0; ok[k]; k++)
        if (zl_wordeq(s, n, ok[k])) return 1;
    return 0;
}

static int comment(pw_t *w)
{
    const char *s = w->src;
    size_t n = w->n, a = w->i, j;
    uint32_t line = w->line, col = (uint32_t) (a - w->lstart) + 1;
    int kind = s[a] == '(' ? 1 : s[a] == '/' ? 2 : 0, nest = 1;
    j = a + (kind ? 2 : 1);
    for (;;) {
        if (kind == 2) {
            if (j >= n || s[j] == '\n') break;
            j++;
            continue;
        }
        if (j >= n) return pfail_at(w, line, col, ZL_E_SYNTAX, "unterminated comment");
        if (s[j] == '\n') TRY(newline(w, j));
        if (!kind && s[j] == '}') {
            j++;
            break;
        }
        if (kind && s[j] == '*' && j + 1 < n && s[j + 1] == ')') {
            j += 2;
            if (--nest == 0) break;
            continue;
        }
        if (!pas(w) && s[j] == '(' && j + 1 < n && s[j + 1] == '*') {
            nest++; /* Modula-2 and Oberon comments nest */
            j += 2;
            continue;
        }
        if (pas(w) && (s[j] == '{' || (s[j] == '(' && j + 1 < n && s[j + 1] == '*')))
            return lexfail(w, j, ZL_E_UNSUPPORTED, "a comment inside a comment");
        j++;
    }
    size_t in = a + (kind ? 2 : 1), inlen = j - in - (kind == 1 ? 2 : kind == 2 ? 0 : 1);
    if (kind != 2 && pas(w) && inlen && s[in] == '$') {
        if (w->dia == D_ISO && !zl_wordeq(s + in, inlen, "$MODE ISO"))
            return pfail_at(w, line, col, ZL_E_UNSUPPORTED,
                            "compiler directive (only {$mode iso} is accepted)");
        if (w->dia == D_DELPHI && !delphi_directive(s + in, inlen))
            return pfail_at(w, line, col, ZL_E_UNSUPPORTED,
                            "compiler directive (only {$APPTYPE CONSOLE}, {$MODE DELPHI}, "
                            "{$Q+} and {$R+} are accepted)");
    }
    if (w->ncom >= P_COMMENTS)
        return pfail_at(w, line, col, ZL_E_LIMIT, "too many comments in one line");
    w->com[w->ncom].pos = (uint32_t) a;
    w->com[w->ncom].len = (uint32_t) (j - a);
    w->com[w->ncom].kind = (uint8_t) kind;
    w->ncom++;
    w->i = j;
    return ZL_OK;
}

static int word_is(const pw_t *w, uint32_t pos, uint32_t len, const char *word)
{
    if (!w->cs) return zl_wordeq(w->src + pos, len, word);
    for (uint32_t k = 0; k < len; k++)
        if (!word[k] || w->src[pos + k] != word[k]) return 0;
    return word[len] == 0;
}

static int is_hex(int ch)
{
    return zl_isdigit(ch) || (zl_upper(ch) >= 'A' && zl_upper(ch) <= 'F');
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
        } else if ((ch == '{' && pas(w)) || (ch == '(' && w->i + 1 < n && s[w->i + 1] == '*') ||
                   (ch == '/' && w->dia == D_DELPHI && w->i + 1 < n && s[w->i + 1] == '/')) {
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
    if (zl_isalpha(ch) || (ch == '_' && w->dia == D_DELPHI)) {
        while (j < n && (zl_isalnum((unsigned char) s[j]) || s[j] == '_')) {
            if (s[j] == '_' && w->dia != D_DELPHI)
                return lexfail(w, j, ZL_E_UNSUPPORTED,
                               w->dia == D_ISO ? "an underscore in a name (not ISO 7185)"
                                               : "an underscore in a name");
            j++;
        }
        if (j - a > P_NAMELEN) return lexfail(w, a, ZL_E_LIMIT, "name too long");
        t->kind = TK_ID;
        for (size_t k = 0; k < sizeof kwtab / sizeof kwtab[0]; k++)
            if ((kwtab[k].dm & (1u << w->dia)) &&
                word_is(w, (uint32_t) a, (uint32_t) (j - a), kwtab[k].name)) {
                t->kind = TK_KW;
                t->op = kwtab[k].k;
                break;
            }
    } else if (zl_isdigit(ch) || (ch == '$' && w->dia == D_DELPHI && a + 1 < n &&
                                  is_hex((unsigned char) s[a + 1]))) {
        int64_t v = 0;
        if (ch == '$') {
            for (j = a + 1; j < n && is_hex((unsigned char) s[j]); j++)
                if (v <= P_MAXINT) {
                    int d = zl_upper((unsigned char) s[j]);
                    v = v * 16 + (d <= '9' ? d - '0' : d - 'A' + 10);
                }
        } else {
            while (j < n && zl_isdigit((unsigned char) s[j])) {
                if (v <= P_MAXINT) v = v * 10 + (s[j] - '0');
                j++;
            }
        }
        if (j < n && pas(w) &&
            ((s[j] == '.' && j + 1 < n && zl_isdigit((unsigned char) s[j + 1])) ||
             zl_upper((unsigned char) s[j]) == 'E'))
            return lexfail(w, a, ZL_E_UNSUPPORTED, "real number (integer subset)");
        if (j < n && !pas(w) && s[j] == '.' && !(j + 1 < n && s[j + 1] == '.'))
            return lexfail(w, a, ZL_E_UNSUPPORTED, "real number (integer subset)");
        if (j < n && !pas(w) && zl_isalnum((unsigned char) s[j]))
            return lexfail(w, a, ZL_E_UNSUPPORTED,
                           "a hexadecimal, octal or character literal (decimal integers only)");
        if (v > P_MAXINT)
            return lexfail(w, a, ZL_E_UNSUPPORTED,
                           pas(w) ? "integer literal larger than maxint"
                                  : "integer literal larger than MAX(INTEGER)");
        t->kind = TK_NUM;
        t->v = v;
    } else if (ch == '\'' && w->dia != D_OBERON) {
        w->slen = 0;
        for (j = a + 1;; j++) {
            if (j >= n || s[j] == '\n') return lexfail(w, a, ZL_E_SYNTAX, "unterminated string");
            if (s[j] == '\'') {
                if (pas(w) && j + 1 < n && s[j + 1] == '\'')
                    j++;
                else
                    break;
            }
            if (w->slen >= P_STRLEN) return lexfail(w, a, ZL_E_LIMIT, "string too long");
            w->sbuf[w->slen++] = s[j];
        }
        j++;
        t->kind = TK_STR;
    } else if (ch == '"' && !pas(w)) {
        w->slen = 0;
        for (j = a + 1;; j++) {
            if (j >= n || s[j] == '\n') return lexfail(w, a, ZL_E_SYNTAX, "unterminated string");
            if (s[j] == '"') break;
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
        else if (ch == '<' && two == '>' && w->dia != D_OBERON)
            t->op = SY_NE;
        else if (ch == '>' && two == '=')
            t->op = SY_GE;
        else if (ch == '.' && two == '.')
            t->op = SY_DOTDOT;
        else if (ch == '#' && !pas(w))
            t->op = SY_NE;
        else
            t->op = (int16_t) ch;
        if (t->op >= 256 && ch != '#') j++;
        int ok = ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '=' || ch == '<' ||
                 ch == '>' || ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == ',' ||
                 ch == ';' || ch == ':' || ch == '.' || ch == '^';
        if (pas(w)) ok = ok || ch == '@';
        if (!pas(w)) ok = ok || ch == '&' || ch == '~' || ch == '|' || ch == '{' || ch == '}';
        if (t->op < 256 && !ok) {
            if (ch == '#' && w->dia == D_DELPHI)
                return lexfail(w, a, ZL_E_UNSUPPORTED, "character codes (#n)");
            if (ch == '_')
                return lexfail(w, a, ZL_E_UNSUPPORTED,
                               w->dia == D_ISO ? "an underscore in a name (not ISO 7185)"
                                               : "an underscore in a name");
            return lexfail(w, a, ZL_E_SYNTAX,
                           ch == '"'                         ? "strings are written in single quotes"
                           : ch == '\'' && w->dia == D_OBERON ? "strings are written in double quotes"
                                                              : "unexpected character");
        }
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

/* The current token is the identifier `word` (upper case; for Modula-2
 * and Oberon spelled exactly so, for Pascal and Delphi in any case). */
static int tok_is(pw_t *w, const char *word)
{
    return w->t.kind == TK_ID && word_is(w, w->t.pos, w->t.len, word);
}

/* A keyword that one of the languages has and this subset does not. */
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
    case K_POINTER:
        return pfail(w, ZL_E_UNSUPPORTED, "pointers and nil");
    case K_PACKED:
        return pfail(w, ZL_E_UNSUPPORTED, "packed types");
    case K_RECORD:
        return pfail(w, ZL_E_UNSUPPORTED, "record types");
    case K_TYPE:
        return pfail(w, ZL_E_UNSUPPORTED, "type declarations");
    case K_WITH:
        return pfail(w, ZL_E_UNSUPPORTED, "with statements");
    case K_IS:
        return pfail(w, ZL_E_UNSUPPORTED, "type tests (IS)");
    case K_M2MOD:
        return pfail(w, ZL_E_UNSUPPORTED, "definition and implementation modules");
    case K_DUNIT:
        return pfail(w, ZL_E_UNSUPPORTED, "units, uses and libraries");
    case K_DCLASS:
        return pfail(w, ZL_E_UNSUPPORTED, "classes and objects");
    case K_DEXC:
        return pfail(w, ZL_E_UNSUPPORTED, "exceptions");
    case K_DSTR:
        return pfail(w, ZL_E_UNSUPPORTED, "char and string values");
    case K_DSHIFT:
        return pfail(w, ZL_E_UNSUPPORTED, "shl and shr");
    case K_DOTHER:
        return pfail(w, ZL_E_UNSUPPORTED, "inline assembler, out parameters and threadvar");
    default:
        return pfail(w, ZL_E_SYNTAX, "unexpected keyword");
    }
}

static int name_in(pw_t *w, const char *const *list)
{
    for (int k = 0; list[k]; k++)
        if (tok_is(w, list[k])) return 1;
    return 0;
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
    static const char *const dreal[] = {"DOUBLE", "SINGLE", "EXTENDED", "CURRENCY", "FRAC",
                                        "INT",    "PI",     "RANDOM",   "RANDOMIZE", 0};
    static const char *const dstr[] = {"ANSISTRING", "WIDESTRING", "SHORTSTRING", "INTTOSTR",
                                       "STRTOINT",   "LENGTH",     "COPY",        "POS",
                                       "ANSICHAR",   "WIDECHAR",   0};
    static const char *const dint[] = {"INT64",    "CARDINAL", "BYTE",    "WORD",    "SHORTINT",
                                       "SMALLINT", "LONGWORD", "UINT64",  "NATIVEINT",
                                       "INT8",     "INT16",    "INT32",   "UINT8",   "UINT16",
                                       "UINT32",   "QWORD",    "NATIVEUINT", 0};
    static const char *const dsys[] = {"HIGH", "LOW", "SIZEOF", 0};
    static const char *const dmisc[] = {"HALT", "ASSERT", "GETMEM", "FREEMEM", "SWAP", 0};
    static const char *const m2types[] = {"CARDINAL", "LONGINT",  "LONGCARD", "SHORTINT",
                                          "SHORTCARD", "BITSET",  "PROC",     "INTEGER8",
                                          "INTEGER16", "INTEGER32", "INTEGER64", "WORD",
                                          "ADDRESS",  "BYTE",     "SET",      0};
    static const char *const m2real[] = {"REAL", "LONGREAL", "SHORTREAL", "FLOAT", "TRUNC",
                                         "FLOOR", "FLT",     "LFLOAT",    0};
    static const char *const m2char[] = {"CHAR", "CHR", "CAP", "ORD", "VAL", 0};
    static const char *const m2sys[] = {"SIZE", "TSIZE", "HIGH", "ADR", "LEN", "MAX", "MIN", 0};
    static const char *const m2misc[] = {"HALT", "INCL", "EXCL", "NEW",  "DISPOSE", "ASSERT",
                                         "LSL",  "ASR",  "ROR",  "PACK", "UNPK",    "NIL",
                                         0};
    static const char *const m2io[] = {"Write",    "WriteCard", "WriteHex", "WriteOct",
                                       "Read",     "ReadInt",   "ReadCard", "ReadString",
                                       "WriteBf",  "Done",      "EOL",      "OpenInput",
                                       "OpenOutput", "CloseInput", "CloseOutput", "In",
                                       "Texts",    "Files",     0};
    if (pas(w)) {
        if (name_in(w, real) || (w->dia == D_DELPHI && name_in(w, dreal)))
            return pfail(w, ZL_E_UNSUPPORTED, "real values (integer subset)");
        if (name_in(w, chars) || (w->dia == D_DELPHI && name_in(w, dstr)))
            return pfail(w, ZL_E_UNSUPPORTED, "char and string values");
        if (name_in(w, files)) return pfail(w, ZL_E_UNSUPPORTED, "files and file functions");
        if (name_in(w, heap)) return pfail(w, ZL_E_UNSUPPORTED, "new, dispose, pack, unpack");
        if (w->dia == D_DELPHI) {
            if (name_in(w, dint))
                return pfail(w, ZL_E_UNSUPPORTED, "integer types other than Integer and LongInt");
            if (name_in(w, dsys)) return pfail(w, ZL_E_UNSUPPORTED, "High, Low and SizeOf");
            if (name_in(w, dmisc))
                return pfail(w, ZL_E_UNSUPPORTED, "Halt, Assert and the memory procedures");
        }
        return pfail(w, ZL_E_UNDEFINED, "unknown name");
    }
    if (name_in(w, m2types))
        return pfail(w, ZL_E_UNSUPPORTED, "types other than INTEGER and BOOLEAN");
    if (name_in(w, m2real)) return pfail(w, ZL_E_UNSUPPORTED, "real values (integer subset)");
    if (name_in(w, m2char)) return pfail(w, ZL_E_UNSUPPORTED, "CHAR values and conversions");
    if (name_in(w, m2sys))
        return pfail(w, ZL_E_UNSUPPORTED, w->dia == D_M2 ? "SIZE, HIGH, ADR, and MAX and MIN "
                                                          "of types other than INTEGER"
                                                        : "LEN and the SYSTEM procedures");
    if (name_in(w, m2misc))
        return pfail(w, ZL_E_UNSUPPORTED, "this standard procedure (outside the subset)");
    if (w->dia == D_M2 && name_in(w, m2io))
        return pfail(w, ZL_E_UNSUPPORTED,
                     "InOut procedures other than WriteInt, WriteString and WriteLn");
    return pfail(w, ZL_E_UNDEFINED, "unknown name");
}

/* ------------------------------------------------------------ symbols */

static int name_eq(pw_t *w, uint32_t p1, uint32_t l1, uint32_t p2, uint32_t l2)
{
    if (l1 != l2) return 0;
    for (uint32_t k = 0; k < l1; k++) {
        int a = (unsigned char) w->src[p1 + k], b = (unsigned char) w->src[p2 + k];
        if (w->cs ? a != b : zl_upper(a) != zl_upper(b)) return 0;
    }
    return 1;
}

static int sym_is(pw_t *w, uint32_t k, uint32_t pos, uint32_t len)
{
    if (w->sym[k].res) return zl_wordeq(w->src + pos, len, "RESULT");
    return name_eq(w, w->sym[k].pos, w->sym[k].len, pos, len);
}

static int is_varkind(int kind)
{
    return kind == Y_VAR || kind == Y_VPARAM || kind == Y_RPARAM || kind == Y_ARR;
}

/* The innermost declaration of the current identifier, or -1. An Oberon
 * procedure does not see the variables of the procedures around it. */
static int32_t lookup(pw_t *w)
{
    for (uint32_t k = w->nsym; k-- > 0;) {
        if (!sym_is(w, k, w->t.pos, w->t.len)) continue;
        if (w->dia == D_OBERON && is_varkind(w->sym[k].kind) && w->sym[k].level &&
            w->sym[k].level < w->level)
            continue;
        return (int32_t) k;
    }
    return -1;
}

static int imported_name(pw_t *w)
{
    if (w->dia == D_M2)
        return ((w->imports & IMP_WRITEINT) && tok_is(w, "WriteInt")) ||
               ((w->imports & IMP_WRITESTRING) && tok_is(w, "WriteString")) ||
               ((w->imports & IMP_WRITELN) && tok_is(w, "WriteLn"));
    if (w->dia == D_OBERON) return (w->imports & IMP_OUT) && tok_is(w, "Out");
    return 0;
}

/* Declare the current identifier in the open block; returns its index. */
static int32_t declare(pw_t *w, int kind, int type)
{
    for (uint32_t k = w->scope[w->level]; k < w->nsym; k++)
        if (sym_is(w, k, w->t.pos, w->t.len)) {
            pfail(w, ZL_E_SYNTAX, "name declared twice in one block");
            return -1;
        }
    if (w->level == 0 && imported_name(w)) {
        pfail(w, ZL_E_SYNTAX, "name declared twice in one block (it is imported)");
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

/* TRUE or FALSE: 1 or 0; anything else -1. */
static int bool_tok(pw_t *w)
{
    if (w->dia == D_OBERON) return is_kw(w, K_TRUE) ? 1 : is_kw(w, K_FALSE) ? 0 : -1;
    return tok_is(w, "TRUE") ? 1 : tok_is(w, "FALSE") ? 0 : -1;
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
    } else if (w->t.kind == TK_ID || bool_tok(w) >= 0) {
        int32_t y = w->t.kind == TK_ID ? lookup(w) : -1;
        k->named = 1;
        k->pos = w->t.pos;
        k->len = w->t.len;
        if (y >= 0) {
            if (w->sym[y].kind != Y_CONST) return pfail(w, nc, msg);
            k->v = w->sym[y].v;
            k->type = w->sym[y].type;
        } else if (pas(w) && tok_is(w, "MAXINT")) {
            k->v = P_MAXINT;
            k->type = TY_INT;
        } else if (bool_tok(w) >= 0) {
            k->v = bool_tok(w);
            k->type = TY_BOOL;
        } else if (!pas(w) && (tok_is(w, "MAX") || tok_is(w, "MIN"))) {
            return pfail(w, ZL_E_UNSUPPORTED, "a constant expression (a number or a name only)");
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
    TRY(lex(w));
    if (!pas(w) && (is_sym(w, '+') || is_sym(w, '-') || is_sym(w, '*') || is_kw(w, K_DIV) ||
                    is_kw(w, K_MOD) || is_sym(w, '(')))
        return pfail(w, ZL_E_UNSUPPORTED, "a constant expression (a number or a name only)");
    return ZL_OK;
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
        if (is_sym(w, '[')) {
            pfail(w, ZL_E_UNSUPPORTED, "multi-dimensional arrays");
            return -1;
        }
    } else if (is_sym(w, '[')) {
        pfail(w, ZL_E_SYNTAX, "only an array takes an index");
        return -1;
    }
    if (!pas(w) && (is_sym(w, '.') || is_sym(w, '^')))
        return pfail(w, ZL_E_UNSUPPORTED, "records and pointers"), -1;
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
        if (w->dia == D_ISO) {
            pfail(w, ZL_E_SYNTAX, "too many arguments");
            return -2;
        }
        if (lex(w)) return -2;
        if (!is_sym(w, ')')) {
            pfail(w, ZL_E_SYNTAX, "too many arguments");
            return -2;
        }
        return lex(w) ? -2 : -1;
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

static int32_t p_factor(pw_t *w);

static int32_t neg_node(pw_t *w, int32_t a, uint32_t line)
{
    int32_t n = typed(w, a, TY_INT, "a sign needs an integer");
    if (n >= 0) n = newnode(w, N_NEG, TY_INT, n, -1);
    if (n >= 0) w->pool[n].line = line;
    return n;
}

static int is_not(pw_t *w)
{
    return is_kw(w, K_NOT) || (!pas(w) && is_sym(w, '~'));
}

/* name(...) of a predefined function: '(' expected after it */
static int32_t p_fn(pw_t *w, int op, uint32_t line)
{
    int32_t n;
    if (lex(w)) return -1;
    if (!is_sym(w, '(')) {
        pfail(w, ZL_E_SYNTAX, "'(' expected after a predefined function");
        return -1;
    }
    if (lex(w)) return -1;
    if (op == F_MAXI || op == F_MINI) {
        if (!tok_is(w, "INTEGER")) {
            pfail(w, ZL_E_UNSUPPORTED, "MAX and MIN of types other than INTEGER");
            return -1;
        }
        if (lex(w) || expect_sym(w, ')', "')' expected")) return -1;
        if ((n = newnode(w, N_FN, TY_INT, -1, -1)) < 0) return -1;
        w->pool[n].op = (uint8_t) op;
        w->pool[n].v = op == F_MAXI ? P_MAXINT : P_MININT;
        w->pool[n].line = line;
        return n;
    }
    int32_t a = p_expr(w);
    if (a < 0) return -1;
    int at = w->pool[a].type;
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
    w->pool[n].line = line;
    return n;
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
        pfail(w, ZL_E_UNSUPPORTED,
              pas(w) ? "char and string values (only write takes strings)"
                     : "CHAR and string values (only the output procedures take strings)");
        return -1;
    }
    if (is_sym(w, '(')) {
        if (lex(w)) return -1;
        n = p_expr(w);
        if (n < 0 || expect_sym(w, ')', "')' expected")) return -1;
        return n;
    }
    if (is_not(w)) {
        if (lex(w)) return -1;
        int32_t a = p_factor(w);
        if (a < 0) return -1;
        if (w->dia == D_DELPHI && w->pool[a].type == TY_INT)
            return newnode(w, N_NOT, TY_INT, a, -1); /* bitwise */
        a = typed(w, a, TY_BOOL, pas(w) ? "not needs a Boolean" : "NOT needs a Boolean");
        return a < 0 ? -1 : newnode(w, N_NOT, TY_BOOL, a, -1);
    }
    if (is_sym(w, '+') || is_sym(w, '-')) {
        if (w->dia == D_DELPHI) { /* a sign may start any factor in Delphi */
            int neg = is_sym(w, '-');
            if (lex(w)) return -1;
            int32_t a = p_factor(w);
            if (!neg) return typed(w, a, TY_INT, "a sign needs an integer");
            return a < 0 ? -1 : neg_node(w, a, t.line);
        }
        pfail(w, ZL_E_SYNTAX, "a sign only starts an expression: put it in parentheses");
        return -1;
    }
    if (is_sym(w, '/')) {
        pfail(w, ZL_E_UNSUPPORTED, pas(w) ? "real division '/' (use div)" : "real division '/' (use DIV)");
        return -1;
    }
    if (is_sym(w, '^') || is_sym(w, '@')) {
        pfail(w, ZL_E_UNSUPPORTED, "pointers");
        return -1;
    }
    if (is_sym(w, '{')) {
        pfail(w, ZL_E_UNSUPPORTED, "set values");
        return -1;
    }
    if (bool_tok(w) >= 0 && (t.kind == TK_KW || lookup(w) < 0)) {
        if ((n = newnode(w, N_CNAME, TY_BOOL, -1, -1)) < 0) return -1;
        w->pool[n].pos = t.pos;
        w->pool[n].len = t.len;
        w->pool[n].v = bool_tok(w);
        return lex(w) ? -1 : n;
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
            if (!pas(w) && !is_sym(w, '(')) {
                pfail(w, ZL_E_UNSUPPORTED, "procedure values (call a function with ( ))");
                return -1;
            }
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
            if (pas(w))
                pfail(w, ZL_E_SYNTAX, "a procedure has no value");
            else
                pfail(w, ZL_E_SYNTAX, "a proper procedure has no value");
            return -1;
        }
    }
    if (pas(w) && tok_is(w, "MAXINT")) {
        if ((n = newnode(w, N_CNAME, TY_INT, -1, -1)) < 0) return -1;
        w->pool[n].pos = t.pos;
        w->pool[n].len = t.len;
        w->pool[n].v = P_MAXINT;
        return lex(w) ? -1 : n;
    }
    if (pas(w)) {
        static const char *const fns[] = {"ABS", "SQR", "ODD", "SUCC", "PRED", "ORD"};
        for (int k = 0; k < 6; k++)
            if (tok_is(w, fns[k])) return p_fn(w, F_ABS + k, t.line);
    } else {
        if (tok_is(w, "ABS")) return p_fn(w, F_ABS, t.line);
        if (tok_is(w, "ODD")) return p_fn(w, F_ODD, t.line);
        if (w->dia == D_M2 && tok_is(w, "MAX")) return p_fn(w, F_MAXI, t.line);
        if (w->dia == D_M2 && tok_is(w, "MIN")) return p_fn(w, F_MINI, t.line);
        if (imported_name(w)) {
            pfail(w, ZL_E_SYNTAX, "a proper procedure has no value");
            return -1;
        }
    }
    refuse_name(w);
    return -1;
}

static int mulop(pw_t *w)
{
    if (is_sym(w, '*')) return O_MUL;
    if (is_kw(w, K_DIV)) return O_DIV;
    if (is_kw(w, K_MOD)) return O_MOD;
    if (is_kw(w, K_AND) || (!pas(w) && is_sym(w, '&'))) return O_AND;
    return 0;
}

static int addop(pw_t *w)
{
    if (is_sym(w, '+')) return O_ADD;
    if (is_sym(w, '-')) return O_SUB;
    if (is_kw(w, K_OR)) return O_OR;
    if (is_kw(w, K_XOR)) return O_XOR;
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
    } else if (op == O_AND || op == O_OR || op == O_XOR) {
        if (w->dia == D_DELPHI && lt == TY_INT && rt == TY_INT) {
            type = TY_INT; /* bitwise */
        } else if (lt != TY_BOOL || rt != TY_BOOL) {
            pfail(w, ZL_E_SYNTAX,
                  w->dia == D_DELPHI ? "and/or/xor need two Booleans or two integers"
                  : pas(w)           ? "and/or need Booleans"
                                     : "AND/OR need Booleans");
            return -1;
        } else {
            type = TY_BOOL;
        }
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
        pfail(w, ZL_E_UNSUPPORTED, pas(w) ? "real division '/' (use div)" : "real division '/' (use DIV)");
        return -1;
    }
    if (l >= 0 && is_kw(w, K_DSHIFT)) {
        pfail(w, ZL_E_UNSUPPORTED, "shl and shr");
        return -1;
    }
    return l;
}

static int32_t p_simple(pw_t *w)
{
    int32_t l;
    int op;
    if ((is_sym(w, '+') || is_sym(w, '-')) && w->dia != D_DELPHI) {
        int neg = is_sym(w, '-');
        uint32_t line = w->t.line;
        if (lex(w)) return -1;
        l = typed(w, p_term(w), TY_INT, "a sign needs an integer");
        if (l >= 0 && neg) l = neg_node(w, l, line);
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
    if (l >= 0 && is_kw(w, K_IS)) {
        pfail(w, ZL_E_UNSUPPORTED, "type tests (IS)");
        return -1;
    }
    w->depth--;
    return l;
}

/* ------------------------------------------------------------ printing */

static int prec(const pw_t *w, const pnode_t *n)
{
    if (n->kind == N_BIN) return n->op <= O_GE ? 1 : n->op <= O_XOR ? 2 : 3;
    if (n->kind == N_NEG) return w->dia == D_DELPHI ? 4 : 2;
    return 4;
}

static void pexpr(pw_t *w, int32_t id, int need, int start)
{
    static const char *const pasop[] = {"",      " = ",   " <> ",  " < ",   " <= ",
                                        " > ",   " >= ",  " + ",   " - ",   " or ",
                                        " xor ", " * ",   " div ", " mod ", " and "};
    static const char *const modop[] = {"",   " = ",   " # ",   " < ",   " <= ", " > ",
                                        " >= ", " + ", " - ",   " OR ",  "",     " * ",
                                        " DIV ", " MOD ", " AND "};
    static const char *const pasfn[] = {"", "abs(", "sqr(", "odd(", "succ(", "pred(", "ord("};
    static const char *const modfn[] = {"",  "ABS(", "", "ODD(", "", "", "",
                                        "MAX(INTEGER)", "MIN(INTEGER)"};
    const pnode_t *n = &w->pool[id];
    int p = prec(w, n), paren = p < need || (n->kind == N_NEG && !start);
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
        tw_sym(w, n->sym);
        break;
    case N_ELEM:
        tw_sym(w, n->sym);
        tw_s(w, "[");
        pexpr(w, n->l, 0, 1);
        tw_s(w, "]");
        break;
    case N_CALL:
        tw_sym(w, n->sym);
        if (n->r < 0 && !pas(w)) tw_s(w, "()");
        for (int32_t a = n->r, first = 1; a >= 0; a = w->pool[a].next, first = 0) {
            tw_s(w, first ? "(" : ", ");
            pexpr(w, a, 0, 1);
            if (w->pool[a].next < 0) tw_s(w, ")");
        }
        break;
    case N_FN:
        tw_s(w, pas(w) ? pasfn[n->op] : modfn[n->op]);
        if (n->l >= 0) {
            pexpr(w, n->l, 0, 1);
            tw_s(w, ")");
        }
        break;
    case N_NEG:
        tw_s(w, "-");
        pexpr(w, n->l, w->dia == D_DELPHI ? 4 : 3, 0);
        break;
    case N_NOT:
        tw_s(w, pas(w) ? "not " : w->dia == D_M2 ? "NOT " : "~");
        pexpr(w, n->l, 4, 0);
        break;
    default: {
        const char *op = pas(w) ? pasop[n->op] : modop[n->op];
        if (w->dia == D_OBERON && n->op == O_AND) op = " & ";
        if (p == 1) {
            pexpr(w, n->l, 2, 1);
            tw_s(w, op);
            pexpr(w, n->r, 2, 1);
        } else {
            pexpr(w, n->l, p, p == 2 ? start : 0);
            tw_s(w, op);
            pexpr(w, n->r, p + 1, 0);
        }
        break;
    }
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

/* x y -> the quotient or remainder as the dialect defines it */
static int lower_divmod(pw_t *w, int mod)
{
    if (w->dia == D_DELPHI) return emit(w, mod ? ZL_MODT : ZL_DIVT, 0, 0);
    if (w->dia == D_ISO && !mod) return emit(w, ZL_DIVT, 0, 0);
    /* ISO mod, and Modula-2 and Oberon DIV and MOD: an error for a divisor
     * below 1; rounding down, so a remainder is never negative */
    TRY(emit(w, ZL_DUP, 0, 0));
    TRY(emit(w, ZL_PUSH, 0, 0));
    TRY(emit(w, ZL_LT, 0, 0));
    int32_t j = zl_emit(w->c, ZL_JZ, 0, 0);
    TRY(emit(w, ZL_TRAP, ZL_E_DOMAIN, 0));
    zl_patch(w->c, j, (int64_t) w->c->p->ncode);
    return emit(w, mod ? ZL_MODF : ZL_DIVF, 0, 0);
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
        if (n->op == F_MAXI || n->op == F_MINI) return emit(w, ZL_PUSH, n->v, 0);
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
        return emit(w, n->type == TY_INT ? ZL_INVERT : ZL_NOT, 0, 0);
    default:
        break;
    }
    TRY(lower(w, n->l));
    w->c->line = n->line;
    if ((n->op == O_AND || n->op == O_OR) && n->type == TY_BOOL) { /* short-circuit */
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
    case O_AND:
        return emit(w, ZL_AND, 0, 0); /* Delphi: bitwise on integers */
    case O_OR:
        return emit(w, ZL_OR, 0, 0);
    case O_XOR:
        return emit(w, ZL_XOR, 0, 0);
    case O_DIV:
        return lower_divmod(w, 0);
    default:
        return lower_divmod(w, 1);
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
            return pfail(w, ZL_E_SYNTAX, pas(w) ? "a function's result is set only inside it"
                                                : "a function cannot be assigned");
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
        tw_sym(w, t->func);
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

/* a plain variable of this block or the program */
static int emit_loadv(pw_t *w, int32_t y)
{
    const psym_t *s = &w->sym[y];
    if (s->level == 0) return emit(w, ZL_LOAD, s->addr, 0);
    return emit(w, ZL_LOADF, s->addr, hops(w, y));
}

static int emit_storev(pw_t *w, int32_t y)
{
    const psym_t *s = &w->sym[y];
    if (s->level == 0) return emit(w, ZL_STORE, s->addr, 0);
    return emit(w, ZL_STOREF, s->addr, hops(w, y));
}

/* ------------------------------------------------------------ loops */

static int loop_open(pw_t *w)
{
    if (w->nloop > ZL_MAX_DEPTH) return pfail(w, ZL_E_LIMIT, "loops nested too deeply");
    w->loops[w->nloop++] = w->njl;
    return ZL_OK;
}

/* A break (cont 0), continue (1) or EXIT out of the innermost open loop. */
static int loop_jump(pw_t *w, int cont)
{
    if (w->njl >= P_JUMPS) return pfail(w, ZL_E_LIMIT, "too many loop exits");
    int32_t at = zl_emit(w->c, ZL_JMP, 0, 0);
    if (at < 0) return status(w);
    w->jl[w->njl].at = at;
    w->jl[w->njl].cont = (uint8_t) cont;
    w->njl++;
    return ZL_OK;
}

static void loop_close(pw_t *w, int64_t cont, int64_t brk)
{
    uint32_t j0 = w->loops[--w->nloop];
    for (uint32_t j = j0; j < w->njl; j++) zl_patch(w->c, w->jl[j].at, w->jl[j].cont ? cont : brk);
    w->njl = j0;
}

/* ------------------------------------------------------------ case */

/* One case label (a constant, or a range in Delphi, Modula-2 and Oberon)
 * pointing at arm; refuses one used twice. */
static int case_label(pw_t *w, uint32_t lab0, int64_t arm, int lfirst)
{
    pconst_t k, h;
    TRY(p_constant(w, &k));
    if (k.type != TY_INT)
        return pfail(w, ZL_E_SYNTAX, pas(w) ? "case labels are integers" : "CASE labels are integers");
    h = k;
    int range = w->dia != D_ISO && is_sym(w, SY_DOTDOT);
    if (range) {
        TRY(lex(w));
        TRY(p_constant(w, &h));
        if (h.type != TY_INT) return pfail(w, ZL_E_SYNTAX, "case labels are integers");
        if (h.v < k.v) return pfail(w, ZL_E_SYNTAX, "an empty label range");
    }
    for (uint32_t j = lab0; j < w->nlab; j++)
        if (w->lab[j].v <= h.v && k.v <= w->lab[j].hi)
            return pfail(w, ZL_E_SYNTAX, "a case label used twice");
    if (w->nlab >= P_LABELS) return pfail(w, ZL_E_LIMIT, "too many case labels");
    w->lab[w->nlab].v = k.v;
    w->lab[w->nlab].hi = h.v;
    w->lab[w->nlab].addr = (int32_t) arm;
    w->nlab++;
    if (!lfirst) tw_s(w, ", ");
    tw_const(w, &k);
    if (range) {
        tw_s(w, "..");
        tw_const(w, &h);
    }
    return ZL_OK;
}

/* Compare the selector with each label in turn; with no match go to
 * else_at (its code drops the selector), or trap DOMAIN (trap), or drop
 * the selector and carry on (Delphi). */
static int case_dispatch(pw_t *w, uint32_t lab0, uint32_t arm0, int64_t else_at, int trap,
                         uint32_t line)
{
    w->c->line = line;
    for (uint32_t j = lab0; j < w->nlab; j++) {
        if (w->lab[j].v == w->lab[j].hi) {
            TRY(emit(w, ZL_DUP, 0, 0));
            TRY(emit(w, ZL_PUSH, w->lab[j].v, 0));
            TRY(emit(w, ZL_EQ, 0, 0));
            TRY(emit(w, ZL_JNZ, w->lab[j].addr, 0));
            continue;
        }
        TRY(emit(w, ZL_DUP, 0, 0));
        TRY(emit(w, ZL_PUSH, w->lab[j].v, 0));
        TRY(emit(w, ZL_GE, 0, 0));
        int32_t jn = zl_emit(w->c, ZL_JZ, 0, 0);
        TRY(emit(w, ZL_DUP, 0, 0));
        TRY(emit(w, ZL_PUSH, w->lab[j].hi, 0));
        TRY(emit(w, ZL_LE, 0, 0));
        TRY(emit(w, ZL_JNZ, w->lab[j].addr, 0));
        zl_patch(w->c, jn, (int64_t) w->c->p->ncode);
    }
    if (else_at >= 0)
        TRY(emit(w, ZL_JMP, else_at, 0));
    else if (trap)
        TRY(emit(w, ZL_TRAP, ZL_E_DOMAIN, 0)); /* no label matched: an error */
    else
        TRY(emit(w, ZL_DROP, 0, 0));
    for (uint32_t j = arm0; j < w->narm; j++)
        zl_patch(w->c, w->armjmp[j], (int64_t) w->c->p->ncode);
    w->nlab = lab0;
    w->narm = arm0;
    return status(w);
}

static int arm_end(pw_t *w, uint32_t line)
{
    if (w->narm >= P_LABELS) return pfail(w, ZL_E_LIMIT, "too many case arms");
    w->c->line = line;
    w->armjmp[w->narm++] = zl_emit(w->c, ZL_JMP, 0, 0);
    return status(w);
}

/* ------------------------------------------------------------ shared statements */

static int p_assign(pw_t *w, int32_t y)
{
    ptarget_t t;
    uint32_t line = w->t.line;
    TRY(p_target(w, &t, y, pas(w)));
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
    tw_sym(w, y);
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

/* Inc(v[, n]) / Dec(v[, n]) and INC / DEC */
static int p_incdec(pw_t *w, int dec)
{
    uint32_t line = w->t.line;
    ptarget_t t;
    tw_s(w, pas(w) ? (dec ? "dec(" : "inc(") : (dec ? "DEC(" : "INC("));
    TRY(lex(w));
    TRY(expect_sym(w, '(', "'(' expected"));
    if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a variable is expected");
    int32_t y = lookup(w);
    if (y < 0) return refuse_name(w);
    if (!is_varkind(w->sym[y].kind)) return pfail(w, ZL_E_SYNTAX, "a variable is expected");
    TRY(p_target(w, &t, y, 0));
    if (t.type != TY_INT) return pfail(w, ZL_E_SYNTAX, "INC and DEC take integer variables");
    tw_target(w, &t);
    int32_t e = -1;
    if (is_sym(w, ',')) {
        TRY(lex(w));
        e = typed(w, p_expr(w), TY_INT, "the step of INC and DEC is an integer");
        if (e < 0) return status(w);
        tw_s(w, ", ");
        tw_expr(w, e);
    }
    tw_s(w, ")");
    TRY(expect_sym(w, ')', "')' expected"));
    const pnode_t *n = &w->pool[t.node];
    w->c->line = line;
    if (n->kind == N_VAR && w->sym[n->sym].kind != Y_RPARAM) {
        TRY(emit_loadv(w, n->sym));
    } else { /* the address once: v's index is evaluated once */
        TRY(lower_addr(w, n));
        w->c->line = line;
        TRY(emit(w, ZL_DUP, 0, 0));
        TRY(emit(w, ZL_LOADA, 0, 0));
    }
    if (e >= 0)
        TRY(lower(w, e));
    else
        TRY(emit(w, ZL_PUSH, 1, 0));
    w->c->line = line;
    TRY(emit(w, dec ? ZL_SUB : ZL_ADD, 0, 0));
    if (n->kind == N_VAR && w->sym[n->sym].kind != Y_RPARAM) return emit_storev(w, n->sym);
    TRY(emit(w, ZL_SWAP, 0, 0));
    return emit(w, ZL_STOREA, 0, 0);
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

/* text padded on the left to width, and cut to it when cut */
static int32_t fitted(pw_t *w, const char *s, uint32_t n, int64_t width, int cut)
{
    char b[P_STRLEN + 64 + 1];
    uint32_t k = 0;
    if (cut && width >= 0 && (uint32_t) width < n) n = (uint32_t) width;
    while (width >= 0 && k + n < (uint32_t) width) b[k++] = ' ';
    for (uint32_t j = 0; j < n; j++) b[k++] = s[j];
    return zl_addstr(w->c, b, k);
}

/* ------------------------------------------------------------ Pascal and Delphi statements */

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

static int p_write(pw_t *w, int ln)
{
    uint32_t line = w->t.line;
    int del = w->dia == D_DELPHI;
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
            int32_t si = fitted(w, str, n, width, !del);
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
                TRY(emit(w, ZL_OUTI, 0, has_w ? (int32_t) k.v : del ? 0 : P_DEFWIDTH));
            } else {
                int64_t width = has_w ? k.v : del ? 0 : 5;
                int32_t st = fitted(w, del ? "TRUE" : "true", 4, width, !del),
                        sf = fitted(w, del ? "FALSE" : "false", 5, width, !del);
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
    TRY(loop_open(w));
    TRY(p_statement(w, L + 1, CTX_BRANCH));
    w->c->line = line;
    TRY(emit(w, ZL_JMP, top, 0));
    zl_patch(w->c, jx, (int64_t) w->c->p->ncode);
    loop_close(w, top, (int64_t) w->c->p->ncode);
    return status(w);
}

static int p_repeat(pw_t *w, uint32_t L)
{
    int64_t top = w->c->p->ncode;
    tw_s(w, "repeat");
    TRY(lex(w));
    TRY(loop_open(w));
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
    int64_t cont = w->c->p->ncode;
    TRY(lower(w, e));
    w->c->line = line;
    TRY(emit(w, ZL_JZ, top, 0));
    loop_close(w, cont, (int64_t) w->c->p->ncode);
    return status(w);
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
    if (s->kind != Y_VAR || s->level != w->level || s->res)
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
    TRY(loop_open(w));
    TRY(p_statement(w, L + 1, CTX_BRANCH));
    w->nfor--;
    w->c->line = line;
    int64_t cont = w->c->p->ncode;
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
    loop_close(w, cont, (int64_t) w->c->p->ncode);
    return status(w);
}

static int p_case(pw_t *w, uint32_t L)
{
    uint32_t line = w->t.line, lab0 = w->nlab, arm0 = w->narm;
    int del = w->dia == D_DELPHI;
    int64_t else_at = -1;
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
        if (is_kw(w, K_ELSE)) {
            if (!del) return pfail(w, ZL_E_UNSUPPORTED, "case ... else (not ISO 7185)");
            if (first) return pfail(w, ZL_E_SYNTAX, "a case label is expected");
            break;
        }
        tw_s(w, first ? "\n" : ";\n");
        bol(w, L + 1);
        int64_t arm = w->c->p->ncode;
        for (int lfirst = 1;; lfirst = 0) {
            TRY(case_label(w, lab0, arm, lfirst));
            if (!is_sym(w, ',')) break;
            TRY(lex(w));
        }
        tw_s(w, ":\n");
        TRY(expect_sym(w, ':', "':' expected after case labels"));
        w->c->line = line;
        TRY(emit(w, ZL_DROP, 0, 0));
        TRY(p_statement(w, L + 2, CTX_BRANCH));
        TRY(arm_end(w, line));
        if (is_sym(w, ';')) {
            TRY(lex(w));
            if (is_kw(w, K_END)) break;
        } else if (is_kw(w, K_END) || (del && is_kw(w, K_ELSE))) {
            if (is_kw(w, K_END)) break;
        } else {
            return pfail(w, ZL_E_SYNTAX, "';' or end expected after a case arm");
        }
    }
    if (is_kw(w, K_ELSE)) { /* Delphi: else and a statement list */
        tw_s(w, ";\n");
        bol(w, L);
        tw_s(w, "else");
        TRY(lex(w));
        w->c->line = line;
        else_at = w->c->p->ncode;
        TRY(emit(w, ZL_DROP, 0, 0));
        TRY(p_sequence(w, L + 1));
        TRY(arm_end(w, line));
        if (!is_kw(w, K_END)) return pfail(w, ZL_E_SYNTAX, "end expected (or a ';' is missing)");
    }
    tw_s(w, "\n");
    bol(w, L);
    tw_s(w, "end");
    TRY(lex(w));
    zl_patch(w->c, jd, (int64_t) w->c->p->ncode);
    return case_dispatch(w, lab0, arm0, else_at, !del, line);
}

/* The current function (Delphi Exit), or -1. */
static int32_t cur_func(pw_t *w)
{
    int32_t f = w->level ? w->cur[w->level] : -1;
    return f >= 0 && w->sym[f].kind == Y_FUNC ? f : -1;
}

/* Delphi: Exit and Exit(value) */
static int p_exit(pw_t *w)
{
    uint32_t line = w->t.line;
    int32_t f = cur_func(w);
    tw_s(w, "exit");
    TRY(lex(w));
    if (is_sym(w, '(')) {
        TRY(lex(w));
        if (is_sym(w, ')')) {
            TRY(lex(w));
        } else {
            if (f < 0) return pfail(w, ZL_E_SYNTAX, "Exit with a value outside a function");
            int32_t e = p_expr(w);
            if (e < 0) return status(w);
            if (w->pool[e].type != w->sym[f].type)
                return pfail_at(w, line, 0, ZL_E_SYNTAX, "Exit with a value of the wrong type");
            tw_s(w, "(");
            tw_expr(w, e);
            tw_s(w, ")");
            TRY(expect_sym(w, ')', "')' expected"));
            TRY(lower(w, e));
            w->c->line = line;
            TRY(emit(w, ZL_LEAVE, 0, 0));
            return emit(w, ZL_RET, 0, 0);
        }
    }
    w->c->line = line;
    if (w->level == 0) return emit(w, ZL_HALT, 0, 0);
    if (f >= 0) TRY(emit(w, ZL_LOADF, w->sym[f].n, 0));
    TRY(emit(w, ZL_LEAVE, 0, 0));
    return emit(w, ZL_RET, 0, 0);
}

static int p_brk(pw_t *w, int cont)
{
    if (!w->nloop)
        return pfail(w, ZL_E_SYNTAX, cont ? "Continue outside a loop" : "Break outside a loop");
    tw_s(w, cont ? "continue" : "break");
    w->c->line = w->t.line;
    TRY(loop_jump(w, cont));
    return lex(w);
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
        int del = w->dia == D_DELPHI;
        if (y >= 0) {
            int k = w->sym[y].kind;
            if (k == Y_PROC)
                s = p_call(w, y);
            else if (k == Y_FUNC &&
                     !(w->level > w->sym[y].level && w->cur[w->sym[y].level + 1] == y)) {
                s = del ? pfail(w, ZL_E_UNSUPPORTED, "a function called as a statement")
                        : pfail(w, ZL_E_SYNTAX, "a function call is not a statement");
            } else
                s = p_assign(w, y);
        } else if (tok_is(w, "WRITE") || tok_is(w, "WRITELN")) {
            s = p_write(w, tok_is(w, "WRITELN"));
        } else if (tok_is(w, "READ") || tok_is(w, "READLN")) {
            s = p_read(w, tok_is(w, "READLN"));
        } else if (del && tok_is(w, "EXIT")) {
            s = p_exit(w);
        } else if (del && (tok_is(w, "BREAK") || tok_is(w, "CONTINUE"))) {
            s = p_brk(w, tok_is(w, "CONTINUE"));
        } else if (del && (tok_is(w, "INC") || tok_is(w, "DEC"))) {
            s = p_incdec(w, tok_is(w, "DEC"));
        } else {
            s = refuse_name(w);
        }
    } else {
        s = pfail(w, ZL_E_SYNTAX, "a statement is expected");
    }
    w->depth--;
    return s ? s : status(w);
}

/* ------------------------------------------------------------ Modula-2 and Oberon statements */

static int m_statement(pw_t *w, uint32_t L);

static int m_end(pw_t *w)
{
    return is_kw(w, K_END) || is_kw(w, K_ELSE) || is_kw(w, K_ELSIF) || is_kw(w, K_UNTIL) ||
           is_sym(w, '|') || (w->dia == D_OBERON && is_kw(w, K_RETURN)) || w->t.kind == TK_EOF;
}

/* StatementSequence: statements separated by ';', empty ones dropped from
 * the twin. Returns the number of statements printed in *n. */
static int m_sequence(pw_t *w, uint32_t L, int *count)
{
    int n = 0;
    for (;;) {
        if (!is_sym(w, ';') && !m_end(w)) {
            tw_s(w, n ? ";\n" : "\n");
            n++;
            TRY(m_statement(w, L));
        }
        if (!is_sym(w, ';')) break;
        TRY(lex(w));
    }
    if (count) *count = n;
    return ZL_OK;
}

/* END, on a line of its own at level L */
static int m_close(pw_t *w, uint32_t L)
{
    tw_s(w, "\n");
    bol(w, L);
    tw_s(w, "END");
    return expect_kw(w, K_END, "END expected (or a ';' is missing)");
}

static int m_if(pw_t *w, uint32_t L)
{
    int32_t ends[ZL_MAX_DEPTH];
    uint32_t nend = 0;
    tw_s(w, "IF ");
    for (;;) {
        uint32_t line = w->t.line;
        TRY(lex(w));
        w->npool = 0;
        int32_t e = p_cond(w, "IF needs a Boolean condition");
        if (e < 0) return status(w);
        tw_expr(w, e);
        tw_s(w, " THEN");
        TRY(lower(w, e));
        w->c->line = line;
        int32_t jf = zl_emit(w->c, ZL_JZ, 0, 0);
        TRY(expect_kw(w, K_THEN, "THEN expected"));
        TRY(m_sequence(w, L + 1, 0));
        if (!is_kw(w, K_ELSIF) && !is_kw(w, K_ELSE)) {
            zl_patch(w->c, jf, (int64_t) w->c->p->ncode);
            break;
        }
        if (nend >= ZL_MAX_DEPTH) return pfail(w, ZL_E_LIMIT, "too many ELSIF arms");
        w->c->line = w->t.line;
        ends[nend++] = zl_emit(w->c, ZL_JMP, 0, 0);
        zl_patch(w->c, jf, (int64_t) w->c->p->ncode);
        tw_s(w, "\n");
        bol(w, L);
        if (is_kw(w, K_ELSE)) {
            tw_s(w, "ELSE");
            TRY(lex(w));
            TRY(m_sequence(w, L + 1, 0));
            break;
        }
        tw_s(w, "ELSIF ");
    }
    for (uint32_t k = 0; k < nend; k++) zl_patch(w->c, ends[k], (int64_t) w->c->p->ncode);
    return m_close(w, L);
}

static int m_while(pw_t *w, uint32_t L)
{
    int64_t top = w->c->p->ncode;
    tw_s(w, "WHILE ");
    for (;;) {
        uint32_t line = w->t.line;
        TRY(lex(w));
        w->npool = 0;
        int32_t e = p_cond(w, "WHILE needs a Boolean condition");
        if (e < 0) return status(w);
        tw_expr(w, e);
        tw_s(w, " DO");
        TRY(lower(w, e));
        w->c->line = line;
        int32_t jx = zl_emit(w->c, ZL_JZ, 0, 0);
        TRY(expect_kw(w, K_DO, "DO expected"));
        TRY(m_sequence(w, L + 1, 0));
        w->c->line = line;
        TRY(emit(w, ZL_JMP, top, 0));
        zl_patch(w->c, jx, (int64_t) w->c->p->ncode);
        if (!is_kw(w, K_ELSIF)) break;
        if (w->dia != D_OBERON) return pfail(w, ZL_E_SYNTAX, "END expected (ELSIF in WHILE is Oberon)");
        tw_s(w, "\n");
        bol(w, L);
        tw_s(w, "ELSIF ");
    }
    return m_close(w, L);
}

static int m_repeat(pw_t *w, uint32_t L)
{
    int64_t top = w->c->p->ncode;
    tw_s(w, "REPEAT");
    TRY(lex(w));
    TRY(m_sequence(w, L + 1, 0));
    tw_s(w, "\n");
    bol(w, L);
    tw_s(w, "UNTIL ");
    uint32_t line = w->t.line;
    TRY(expect_kw(w, K_UNTIL, "UNTIL expected (or a ';' is missing)"));
    w->npool = 0;
    int32_t e = p_cond(w, "UNTIL needs a Boolean condition");
    if (e < 0) return status(w);
    tw_expr(w, e);
    TRY(lower(w, e));
    w->c->line = line;
    return emit(w, ZL_JZ, top, 0);
}

static int m_loop(pw_t *w, uint32_t L)
{
    int64_t top = w->c->p->ncode;
    uint32_t line = w->t.line;
    tw_s(w, "LOOP");
    TRY(lex(w));
    TRY(loop_open(w));
    TRY(m_sequence(w, L + 1, 0));
    w->c->line = line;
    TRY(emit(w, ZL_JMP, top, 0));
    loop_close(w, top, (int64_t) w->c->p->ncode);
    return m_close(w, L);
}

static int m_for(pw_t *w, uint32_t L)
{
    uint32_t line = w->t.line;
    pconst_t k;
    int ob = w->dia == D_OBERON;
    tw_s(w, "FOR ");
    TRY(lex(w));
    if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a control variable is expected");
    int32_t y = lookup(w);
    if (y < 0) return refuse_name(w);
    psym_t *s = &w->sym[y];
    if (!ob && (s->kind != Y_VAR || s->level != w->level))
        return pfail(w, ZL_E_SYNTAX, "the control variable must be a variable of this block");
    if (ob && s->kind != Y_VAR && s->kind != Y_VPARAM)
        return pfail(w, ZL_E_UNSUPPORTED, "a control variable that is not a plain variable");
    if (s->type != TY_INT) return pfail(w, ZL_E_SYNTAX, "the control variable is an INTEGER");
    if (is_active_for(w, y)) return pfail(w, ZL_E_SYNTAX, "the control variable of an open loop");
    TRY(lex(w));
    TRY(expect_sym(w, SY_ASSIGN, "':=' expected"));
    int32_t e1 = typed(w, p_expr(w), TY_INT, "a FOR loop runs over integers");
    if (e1 < 0) return status(w);
    TRY(expect_kw(w, K_TO, "TO expected"));
    int32_t e2 = typed(w, p_expr(w), TY_INT, "a FOR loop runs over integers");
    if (e2 < 0) return status(w);
    int64_t step = 1;
    int has_by = 0;
    if (is_kw(w, K_BY)) {
        TRY(lex(w));
        TRY(p_constant_as(w, &k, ZL_E_UNSUPPORTED, "a step that is not a constant"));
        if (k.type != TY_INT) return pfail(w, ZL_E_SYNTAX, "the step is an integer");
        if (k.v == 0) return pfail(w, ZL_E_SYNTAX, "a step of zero");
        step = k.v;
        has_by = k.named || k.v != 1;
    }
    tw_name(w, s->pos, s->len);
    tw_s(w, " := ");
    tw_expr(w, e1);
    tw_s(w, " TO ");
    tw_expr(w, e2);
    if (has_by) {
        tw_s(w, " BY ");
        tw_const(w, &k);
    }
    tw_s(w, " DO");
    TRY(expect_kw(w, K_DO, "DO expected"));
    int up = step > 0;
    if (ob) {
        /* the report: v := beg; WHILE v <= end DO S; v := v + inc END */
        TRY(lower(w, e1));
        w->c->line = line;
        TRY(emit_storev(w, y));
        int64_t top = w->c->p->ncode;
        TRY(emit_loadv(w, y));
        TRY(lower(w, e2));
        w->c->line = line;
        TRY(emit(w, up ? ZL_LE : ZL_GE, 0, 0));
        int32_t jx = zl_emit(w->c, ZL_JZ, 0, 0);
        TRY(m_sequence(w, L + 1, 0));
        w->c->line = line;
        TRY(emit_loadv(w, y));
        TRY(emit(w, ZL_PUSH, step, 0));
        TRY(emit(w, ZL_ADD, 0, 0));
        TRY(emit_storev(w, y));
        TRY(emit(w, ZL_JMP, top, 0));
        zl_patch(w->c, jx, (int64_t) w->c->p->ncode);
        return m_close(w, L);
    }
    /* Modula-2: the limit once; the variable never steps past it (so a
     * limit next to MAX(INTEGER) does not overflow) and keeps the last
     * value it took, as gm2 leaves it */
    int32_t lim = cells(w, 1);
    if (lim < 0) return status(w);
    int g = w->level == 0;
    TRY(lower(w, e1));
    TRY(lower(w, e2));
    w->c->line = line;
    TRY(emit(w, g ? ZL_STORE : ZL_STOREF, lim, 0));
    TRY(emit_storev(w, y));
    TRY(emit_loadv(w, y));
    TRY(emit(w, g ? ZL_LOAD : ZL_LOADF, lim, 0));
    TRY(emit(w, up ? ZL_GT : ZL_LT, 0, 0));
    int32_t jx = zl_emit(w->c, ZL_JNZ, 0, 0);
    int64_t top = w->c->p->ncode;
    w->fors[w->nfor++] = y;
    TRY(m_sequence(w, L + 1, 0));
    w->nfor--;
    w->c->line = line;
    TRY(emit_loadv(w, y));
    TRY(emit(w, ZL_PUSH, up ? (int64_t) P_MAXINT - step : (int64_t) P_MININT - step, 0));
    TRY(emit(w, up ? ZL_GT : ZL_LT, 0, 0));
    int32_t jg = zl_emit(w->c, ZL_JNZ, 0, 0);
    TRY(emit_loadv(w, y));
    TRY(emit(w, ZL_PUSH, step, 0));
    TRY(emit(w, ZL_ADD, 0, 0));
    TRY(emit(w, ZL_DUP, 0, 0));
    TRY(emit(w, g ? ZL_LOAD : ZL_LOADF, lim, 0));
    TRY(emit(w, up ? ZL_GT : ZL_LT, 0, 0));
    int32_t jp = zl_emit(w->c, ZL_JNZ, 0, 0);
    TRY(emit_storev(w, y));
    TRY(emit(w, ZL_JMP, top, 0));
    zl_patch(w->c, jp, (int64_t) w->c->p->ncode);
    TRY(emit(w, ZL_DROP, 0, 0));
    zl_patch(w->c, jx, (int64_t) w->c->p->ncode);
    zl_patch(w->c, jg, (int64_t) w->c->p->ncode);
    return m_close(w, L);
}

static int m_case(pw_t *w, uint32_t L)
{
    uint32_t line = w->t.line, lab0 = w->nlab, arm0 = w->narm;
    int64_t else_at = -1;
    tw_s(w, "CASE ");
    TRY(lex(w));
    int32_t e = p_expr(w);
    if (e < 0) return status(w);
    if (w->pool[e].type != TY_INT)
        return pfail(w, ZL_E_UNSUPPORTED, "a BOOLEAN CASE selector (integer labels only)");
    tw_expr(w, e);
    tw_s(w, " OF");
    TRY(lower(w, e));
    w->c->line = line;
    int32_t jd = zl_emit(w->c, ZL_JMP, 0, 0);
    TRY(expect_kw(w, K_OF, "OF expected"));
    int arms = 0;
    for (;;) {
        if (!is_sym(w, '|') && !is_kw(w, K_ELSE) && !is_kw(w, K_END)) { /* a non-empty case */
            tw_s(w, "\n");
            bol(w, L);
            if (arms++) tw_s(w, "| ");
            int64_t arm = w->c->p->ncode;
            for (int lfirst = 1;; lfirst = 0) {
                TRY(case_label(w, lab0, arm, lfirst));
                if (!is_sym(w, ',')) break;
                TRY(lex(w));
            }
            tw_s(w, ":");
            TRY(expect_sym(w, ':', "':' expected after CASE labels"));
            w->c->line = line;
            TRY(emit(w, ZL_DROP, 0, 0));
            TRY(m_sequence(w, L + 1, 0));
            TRY(arm_end(w, line));
        }
        if (!is_sym(w, '|')) break;
        TRY(lex(w));
    }
    if (is_kw(w, K_ELSE)) {
        if (w->dia == D_OBERON) return pfail(w, ZL_E_UNSUPPORTED, "CASE ... ELSE (not Oberon-07)");
        tw_s(w, "\n");
        bol(w, L);
        tw_s(w, "ELSE");
        TRY(lex(w));
        w->c->line = line;
        else_at = w->c->p->ncode;
        TRY(emit(w, ZL_DROP, 0, 0));
        TRY(m_sequence(w, L + 1, 0));
        TRY(arm_end(w, line));
    }
    TRY(m_close(w, L));
    zl_patch(w->c, jd, (int64_t) w->c->p->ncode);
    return case_dispatch(w, lab0, arm0, else_at, 1, line);
}

static int m_return(pw_t *w)
{
    uint32_t line = w->t.line;
    int32_t f = cur_func(w);
    tw_s(w, "RETURN");
    if (w->level == 0) return pfail(w, ZL_E_UNSUPPORTED, "RETURN in the module body");
    TRY(lex(w));
    w->c->line = line;
    if (f >= 0) {
        if (is_sym(w, ';') || m_end(w))
            return pfail(w, ZL_E_SYNTAX, "a function procedure returns a value");
        int32_t e = p_expr(w);
        if (e < 0) return status(w);
        if (w->pool[e].type != w->sym[f].type)
            return pfail_at(w, line, 0, ZL_E_SYNTAX, "RETURN of the wrong type");
        tw_s(w, " ");
        tw_expr(w, e);
        TRY(lower(w, e));
        w->c->line = line;
    } else if (!is_sym(w, ';') && !m_end(w)) {
        return pfail(w, ZL_E_SYNTAX, "a proper procedure returns no value");
    }
    TRY(emit(w, ZL_LEAVE, 0, 0));
    return emit(w, ZL_RET, 0, 0);
}

/* the string literal argument of an output procedure */
static int m_string(pw_t *w, int one)
{
    if (w->t.kind != TK_STR)
        return pfail(w, ZL_E_UNSUPPORTED, one ? "Out.Char of anything but a one-character literal"
                                              : "a string that is not a literal");
    if (one && w->slen != 1)
        return pfail(w, ZL_E_SYNTAX, "Out.Char takes a one-character string");
    if (w->tw) {
        int dq = 1;
        for (uint32_t k = 0; k < w->slen; k++)
            if (w->sbuf[k] == '"') dq = 0;
        zl_putc(w->tw, dq ? '"' : '\'');
        zl_put(w->tw, w->sbuf, w->slen);
        zl_putc(w->tw, dq ? '"' : '\'');
    }
    int32_t si = zl_addstr(w->c, w->sbuf, w->slen);
    if (si < 0) return status(w);
    w->c->line = w->t.line;
    TRY(emit(w, ZL_OUTS, si, 0));
    return lex(w);
}

/* WriteInt(e, w) / Out.Int(e, w) */
static int m_writeint(pw_t *w)
{
    pconst_t k;
    uint32_t line = w->t.line;
    TRY(expect_sym(w, '(', "'(' expected"));
    int32_t e = typed(w, p_expr(w), TY_INT, "only integers are written");
    if (e < 0) return status(w);
    tw_s(w, "(");
    tw_expr(w, e);
    tw_s(w, ", ");
    TRY(expect_sym(w, ',', "',' and a field width expected"));
    TRY(p_width(w, &k));
    tw_const(w, &k);
    tw_s(w, ")");
    TRY(expect_sym(w, ')', "')' expected"));
    TRY(lower(w, e));
    w->c->line = line;
    return emit(w, ZL_OUTI, 0, (int32_t) k.v);
}

/* the empty parentheses a call without parameters may have */
static int m_noargs(pw_t *w)
{
    if (!is_sym(w, '(')) return ZL_OK;
    TRY(lex(w));
    return expect_sym(w, ')', "this procedure takes no arguments");
}

static int m_output(pw_t *w)
{
    uint32_t line = w->t.line;
    if (w->dia == D_M2) {
        int which = tok_is(w, "WriteInt") ? 0 : tok_is(w, "WriteString") ? 1 : 2;
        tw_name(w, w->t.pos, w->t.len);
        TRY(lex(w));
        if (which == 0) return m_writeint(w);
        if (which == 1) {
            tw_s(w, "(");
            TRY(expect_sym(w, '(', "'(' expected"));
            TRY(m_string(w, 0));
            tw_s(w, ")");
            return expect_sym(w, ')', "')' expected");
        }
        TRY(m_noargs(w));
        w->c->line = line;
        return emit(w, ZL_NL, 0, 0);
    }
    tw_s(w, "Out.");
    TRY(lex(w));
    TRY(expect_sym(w, '.', "'.' expected after Out"));
    if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a procedure of Out is expected");
    if (tok_is(w, "Int")) {
        tw_s(w, "Int");
        TRY(lex(w));
        return m_writeint(w);
    }
    if (tok_is(w, "String") || tok_is(w, "Char")) {
        int one = tok_is(w, "Char");
        tw_s(w, one ? "Char(" : "String(");
        TRY(lex(w));
        TRY(expect_sym(w, '(', "'(' expected"));
        TRY(m_string(w, one));
        tw_s(w, ")");
        return expect_sym(w, ')', "')' expected");
    }
    if (tok_is(w, "Ln")) {
        tw_s(w, "Ln");
        TRY(lex(w));
        TRY(m_noargs(w));
        w->c->line = line;
        return emit(w, ZL_NL, 0, 0);
    }
    return pfail(w, ZL_E_UNSUPPORTED, "Out procedures other than Int, String, Char and Ln");
}

static int m_statement(pw_t *w, uint32_t L)
{
    int s;
    if (++w->depth > ZL_MAX_DEPTH) return pfail(w, ZL_E_LIMIT, "statements nested too deeply");
    w->npool = 0;
    w->c->line = w->t.line;
    bol(w, L);
    if (w->t.kind == TK_KW) {
        switch (w->t.op) {
        case K_IF:
            s = m_if(w, L);
            break;
        case K_WHILE:
            s = m_while(w, L);
            break;
        case K_REPEAT:
            s = m_repeat(w, L);
            break;
        case K_FOR:
            s = m_for(w, L);
            break;
        case K_LOOP:
            s = m_loop(w, L);
            break;
        case K_EXIT:
            if (!w->nloop) {
                s = pfail(w, ZL_E_SYNTAX, "EXIT outside a LOOP");
                break;
            }
            tw_s(w, "EXIT");
            w->c->line = w->t.line;
            s = loop_jump(w, 0);
            if (!s) s = lex(w);
            break;
        case K_CASE:
            s = m_case(w, L);
            break;
        case K_RETURN:
            s = m_return(w);
            break;
        case K_BEGIN:
            s = pfail(w, ZL_E_SYNTAX, "BEGIN is not a statement here");
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
            if (k == Y_PROC) {
                s = p_call(w, y);
            } else if (k == Y_FUNC) {
                s = pfail(w, ZL_E_SYNTAX, "a function call is not a statement");
            } else {
                s = p_assign(w, y);
            }
        } else if (tok_is(w, "INC") || tok_is(w, "DEC")) {
            s = p_incdec(w, tok_is(w, "DEC"));
        } else if (imported_name(w)) {
            s = m_output(w);
        } else if (w->dia == D_OBERON && tok_is(w, "Out")) {
            s = pfail(w, ZL_E_UNDEFINED, "Out is used but not imported");
        } else if (w->dia == D_M2 &&
                   (tok_is(w, "WriteInt") || tok_is(w, "WriteString") || tok_is(w, "WriteLn"))) {
            s = pfail(w, ZL_E_UNDEFINED, "not imported (FROM InOut IMPORT ...)");
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
        int i = tok_is(w, "INTEGER") || (w->dia == D_DELPHI && tok_is(w, "LONGINT")),
            b = tok_is(w, "BOOLEAN");
        if (i || b) {
            *type = i ? TY_INT : TY_BOOL;
            if (pas(w))
                tw_s(w, *type == TY_INT ? "integer" : "boolean");
            else
                tw_s(w, *type == TY_INT ? "INTEGER" : "BOOLEAN");
            return lex(w);
        }
        if (pas(w)) {
            if (tok_is(w, "REAL")) return pfail(w, ZL_E_UNSUPPORTED, "real values (integer subset)");
            if (tok_is(w, "CHAR") || tok_is(w, "STRING"))
                return pfail(w, ZL_E_UNSUPPORTED, "char and string values");
            if (tok_is(w, "TEXT")) return pfail(w, ZL_E_UNSUPPORTED, "files and file functions");
            if (w->dia == D_DELPHI) return refuse_name(w);
        } else {
            return refuse_name(w);
        }
    }
    if (w->t.kind == TK_KW) {
        if (is_kw(w, K_ARRAY))
            return pfail(w, ZL_E_UNSUPPORTED, "array parameters and arrays of arrays");
        if (is_kw(w, K_PROCEDURE) || is_kw(w, K_FUNCTION))
            return pfail(w, ZL_E_UNSUPPORTED, "procedural parameters");
        if (is_kw(w, K_DSTR)) return pfail(w, ZL_E_UNSUPPORTED, "char and string values");
        return refuse_kw(w);
    }
    if (is_sym(w, '^')) return pfail(w, ZL_E_UNSUPPORTED, "pointers");
    if (is_sym(w, '(')) return pfail(w, ZL_E_UNSUPPORTED, "enumerated types");
    if (w->t.kind == TK_NUM || is_sym(w, '-') || is_sym(w, '+') || is_sym(w, '['))
        return pfail(w, ZL_E_UNSUPPORTED, "subrange types");
    if (w->t.kind == TK_ID && lookup(w) >= 0 && w->sym[lookup(w)].kind == Y_CONST)
        return pfail(w, ZL_E_UNSUPPORTED, "subrange types");
    if (w->t.kind == TK_ID)
        return pfail(w, ZL_E_UNSUPPORTED, pas(w) ? "types other than integer and Boolean"
                                                 : "types other than INTEGER and BOOLEAN");
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
        if (w->dia == D_OBERON && is_sym(w, '*'))
            return pfail(w, ZL_E_UNSUPPORTED, "export marks (*)");
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

static int const_item(pw_t *w, uint32_t B)
{
    pconst_t k;
    if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a constant's name is expected");
    ptok_t name = w->t;
    bol(w, B + 1);
    tw_name(w, name.pos, name.len);
    tw_s(w, " = ");
    TRY(lex(w));
    if (w->dia == D_OBERON && is_sym(w, '*'))
        return pfail(w, ZL_E_UNSUPPORTED, "export marks (*)");
    if (w->dia == D_DELPHI && is_sym(w, ':'))
        return pfail(w, ZL_E_UNSUPPORTED, "typed constants");
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
    return expect_sym(w, ';', "';' expected");
}

static int p_const_part(pw_t *w, uint32_t B)
{
    bol(w, B);
    tw_k(w, "const\n", "CONST\n");
    TRY(lex(w));
    if (!pas(w)) { /* Modula-2 and Oberon: the section may be empty */
        while (w->t.kind == TK_ID) TRY(const_item(w, B));
        return ZL_OK;
    }
    do {
        TRY(const_item(w, B));
    } while (w->t.kind == TK_ID);
    return ZL_OK;
}

/* array[lo..hi] of t (Pascal, Delphi), ARRAY [lo..hi] OF t (Modula-2),
 * ARRAY n OF t (Oberon) */
static int p_array(pw_t *w, const ptok_t *names, uint32_t n)
{
    pconst_t lo, hi;
    int type;
    TRY(lex(w));
    if (w->dia == D_OBERON) {
        tw_s(w, "ARRAY ");
        TRY(p_constant(w, &hi));
        if (hi.type != TY_INT) return pfail(w, ZL_E_SYNTAX, "an array's length is an integer");
        tw_const(w, &hi);
        if (is_sym(w, ',')) return pfail(w, ZL_E_UNSUPPORTED, "multi-dimensional arrays");
        if (hi.v < 1) return pfail(w, ZL_E_SYNTAX, "an array's length must be positive");
        lo.v = 0;
        hi.v -= 1;
        tw_s(w, " OF ");
        TRY(expect_kw(w, K_OF, "OF expected"));
    } else {
        tw_s(w, pas(w) ? "array[" : "ARRAY [");
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
        tw_s(w, pas(w) ? "] of " : "] OF ");
        TRY(expect_sym(w, ']', "']' expected"));
        if (is_sym(w, ',')) return pfail(w, ZL_E_UNSUPPORTED, "multi-dimensional arrays");
        TRY(expect_kw(w, K_OF, pas(w) ? "of expected" : "OF expected"));
    }
    if (is_kw(w, K_ARRAY)) return pfail(w, ZL_E_UNSUPPORTED, "multi-dimensional arrays");
    TRY(p_typename(w, &type));
    if (hi.v < lo.v) return pfail(w, ZL_E_SYNTAX, "an array's upper bound is below its lower");
    if (hi.v - lo.v + 1 > (w->level ? P_FRAME : ZL_MAX_SLOTS))
        return pfail(w, ZL_E_LIMIT, "array too large");
    return declare_names(w, names, n, Y_ARR, type, (int32_t) lo.v, (int32_t) (hi.v - lo.v + 1));
}

static int p_var_part(pw_t *w, uint32_t B)
{
    bol(w, B);
    tw_k(w, "var\n", "VAR\n");
    TRY(lex(w));
    if (!pas(w) && w->t.kind != TK_ID) return ZL_OK; /* an empty VAR section */
    do {
        ptok_t names[P_GROUP];
        uint32_t n;
        bol(w, B + 1);
        TRY(p_names(w, names, &n));
        if (is_kw(w, K_ARRAY)) {
            TRY(p_array(w, names, n));
        } else {
            int type;
            TRY(p_typename(w, &type));
            TRY(declare_names(w, names, n, Y_VAR, type, 0, 0));
        }
        if (w->dia == D_DELPHI && is_sym(w, '='))
            return pfail(w, ZL_E_UNSUPPORTED, "initialised variables");
        tw_s(w, ";\n");
        TRY(expect_sym(w, ';', "';' expected"));
    } while (w->t.kind == TK_ID);
    return ZL_OK;
}

/* The formal parameters of y, after its name, and its result type. */
static int p_params(pw_t *w, int32_t y, int func)
{
    if (is_sym(w, '(')) {
        TRY(lex(w));
        if (is_sym(w, ')') && w->dia != D_ISO) { /* an empty list */
            TRY(lex(w));
            if (func && !pas(w)) tw_s(w, "()");
        } else {
            tw_s(w, "(");
            for (int first = 1;; first = 0) {
                ptok_t names[P_GROUP];
                uint32_t n;
                int type, var = is_kw(w, K_VAR);
                if (!first) tw_s(w, "; ");
                if (is_kw(w, K_PROCEDURE) || is_kw(w, K_FUNCTION))
                    return pfail(w, ZL_E_UNSUPPORTED, "procedural parameters");
                if (w->dia == D_DELPHI && (is_kw(w, K_CONST) || is_kw(w, K_DOTHER)))
                    return pfail(w, ZL_E_UNSUPPORTED, "const and out parameters");
                if (var) {
                    tw_k(w, "var ", "VAR ");
                    TRY(lex(w));
                }
                TRY(p_names(w, names, &n));
                TRY(p_typename(w, &type));
                if (w->dia == D_DELPHI && is_sym(w, '='))
                    return pfail(w, ZL_E_UNSUPPORTED, "default parameter values");
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
    } else if (func && !pas(w)) {
        if (is_sym(w, ':'))
            return pfail(w, ZL_E_SYNTAX, "a function procedure needs a parameter list: ()");
    }
    if (func) {
        int type;
        tw_s(w, ": ");
        if (!is_sym(w, ':'))
            return pfail(w, ZL_E_SYNTAX, pas(w) ? "a function needs ': type'"
                                                : "a function procedure needs ': type'");
        TRY(lex(w));
        TRY(p_typename(w, &type));
        w->sym[y].type = (uint8_t) type;
        w->sym[y].n = w->fsize[w->level]++;
    } else if (is_sym(w, ':')) {
        return pfail(w, ZL_E_SYNTAX, "a procedure has no result type");
    }
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
    TRY(p_params(w, y, func));
    tw_s(w, ";\n");
    TRY(expect_sym(w, ';', "';' expected"));
    if (tok_is(w, "FORWARD") || tok_is(w, "EXTERNAL"))
        return pfail(w, ZL_E_UNSUPPORTED, "forward and external declarations");
    if (w->dia == D_DELPHI &&
        (tok_is(w, "OVERLOAD") || tok_is(w, "INLINE") || tok_is(w, "REGISTER") ||
         tok_is(w, "CDECL") || tok_is(w, "STDCALL") || tok_is(w, "PASCAL") ||
         tok_is(w, "SAFECALL") || is_kw(w, K_DOTHER)))
        return pfail(w, ZL_E_UNSUPPORTED, "procedure directives");
    if (func && w->dia == D_DELPHI) { /* Result: the function's result cell */
        if (w->nsym >= P_SYMS) return pfail(w, ZL_E_LIMIT, "too many names");
        psym_t *r = &w->sym[w->nsym++];
        zl_bzero(r, sizeof *r);
        r->res = 1;
        r->kind = Y_VAR;
        r->type = w->sym[y].type;
        r->level = (uint8_t) w->level;
        r->addr = w->sym[y].n;
    }
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
    if (w->dia == D_DELPHI) { /* any order, repeated */
        for (;;) {
            if (is_kw(w, K_CONST)) {
                TRY(p_const_part(w, B));
            } else if (is_kw(w, K_VAR)) {
                TRY(p_var_part(w, B));
            } else if (is_kw(w, K_PROCEDURE) || is_kw(w, K_FUNCTION)) {
                if (jover < 0 && (jover = zl_emit(w->c, ZL_JMP, 0, 0)) < 0) return status(w);
                TRY(p_proc(w, is_kw(w, K_FUNCTION)));
            } else if (is_kw(w, K_LABEL) || is_kw(w, K_TYPE) || is_kw(w, K_DUNIT) ||
                       is_kw(w, K_DSTR) || is_kw(w, K_DOTHER) || is_kw(w, K_DCLASS)) {
                return refuse_kw(w);
            } else {
                break;
            }
        }
    } else {
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
    }
    if (jover >= 0) zl_patch(w->c, jover, (int64_t) w->c->p->ncode);
    if (!is_kw(w, K_BEGIN)) return pfail(w, ZL_E_SYNTAX, "begin expected");
    bol(w, B);
    return p_compound(w, B);
}

/* ------------------------------------------------------------ Modula-2 and Oberon blocks */

static int m_decls(pw_t *w, uint32_t S);

/* END Name (of a procedure or the module) */
static int m_end_name(pw_t *w, uint32_t pos, uint32_t len, uint32_t L)
{
    TRY(m_close(w, L));
    if (w->t.kind != TK_ID || !name_eq(w, w->t.pos, w->t.len, pos, len))
        return pfail(w, ZL_E_SYNTAX, "END must be followed by the procedure's or module's name");
    tw_s(w, " ");
    tw_name(w, pos, len);
    return lex(w);
}

/* PROCEDURE at heading level H */
static int m_proc(pw_t *w, uint32_t H)
{
    uint32_t line = w->t.line;
    bol(w, H);
    tw_s(w, "PROCEDURE ");
    TRY(lex(w));
    if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a name is expected");
    ptok_t name = w->t;
    TRY(lex(w));
    if (w->dia == D_OBERON && is_sym(w, '*')) return pfail(w, ZL_E_UNSUPPORTED, "export marks (*)");
    /* a function procedure has ': type' after its parameter list */
    int func = 0;
    if (is_sym(w, '(')) { /* look ahead for the closing ')' and a ':' */
        size_t j = w->i;
        int depth = 1;
        while (j < w->n && depth) {
            if (w->src[j] == '(' && j + 1 < w->n && w->src[j + 1] == '*') {
                int cd = 1;
                for (j += 2; j < w->n && cd; j++) {
                    if (w->src[j] == '(' && j + 1 < w->n && w->src[j + 1] == '*') cd++, j++;
                    else if (w->src[j] == '*' && j + 1 < w->n && w->src[j + 1] == ')') cd--, j++;
                }
                continue;
            }
            if (w->src[j] == '(') depth++;
            if (w->src[j] == ')') depth--;
            j++;
        }
        while (j < w->n && (zl_isspace((unsigned char) w->src[j]) || w->src[j] == '\n')) j++;
        func = j < w->n && w->src[j] == ':' && !(j + 1 < w->n && w->src[j + 1] == '=');
    } else if (is_sym(w, ':')) {
        return pfail(w, ZL_E_SYNTAX, "a function procedure needs a parameter list: ()");
    }
    ptok_t save = w->t;
    w->t = name;
    int32_t y = declare(w, func ? Y_FUNC : Y_PROC, 0);
    w->t = save;
    if (y < 0) return status(w);
    tw_name(w, name.pos, name.len);
    if (w->level >= P_NEST) return pfail(w, ZL_E_LIMIT, "procedures nested too deeply");
    w->level++;
    w->scope[w->level] = w->nsym;
    w->fsize[w->level] = 0;
    w->cur[w->level] = y;
    w->sym[y].pfirst = (uint16_t) w->nparam;
    TRY(p_params(w, y, func));
    tw_s(w, ";\n");
    TRY(expect_sym(w, ';', "';' expected"));
    w->c->line = line;
    w->sym[y].addr = (int32_t) w->c->p->ncode;
    int32_t enter = zl_emit(w->c, ZL_ENTER, 0, w->sym[y].np);
    if (enter < 0) return status(w);
    TRY(m_decls(w, H + 1));
    bol(w, H);
    tw_s(w, "BEGIN");
    int count = 0;
    if (is_kw(w, K_BEGIN)) {
        TRY(lex(w));
        TRY(m_sequence(w, H + 1, &count));
    }
    w->c->line = w->t.line;
    if (w->dia == D_OBERON) {
        if (is_kw(w, K_RETURN)) {
            uint32_t rline = w->t.line;
            if (!func) return pfail(w, ZL_E_SYNTAX, "a proper procedure has no RETURN");
            tw_s(w, count ? ";\n" : "\n");
            bol(w, H + 1);
            tw_s(w, "RETURN ");
            TRY(lex(w));
            w->npool = 0;
            int32_t e = p_expr(w);
            if (e < 0) return status(w);
            if (w->pool[e].type != w->sym[y].type)
                return pfail_at(w, rline, 0, ZL_E_SYNTAX, "RETURN of the wrong type");
            tw_expr(w, e);
            TRY(lower(w, e));
            w->c->line = rline;
        } else if (func) {
            return pfail(w, ZL_E_SYNTAX, "a function procedure ends with RETURN and a value");
        }
        TRY(emit(w, ZL_LEAVE, 0, 0));
        TRY(emit(w, ZL_RET, 0, 0));
    } else if (func) { /* Modula-2: falling off a function's end is an error */
        TRY(emit(w, ZL_TRAP, ZL_E_DOMAIN, 0));
    } else {
        TRY(emit(w, ZL_LEAVE, 0, 0));
        TRY(emit(w, ZL_RET, 0, 0));
    }
    TRY(m_end_name(w, name.pos, name.len, H));
    w->c->p->code[enter].a = w->fsize[w->level];
    w->nsym = w->scope[w->level];
    w->level--;
    tw_s(w, ";\n");
    return expect_sym(w, ';', "';' expected after a procedure's end");
}

/* CONST, VAR and PROCEDURE sections; S is their level in the twin */
static int m_decls(pw_t *w, uint32_t S)
{
    int32_t jover = -1;
    int stage = 0; /* Oberon: CONST, TYPE, VAR, then procedures */
    for (;;) {
        int st = is_kw(w, K_CONST) ? 1 : is_kw(w, K_TYPE) ? 2 : is_kw(w, K_VAR) ? 3
                 : is_kw(w, K_PROCEDURE)                   ? 4
                                                           : 0;
        if (!st) break;
        if (w->dia == D_OBERON && (st < stage || (st == stage && st != 4)))
            return pfail(w, ZL_E_SYNTAX, "declarations out of order (CONST, TYPE, VAR, procedures)");
        stage = st;
        if (st == 1) {
            TRY(p_const_part(w, S));
        } else if (st == 2) {
            return refuse_kw(w);
        } else if (st == 3) {
            TRY(p_var_part(w, S));
        } else {
            if (jover < 0 && (jover = zl_emit(w->c, ZL_JMP, 0, 0)) < 0) return status(w);
            TRY(m_proc(w, S));
        }
    }
    if (jover >= 0) zl_patch(w->c, jover, (int64_t) w->c->p->ncode);
    if (is_kw(w, K_FROM) || is_kw(w, K_IMPORT))
        return pfail(w, ZL_E_SYNTAX, "imports come right after the module heading");
    return ZL_OK;
}

/* FROM InOut IMPORT ...; (Modula-2) or IMPORT Out; (Oberon) */
static int m_imports(pw_t *w)
{
    while (is_kw(w, K_FROM) || is_kw(w, K_IMPORT)) {
        bol(w, 0);
        if (w->dia == D_M2) {
            if (is_kw(w, K_IMPORT))
                return pfail(w, ZL_E_UNSUPPORTED,
                             "IMPORT of a whole module (use FROM InOut IMPORT ...)");
            TRY(lex(w));
            if (!tok_is(w, "InOut"))
                return pfail(w, w->t.kind == TK_ID ? ZL_E_UNSUPPORTED : ZL_E_SYNTAX,
                             "modules other than InOut");
            TRY(lex(w));
            tw_s(w, "FROM InOut IMPORT ");
            TRY(expect_kw(w, K_IMPORT, "IMPORT expected"));
            for (int first = 1;; first = 0) {
                uint32_t bit = tok_is(w, "WriteInt")      ? IMP_WRITEINT
                               : tok_is(w, "WriteString") ? IMP_WRITESTRING
                               : tok_is(w, "WriteLn")     ? IMP_WRITELN
                                                          : 0;
                if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a name is expected");
                if (!bit)
                    return pfail(w, ZL_E_UNSUPPORTED,
                                 "InOut procedures other than WriteInt, WriteString and WriteLn");
                if (w->imports & bit) return pfail(w, ZL_E_SYNTAX, "a name imported twice");
                w->imports |= bit;
                if (!first) tw_s(w, ", ");
                tw_name(w, w->t.pos, w->t.len);
                TRY(lex(w));
                if (!is_sym(w, ',')) break;
                TRY(lex(w));
            }
        } else {
            if (is_kw(w, K_FROM)) return pfail(w, ZL_E_SYNTAX, "IMPORT expected");
            tw_s(w, "IMPORT ");
            TRY(lex(w));
            for (int first = 1;; first = 0) {
                if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "a module name is expected");
                ptok_t m = w->t;
                TRY(lex(w));
                if (is_sym(w, SY_ASSIGN)) return pfail(w, ZL_E_UNSUPPORTED, "import aliases");
                ptok_t save = w->t;
                w->t = m;
                int out = tok_is(w, "Out");
                w->t = save;
                if (!out) return pfail_at(w, m.line, m.col, ZL_E_UNSUPPORTED, "modules other than Out");
                if (w->imports & IMP_OUT) return pfail(w, ZL_E_SYNTAX, "a module imported twice");
                w->imports |= IMP_OUT;
                if (!first) tw_s(w, ", ");
                tw_s(w, "Out");
                if (!is_sym(w, ',')) break;
                TRY(lex(w));
            }
        }
        tw_s(w, ";\n");
        TRY(expect_sym(w, ';', "';' expected"));
    }
    return ZL_OK;
}

static int module_compile(pw_t *w)
{
    bol(w, 0);
    tw_s(w, "MODULE ");
    if (is_kw(w, K_M2MOD)) return refuse_kw(w);
    TRY(expect_kw(w, K_MODULE, "MODULE expected"));
    if (w->t.kind != TK_ID) return pfail(w, ZL_E_SYNTAX, "the module's name is expected");
    ptok_t name = w->t;
    tw_name(w, name.pos, name.len);
    TRY(lex(w));
    if (is_sym(w, '[')) return pfail(w, ZL_E_UNSUPPORTED, "module priorities");
    tw_s(w, ";\n");
    TRY(expect_sym(w, ';', "';' expected after the module heading"));
    TRY(m_imports(w));
    TRY(m_decls(w, 0));
    bol(w, 0);
    tw_s(w, "BEGIN");
    if (is_kw(w, K_BEGIN)) {
        TRY(lex(w));
        TRY(m_sequence(w, 1, 0));
    }
    if (is_kw(w, K_RETURN)) return pfail(w, ZL_E_UNSUPPORTED, "RETURN in the module body");
    TRY(m_end_name(w, name.pos, name.len, 0));
    if (!is_sym(w, '.')) return pfail(w, ZL_E_SYNTAX, "'.' expected after the module's end");
    tw_s(w, ".\n");
    TRY(lex(w));
    if (w->t.kind != TK_EOF) return pfail(w, ZL_E_SYNTAX, "text after the module's end");
    if (w->ncom) bol(w, 0);
    w->c->line = w->t.line;
    TRY(emit(w, ZL_HALT, 0, 0));
    return status(w);
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
    w->dia = c->p->lang == ZL_LANG_DELPHI    ? D_DELPHI
             : c->p->lang == ZL_LANG_MODULA2 ? D_M2
             : c->p->lang == ZL_LANG_OBERON  ? D_OBERON
                                             : D_ISO;
    w->cs = !pas(w);
    c->p->word_bits = 32; /* integer under fpc, gm2 and OBNC: 2147483647 at most */
    TRY(lex(w));
    if (!pas(w)) return module_compile(w);
    bol(w, 0);
    tw_s(w, "program ");
    if (is_kw(w, K_DUNIT)) return refuse_kw(w);
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
    if (is_kw(w, K_DUNIT)) return refuse_kw(w);
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
