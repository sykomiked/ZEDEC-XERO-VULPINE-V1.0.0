/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_logo.c — Logo front-end: an integer subset of UCBLogo 6.2 (the 1967
 * language as Berkeley Logo runs it), and its twin "Kiteling". Programs run
 * with 62-bit values: numbers, the words "true and "false, quoted words
 * (symbols) and lists built from list cells.
 *
 * Admitted: to ... end procedures with :inputs, recursion, output / op,
 * stop; make "x value, local "x (at the start of a procedure body), :x and
 * thing "x; print / pr, show, type (also with parentheses and any number
 * of inputs); repeat n [...] and repcount; for [i a b] and [i a b step],
 * where a, b and step are numbers or :names; while [cond] [...] and
 * until [cond] [...]; if c [...], ifelse c [...] [...], and ifelse as an
 * operation with one expression in each list; sum difference product
 * quotient remainder modulo minus (sum, product, and, or, list and
 * sentence also in the variadic form "(sum 1 2 3)"); infix + - * / and
 * = <> < > <= >= with Logo precedence (comparisons lowest, then + -, then
 * * /, then unary minus; a prefix procedure takes whole infix expressions
 * as its inputs, so "sum 1 2 * 3" is 7); equalp notequalp lessp greaterp
 * lessequalp greaterequalp and or not; first butfirst / bf last butlast /
 * bl fput lput list sentence / se item count emptyp listp numberp wordp
 * memberp; word literals "abc and literal lists [1 [2 b] c] (built once,
 * before the first instruction runs); ; comments.
 *
 * Input: readword / rw outputs the next integer of the input (the run's
 * input list; UCBLogo reads it from its read stream, one integer per
 * line), and readlist / rl outputs it as a list of one word. With none
 * left the program stops with ZL_E_INPUT, where UCBLogo outputs the empty
 * list (readword) or word (readlist). Input to these programs is integers
 * only, written plainly (no '+' sign and no leading zeros, since a
 * word keeps its spelling in UCBLogo), of magnitude below 10^15 for
 * arithmetic on them to agree; readchar, readchars and readrawline are
 * refused.
 *
 * Minus signs follow UCBLogo: "a - b" and "a-b" subtract; "a -b" is two
 * inputs. Refused where UCBLogo's reading is surprising: a sign before a
 * procedure name ("-sum 1 2", which UCBLogo applies to the whole infix
 * expression that follows), a sign on a sign ("- -:x" is -:x there), a
 * sign with a space after it ("2 - - 3 - 1" is 2 - (-3 - 1) there) and a
 * sign written against an operator ("2*-3+1" is 2 * (-3 + 1) there).
 *
 * Also refused for the same reason: inside a parenthesized call, a sum,
 * product, list, sentence, and or or right after an infix operator, which
 * in UCBLogo takes every input up to the ")" ("(list 1 + sum 2 3 4)" is
 * [10]); a stop inside a repeat that is the last instruction of a
 * procedure (UCBLogo runs it as a tail call, and the stop escapes the
 * procedure, from the top level ending the whole file); defining a
 * procedure with the name of a primitive.
 *
 * Exact subset: numbers are integers; '/' and quotient must divide
 * exactly, or the program stops with ZL_E_INEXACT instead of making a
 * decimal. Arithmetic or ordering on a non-number, a non-boolean where a
 * condition is needed, first / butfirst / last / butlast of the empty list
 * or of a word, a negative repeat count, and reading a variable that has no value stop with
 * ZL_E_DOMAIN; item out of range stops with ZL_E_BOUNDS; list cells are
 * never reclaimed (ZL_E_HEAP when they run out). UCBLogo prints numbers of
 * magnitude 10^15 and more in floating-point notation; Lingua keeps them
 * exact up to 2^62, so programs agree with UCBLogo below 10^15, and
 * literals of 10^15 and more are refused.
 *
 * Refused by name: turtle graphics, words as character strings (word,
 * char, ascii, ...; first, count and the like of a word stop with
 * ZL_E_DOMAIN), decimals and real functions, random numbers, run / apply
 * and the other ways to run a computed list, property lists, test / iftrue
 * / iffalse, catch / throw, local with a list or in the middle of a body,
 * make / thing of a computed name, if as an operation, quoted numbers, and
 * words with characters other than letters, digits and _. Words compare
 * ignoring case in UCBLogo, so a program must spell each word one way.
 * abs is not a UCBLogo 6.2 primitive and is an unknown procedure.
 *
 * Matching UCBLogo 6.2 where it differs from other Logos: stop inside a
 * repeat (directly, or through if / ifelse / while / for inside it) ends
 * the innermost repeat of the procedure, not the procedure; repcount is
 * dynamic (a procedure called in a repeat sees its count) and is -1 outside
 * every repeat; for counts down when the start is above the end; the for
 * variable is local to the loop; modulo takes the sign of the divisor, and
 * a zero result with a negative divisor is the divisor ("modulo 6 -3" is
 * -3); print, show and type with several inputs compute all of them before
 * printing any.
 *
 * Scope: Logo is dynamically scoped (a procedure sees its callers' locals).
 * Lingua implements this exactly by shallow binding: every variable name
 * has one global slot holding its current value; a procedure saves the
 * slots of its inputs and locals in its frame on entry (ENTER, LOADF /
 * STOREF) and restores them on every way out (the end, output, stop), and
 * a for loop and a repeat (for repcount) save and restore the same way.
 * make writes the slot, which is the innermost dynamic binding, or a global
 * when there is none. A top-level instruction may only call procedures
 * (directly or through other procedures) defined above it, as when UCBLogo
 * loads the file; a procedure body may call any procedure in the file.
 *
 * Kiteling (the twin): lower-case primitives, procedure and variable
 * names, full primitive names (print not pr, output not op, butfirst not
 * bf, sentence not se, readword not rw, readlist not rl), :x for
 * thing "x; one space between tokens, none inside brackets; one
 * instruction per line; a procedure is its title line, its body lines
 * indented by two spaces, and "end"; minimal
 * parentheses (a procedure call is parenthesized only where an infix
 * operator would otherwise extend its last input, or where it has other
 * than its usual number of inputs, or where it would take every input up
 * to ")" as above); a unary minus is written against its operand ("-:x",
 * "3 * -2", "-(sum 1 2)", "-(repcount)"); quoted words and words in lists keep their
 * spelling; comments keep their text after "; ", at the end of the last
 * instruction of their line or on their own line; blank lines dropped. */
#include "zl_int.h"

#define L_TOK    4096
#define L_NODE   2048
#define L_PROC   128
#define L_PARAMS 16
#define L_NAME   256
#define L_FIX    1024
#define L_LIT    256
#define L_TOP    512
#define L_BIND   128
#define L_ARGS   32
#define L_LIMIT  1000000000000000ll /* UCBLogo prints 10^15 and up as floating point */

enum { T_EOF = 0, T_NL, T_COMMENT, T_NUM, T_WORD, T_QUOTE, T_VAR, T_LB, T_RB, T_LP, T_RP, T_OP };
enum { O_ADD = 1, O_SUB, O_MUL, O_DIV, O_EQ, O_NE, O_LT, O_GT, O_LE, O_GE };
#define SP_BEFORE 1u
#define SP_AFTER  2u

typedef struct {
    uint8_t kind, sp;
    uint16_t len;
    uint32_t off, line, col;
    int64_t v; /* number; operator; matching bracket */
} ltok_t;

enum {
    N_NUM = 1,
    N_WORD,   /* v: value */
    N_VAR,    /* v: name */
    N_LIST,   /* tok: its '['; v: slot (or -1 for []) */
    N_NEG,    /* a */
    N_BIN,    /* op: O_*, a, b */
    N_PRIM,   /* op: primitive, a: inputs, n */
    N_USER,   /* v: procedure, a: inputs, n */
    N_IFELSE, /* a: condition, b, c: expressions */
    S_PRIM,   /* op: print show type output stop make local; a: inputs; v: name */
    S_USER,
    S_REPEAT, /* a: count, b: body */
    S_FOR,    /* v: name, a: start, end [, step], n, b: body */
    S_WHILE,  /* op: P_WHILE / P_UNTIL, a: condition, b: body */
    S_IF,     /* a: condition, b: then */
    S_IFELSE  /* a: condition, b: then, c: else */
};

typedef struct {
    uint8_t kind, op, d, paren;
    uint16_t n, pad;
    int32_t a, b, c, next;
    uint32_t tok;
    int64_t v;
} lnode_t;

enum {
    P_PRINT = 1,
    P_SHOW,
    P_TYPE,
    P_MAKE,
    P_LOCAL,
    P_REPEAT,
    P_FOR,
    P_WHILE,
    P_UNTIL,
    P_IF,
    P_IFELSE,
    P_OUTPUT,
    P_STOP,
    P_THING,
    P_SUM,
    P_DIFF,
    P_PROD,
    P_QUOT,
    P_REM,
    P_MOD,
    P_MINUS,
    P_EQUALP,
    P_NOTEQUALP,
    P_LESSP,
    P_GREATERP,
    P_LESSEQ,
    P_GREATEREQ,
    P_AND,
    P_OR,
    P_NOT,
    P_FIRST,
    P_BF,
    P_LAST,
    P_BL,
    P_FPUT,
    P_LPUT,
    P_LIST,
    P_SE,
    P_ITEM,
    P_COUNT,
    P_EMPTYP,
    P_LISTP,
    P_NUMBERP,
    P_WORDP,
    P_MEMBERP,
    P_REPCOUNT,
    P_READWORD,
    P_READLIST
};
#define VAR_ARGS 255
enum { K_CMD = 1, K_OP };

typedef struct {
    const char *name, *canon;
    uint8_t id, kind, nargs, minargs, maxargs;
} lprim_t;

static const lprim_t prims[] = {
    {"print", "print", P_PRINT, K_CMD, 1, 0, VAR_ARGS},
    {"pr", "print", P_PRINT, K_CMD, 1, 0, VAR_ARGS},
    {"show", "show", P_SHOW, K_CMD, 1, 0, VAR_ARGS},
    {"type", "type", P_TYPE, K_CMD, 1, 0, VAR_ARGS},
    {"make", "make", P_MAKE, K_CMD, 2, 2, 2},
    {"local", "local", P_LOCAL, K_CMD, 1, 1, 1},
    {"repeat", "repeat", P_REPEAT, K_CMD, 2, 2, 2},
    {"for", "for", P_FOR, K_CMD, 2, 2, 2},
    {"while", "while", P_WHILE, K_CMD, 2, 2, 2},
    {"until", "until", P_UNTIL, K_CMD, 2, 2, 2},
    {"if", "if", P_IF, K_CMD, 2, 2, 2},
    {"ifelse", "ifelse", P_IFELSE, K_CMD, 3, 3, 3},
    {"output", "output", P_OUTPUT, K_CMD, 1, 1, 1},
    {"op", "output", P_OUTPUT, K_CMD, 1, 1, 1},
    {"stop", "stop", P_STOP, K_CMD, 0, 0, 0},
    {"thing", "thing", P_THING, K_OP, 1, 1, 1},
    {"sum", "sum", P_SUM, K_OP, 2, 0, VAR_ARGS},
    {"difference", "difference", P_DIFF, K_OP, 2, 2, 2},
    {"product", "product", P_PROD, K_OP, 2, 0, VAR_ARGS},
    {"quotient", "quotient", P_QUOT, K_OP, 2, 2, 2},
    {"remainder", "remainder", P_REM, K_OP, 2, 2, 2},
    {"modulo", "modulo", P_MOD, K_OP, 2, 2, 2},
    {"minus", "minus", P_MINUS, K_OP, 1, 1, 1},
    {"equalp", "equalp", P_EQUALP, K_OP, 2, 2, 2},
    {"notequalp", "notequalp", P_NOTEQUALP, K_OP, 2, 2, 2},
    {"lessp", "lessp", P_LESSP, K_OP, 2, 2, 2},
    {"greaterp", "greaterp", P_GREATERP, K_OP, 2, 2, 2},
    {"lessequalp", "lessequalp", P_LESSEQ, K_OP, 2, 2, 2},
    {"greaterequalp", "greaterequalp", P_GREATEREQ, K_OP, 2, 2, 2},
    {"and", "and", P_AND, K_OP, 2, 0, VAR_ARGS},
    {"or", "or", P_OR, K_OP, 2, 0, VAR_ARGS},
    {"not", "not", P_NOT, K_OP, 1, 1, 1},
    {"first", "first", P_FIRST, K_OP, 1, 1, 1},
    {"butfirst", "butfirst", P_BF, K_OP, 1, 1, 1},
    {"bf", "butfirst", P_BF, K_OP, 1, 1, 1},
    {"last", "last", P_LAST, K_OP, 1, 1, 1},
    {"butlast", "butlast", P_BL, K_OP, 1, 1, 1},
    {"bl", "butlast", P_BL, K_OP, 1, 1, 1},
    {"fput", "fput", P_FPUT, K_OP, 2, 2, 2},
    {"lput", "lput", P_LPUT, K_OP, 2, 2, 2},
    {"list", "list", P_LIST, K_OP, 2, 0, VAR_ARGS},
    {"sentence", "sentence", P_SE, K_OP, 2, 0, VAR_ARGS},
    {"se", "sentence", P_SE, K_OP, 2, 0, VAR_ARGS},
    {"item", "item", P_ITEM, K_OP, 2, 2, 2},
    {"count", "count", P_COUNT, K_OP, 1, 1, 1},
    {"emptyp", "emptyp", P_EMPTYP, K_OP, 1, 1, 1},
    {"listp", "listp", P_LISTP, K_OP, 1, 1, 1},
    {"numberp", "numberp", P_NUMBERP, K_OP, 1, 1, 1},
    {"wordp", "wordp", P_WORDP, K_OP, 1, 1, 1},
    {"memberp", "memberp", P_MEMBERP, K_OP, 2, 2, 2},
    {"repcount", "repcount", P_REPCOUNT, K_OP, 0, 0, 0},
    {"readword", "readword", P_READWORD, K_OP, 0, 0, 0},
    {"rw", "readword", P_READWORD, K_OP, 0, 0, 0},
    {"readlist", "readlist", P_READLIST, K_OP, 0, 0, 0},
    {"rl", "readlist", P_READLIST, K_OP, 0, 0, 0},
};
#define NPRIMS (sizeof prims / sizeof prims[0])

/* Primitives outside the subset, refused by name. */
static const struct {
    const char *name, *why;
} refused[] = {
    {"forward", "turtle graphics are outside the subset"},
    {"fd", "turtle graphics are outside the subset"},
    {"back", "turtle graphics are outside the subset"},
    {"bk", "turtle graphics are outside the subset"},
    {"left", "turtle graphics are outside the subset"},
    {"lt", "turtle graphics are outside the subset"},
    {"right", "turtle graphics are outside the subset"},
    {"rt", "turtle graphics are outside the subset"},
    {"penup", "turtle graphics are outside the subset"},
    {"pu", "turtle graphics are outside the subset"},
    {"pendown", "turtle graphics are outside the subset"},
    {"pd", "turtle graphics are outside the subset"},
    {"home", "turtle graphics are outside the subset"},
    {"clearscreen", "turtle graphics are outside the subset"},
    {"cs", "turtle graphics are outside the subset"},
    {"setxy", "turtle graphics are outside the subset"},
    {"setpos", "turtle graphics are outside the subset"},
    {"setheading", "turtle graphics are outside the subset"},
    {"seth", "turtle graphics are outside the subset"},
    {"hideturtle", "turtle graphics are outside the subset"},
    {"ht", "turtle graphics are outside the subset"},
    {"showturtle", "turtle graphics are outside the subset"},
    {"st", "turtle graphics are outside the subset"},
    {"setpencolor", "turtle graphics are outside the subset"},
    {"word", "words as character strings are outside the subset"},
    {"char", "words as character strings are outside the subset"},
    {"ascii", "words as character strings are outside the subset"},
    {"uppercase", "words as character strings are outside the subset"},
    {"lowercase", "words as character strings are outside the subset"},
    {"run", "running a computed list is outside the subset"},
    {"runresult", "running a computed list is outside the subset"},
    {"apply", "running a computed list is outside the subset"},
    {"invoke", "running a computed list is outside the subset"},
    {"foreach", "running a computed list is outside the subset"},
    {"map", "running a computed list is outside the subset"},
    {"filter", "running a computed list is outside the subset"},
    {"reduce", "running a computed list is outside the subset"},
    {"define", "running a computed list is outside the subset"},
    {"pprop", "property lists are outside the subset"},
    {"gprop", "property lists are outside the subset"},
    {"remprop", "property lists are outside the subset"},
    {"plist", "property lists are outside the subset"},
    {"sqrt", "decimals are outside the subset (integers only)"},
    {"sin", "decimals are outside the subset (integers only)"},
    {"cos", "decimals are outside the subset (integers only)"},
    {"arctan", "decimals are outside the subset (integers only)"},
    {"exp", "decimals are outside the subset (integers only)"},
    {"ln", "decimals are outside the subset (integers only)"},
    {"log10", "decimals are outside the subset (integers only)"},
    {"power", "decimals are outside the subset (integers only)"},
    {"int", "decimals are outside the subset (integers only)"},
    {"round", "decimals are outside the subset (integers only)"},
    {"random", "random numbers are outside the subset"},
    {"rerandom", "random numbers are outside the subset"},
    {"test", "test / iftrue / iffalse are outside the subset"},
    {"iftrue", "test / iftrue / iffalse are outside the subset"},
    {"ift", "test / iftrue / iffalse are outside the subset"},
    {"iffalse", "test / iftrue / iffalse are outside the subset"},
    {"iff", "test / iftrue / iffalse are outside the subset"},
    {"catch", "catch / throw are outside the subset"},
    {"throw", "catch / throw are outside the subset"},
    {"forever", "forever is outside the subset"},
    {"name", "name is outside the subset (write make)"},
    {"readchar", "input is integers, one per line, read with readword or readlist"},
    {"readchars", "input is integers, one per line, read with readword or readlist"},
    {"readrawline", "input is integers, one per line, read with readword or readlist"},
    {"rc", "input is integers, one per line, read with readword or readlist"},
    {"rcs", "input is integers, one per line, read with readword or readlist"},
};
#define NREFUSED (sizeof refused / sizeof refused[0])

/* Every name UCBLogo 6.2 defines (its primitives and library procedures,
 * from its help files): a program may not redefine one, and one outside
 * the subset is refused rather than reported unknown. */
static const char ucb_names[] =
    "allopen allowgetset and apply arc arctan arity array arrayp arraytolist ascii ashift "
    "back background backslashedp beforep bf bfs bg bitand bitnot bitor bitxor bk bl "
    "buried buriedp bury buryall buryname butfirst butfirsts butlast button buttonact "
    "buttonp bye cascade case caseignoredp catch char clean clearscreen cleartext clickpos "
    "close closeall co combine commandline cond contents continue copydef cos count "
    "crossmap cs cslsload ct cursor decreasefont define definedp dequeue difference "
    "dribble ed edall edit editfile edn edns edpl edpls edps emptyp eofp epspict equalp er "
    "erall erase erasefile erf ern erns erpl erpls erps erract error exp fd fence filep "
    "fill filled filter find first firsts font for foreach forever form forward fput fs "
    "fullprintp fullscreen fulltext gc gensym global goto gprop greaterequalp greaterp "
    "heading help hideturtle home ht if ifelse iff iffalse ift iftrue ignore increasefont "
    "int invoke iseq item keyact keyp label labelsize last left lessequalp lessp linep "
    "list listp listtoarray ln load loadnoisily loadpict local localmake log10 "
    "logoplatform logoversion lowercase lput lshift lt macroexpand macrop make map mapdse "
    "mdarray mditem mdsetitem member memberp minus modulo mousepos name namelist namep "
    "names nodes nodribble norefresh not notequalp numberp op openappend openread "
    "openupdate openwrite or output palette parse pause pc pd pe pen pencolor pendown "
    "pendownp penerase penmode penpaint penpattern penreverse pensize penup pick plist "
    "plistp plists pllist po poall pon pons pop popl popls pops pos pot pots power pprop "
    "ppt pr prefix primitivep primitives print printdepthlimit printout printwidthlimit "
    "procedurep procedures product pu push px queue quoted quotient radarctan radcos "
    "radsin random rawascii rc rcs readchar readchars reader readlist readpos readrawline "
    "readword redefp reduce refresh remainder remdup remove remprop repcount repeat "
    "rerandom reverse right rl round rseq rt run runparse runresult rw save savel savepict "
    "screenmode scrunch se sentence setbackground setbg setcslsloc setcursor seteditor "
    "setfont seth setheading sethelploc setitem setlabelheight setlibloc setmargins "
    "setpalette setpc setpen setpencolor setpenpattern setpensize setpos setprefix setread "
    "setreadpos setscrunch settc settemploc settextcolor settextfont settextsize setwrite "
    "setwritepos setx setxy sety shell show shownp showturtle sin splitscreen sqrt ss st "
    "standout startup step stepped steppedp stop substringp sum tag test text textscreen "
    "textsize thing throw to towards trace traced tracedp transfer ts turtlemode type "
    "unbury unburyall unburyname unburyonedit unstep until untrace uppercase "
    "usealternatenames vbarredp wait while window word wordp wrap writepos writer xcor ycor";

static int ucb_name(const char *src, uint32_t off, uint32_t len)
{
    const char *p = ucb_names;
    while (*p) {
        uint32_t i = 0;
        while (i < len && p[i] && p[i] != ' ' &&
               p[i] == ((src[off + i] >= 'A' && src[off + i] <= 'Z') ? src[off + i] + 32
                                                                     : src[off + i]))
            i++;
        if (i == len && (p[i] == ' ' || p[i] == 0)) return 1;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    return 0;
}

/* Run-time helpers written in IR, emitted after HALT when used. */
enum {
    H_TOBOOL = 0,
    H_FROMBOOL,
    H_EQL,
    H_MEM,
    H_CNT,
    H_ITEM,
    H_LAST,
    H_BL,
    H_APP,
    H_ASLIST,
    H_SE,
    H_LPUT,
    H_FPUT,
    H_MODULO,
    H__COUNT
};

enum { B_PARAM = 1, B_LOCAL, B_FOR, B_REPEAT };

typedef struct {
    uint32_t tok; /* its name */
    uint8_t nparams, isop;
    int16_t params[L_PARAMS]; /* name ids (set when compiled) */
    int32_t addr;
} lproc_t;

typedef struct {
    ltok_t tok[L_TOK];
    uint32_t ntok, pos;
    lnode_t node[L_NODE];
    uint32_t nnode;
    lproc_t proc[L_PROC];
    uint32_t nproc, ndef;
    struct {
        uint32_t off, len;
        int32_t slot;
    } name[L_NAME];
    uint32_t nname;
    struct {
        int32_t at, target; /* target: procedure, or -1 - helper */
    } fix[L_FIX];
    uint32_t nfix;
    struct {
        int32_t slot;
        uint32_t tok;
    } lit[L_LIT];
    uint32_t nlit;
    struct {
        uint32_t proc, defs, line;
    } top[L_TOP];
    uint32_t ntop;
    uint64_t edge[L_PROC][L_PROC / 64];
    uint64_t seen[L_PROC / 64];
    struct {
        uint8_t kind;
        int32_t cell, slot, name;
    } bind[L_BIND];
    uint32_t nbind;
    struct {
        uint32_t bind_at;
        int32_t chain; /* stop-jumps to the end of this repeat, chained through .a */
    } rep[ZL_MAX_DEPTH];
    uint32_t nrep;
    int cur; /* procedure being compiled, -1 at top level */
    int body_started;
    uint32_t ncells, depth, nest;
    int inpc; /* directly inside the inputs of a parenthesized call */
    int32_t rc_slot, unb, space_str, scratch;
    uint8_t helper_used[H__COUNT];
    int32_t helper_addr[H__COUNT];
    const char *src;
    size_t len;
    zl_cc_t *c;
    zl_out_t *tw;
} lw_t;

ZL_STATIC_ASSERT(sizeof(lw_t) <= sizeof(((zl_work_t *) 0)->fe), lw_fits);

#define UNB(w) (ZL_V_SYM + (w)->unb)

/* ------------------------------------------------------------ helpers */

static int lfail_at(lw_t *w, uint32_t t, int st, const char *msg)
{
    if (t < w->ntok) {
        w->c->line = w->tok[t].line;
        w->c->col = w->tok[t].col;
    }
    return zl_fail(w->c, st, msg);
}
static int lfail(lw_t *w, int st, const char *msg)
{
    return lfail_at(w, w->pos, st, msg);
}

static int lower_c(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
}

/* Case-insensitive compare of source text against a lower-case name. */
static int teq(const lw_t *w, uint32_t t, const char *name)
{
    const ltok_t *k = &w->tok[t];
    size_t i = 0;
    for (; i < k->len; i++)
        if (!name[i] || lower_c((unsigned char) w->src[k->off + i]) != name[i]) return 0;
    return name[i] == 0;
}

static int is_word(const lw_t *w, uint32_t t, const char *name)
{
    return w->tok[t].kind == T_WORD && teq(w, t, name);
}

static const lprim_t *find_prim(const lw_t *w, uint32_t t)
{
    for (size_t i = 0; i < NPRIMS; i++)
        if (teq(w, t, prims[i].name)) return &prims[i];
    return 0;
}

static const char *find_refused(const lw_t *w, uint32_t t)
{
    for (size_t i = 0; i < NREFUSED; i++)
        if (teq(w, t, refused[i].name)) return refused[i].why;
    return 0;
}

static int same_text(const lw_t *w, uint32_t a, uint32_t b)
{
    const ltok_t *x = &w->tok[a], *y = &w->tok[b];
    if (x->len != y->len) return 0;
    for (uint32_t i = 0; i < x->len; i++)
        if (lower_c((unsigned char) w->src[x->off + i]) !=
            lower_c((unsigned char) w->src[y->off + i]))
            return 0;
    return 1;
}

static int find_proc(const lw_t *w, uint32_t t)
{
    for (uint32_t i = 0; i < w->nproc; i++)
        if (same_text(w, w->proc[i].tok, t)) return (int) i;
    return -1;
}

/* Variable name id for a :x, "x or for token (case-insensitive); a slot is
 * reserved the first time a name is seen. */
static int name_id(lw_t *w, uint32_t t)
{
    const ltok_t *k = &w->tok[t];
    for (uint32_t i = 0; i < w->nname; i++) {
        if (w->name[i].len != k->len) continue;
        uint32_t j = 0;
        while (j < k->len && lower_c((unsigned char) w->src[w->name[i].off + j]) ==
                                 lower_c((unsigned char) w->src[k->off + j]))
            j++;
        if (j == k->len) return (int) i;
    }
    if (w->nname >= L_NAME) {
        lfail_at(w, t, ZL_E_LIMIT, "too many variable names");
        return -1;
    }
    int32_t s = zl_slots(w->c, 1);
    if (s < 0) return -1;
    w->name[w->nname].off = k->off;
    w->name[w->nname].len = k->len;
    w->name[w->nname].slot = s;
    return (int) w->nname++;
}

/* The value of a quoted word or a word in a literal list. */
static int word_value(lw_t *w, uint32_t t, int64_t *out)
{
    const ltok_t *k = &w->tok[t];
    const char *s = w->src + k->off;
    if (teq(w, t, "true") || teq(w, t, "false")) {
        int tr = teq(w, t, "true");
        for (uint32_t i = 0; i < k->len; i++)
            if (s[i] >= 'A' && s[i] <= 'Z')
                return lfail_at(w, t, ZL_E_UNSUPPORTED,
                                "write true and false in lower case (UCBLogo ignores case)");
        *out = tr ? ZL_V_TRUE : ZL_V_FALSE;
        return ZL_OK;
    }
    zl_prog_t *p = w->c->p;
    for (uint32_t i = 0; i < p->nstr; i++) { /* one spelling per word */
        if (p->str_len[i] != k->len) continue;
        uint32_t j = 0, exact = 1;
        while (j < k->len && lower_c((unsigned char) p->str[p->str_off[i] + j]) ==
                                 lower_c((unsigned char) s[j])) {
            if (p->str[p->str_off[i] + j] != s[j]) exact = 0;
            j++;
        }
        if (j == k->len && !exact)
            return lfail_at(w, t, ZL_E_UNSUPPORTED,
                            "a word spelled two ways (UCBLogo ignores case): write it one way");
    }
    int32_t i = zl_addstr(w->c, s, k->len);
    if (i < 0) return w->c->d->status;
    *out = ZL_V_SYM + i;
    return ZL_OK;
}

/* ------------------------------------------------------------ lexing */

static int delim(int ch)
{
    return ch == 0 || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '[' ||
           ch == ']' || ch == '(' || ch == ')' || ch == ';';
}
static int opchar(int ch)
{
    return ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '=' || ch == '<' || ch == '>';
}

static int lex(lw_t *w)
{
    const char *s = w->src;
    size_t n = w->len, i = 0, ls = 0;
    uint32_t line = 1, depth = 0, paren = 0, stack[ZL_MAX_DEPTH];
    int space = 1;
#define AT(k) ((k) < n ? (unsigned char) s[k] : 0)
    for (;;) {
        if (w->ntok + 2 >= L_TOK) {
            w->c->line = line;
            return zl_fail(w->c, ZL_E_LIMIT, "program too long for the token table");
        }
        ltok_t *t = &w->tok[w->ntok];
        zl_bzero(t, sizeof *t);
        t->line = line;
        t->col = (uint32_t) (i - ls) + 1;
        t->off = (uint32_t) i;
        if (i >= n || s[i] == '\n') {
            if (paren) {
                w->c->line = line;
                return zl_fail(w->c, ZL_E_SYNTAX, "'(' not closed on its line");
            }
            if (depth == 0 && w->ntok && w->tok[w->ntok - 1].kind != T_NL) {
                t->kind = T_NL;
                w->ntok++;
            }
            if (i >= n) break;
            line++;
            ls = ++i;
            space = 1;
            continue;
        }
        int ch = (unsigned char) s[i];
        if (zl_isspace(ch)) {
            i++;
            space = 1;
            continue;
        }
        w->c->line = line;
        w->c->col = t->col;
        t->sp = space ? SP_BEFORE : 0;
        int prev = w->ntok ? w->tok[w->ntok - 1].kind : T_NL;
        size_t a = i;
        if (ch == ';') {
            if (depth) return zl_fail(w->c, ZL_E_UNSUPPORTED, "a comment inside brackets");
            a = ++i;
            while (i < n && s[i] != '\n') i++;
            size_t b = i;
            while (a < b && zl_isspace((unsigned char) s[a])) a++;
            while (b > a && zl_isspace((unsigned char) s[b - 1])) b--;
            t->kind = T_COMMENT;
            t->off = (uint32_t) a;
            t->len = (uint16_t) (b - a);
            if (b - a > 0xffff) return zl_fail(w->c, ZL_E_LIMIT, "comment too long");
            w->ntok++;
            continue;
        }
        if (ch == '[') {
            if (depth >= ZL_MAX_DEPTH)
                return zl_fail(w->c, ZL_E_LIMIT, "brackets nested too deeply");
            stack[depth++] = w->ntok;
            t->kind = T_LB;
            i++;
        } else if (ch == ']') {
            if (!depth) return zl_fail(w->c, ZL_E_SYNTAX, "']' without '['");
            t->kind = T_RB;
            t->v = stack[--depth];
            w->tok[stack[depth]].v = w->ntok;
            i++;
        } else if (ch == '(') {
            t->kind = T_LP;
            paren++;
            i++;
        } else if (ch == ')') {
            if (!paren) return zl_fail(w->c, ZL_E_SYNTAX, "')' without '('");
            t->kind = T_RP;
            paren--;
            i++;
        } else if (zl_isdigit(ch) ||
                   (ch == '-' && zl_isdigit(AT(i + 1)) &&
                    (space || prev == T_LB || prev == T_LP || prev == T_OP || prev == T_NL))) {
            int neg = ch == '-';
            if (neg) i++;
            size_t d = i;
            int64_t v = 0;
            while (i < n && zl_isdigit((unsigned char) s[i])) {
                v = v * 10 + (s[i] - '0');
                if (v >= L_LIMIT)
                    return zl_fail(w->c, ZL_E_UNSUPPORTED,
                                   "a number of 10^15 or more (UCBLogo reads it as a decimal)");
                i++;
            }
            if (AT(i) == '.' || zl_isalpha(AT(i)))
                return zl_fail(w->c, ZL_E_UNSUPPORTED,
                               AT(i) == '.' || zl_upper(AT(i)) == 'E'
                                   ? "decimals are outside the subset (integers only)"
                                   : "a word that starts with a digit");
            if ((s[d] == '0' && i - d > 1) || (neg && v == 0))
                return zl_fail(w->c, ZL_E_UNSUPPORTED,
                               "write a number without leading zeros or -0");
            t->kind = T_NUM;
            t->v = neg ? -v : v;
        } else if (opchar(ch)) {
            t->kind = T_OP;
            i++;
            switch (ch) {
            case '+':
                t->v = O_ADD;
                break;
            case '-':
                t->v = O_SUB;
                break;
            case '*':
                t->v = O_MUL;
                break;
            case '/':
                t->v = O_DIV;
                break;
            case '=':
                t->v = O_EQ;
                break;
            case '<':
                t->v = AT(i) == '=' ? O_LE : AT(i) == '>' ? O_NE : O_LT;
                if (t->v != O_LT) i++;
                break;
            default:
                t->v = AT(i) == '=' ? O_GE : O_GT;
                if (t->v == O_GE) i++;
                break;
            }
        } else if (ch == '"' || ch == ':' || zl_isalpha(ch)) {
            t->kind = ch == '"' ? T_QUOTE : ch == ':' ? T_VAR : T_WORD;
            if (ch == '"' || ch == ':') {
                i++;
                if (ch == '"' && zl_isdigit(AT(i)))
                    return zl_fail(w->c, ZL_E_UNSUPPORTED, "a quoted number: write the number");
                if (!zl_isalpha(AT(i)))
                    return zl_fail(w->c, ch == '"' && delim(AT(i)) ? ZL_E_UNSUPPORTED : ZL_E_SYNTAX,
                                   ch == '"' && delim(AT(i))
                                       ? "the empty word is outside the subset"
                                       : "a name starts with a letter");
            }
            a = i;
            while (i < n && (zl_isalnum((unsigned char) s[i]) || s[i] == '_')) i++;
            if (!delim(AT(i)) && !(ch != '"' && opchar(AT(i))))
                return zl_fail(w->c, ZL_E_UNSUPPORTED,
                               "words and names are letters, digits and _ only");
            if (i - a > 64) return zl_fail(w->c, ZL_E_LIMIT, "name too long");
            t->off = (uint32_t) a;
        } else if (ch == '~') {
            return zl_fail(w->c, ZL_E_UNSUPPORTED, "'~' line continuation is outside the subset");
        } else {
            return zl_fail(w->c, ZL_E_SYNTAX, "unexpected character");
        }
        if (t->kind != T_WORD && t->kind != T_QUOTE && t->kind != T_VAR) t->off = (uint32_t) a;
        t->len = (uint16_t) (i - t->off);
        if (delim(AT(i)) && AT(i) != '[' && AT(i) != ']' && AT(i) != '(' && AT(i) != ')')
            t->sp |= SP_AFTER;
        space = 0;
        w->ntok++;
    }
    if (depth) return zl_fail(w->c, ZL_E_SYNTAX, "'[' not closed");
    w->tok[w->ntok].kind = T_EOF;
    w->tok[w->ntok].line = line;
    w->tok[w->ntok].off = (uint32_t) n;
    w->ntok++;
    return ZL_OK;
#undef AT
}

/* ------------------------------------------------------------ procedures (first pass) */

static uint32_t next_line(const lw_t *w, uint32_t t)
{
    while (w->tok[t].kind != T_NL && w->tok[t].kind != T_EOF) t++;
    return w->tok[t].kind == T_NL ? t + 1 : t;
}

static int prepass(lw_t *w)
{
    uint32_t t = 0;
    while (w->tok[t].kind != T_EOF) {
        if (!is_word(w, t, "to")) {
            t = next_line(w, t);
            continue;
        }
        uint32_t nt = t + 1;
        if (w->tok[nt].kind != T_WORD)
            return lfail_at(w, nt, ZL_E_SYNTAX, "procedure name expected");
        if (find_prim(w, nt) || find_refused(w, nt) || is_word(w, nt, "end") ||
            ucb_name(w->src, w->tok[nt].off, w->tok[nt].len))
            return lfail_at(w, nt, ZL_E_SYNTAX, "a UCBLogo primitive cannot be redefined");
        if (find_proc(w, nt) >= 0) return lfail_at(w, nt, ZL_E_SYNTAX, "procedure defined twice");
        if (w->nproc >= L_PROC) return lfail_at(w, nt, ZL_E_LIMIT, "too many procedures");
        lproc_t *pr = &w->proc[w->nproc];
        pr->tok = nt;
        uint32_t k = nt + 1;
        while (w->tok[k].kind == T_VAR) {
            if (pr->nparams >= L_PARAMS) return lfail_at(w, k, ZL_E_LIMIT, "too many inputs");
            pr->nparams++;
            k++;
        }
        if (w->tok[k].kind == T_LB)
            return lfail_at(w, k, ZL_E_UNSUPPORTED, "optional inputs are outside the subset");
        if (w->tok[k].kind == T_COMMENT) k++;
        if (w->tok[k].kind != T_NL)
            return lfail_at(w, k, ZL_E_SYNTAX, "the title line holds only the name and :inputs");
        w->nproc++;
        for (t = k + 1;; t = next_line(w, t)) {
            if (w->tok[t].kind == T_EOF) return lfail_at(w, nt, ZL_E_SYNTAX, "to without end");
            if (is_word(w, t, "end")) break;
            if (is_word(w, t, "to"))
                return lfail_at(w, t, ZL_E_SYNTAX, "to inside a procedure (end is missing)");
            for (uint32_t j = t; w->tok[j].kind != T_NL && w->tok[j].kind != T_EOF; j++)
                if (is_word(w, j, "output") || is_word(w, j, "op")) pr->isop = 1;
        }
        t = next_line(w, t);
    }
    return ZL_OK;
}

/* ------------------------------------------------------------ parsing */

static int32_t mk(lw_t *w, int kind, uint32_t tok)
{
    if (w->nnode >= L_NODE) {
        lfail_at(w, tok, ZL_E_LIMIT, "instruction too large");
        return -1;
    }
    lnode_t *n = &w->node[w->nnode];
    zl_bzero(n, sizeof *n);
    n->kind = (uint8_t) kind;
    n->a = n->b = n->c = n->next = -1;
    n->tok = tok;
    n->d = 1;
    return (int32_t) w->nnode++;
}

/* Depth of a node from its children; bounds every later recursion. */
static int fixd(lw_t *w, int32_t id)
{
    lnode_t *n = &w->node[id];
    uint32_t d = 0;
    int32_t kids[3] = {n->a, n->b, n->c};
    for (int k = 0; k < 3; k++)
        for (int32_t x = kids[k]; x >= 0; x = w->node[x].next)
            if (w->node[x].d > d) d = w->node[x].d;
    if (d + 1 > ZL_MAX_DEPTH)
        return lfail_at(w, n->tok, ZL_E_LIMIT, "instruction nested too deeply");
    n->d = (uint8_t) (d + 1);
    return ZL_OK;
}

static int is_endtok(int k)
{
    return k == T_NL || k == T_EOF || k == T_COMMENT || k == T_RB || k == T_RP;
}

static int32_t expr(lw_t *w);
static int32_t instr_list(lw_t *w);

/* The inputs of a call: n full expressions, or (paren) up to ')'. */
static int args(lw_t *w, int32_t id, int paren, unsigned want)
{
    int32_t last = -1;
    unsigned k = 0;
    int inpc = w->inpc;
    w->inpc = paren;
    for (;; k++) {
        int kind = w->tok[w->pos].kind;
        if (paren ? kind == T_RP : k == want) break;
        if (is_endtok(kind))
            return lfail(w, ZL_E_SYNTAX, paren ? "')' expected" : "not enough inputs");
        if (k >= L_ARGS) return lfail(w, ZL_E_LIMIT, "too many inputs");
        int32_t e = expr(w);
        if (e < 0) return w->c->d->status;
        if (last < 0)
            w->node[id].a = e;
        else
            w->node[last].next = e;
        last = e;
    }
    w->inpc = inpc;
    w->node[id].n = (uint16_t) k;
    return ZL_OK;
}

/* A literal data list: numbers, words and lists. */
static int32_t data_list(lw_t *w)
{
    uint32_t lb = w->pos, rb = (uint32_t) w->tok[lb].v;
    for (uint32_t t = lb + 1; t < rb; t++) {
        int k = w->tok[t].kind;
        if (k == T_WORD) {
            int64_t v;
            if (word_value(w, t, &v)) return -1;
        } else if (k != T_NUM && k != T_LB && k != T_RB) {
            lfail_at(w, t, ZL_E_UNSUPPORTED, "a literal list holds only numbers, words and lists");
            return -1;
        }
    }
    int32_t id = mk(w, N_LIST, lb);
    if (id < 0) return -1;
    w->node[id].v = -1;
    if (rb > lb + 1) {
        if (w->nlit >= L_LIT) {
            lfail(w, ZL_E_LIMIT, "too many literal lists");
            return -1;
        }
        int32_t s = zl_slots(w->c, 1);
        if (s < 0) return -1;
        w->lit[w->nlit].slot = s;
        w->lit[w->nlit].tok = lb;
        w->nlit++;
        w->node[id].v = s;
    }
    w->pos = rb + 1;
    return id;
}

/* [expression], the lists of ifelse as an operation. */
static int32_t expr_list(lw_t *w)
{
    if (w->tok[w->pos].kind != T_LB) {
        lfail(w, ZL_E_SYNTAX, "'[' expected");
        return -1;
    }
    uint32_t rb = (uint32_t) w->tok[w->pos].v;
    int inpc = w->inpc;
    w->pos++;
    w->inpc = 0;
    int32_t e = expr(w);
    w->inpc = inpc;
    if (e < 0) return -1;
    if (w->pos != rb) {
        lfail(w, ZL_E_UNSUPPORTED, "an ifelse operation needs one expression in each list");
        return -1;
    }
    w->pos++;
    return e;
}

static int32_t call(lw_t *w, int paren);
static int binop_at(lw_t *w, int level);

/* A name that is neither in the subset nor defined by the program. */
static int unknown(lw_t *w, uint32_t t)
{
    if (is_word(w, t, "to") || is_word(w, t, "end"))
        return lfail(w, ZL_E_SYNTAX, "to and end belong on lines of their own");
    if (ucb_name(w->src, w->tok[t].off, w->tok[t].len))
        return lfail(w, ZL_E_UNSUPPORTED, "a UCBLogo primitive outside the subset");
    return lfail(w, ZL_E_UNDEFINED, "unknown procedure");
}

static int32_t primary(lw_t *w)
{
    uint32_t t = w->pos;
    ltok_t *k = &w->tok[t];
    int32_t id;
    switch (k->kind) {
    case T_NUM:
        if ((id = mk(w, N_NUM, t)) < 0) return -1;
        w->node[id].v = k->v;
        w->pos++;
        return id;
    case T_QUOTE:
        if ((id = mk(w, N_WORD, t)) < 0) return -1;
        if (word_value(w, t, &w->node[id].v)) return -1;
        w->pos++;
        return id;
    case T_VAR: {
        int nm = name_id(w, t);
        if (nm < 0 || (id = mk(w, N_VAR, t)) < 0) return -1;
        w->node[id].v = nm;
        w->pos++;
        return id;
    }
    case T_LB:
        return data_list(w);
    case T_LP:
        if (w->tok[t + 1].kind == T_WORD) {
            const lprim_t *pm = find_prim(w, t + 1);
            int pi = find_proc(w, t + 1);
            if ((pm && pm->maxargs) || (pi >= 0 && w->proc[pi].nparams)) {
                w->pos++;
                id = call(w, 1);
                if (id < 0) return -1;
                if (w->tok[w->pos].kind != T_RP) {
                    lfail(w, ZL_E_SYNTAX, "')' expected");
                    return -1;
                }
                w->pos++;
                return id;
            }
        }
        int inpc = w->inpc;
        w->pos++;
        w->inpc = 0;
        id = expr(w);
        w->inpc = inpc;
        if (id < 0) return -1;
        if (w->tok[w->pos].kind != T_RP) {
            lfail(w, ZL_E_SYNTAX, "')' expected");
            return -1;
        }
        w->pos++;
        return id;
    case T_WORD:
        return call(w, 0);
    default:
        lfail(w, ZL_E_SYNTAX, "an input is missing");
        return -1;
    }
}

/* A procedure call in an expression (an operation). */
static int32_t call(lw_t *w, int paren)
{
    uint32_t t = w->pos;
    const lprim_t *pm = find_prim(w, t);
    int32_t id;
    if (pm) {
        if (pm->id == P_IFELSE) {
            w->pos++;
            if ((id = mk(w, N_IFELSE, t)) < 0) return -1;
            if ((w->node[id].a = expr(w)) < 0) return -1;
            if ((w->node[id].b = expr_list(w)) < 0) return -1;
            if ((w->node[id].c = expr_list(w)) < 0) return -1;
            if (binop_at(w, 0) || binop_at(w, 1) || binop_at(w, 2)) {
                lfail(w, ZL_E_SYNTAX,
                      "an operator after the last list of ifelse applies to the list");
                return -1;
            }
            return fixd(w, id) ? -1 : id;
        }
        if (pm->id == P_IF) {
            lfail(w, ZL_E_UNSUPPORTED, "if as an operation is outside the subset (write ifelse)");
            return -1;
        }
        if (pm->kind != K_OP) {
            lfail(w, ZL_E_SYNTAX, "this procedure does not output a value");
            return -1;
        }
        w->pos++;
        if (pm->id == P_THING) {
            if (paren || w->tok[w->pos].kind != T_QUOTE) {
                lfail(w, ZL_E_UNSUPPORTED, "thing of a computed name is outside the subset");
                return -1;
            }
            int nm = name_id(w, w->pos);
            if (nm < 0 || (id = mk(w, N_VAR, t)) < 0) return -1;
            w->node[id].v = nm;
            w->pos++;
            return id;
        }
        if (!paren && pm->maxargs == VAR_ARGS && w->inpc && t && w->tok[t - 1].kind == T_OP) {
            /* UCBLogo: (list 1 + sum 2 3 4) is [10]; the sum takes every input to ')' */
            lfail(w, ZL_E_UNSUPPORTED,
                  "inside a parenthesized call, a sum, product, list, "
                  "sentence, and or or after an operator takes every "
                  "input up to ')' in UCBLogo: parenthesize it");
            return -1;
        }
        if ((id = mk(w, N_PRIM, t)) < 0) return -1;
        w->node[id].op = pm->id;
        w->node[id].paren = (uint8_t) paren;
        if (args(w, id, paren, pm->nargs)) return -1;
        if (w->node[id].n < pm->minargs ||
            (pm->maxargs != VAR_ARGS && w->node[id].n > pm->maxargs)) {
            lfail(w, ZL_E_SYNTAX, "wrong number of inputs");
            return -1;
        }
        return fixd(w, id) ? -1 : id;
    }
    const char *why = find_refused(w, t);
    if (why) {
        lfail(w, ZL_E_UNSUPPORTED, why);
        return -1;
    }
    int pi = find_proc(w, t);
    if (pi < 0) {
        unknown(w, t);
        return -1;
    }
    if (!w->proc[pi].isop) {
        lfail(w, ZL_E_SYNTAX, "this procedure does not output a value");
        return -1;
    }
    w->pos++;
    if ((id = mk(w, N_USER, t)) < 0) return -1;
    w->node[id].v = pi;
    if (args(w, id, paren, w->proc[pi].nparams)) return -1;
    if (w->node[id].n != w->proc[pi].nparams) {
        lfail(w, ZL_E_SYNTAX, "wrong number of inputs");
        return -1;
    }
    return fixd(w, id) ? -1 : id;
}

static int32_t unary(lw_t *w)
{
    uint32_t t = w->pos;
    int sign = (w->tok[t].kind == T_OP && w->tok[t].v == O_SUB) ||
               (w->tok[t].kind == T_NUM && w->src[w->tok[t].off] == '-');
    if (sign && t && w->tok[t - 1].kind == T_OP && !(w->tok[t].sp & SP_BEFORE)) {
        /* UCBLogo reads 2*-3+1 as 2*(-3+1) and :n--1 - 1 as :n-(-1-1) */
        lfail(w, ZL_E_UNSUPPORTED, "a sign written against an operator: put a space between them");
        return -1;
    }
    if (w->tok[t].kind == T_OP && w->tok[t].v == O_SUB) {
        if (w->tok[t].sp & SP_AFTER) {
            int prev = t ? w->tok[t - 1].kind : T_NL;
            if (prev != T_OP && prev != T_LP) {
                lfail(w, ZL_E_SYNTAX,
                      "a '-' with a space after it is a sign only after an "
                      "operator or '('");
                return -1;
            }
            /* UCBLogo reads 2 - - 3 - 1 as 2 - (-3 - 1) */
            lfail(w, ZL_E_UNSUPPORTED, "a sign with a space after it: write -3, -:x or -(...)");
            return -1;
        }
        if (w->tok[t + 1].kind == T_WORD) {
            lfail(w, ZL_E_UNSUPPORTED,
                  "a minus sign before a procedure call (write minus, or -(...))");
            return -1;
        }
        if ((w->tok[t + 1].kind == T_OP && w->tok[t + 1].v == O_SUB) ||
            (w->tok[t + 1].kind == T_NUM && w->src[w->tok[t + 1].off] == '-')) {
            lfail(w, ZL_E_UNSUPPORTED,
                  "a sign on a sign (UCBLogo reads \"- -:x\" as -:x): write -(-x)");
            return -1;
        }
        if (++w->depth > ZL_MAX_DEPTH) {
            lfail(w, ZL_E_LIMIT, "expression nested too deeply");
            return -1;
        }
        w->pos++;
        int32_t l = unary(w);
        w->depth--;
        if (l < 0) return -1;
        if (w->node[l].kind == N_NUM) { /* a sign on a number is the number */
            w->node[l].v = -w->node[l].v;
            return l;
        }
        int32_t id = mk(w, N_NEG, t);
        if (id < 0) return -1;
        w->node[id].a = l;
        return fixd(w, id) ? -1 : id;
    }
    return primary(w);
}

/* Is the token at the cursor a binary operator of this level? */
static int binop_at(lw_t *w, int level)
{
    const ltok_t *k = &w->tok[w->pos];
    if (k->kind != T_OP) return 0;
    if (k->v == O_SUB && (k->sp & SP_BEFORE) && !(k->sp & SP_AFTER)) return 0; /* "a -b" */
    if (level == 0) return k->v >= O_EQ;
    if (level == 1) return k->v == O_ADD || k->v == O_SUB;
    return k->v == O_MUL || k->v == O_DIV;
}

static int32_t binary(lw_t *w, int level)
{
    int32_t l = level == 2 ? unary(w) : binary(w, level + 1);
    while (l >= 0 && binop_at(w, level)) {
        uint32_t t = w->pos++;
        int32_t r = level == 2 ? unary(w) : binary(w, level + 1);
        if (r < 0) return -1;
        int32_t id = mk(w, N_BIN, t);
        if (id < 0) return -1;
        w->node[id].op = (uint8_t) w->tok[t].v;
        w->node[id].a = l;
        w->node[l].next = -1;
        w->node[id].b = r;
        if (fixd(w, id)) return -1;
        l = id;
    }
    return l;
}

static int32_t expr(lw_t *w)
{
    if (++w->depth > ZL_MAX_DEPTH) {
        lfail(w, ZL_E_LIMIT, "expression nested too deeply");
        return -1;
    }
    int32_t e = binary(w, 0);
    w->depth--;
    return e;
}

/* A for control list: [name start end] or [name start end step]. */
static int for_list(lw_t *w, int32_t id)
{
    uint32_t lb = w->pos, rb = (uint32_t) w->tok[lb].v, t = lb + 1;
    if (w->tok[lb].kind != T_LB || w->tok[t].kind != T_WORD)
        return lfail(w, ZL_E_SYNTAX, "for needs [name start end]");
    int nm = name_id(w, t);
    if (nm < 0) return w->c->d->status;
    w->node[id].v = nm;
    int32_t last = -1;
    uint16_t k = 0;
    for (t++; t < rb; t++, k++) {
        int32_t e;
        if (k >= 3 || (w->tok[t].kind != T_NUM && w->tok[t].kind != T_VAR))
            return lfail_at(w, t, ZL_E_UNSUPPORTED,
                            "for takes [name start end] or [name start end step], each a number "
                            "or :name");
        if (w->tok[t].kind == T_NUM) {
            if ((e = mk(w, N_NUM, t)) < 0) return w->c->d->status;
            w->node[e].v = w->tok[t].v;
        } else {
            int v = name_id(w, t);
            if (v < 0 || (e = mk(w, N_VAR, t)) < 0) return w->c->d->status;
            w->node[e].v = v;
        }
        if (last < 0)
            w->node[id].a = e;
        else
            w->node[last].next = e;
        last = e;
    }
    if (k < 2) return lfail(w, ZL_E_SYNTAX, "for needs [name start end]");
    w->node[id].n = k;
    w->pos = rb + 1;
    return ZL_OK;
}

/* One instruction. */
static int32_t instr(lw_t *w)
{
    uint32_t t = w->pos;
    int paren = 0;
    int32_t id;
    if (++w->depth > ZL_MAX_DEPTH) {
        lfail(w, ZL_E_LIMIT, "instructions nested too deeply");
        return -1;
    }
    if (w->tok[t].kind == T_LP && w->tok[t + 1].kind == T_WORD) {
        paren = 1;
        t++;
    }
    if (w->tok[t].kind != T_WORD) {
        lfail(w, ZL_E_SYNTAX, "an instruction starts with a procedure name");
        return -1;
    }
    w->pos = t;
    const lprim_t *pm = find_prim(w, t);
    if (pm && pm->kind == K_CMD) {
        int pid = pm->id;
        if (paren && pid != P_PRINT && pid != P_SHOW && pid != P_TYPE) {
            lfail(w, ZL_E_UNSUPPORTED, "this instruction in parentheses is outside the subset");
            return -1;
        }
        w->pos++;
        switch (pid) {
        case P_PRINT:
        case P_SHOW:
        case P_TYPE:
        case P_OUTPUT:
        case P_STOP:
            if ((id = mk(w, S_PRIM, t)) < 0) return -1;
            w->node[id].op = (uint8_t) pid;
            w->node[id].paren = (uint8_t) paren;
            if (args(w, id, paren, pm->nargs)) return -1;
            if (paren) w->pos++;
            break;
        case P_MAKE:
        case P_LOCAL: {
            if (w->tok[w->pos].kind != T_QUOTE) {
                lfail(w, ZL_E_UNSUPPORTED,
                      pid == P_MAKE ? "make of a computed name is outside the subset"
                                    : "local takes one quoted name");
                return -1;
            }
            int nm = name_id(w, w->pos);
            if (nm < 0 || (id = mk(w, S_PRIM, t)) < 0) return -1;
            w->node[id].op = (uint8_t) pid;
            w->node[id].v = nm;
            w->pos++;
            if (pid == P_MAKE) {
                if (is_endtok(w->tok[w->pos].kind)) {
                    lfail(w, ZL_E_SYNTAX, "not enough inputs");
                    return -1;
                }
                if ((w->node[id].a = expr(w)) < 0) return -1;
            }
            break;
        }
        case P_REPEAT:
        case P_IF:
        case P_IFELSE:
        case P_WHILE:
        case P_UNTIL: {
            int kind = pid == P_REPEAT   ? S_REPEAT
                       : pid == P_IF     ? S_IF
                       : pid == P_IFELSE ? S_IFELSE
                                         : S_WHILE;
            if ((id = mk(w, kind, t)) < 0) return -1;
            w->node[id].op = (uint8_t) pid;
            if (kind == S_WHILE) {
                if ((w->node[id].a = expr_list(w)) < 0) return -1;
            } else {
                if (is_endtok(w->tok[w->pos].kind)) {
                    lfail(w, ZL_E_SYNTAX, "not enough inputs");
                    return -1;
                }
                if ((w->node[id].a = expr(w)) < 0) return -1;
            }
            if ((w->node[id].b = instr_list(w)) < -1) return -1;
            if (kind == S_IFELSE && (w->node[id].c = instr_list(w)) < -1) return -1;
            break;
        }
        default: /* P_FOR */
            if ((id = mk(w, S_FOR, t)) < 0) return -1;
            if (for_list(w, id)) return -1;
            if ((w->node[id].b = instr_list(w)) < -1) return -1;
            break;
        }
        w->depth--;
        return fixd(w, id) ? -1 : id;
    }
    if (pm) {
        lfail(w, ZL_E_SYNTAX, "a value with nothing to do with it");
        return -1;
    }
    if (is_word(w, t, "to") || is_word(w, t, "end")) {
        lfail(w, ZL_E_SYNTAX, "to and end belong on lines of their own");
        return -1;
    }
    const char *why = find_refused(w, t);
    if (why) {
        lfail(w, ZL_E_UNSUPPORTED, why);
        return -1;
    }
    int pi = find_proc(w, t);
    if (pi < 0) {
        unknown(w, t);
        return -1;
    }
    if (w->proc[pi].isop) {
        lfail(w, ZL_E_SYNTAX, "the output of this procedure is not used");
        return -1;
    }
    w->pos++;
    if ((id = mk(w, S_USER, t)) < 0) return -1;
    w->node[id].v = pi;
    if (args(w, id, paren, w->proc[pi].nparams)) return -1;
    if (w->node[id].n != w->proc[pi].nparams) {
        lfail(w, ZL_E_SYNTAX, "wrong number of inputs");
        return -1;
    }
    if (paren) w->pos++;
    w->depth--;
    return fixd(w, id) ? -1 : id;
}

/* [instructions]: returns the first (or -1 for an empty list); < -1 on failure. */
static int32_t instr_list(lw_t *w)
{
    if (w->tok[w->pos].kind != T_LB) {
        lfail(w, ZL_E_SYNTAX, "'[' expected");
        return -2;
    }
    uint32_t rb = (uint32_t) w->tok[w->pos].v;
    int32_t first = -1, last = -1;
    w->pos++;
    while (w->pos < rb) {
        int32_t s = instr(w);
        if (s < 0) return -2;
        if (last < 0)
            first = s;
        else
            w->node[last].next = s;
        last = s;
    }
    w->pos++;
    return first;
}

/* ------------------------------------------------------------ twin */

static void tw_s(lw_t *w, const char *s)
{
    if (w->tw) zl_puts(w->tw, s);
}
static void tw_c(lw_t *w, char ch)
{
    if (w->tw) zl_putc(w->tw, ch);
}
static void tw_lower(lw_t *w, uint32_t t)
{
    if (!w->tw) return;
    for (uint32_t i = 0; i < w->tok[t].len; i++)
        zl_putc(w->tw, (char) lower_c((unsigned char) w->src[w->tok[t].off + i]));
}
static void tw_text(lw_t *w, uint32_t t)
{
    if (w->tw) zl_put(w->tw, w->src + w->tok[t].off, w->tok[t].len);
}

static const char *prim_canon(int id)
{
    for (size_t i = 0; i < NPRIMS; i++)
        if (prims[i].id == id) return prims[i].canon;
    return "?";
}
static const lprim_t *prim_of(int id)
{
    for (size_t i = 0; i < NPRIMS; i++)
        if (prims[i].id == id) return &prims[i];
    return &prims[0];
}

static int prec(const lnode_t *n)
{
    if (n->kind == N_BIN) return n->op >= O_EQ ? 1 : n->op <= O_SUB ? 2 : 3;
    if (n->kind == N_NEG) return 4;
    return 5;
}

static void tw_expr(lw_t *w, int32_t id, int need, int tail);

/* tail 2: an input of a parenthesized call; 3: also after an operator */
static void tw_args(lw_t *w, int32_t a, int paren)
{
    for (; a >= 0; a = w->node[a].next) {
        tw_c(w, ' ');
        tw_expr(w, a, 0, paren ? 2 : 1);
    }
}

static void tw_data(lw_t *w, uint32_t lb)
{
    uint32_t rb = (uint32_t) w->tok[lb].v;
    int open = 1;
    tw_c(w, '[');
    for (uint32_t t = lb + 1; t < rb; t++) {
        int k = w->tok[t].kind;
        if (k != T_RB && !open) tw_c(w, ' ');
        open = k == T_LB;
        if (k == T_LB)
            tw_c(w, '[');
        else if (k == T_RB)
            tw_c(w, ']');
        else if (k == T_NUM) {
            if (w->tw) zl_puti(w->tw, w->tok[t].v);
        } else
            tw_text(w, t);
    }
    tw_c(w, ']');
}

static void tw_expr(lw_t *w, int32_t id, int need, int tail)
{
    const lnode_t *n = &w->node[id];
    int p = prec(n);
    int callp = (n->kind == N_PRIM || n->kind == N_USER || n->kind == N_IFELSE);
    int nargs = n->kind == N_PRIM   ? prim_of(n->op)->nargs
                : n->kind == N_USER ? w->proc[n->v].nparams
                                    : 3;
    int paren_call = callp && nargs && ((n->kind != N_IFELSE && n->n != nargs) || !tail);
    if (n->kind == N_PRIM && tail == 3 && prim_of(n->op)->maxargs == VAR_ARGS) paren_call = 1;
    int paren = p < need;
    if (paren) tw_c(w, '(');
    switch (n->kind) {
    case N_NUM:
        if (w->tw) zl_puti(w->tw, n->v);
        break;
    case N_WORD:
        tw_c(w, '"');
        tw_text(w, n->tok);
        break;
    case N_VAR:
        tw_c(w, ':');
        tw_lower(w, w->tok[n->tok].kind == T_WORD ? n->tok + 1 : n->tok); /* thing "x is :x */
        break;
    case N_LIST:
        tw_data(w, n->tok);
        break;
    case N_NEG:
        tw_c(w, '-');
        if (w->node[n->a].kind == N_PRIM || w->node[n->a].kind == N_USER) {
            tw_c(w, '('); /* -(repcount): a sign never stands before a name */
            tw_expr(w, n->a, 0, 1);
            tw_c(w, ')');
        } else
            tw_expr(w, n->a, 5, 0);
        break;
    case N_BIN: {
        static const char *const ops[] = {"",     " + ", " - ", " * ",  " / ", " = ",
                                          " <> ", " < ", " > ", " <= ", " >= "};
        tw_expr(w, n->a, p, 0);
        tw_s(w, ops[n->op]);
        tw_expr(w, n->b, p + 1, paren ? 1 : tail >= 2 ? 3 : tail);
        break;
    }
    case N_IFELSE:
        if (paren_call) tw_c(w, '(');
        tw_s(w, "ifelse ");
        tw_expr(w, n->a, 0, 1);
        tw_s(w, " [");
        tw_expr(w, n->b, 0, 1);
        tw_s(w, "] [");
        tw_expr(w, n->c, 0, 1);
        tw_c(w, ']');
        if (paren_call) tw_c(w, ')');
        break;
    default: /* N_PRIM, N_USER */
        if (paren_call) tw_c(w, '(');
        if (n->kind == N_PRIM)
            tw_s(w, prim_canon(n->op));
        else
            tw_lower(w, w->proc[n->v].tok);
        tw_args(w, n->a, paren_call);
        if (paren_call) tw_c(w, ')');
        break;
    }
    if (paren) tw_c(w, ')');
}

static void tw_instr(lw_t *w, int32_t id);

static void tw_list(lw_t *w, int32_t first)
{
    tw_c(w, '[');
    for (int32_t s = first; s >= 0; s = w->node[s].next) {
        if (s != first) tw_c(w, ' ');
        tw_instr(w, s);
    }
    tw_c(w, ']');
}

static void tw_instr(lw_t *w, int32_t id)
{
    const lnode_t *n = &w->node[id];
    switch (n->kind) {
    case S_PRIM:
        if (n->op == P_MAKE || n->op == P_LOCAL) {
            tw_s(w, n->op == P_MAKE ? "make \"" : "local \"");
            tw_lower(w, n->tok + 1);
            if (n->op == P_MAKE) {
                tw_c(w, ' ');
                tw_expr(w, n->a, 0, 1);
            }
            break;
        }
        {
            int paren = n->paren && n->n != 1;
            if (paren) tw_c(w, '(');
            tw_s(w, prim_canon(n->op));
            tw_args(w, n->a, paren);
            if (paren) tw_c(w, ')');
        }
        break;
    case S_USER:
        tw_lower(w, w->proc[n->v].tok);
        tw_args(w, n->a, 0);
        break;
    case S_REPEAT:
        tw_s(w, "repeat ");
        tw_expr(w, n->a, 0, 1);
        tw_c(w, ' ');
        tw_list(w, n->b);
        break;
    case S_FOR:
        tw_s(w, "for [");
        tw_lower(w, n->tok + 2);
        for (int32_t a = n->a; a >= 0; a = w->node[a].next) {
            tw_c(w, ' ');
            tw_expr(w, a, 0, 1);
        }
        tw_s(w, "] ");
        tw_list(w, n->b);
        break;
    case S_WHILE:
        tw_s(w, n->op == P_WHILE ? "while [" : "until [");
        tw_expr(w, n->a, 0, 1);
        tw_s(w, "] ");
        tw_list(w, n->b);
        break;
    case S_IF:
    case S_IFELSE:
        tw_s(w, n->kind == S_IF ? "if " : "ifelse ");
        tw_expr(w, n->a, 0, 1);
        tw_c(w, ' ');
        tw_list(w, n->b);
        if (n->kind == S_IFELSE) {
            tw_c(w, ' ');
            tw_list(w, n->c);
        }
        break;
    }
}

/* ------------------------------------------------------------ lowering */

static int32_t E(lw_t *w, int op, int64_t a, int32_t b)
{
    return zl_emit(w->c, op, a, b);
}

static int addfix(lw_t *w, int32_t at, int32_t target)
{
    if (at < 0) return w->c->d->status;
    if (w->nfix >= L_FIX) return lfail(w, ZL_E_LIMIT, "too many calls");
    w->fix[w->nfix].at = at;
    w->fix[w->nfix].target = target;
    w->nfix++;
    return ZL_OK;
}

static int helper(lw_t *w, int h)
{
    w->helper_used[h] = 1;
    return addfix(w, E(w, ZL_CALL, 0, 0), -1 - h);
}

/* Cells for loop state: frame cells in a procedure, slots at top level. */
static int32_t cell(lw_t *w)
{
    if (w->cur < 0) return zl_slots(w->c, 1);
    if (w->ncells >= ZL_MAX_FRAMES) {
        lfail(w, ZL_E_LIMIT, "procedure frame too large");
        return -1;
    }
    return (int32_t) w->ncells++;
}
static void cload(lw_t *w, int32_t cl)
{
    if (w->cur < 0)
        E(w, ZL_LOAD, cl, 0);
    else
        E(w, ZL_LOADF, cl, 0);
}
static void cstore(lw_t *w, int32_t cl)
{
    if (w->cur < 0)
        E(w, ZL_STORE, cl, 0);
    else
        E(w, ZL_STOREF, cl, 0);
}

static int bind_push(lw_t *w, int kind, int32_t cl, int32_t slot, int32_t name)
{
    if (w->nbind >= L_BIND) return lfail(w, ZL_E_LIMIT, "too many bindings");
    w->bind[w->nbind].kind = (uint8_t) kind;
    w->bind[w->nbind].cell = cl;
    w->bind[w->nbind].slot = slot;
    w->bind[w->nbind].name = name;
    w->nbind++;
    return ZL_OK;
}

/* Undo the bindings above `down_to`, innermost first. */
static void unbind(lw_t *w, uint32_t down_to)
{
    for (uint32_t i = w->nbind; i > down_to; i--) {
        cload(w, w->bind[i - 1].cell);
        E(w, ZL_STORE, w->bind[i - 1].slot, 0);
    }
}

static int bound(const lw_t *w, int name)
{
    for (uint32_t i = 0; i < w->nbind; i++)
        if (w->bind[i].name == name && (w->bind[i].kind == B_PARAM || w->bind[i].kind == B_FOR))
            return 1;
    return 0;
}

static int32_t rc_slot(lw_t *w)
{
    if (w->rc_slot < 0) w->rc_slot = zl_slots(w->c, 1);
    return w->rc_slot;
}

static void set_line(lw_t *w, int32_t id)
{
    w->c->line = w->tok[w->node[id].tok].line;
}

static int val(lw_t *w, int32_t id);

/* Is the value known to be a number, a word or a boolean (never a list)? */
static int atomic(const lw_t *w, int32_t id)
{
    const lnode_t *n = &w->node[id];
    switch (n->kind) {
    case N_NUM:
    case N_WORD:
    case N_NEG:
    case N_BIN:
        return 1;
    case N_PRIM:
        return n->op != P_FIRST && n->op != P_BF && n->op != P_LAST && n->op != P_BL &&
               n->op != P_FPUT && n->op != P_LPUT && n->op != P_LIST && n->op != P_SE &&
               n->op != P_ITEM && n->op != P_READLIST;
    default:
        return 0;
    }
}

/* Is the node a condition (computed as 1 / 0, shown as true / false)? */
static int boolean(const lw_t *w, int32_t id)
{
    const lnode_t *n = &w->node[id];
    if (n->kind == N_BIN) return n->op >= O_EQ;
    if (n->kind != N_PRIM) return 0;
    switch (n->op) {
    case P_EQUALP:
    case P_NOTEQUALP:
    case P_LESSP:
    case P_GREATERP:
    case P_LESSEQ:
    case P_GREATEREQ:
    case P_AND:
    case P_OR:
    case P_NOT:
    case P_EMPTYP:
    case P_LISTP:
    case P_NUMBERP:
    case P_WORDP:
    case P_MEMBERP:
        return 1;
    default:
        return 0;
    }
}

static int flag(lw_t *w, int32_t id);

static int compare(lw_t *w, int op, int32_t a, int32_t b)
{
    int s;
    if ((s = val(w, a)) || (s = val(w, b))) return s;
    switch (op) {
    case O_EQ:
    case O_NE:
        if (atomic(w, a) || atomic(w, b))
            E(w, ZL_EQ, 0, 0);
        else if ((s = helper(w, H_EQL)))
            return s;
        if (op == O_NE) E(w, ZL_NOT, 0, 0);
        break;
    case O_LT:
        E(w, ZL_LT, 0, 0);
        break;
    case O_GT:
        E(w, ZL_GT, 0, 0);
        break;
    case O_LE:
        E(w, ZL_LE, 0, 0);
        break;
    default:
        E(w, ZL_GE, 0, 0);
        break;
    }
    return w->c->failed ? w->c->d->status : ZL_OK;
}

/* Lower a condition to 1 / 0. */
static int flag(lw_t *w, int32_t id)
{
    const lnode_t *n = &w->node[id];
    int32_t a = n->a, b = a >= 0 ? w->node[a].next : -1;
    int s;
    if (!boolean(w, id)) {
        if ((s = val(w, id))) return s;
        return helper(w, H_TOBOOL);
    }
    set_line(w, id);
    if (n->kind == N_BIN) return compare(w, n->op, n->a, n->b);
    switch (n->op) {
    case P_EQUALP:
        return compare(w, O_EQ, a, b);
    case P_NOTEQUALP:
        return compare(w, O_NE, a, b);
    case P_LESSP:
        return compare(w, O_LT, a, b);
    case P_GREATERP:
        return compare(w, O_GT, a, b);
    case P_LESSEQ:
        return compare(w, O_LE, a, b);
    case P_GREATEREQ:
        return compare(w, O_GE, a, b);
    case P_AND:
    case P_OR:
        if (a < 0) {
            E(w, ZL_PUSH, n->op == P_AND, 0);
            break;
        }
        for (int32_t x = a; x >= 0; x = w->node[x].next) {
            if ((s = flag(w, x))) return s;
            if (x != a) E(w, n->op == P_AND ? ZL_AND : ZL_OR, 0, 0);
        }
        break;
    case P_NOT:
        if ((s = flag(w, a))) return s;
        E(w, ZL_NOT, 0, 0);
        break;
    case P_EMPTYP:
        if ((s = val(w, a))) return s;
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        break;
    case P_NUMBERP:
        if ((s = val(w, a))) return s;
        E(w, ZL_ISA, ZL_ISA_NUMBER, 0);
        break;
    case P_LISTP:
    case P_WORDP:
        if ((s = val(w, a))) return s;
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        E(w, ZL_OR, 0, 0);
        if (n->op == P_WORDP) E(w, ZL_NOT, 0, 0);
        break;
    default: /* P_MEMBERP */
        if ((s = val(w, a)) || (s = val(w, b))) return s;
        return helper(w, H_MEM);
    }
    return w->c->failed ? w->c->d->status : ZL_OK;
}

static int call_user(lw_t *w, int32_t id)
{
    const lnode_t *n = &w->node[id];
    int s;
    E(w, ZL_LINK, 0, 0);
    for (int32_t a = n->a; a >= 0; a = w->node[a].next)
        if ((s = val(w, a))) return s;
    set_line(w, id);
    if (w->cur >= 0)
        w->edge[w->cur][n->v / 64] |= 1ull << (n->v % 64);
    else {
        if (w->ntop >= L_TOP) return lfail_at(w, n->tok, ZL_E_LIMIT, "too many calls");
        w->top[w->ntop].proc = (uint32_t) n->v;
        w->top[w->ntop].defs = w->ndef;
        w->top[w->ntop].line = w->tok[n->tok].line;
        w->ntop++;
    }
    return addfix(w, E(w, ZL_CALL, 0, 0), (int32_t) n->v);
}

/* Lower an expression to its Logo value. */
static int val(lw_t *w, int32_t id)
{
    const lnode_t *n = &w->node[id];
    int32_t a = n->a, b = a >= 0 ? w->node[a].next : -1;
    int s;
    if (boolean(w, id)) {
        if ((s = flag(w, id))) return s;
        return helper(w, H_FROMBOOL);
    }
    set_line(w, id);
    switch (n->kind) {
    case N_NUM:
    case N_WORD:
        E(w, ZL_PUSH, n->v, 0);
        break;
    case N_VAR: {
        int32_t sl = w->name[n->v].slot;
        E(w, ZL_LOAD, sl, 0);
        if (!bound(w, (int) n->v)) { /* a variable with no value stops the program */
            E(w, ZL_DUP, 0, 0);
            E(w, ZL_PUSH, UNB(w), 0);
            E(w, ZL_EQ, 0, 0);
            int32_t j = E(w, ZL_JZ, 0, 0);
            E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
            zl_patch(w->c, j, (int64_t) w->c->p->ncode);
        }
        break;
    }
    case N_LIST:
        if (n->v < 0)
            E(w, ZL_PUSH, ZL_V_NIL, 0);
        else
            E(w, ZL_LOAD, n->v, 0);
        break;
    case N_NEG:
        if ((s = val(w, a))) return s;
        E(w, ZL_NEG, 0, 0);
        break;
    case N_BIN:
        if ((s = val(w, a)) || (s = val(w, n->b))) return s;
        E(w,
          n->op == O_ADD   ? ZL_ADD
          : n->op == O_SUB ? ZL_SUB
          : n->op == O_MUL ? ZL_MUL
                           : ZL_DIVX,
          0, 0);
        break;
    case N_USER:
        return call_user(w, id);
    case N_IFELSE: {
        if ((s = flag(w, a))) return s;
        int32_t j = E(w, ZL_JZ, 0, 0);
        if ((s = val(w, n->b))) return s;
        int32_t e = E(w, ZL_JMP, 0, 0);
        zl_patch(w->c, j, (int64_t) w->c->p->ncode);
        if ((s = val(w, n->c))) return s;
        zl_patch(w->c, e, (int64_t) w->c->p->ncode);
        break;
    }
    default: /* N_PRIM */
        switch (n->op) {
        case P_SUM:
        case P_PROD:
            if (a < 0 || w->node[a].next < 0) E(w, ZL_PUSH, n->op == P_PROD, 0);
            for (int32_t x = a; x >= 0; x = w->node[x].next) {
                if ((s = val(w, x))) return s;
                if (x != a || w->node[a].next < 0) E(w, n->op == P_SUM ? ZL_ADD : ZL_MUL, 0, 0);
            }
            break;
        case P_DIFF:
        case P_QUOT:
        case P_REM:
            if ((s = val(w, a)) || (s = val(w, b))) return s;
            E(w, n->op == P_DIFF ? ZL_SUB : n->op == P_QUOT ? ZL_DIVX : ZL_MODT, 0, 0);
            break;
        case P_MOD:
            if ((s = val(w, a)) || (s = val(w, b))) return s;
            return helper(w, H_MODULO);
        case P_MINUS:
            if ((s = val(w, a))) return s;
            E(w, ZL_NEG, 0, 0);
            break;
        case P_FIRST:
        case P_BF:
            if ((s = val(w, a))) return s;
            E(w, n->op == P_FIRST ? ZL_CAR : ZL_CDR, 0, 0);
            break;
        case P_LAST:
        case P_BL:
        case P_COUNT:
            if ((s = val(w, a))) return s;
            return helper(w, n->op == P_LAST ? H_LAST : n->op == P_BL ? H_BL : H_CNT);
        case P_FPUT:
        case P_LPUT:
        case P_ITEM:
            if ((s = val(w, a)) || (s = val(w, b))) return s;
            return helper(w, n->op == P_FPUT ? H_FPUT : n->op == P_LPUT ? H_LPUT : H_ITEM);
        case P_LIST:
            for (int32_t x = a; x >= 0; x = w->node[x].next)
                if ((s = val(w, x))) return s;
            E(w, ZL_PUSH, ZL_V_NIL, 0);
            for (int32_t x = a; x >= 0; x = w->node[x].next) E(w, ZL_CONS, 0, 0);
            break;
        case P_SE:
            if (a < 0) {
                E(w, ZL_PUSH, ZL_V_NIL, 0);
                break;
            }
            for (int32_t x = a; x >= 0; x = w->node[x].next)
                if ((s = val(w, x))) return s;
            if (w->node[a].next < 0) return helper(w, H_ASLIST);
            for (int32_t x = w->node[a].next; x >= 0; x = w->node[x].next)
                if ((s = helper(w, H_SE))) return s;
            break;
        case P_READWORD: /* the next integer of the input; none left traps INPUT */
            E(w, ZL_IN, 0, 0);
            break;
        case P_READLIST: /* the line's one integer, as a list of one word */
            E(w, ZL_IN, 0, 0);
            E(w, ZL_PUSH, ZL_V_NIL, 0);
            E(w, ZL_CONS, 0, 0);
            break;
        default: /* P_REPCOUNT */
            E(w, ZL_LOAD, rc_slot(w), 0);
            break;
        }
        break;
    }
    return w->c->failed ? w->c->d->status : ZL_OK;
}

static int stmts(lw_t *w, int32_t first);

/* Leave the procedure: restore every binding, then return. */
static void leave(lw_t *w)
{
    unbind(w, 0);
    E(w, ZL_LEAVE, 0, 0);
    E(w, ZL_RET, 0, 0);
}

static int stmt(lw_t *w, int32_t id)
{
    lnode_t *n = &w->node[id];
    zl_cc_t *c = w->c;
    int s;
    set_line(w, id);
    switch (n->kind) {
    case S_USER:
        return call_user(w, id);
    case S_PRIM:
        switch (n->op) {
        case P_PRINT:
        case P_SHOW:
        case P_TYPE: {
            int dialect = n->op == P_SHOW ? ZL_DIALECT_LOGO_SHOW : ZL_DIALECT_LOGO;
            int32_t first = -1, k = 0;
            if (n->n > 1) { /* every input is computed before anything is printed */
                for (k = 0; k < n->n; k++) { /* contiguous: allocated before any val() */
                    int32_t cl = cell(w);
                    if (cl < 0) return c->d->status;
                    if (first < 0) first = cl;
                }
                k = 0;
                for (int32_t a = n->a; a >= 0; a = w->node[a].next, k++) {
                    if ((s = val(w, a))) return s;
                    cstore(w, first + k);
                }
            }
            k = 0;
            for (int32_t a = n->a; a >= 0; a = w->node[a].next, k++) {
                if (a != n->a && n->op != P_TYPE) {
                    if (w->space_str < 0 && (w->space_str = zl_addstr(c, " ", 1)) < 0)
                        return c->d->status;
                    E(w, ZL_OUTS, w->space_str, 0);
                }
                if (first >= 0)
                    cload(w, first + k);
                else if ((s = val(w, a)))
                    return s;
                E(w, ZL_OUTV, dialect, 0);
            }
            if (n->op != P_TYPE) E(w, ZL_NL, 0, 0);
            break;
        }
        case P_MAKE:
            if ((s = val(w, n->a))) return s;
            E(w, ZL_STORE, w->name[n->v].slot, 0);
            break;
        case P_LOCAL: {
            if (w->cur < 0) return lfail_at(w, n->tok, ZL_E_SYNTAX, "local outside a procedure");
            if (w->body_started || w->nest)
                return lfail_at(w, n->tok, ZL_E_UNSUPPORTED,
                                "local is admitted only at the start of a procedure body");
            for (uint32_t i = 0; i < w->nbind; i++)
                if (w->bind[i].name == (int32_t) n->v)
                    return lfail_at(w, n->tok + 1, ZL_E_UNSUPPORTED,
                                    "this name is already an input or local here");
            int32_t cl = cell(w);
            if (cl < 0) return c->d->status;
            int32_t sl = w->name[n->v].slot;
            E(w, ZL_LOAD, sl, 0);
            E(w, ZL_STOREF, cl, 0);
            E(w, ZL_PUSH, UNB(w), 0);
            E(w, ZL_STORE, sl, 0);
            return bind_push(w, B_LOCAL, cl, sl, (int32_t) n->v);
        }
        case P_OUTPUT:
            if (w->cur < 0) return lfail_at(w, n->tok, ZL_E_SYNTAX, "output outside a procedure");
            if ((s = val(w, n->a))) return s;
            leave(w);
            break;
        default: /* P_STOP */
            if (w->cur < 0) return lfail_at(w, n->tok, ZL_E_SYNTAX, "stop outside a procedure");
            if (w->nrep) { /* UCBLogo 6.2: stop ends the innermost repeat */
                uint32_t r = w->nrep - 1;
                unbind(w, w->rep[r].bind_at + 1);
                w->rep[r].chain = E(w, ZL_JMP, w->rep[r].chain, 0);
            } else if (w->proc[w->cur].isop) {
                E(w, ZL_TRAP, ZL_E_DOMAIN, 0); /* an operation that does not output */
            } else {
                leave(w);
            }
            break;
        }
        break;
    case S_REPEAT: {
        int32_t lim = cell(w), save = cell(w), rc = rc_slot(w);
        if (lim < 0 || save < 0 || rc < 0) return c->d->status;
        if (w->nrep >= ZL_MAX_DEPTH) return lfail(w, ZL_E_LIMIT, "repeats nested too deeply");
        if ((s = val(w, n->a))) return s;
        E(w, ZL_DUP, 0, 0); /* UCBLogo: repeat doesn't like a negative count */
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_LT, 0, 0);
        int32_t ok = E(w, ZL_JZ, 0, 0);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        zl_patch(c, ok, (int64_t) c->p->ncode);
        cstore(w, lim);
        E(w, ZL_LOAD, rc, 0);
        cstore(w, save);
        if ((s = bind_push(w, B_REPEAT, save, rc, -1))) return s;
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_STORE, rc, 0);
        int32_t top = E(w, ZL_LOAD, rc, 0);
        cload(w, lim);
        E(w, ZL_GT, 0, 0);
        int32_t x = E(w, ZL_JNZ, 0, 0);
        uint32_t r = w->nrep++;
        w->rep[r].bind_at = w->nbind - 1;
        w->rep[r].chain = -1;
        if ((s = stmts(w, n->b))) return s;
        set_line(w, id);
        E(w, ZL_LOAD, rc, 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_ADD, 0, 0);
        E(w, ZL_STORE, rc, 0);
        E(w, ZL_JMP, top, 0);
        int64_t end = (int64_t) c->p->ncode;
        zl_patch(c, x, end);
        for (int32_t j = w->rep[r].chain; j >= 0;) {
            int32_t nx = (int32_t) c->p->code[j].a;
            zl_patch(c, j, end);
            j = nx;
        }
        w->nrep--;
        w->nbind--;
        cload(w, save);
        E(w, ZL_STORE, rc, 0);
        break;
    }
    case S_FOR: {
        int32_t cur = cell(w), lim = cell(w), step = cell(w), save = cell(w);
        int32_t sl = w->name[n->v].slot;
        if (cur < 0 || lim < 0 || step < 0 || save < 0) return c->d->status;
        int32_t e1 = n->a, e2 = w->node[e1].next, e3 = w->node[e2].next;
        if ((s = val(w, e1))) return s;
        cstore(w, cur);
        if ((s = val(w, e2))) return s;
        cstore(w, lim);
        if (e3 >= 0) {
            if ((s = val(w, e3))) return s;
        } else { /* UCBLogo: -1 when the start is above the end, else 1 */
            cload(w, cur);
            cload(w, lim);
            E(w, ZL_GT, 0, 0);
            int32_t j = E(w, ZL_JZ, 0, 0);
            E(w, ZL_PUSH, -1, 0);
            int32_t k = E(w, ZL_JMP, 0, 0);
            zl_patch(c, j, (int64_t) c->p->ncode);
            E(w, ZL_PUSH, 1, 0);
            zl_patch(c, k, (int64_t) c->p->ncode);
        }
        cstore(w, step);
        E(w, ZL_LOAD, sl, 0);
        cstore(w, save);
        int32_t top = (int32_t) c->p->ncode;
        cload(w, cur);
        E(w, ZL_STORE, sl, 0);
        cload(w, step);
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_LT, 0, 0);
        int32_t jpos = E(w, ZL_JZ, 0, 0);
        cload(w, cur);
        cload(w, lim);
        E(w, ZL_LT, 0, 0);
        int32_t x1 = E(w, ZL_JNZ, 0, 0);
        int32_t jb = E(w, ZL_JMP, 0, 0);
        zl_patch(c, jpos, (int64_t) c->p->ncode);
        cload(w, cur);
        cload(w, lim);
        E(w, ZL_GT, 0, 0);
        int32_t x2 = E(w, ZL_JNZ, 0, 0);
        zl_patch(c, jb, (int64_t) c->p->ncode);
        if ((s = bind_push(w, B_FOR, save, sl, (int32_t) n->v))) return s;
        if ((s = stmts(w, n->b))) return s;
        set_line(w, id);
        w->nbind--;
        cload(w, cur);
        cload(w, step);
        E(w, ZL_ADD, 0, 0);
        cstore(w, cur);
        E(w, ZL_JMP, top, 0);
        zl_patch(c, x1, (int64_t) c->p->ncode);
        zl_patch(c, x2, (int64_t) c->p->ncode);
        cload(w, save);
        E(w, ZL_STORE, sl, 0);
        break;
    }
    case S_WHILE: {
        int32_t top = (int32_t) c->p->ncode;
        if ((s = flag(w, n->a))) return s;
        int32_t x = E(w, n->op == P_WHILE ? ZL_JZ : ZL_JNZ, 0, 0);
        if ((s = stmts(w, n->b))) return s;
        set_line(w, id);
        E(w, ZL_JMP, top, 0);
        zl_patch(c, x, (int64_t) c->p->ncode);
        break;
    }
    default: { /* S_IF, S_IFELSE */
        if ((s = flag(w, n->a))) return s;
        int32_t j = E(w, ZL_JZ, 0, 0);
        if ((s = stmts(w, n->b))) return s;
        if (n->kind == S_IFELSE) {
            set_line(w, id);
            int32_t e = E(w, ZL_JMP, 0, 0);
            zl_patch(c, j, (int64_t) c->p->ncode);
            if ((s = stmts(w, n->c))) return s;
            zl_patch(c, e, (int64_t) c->p->ncode);
        } else {
            zl_patch(c, j, (int64_t) c->p->ncode);
        }
        break;
    }
    }
    return c->failed ? c->d->status : ZL_OK;
}

static int stmts(lw_t *w, int32_t first)
{
    w->nest++;
    for (int32_t s = first; s >= 0; s = w->node[s].next) {
        int st = stmt(w, s);
        if (st) return st;
    }
    w->nest--;
    return ZL_OK;
}

/* ------------------------------------------------------------ lines */

static void tw_comment(lw_t *w, uint32_t t, int own_line, int indent)
{
    if (own_line)
        for (int i = 0; i < indent; i++) tw_c(w, ' ');
    else
        tw_c(w, ' ');
    tw_c(w, ';');
    if (w->tok[t].len) {
        tw_c(w, ' ');
        tw_text(w, t);
    }
}

/* End of a line: an optional comment, then the newline (or the end). */
static int line_end(lw_t *w, int own_line, int indent)
{
    if (w->tok[w->pos].kind == T_COMMENT) {
        tw_comment(w, w->pos, own_line, indent);
        w->pos++;
    }
    if (w->tok[w->pos].kind == T_NL)
        w->pos++;
    else if (w->tok[w->pos].kind != T_EOF)
        return lfail(w, ZL_E_SYNTAX, "unexpected text at the end of the line");
    tw_c(w, '\n');
    return ZL_OK;
}

/* UCBLogo runs the last instruction of a procedure as a tail call: a stop
 * inside a repeat there escapes the procedure (from the top level it ends
 * the whole file). Such a stop is refused: rt says the innermost enclosing
 * repeat is in tail position. */
static int tail_stop(lw_t *w, int32_t s, int tail, int rt)
{
    for (; s >= 0; s = w->node[s].next) {
        const lnode_t *n = &w->node[s];
        int t = tail && n->next < 0, st = ZL_OK;
        if (n->kind == S_PRIM && n->op == P_STOP && rt)
            return lfail_at(w, n->tok, ZL_E_UNSUPPORTED,
                            "stop inside a repeat that ends a procedure: UCBLogo's tail call "
                            "lets it escape the procedure");
        if (n->kind == S_REPEAT) st = tail_stop(w, n->b, t, t);
        if (n->kind == S_FOR || n->kind == S_WHILE || n->kind == S_IF || n->kind == S_IFELSE)
            st = tail_stop(w, n->b, t, rt);
        if (!st && n->kind == S_IFELSE) st = tail_stop(w, n->c, t, rt);
        if (st) return st;
    }
    return ZL_OK;
}

/* Is the instruction just read the last one of a procedure body? */
static int last_in_body(lw_t *w)
{
    uint32_t p = w->pos;
    while (w->tok[p].kind == T_NL || w->tok[p].kind == T_COMMENT) p++;
    return is_word(w, p, "end");
}

/* The instructions of one line (top level or a procedure body). */
static int line(lw_t *w, int indent)
{
    if (w->tok[w->pos].kind == T_COMMENT) return line_end(w, 1, indent);
    for (;;) {
        w->nnode = 0;
        w->depth = 0;
        int32_t id = instr(w);
        if (id < 0) return w->c->d->status;
        if (w->cur >= 0 && last_in_body(w)) {
            int32_t nx = w->node[id].next;
            w->node[id].next = -1;
            int st = tail_stop(w, id, 1, 0);
            w->node[id].next = nx;
            if (st) return st;
        }
        for (int i = 0; i < indent; i++) tw_c(w, ' ');
        tw_instr(w, id);
        int s = stmt(w, id);
        if (s) return s;
        if (w->node[id].kind != S_PRIM || w->node[id].op != P_LOCAL) w->body_started = 1;
        int k = w->tok[w->pos].kind;
        if (k == T_NL || k == T_COMMENT || k == T_EOF) return line_end(w, 0, indent);
        if (k == T_RB || k == T_RP) return lfail(w, ZL_E_SYNTAX, "unbalanced ']' or ')'");
        tw_c(w, '\n');
    }
}

static int procedure(lw_t *w)
{
    zl_cc_t *c = w->c;
    uint32_t nt = w->pos + 1;
    int pi = find_proc(w, nt), s;
    lproc_t *pr = &w->proc[pi];
    w->cur = pi;
    w->nbind = 0;
    w->nrep = 0;
    w->ncells = pr->nparams;
    w->body_started = 0;
    c->line = w->tok[nt].line;
    tw_s(w, "to ");
    tw_lower(w, nt);
    int32_t over = E(w, ZL_JMP, 0, 0);
    pr->addr = (int32_t) c->p->ncode;
    int32_t enter = E(w, ZL_ENTER, 0, pr->nparams);
    for (uint32_t k = 0; k < pr->nparams; k++) {
        uint32_t t = nt + 1 + k;
        int nm = name_id(w, t);
        if (nm < 0) return c->d->status;
        for (uint32_t j = 0; j < k; j++)
            if (pr->params[j] == nm) return lfail_at(w, t, ZL_E_SYNTAX, "an input named twice");
        pr->params[k] = (int16_t) nm;
        tw_s(w, " :");
        tw_lower(w, t);
        int32_t sl = w->name[nm].slot;
        E(w, ZL_LOAD, sl, 0);
        E(w, ZL_LOADF, k, 0);
        E(w, ZL_STORE, sl, 0);
        E(w, ZL_STOREF, k, 0);
        if ((s = bind_push(w, B_PARAM, (int32_t) k, sl, nm))) return s;
    }
    w->pos = nt + 1 + pr->nparams;
    if ((s = line_end(w, 0, 0))) return s;
    for (;;) {
        int k = w->tok[w->pos].kind;
        if (k == T_NL) {
            w->pos++;
            continue;
        }
        if (is_word(w, w->pos, "end")) break;
        if ((s = line(w, 2))) return s;
    }
    c->line = w->tok[w->pos].line;
    if (pr->isop)
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0); /* fell off the end of an operation */
    else
        leave(w);
    tw_s(w, "end");
    w->pos++;
    if ((s = line_end(w, 0, 0))) return s;
    if (enter >= 0) c->p->code[enter].a = w->ncells;
    zl_patch(c, over, (int64_t) c->p->ncode);
    w->cur = -1;
    w->nbind = 0;
    w->nrep = 0;
    w->ndef++;
    return c->failed ? c->d->status : ZL_OK;
}

/* ------------------------------------------------------------ run-time helpers */

static int32_t scratch(lw_t *w, int k)
{
    if (w->scratch < 0) w->scratch = zl_slots(w->c, 4);
    return w->scratch + k;
}

/* Copy the list in sa onto the cell in st, cell by cell (sh: head, st: tail). */
static void copy_cell(lw_t *w)
{
    int32_t sa = scratch(w, 0), st = scratch(w, 2), sn = scratch(w, 3);
    E(w, ZL_LOAD, sa, 0);
    E(w, ZL_CAR, 0, 0);
    E(w, ZL_PUSH, ZL_V_NIL, 0);
    E(w, ZL_CONS, 0, 0);
    E(w, ZL_DUP, 0, 0);
    E(w, ZL_STORE, sn, 0);
    E(w, ZL_LOAD, st, 0);
    E(w, ZL_SWAP, 0, 0);
    E(w, ZL_SETCDR, 0, 0);
    E(w, ZL_LOAD, sn, 0);
    E(w, ZL_STORE, st, 0);
    E(w, ZL_LOAD, sa, 0);
    E(w, ZL_CDR, 0, 0);
    E(w, ZL_STORE, sa, 0);
}

/* First cell of a copy: sh = st = (car sa), sa = cdr sa. */
static void copy_first(lw_t *w)
{
    int32_t sa = scratch(w, 0), sh = scratch(w, 1), st = scratch(w, 2);
    E(w, ZL_LOAD, sa, 0);
    E(w, ZL_CAR, 0, 0);
    E(w, ZL_PUSH, ZL_V_NIL, 0);
    E(w, ZL_CONS, 0, 0);
    E(w, ZL_DUP, 0, 0);
    E(w, ZL_STORE, sh, 0);
    E(w, ZL_STORE, st, 0);
    E(w, ZL_LOAD, sa, 0);
    E(w, ZL_CDR, 0, 0);
    E(w, ZL_STORE, sa, 0);
}

static int emit_helper(lw_t *w, int h)
{
    zl_cc_t *c = w->c;
    int32_t j1, j2, j3, top;
#define HERE ((int64_t) c->p->ncode)
    switch (h) {
    case H_TOBOOL: /* true -> 1, false -> 0, anything else stops */
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_PUSH, ZL_V_TRUE, 0);
        E(w, ZL_EQ, 0, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_PUSH, ZL_V_FALSE, 0);
        E(w, ZL_EQ, 0, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j1, HERE);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j2, HERE);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        break;
    case H_FROMBOOL:
        j1 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_PUSH, ZL_V_TRUE, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j1, HERE);
        E(w, ZL_PUSH, ZL_V_FALSE, 0);
        E(w, ZL_RET, 0, 0);
        break;
    case H_EQL: /* a b -- 1|0: equal numbers, words, or lists of equal items */
        top = (int32_t) HERE;
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_EQ, 0, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j3 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_CAR, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_CAR, 0, 0);
        E(w, ZL_CALL, top, 0);
        {
            int32_t j4 = E(w, ZL_JZ, 0, 0);
            E(w, ZL_CDR, 0, 0);
            E(w, ZL_SWAP, 0, 0);
            E(w, ZL_CDR, 0, 0);
            E(w, ZL_SWAP, 0, 0);
            E(w, ZL_JMP, top, 0);
            zl_patch(c, j1, HERE);
            E(w, ZL_DROP, 0, 0);
            E(w, ZL_DROP, 0, 0);
            E(w, ZL_PUSH, 1, 0);
            E(w, ZL_RET, 0, 0);
            zl_patch(c, j2, HERE);
            zl_patch(c, j3, HERE);
            zl_patch(c, j4, HERE);
        }
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_RET, 0, 0);
        break;
    case H_MEM: /* x list -- 1|0 */
        top = (int32_t) HERE;
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j1 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_CAR, 0, 0);
        if (addfix(w, E(w, ZL_CALL, 0, 0), -1 - H_EQL)) return c->d->status;
        j2 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_CDR, 0, 0);
        E(w, ZL_JMP, top, 0);
        zl_patch(c, j1, HERE);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j3 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j2, HERE);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j3, HERE);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        break;
    case H_CNT: /* list -- n */
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_SWAP, 0, 0);
        top = (int32_t) HERE;
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j1 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_CDR, 0, 0);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_ADD, 0, 0);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_JMP, top, 0);
        zl_patch(c, j1, HERE);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j2, HERE);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        break;
    case H_ITEM: /* n list -- item n (from 1) */
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_LT, 0, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        top = (int32_t) HERE;
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_EQ, 0, 0);
        j3 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_CDR, 0, 0);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_SUB, 0, 0);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_JMP, top, 0);
        zl_patch(c, j3, HERE);
        E(w, ZL_CAR, 0, 0);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j2, HERE);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        zl_patch(c, j1, HERE);
        E(w, ZL_TRAP, ZL_E_BOUNDS, 0);
        zl_patch(c, j2, HERE);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        break;
    case H_LAST: /* list -- its last item */
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j1 = E(w, ZL_JZ, 0, 0);
        top = (int32_t) HERE;
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_CDR, 0, 0);
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_JMP, top, 0);
        zl_patch(c, j2, HERE);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_CAR, 0, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j1, HERE);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        break;
    case H_BL: { /* list -- a copy without its last item */
        int32_t sa = scratch(w, 0), sh = scratch(w, 1);
        E(w, ZL_STORE, sa, 0);
        E(w, ZL_LOAD, sa, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j1 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_LOAD, sa, 0);
        E(w, ZL_CDR, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j2 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_PUSH, ZL_V_NIL, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j2, HERE);
        copy_first(w);
        top = (int32_t) HERE;
        E(w, ZL_LOAD, sa, 0);
        E(w, ZL_CDR, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j3 = E(w, ZL_JZ, 0, 0);
        copy_cell(w);
        E(w, ZL_JMP, top, 0);
        zl_patch(c, j3, HERE);
        E(w, ZL_LOAD, sh, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j1, HERE);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        break;
    }
    case H_APP: { /* a b -- a copy of list a ending in b */
        int32_t sa = scratch(w, 0), sh = scratch(w, 1), st = scratch(w, 2), sb = scratch(w, 3);
        /* sb doubles as the new-cell register of copy_cell, so keep b on the stack */
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_STORE, sa, 0);
        E(w, ZL_LOAD, sa, 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j1 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j1, HERE);
        E(w, ZL_LOAD, sa, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        copy_first(w);
        top = (int32_t) HERE;
        E(w, ZL_LOAD, sa, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j3 = E(w, ZL_JZ, 0, 0);
        copy_cell(w);
        E(w, ZL_JMP, top, 0);
        zl_patch(c, j3, HERE);
        E(w, ZL_LOAD, st, 0);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_SETCDR, 0, 0);
        E(w, ZL_LOAD, sh, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j2, HERE);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        (void) sb;
        break;
    }
    case H_ASLIST: /* x -- x if a list, else [x] */
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j2 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_PUSH, ZL_V_NIL, 0);
        E(w, ZL_CONS, 0, 0);
        zl_patch(c, j1, HERE);
        zl_patch(c, j2, HERE);
        E(w, ZL_RET, 0, 0);
        break;
    case H_SE: /* a b -- sentence */
        if (addfix(w, E(w, ZL_CALL, 0, 0), -1 - H_ASLIST)) return c->d->status;
        E(w, ZL_SWAP, 0, 0);
        if (addfix(w, E(w, ZL_CALL, 0, 0), -1 - H_ASLIST)) return c->d->status;
        E(w, ZL_SWAP, 0, 0);
        if (addfix(w, E(w, ZL_JMP, 0, 0), -1 - H_APP)) return c->d->status;
        break;
    case H_LPUT: /* x list -- list with x at the end */
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_PUSH, ZL_V_NIL, 0);
        E(w, ZL_CONS, 0, 0);
        if (addfix(w, E(w, ZL_JMP, 0, 0), -1 - H_APP)) return c->d->status;
        break;
    case H_MODULO: /* a b -- a modulo b, floored; UCBLogo 6.2 gives b, not 0, for b < 0 */
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_MODF, 0, 0);
        E(w, ZL_DUP, 0, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_LT, 0, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_DUP, 0, 0);
        zl_patch(c, j1, HERE);
        zl_patch(c, j2, HERE);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_RET, 0, 0);
        break;
    default: /* H_FPUT: x list -- (x . list), list must be a list */
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        zl_patch(c, j1, HERE);
        E(w, ZL_CONS, 0, 0);
        E(w, ZL_RET, 0, 0);
        zl_patch(c, j2, HERE);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        break;
    }
#undef HERE
    return c->failed ? c->d->status : ZL_OK;
}

/* Build a literal list at init time (elements last to first). */
static void build_list(lw_t *w, uint32_t lb)
{
    uint32_t rb = (uint32_t) w->tok[lb].v;
    E(w, ZL_PUSH, ZL_V_NIL, 0);
    for (uint32_t t = rb - 1; t > lb; t--) {
        const ltok_t *k = &w->tok[t];
        if (k->kind == T_RB) {
            build_list(w, (uint32_t) k->v);
            t = (uint32_t) k->v;
        } else if (k->kind == T_NUM) {
            E(w, ZL_PUSH, k->v, 0);
        } else {
            int64_t v = 0;
            word_value(w, t, &v);
            E(w, ZL_PUSH, v, 0);
        }
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_CONS, 0, 0);
    }
}

/* Does a top-level call reach a procedure not yet defined? (recursion is
 * bounded by the procedure count: each procedure is visited once) */
static int reach_late(lw_t *w, uint32_t p, uint32_t defs)
{
    if (p >= defs) return 1;
    if (w->seen[p / 64] & (1ull << (p % 64))) return 0;
    w->seen[p / 64] |= 1ull << (p % 64);
    for (uint32_t q = 0; q < w->nproc; q++)
        if ((w->edge[p][q / 64] & (1ull << (q % 64))) && reach_late(w, q, defs)) return 1;
    return 0;
}

int zl_logo_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    lw_t *w = (lw_t *) work;
    int s;
    zl_bzero(w, sizeof *w);
    w->c = c;
    w->tw = tw;
    w->src = src;
    w->len = len;
    w->cur = -1;
    w->rc_slot = w->space_str = w->scratch = -1;
    c->p->word_bits = 62;
    if ((w->unb = zl_addstr(c, "", 0)) < 0) return c->d->status;
    if ((s = lex(w)) || (s = prepass(w))) return s;
    int32_t jinit = E(w, ZL_JMP, 0, 0);
    w->pos = 0;
    while (w->tok[w->pos].kind != T_EOF) {
        uint32_t t = w->pos;
        if (w->tok[t].kind == T_NL) {
            w->pos++;
            continue;
        }
        if (is_word(w, t, "to"))
            s = procedure(w);
        else if (is_word(w, t, "end"))
            s = lfail(w, ZL_E_SYNTAX, "end without to");
        else
            s = line(w, 0);
        if (s) return s;
        if (c->failed) return c->d->status;
    }
    c->line = 0;
    E(w, ZL_HALT, 0, 0);
    /* helpers, with the helpers they use */
    w->helper_used[H_EQL] |= w->helper_used[H_MEM];
    w->helper_used[H_ASLIST] |= w->helper_used[H_SE];
    w->helper_used[H_APP] |= w->helper_used[H_SE] | w->helper_used[H_LPUT];
    for (int h = 0; h < H__COUNT; h++) {
        if (!w->helper_used[h]) continue;
        w->helper_addr[h] = (int32_t) c->p->ncode;
        if ((s = emit_helper(w, h))) return s;
    }
    for (uint32_t k = 0; k < w->nfix; k++) {
        int32_t t = w->fix[k].target;
        zl_patch(c, w->fix[k].at, t >= 0 ? w->proc[t].addr : w->helper_addr[-1 - t]);
    }
    for (uint32_t k = 0; k < w->ntop; k++) {
        zl_bzero(w->seen, sizeof w->seen);
        if (reach_late(w, w->top[k].proc, w->top[k].defs)) {
            c->line = w->top[k].line;
            c->col = 0;
            return zl_fail(c, ZL_E_UNDEFINED,
                           "a procedure is called before its definition has been read");
        }
    }
    /* initialization: every variable has no value, repcount is -1, literal lists */
    zl_patch(c, jinit, (int64_t) c->p->ncode);
    for (uint32_t k = 0; k < w->nname; k++) {
        E(w, ZL_PUSH, UNB(w), 0);
        E(w, ZL_STORE, w->name[k].slot, 0);
    }
    if (w->rc_slot >= 0) {
        E(w, ZL_PUSH, -1, 0);
        E(w, ZL_STORE, w->rc_slot, 0);
    }
    for (uint32_t k = 0; k < w->nlit; k++) {
        build_list(w, w->lit[k].tok);
        E(w, ZL_STORE, w->lit[k].slot, 0);
    }
    E(w, ZL_JMP, 1, 0);
    return c->failed ? c->d->status : ZL_OK;
}
