/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_lisp.c — LISP and Scheme front-end: a core subset of Common Lisp
 * (as sbcl runs it) and of R7RS-small Scheme (as guile runs it), sharing
 * one s-expression reader, with the twins "Coilwyrm" (LISP) and
 * "Spiraldrake" (Scheme). c->p->lang says which dialect. Common Lisp,
 * Racket and Clojure are dialects of the same front-end (zl_commonlisp.c,
 * zl_racket.c and zl_clojure.c only call in here), described at the end
 * of this comment.
 *
 * Programs run with 62-bit words: a value is an integer strictly inside
 * +-2^62 or a reference (the empty list, true, false, a quoted symbol or a
 * pair). Functions get procedure frames; list cells are never reclaimed.
 *
 * Admitted in both: integers, quoted symbols and lists (built with CONS at
 * run time; dotted pairs too), string literals as arguments of output
 * forms only; quote, if, cond, when, unless, and, or, let, let*, do, and
 * the procedures + - * / (exact: an inexact quotient traps), abs min max
 * expt, = < > <= >= (two or more arguments), the sign and parity tests,
 * number, symbol, pair and null tests, eq eql equal, car cdr and c[ad]r
 * up to four letters, cons list length append reverse, member and assoc.
 * Self tail calls (loops written as recursion) run in constant space.
 *
 * LISP (Common Lisp): defun, defvar, defparameter (with a value), setq,
 * setf / incf / decf / push / pop on a variable, progn, dotimes, labels,
 * floor truncate ceiling (one value) mod rem 1+ 1-, zerop plusp minusp
 * evenp oddp numberp integerp symbolp null not consp atom listp, first
 * rest second third, rplaca rplacd, print prin1 princ terpri, and
 * (format t "...") with ~a ~d ~s ~% ~~. The empty list is NIL; car and
 * cdr of NIL are NIL.
 *
 * Scheme: define (procedures, also as (define f (lambda ...)), and
 * variables; inside a body, internal defines first), set!, begin, named
 * let, quotient remainder modulo floor-quotient floor-remainder
 * truncate-quotient truncate-remainder, zero? positive? negative?
 * even? odd? number? integer? symbol? null? pair? not, eq? eqv? equal?,
 * memq memv member assq assv assoc, set-car! set-cdr!, display write
 * newline. A form whose value Scheme leaves unspecified (display, set!, an
 * if without else, when, ...) is refused where that value would be used,
 * including through a procedure that returns it.
 *
 * Refused by name (ZL_E_UNSUPPORTED): floats, ratios and bignums (an
 * integer literal outside +-2^62; arithmetic past it traps OVERFLOW),
 * strings as values, characters, vectors, hash tables, lambda and every
 * use of a procedure as a value (funcall, apply, map, #'), macros,
 * backquote, call/cc and non-local exits, multiple values, &optional /
 * &rest / &key, variadic procedures, binding a defvar'd (special)
 * variable, keywords. Using a function or global before the top-level
 * form that defines it has run is refused (ZL_E_UNDEFINED), as both
 * references would stop there. Non-tail recursion deeper than the call
 * stack (ZL_MAX_CALLS) traps RSTACK where sbcl and guile would go on.
 * Output is not pretty-printed: sbcl breaks a printed list that runs past
 * column 80 onto further lines, and zxlang writes it on one.
 *
 * The twins: one space between elements; a top-level form or comment per
 * line; a comment kept only between top-level forms, as "; text"; body
 * forms (defun, define, lambda, let, let*, named let, labels and each of
 * its functions, do, dotimes, when, unless, progn, begin, cond) keep their
 * first 1-3 elements (the head and what names or binds) on the opening
 * line and put each later element on its own line, indented two columns
 * past the form's opening parenthesis. Every other list stays on one line,
 * unless one of its elements spans lines: then each element after the
 * second goes on its own line in the same way.
 * (quote x) is written 'x; a quoted number, boolean or empty list is
 * written bare where it means itself. Coilwyrm writes symbols upper case,
 * the empty list as NIL (but () for an empty parameter or binding list),
 * a bare let binding X as (X NIL), SETF of a variable as SETQ, and FIRST
 * REST SECOND THIRD as CAR CDR CADR CADDR. Spiraldrake keeps the case of
 * symbols, writes #t and #f, and (define f (lambda (x) ...)) as
 * (define (f x) ...).
 *
 * Common Lisp (twin Braidwyrm) is the LISP subset, the same core run
 * against sbcl, plus (case key (keys form ...) ... [(otherwise form ...)])
 * with number and symbol keys compared by EQL. Braidwyrm writes modern
 * lower case: symbols, nil and t in lower case, SETQ as setf, CAR CDR
 * CADR CADDR as first rest second third, and a T case clause as otherwise.
 *
 * Racket (twin Dialectwyrm) is the Scheme subset as racket runs a module:
 * the first line is #lang racket or #lang racket/base; [ ] are brackets
 * too; if needs an else; add1 sub1 displayln print println void and printf
 * (~a ~s ~v ~n ~% ~~); from #lang racket only, first rest second third
 * (which trap unless given a list), empty? cons? and empty. A module prints
 * each top-level value that is not void as racket's print does ('(1 a),
 * 'sym, #t, 5): void is a symbol no program can name, and only the top
 * level may see it, as Scheme's unspecified values. Pairs are immutable
 * (set-car! and set-cdr! are refused), the floor-quotient family is not in
 * racket, and a top-level begin (which splices) is refused. Dialectwyrm
 * writes ( ) for [ ], '() for empty and null, null? for empty? and pair?
 * for cons?, and keeps the #lang line as written.
 *
 * Clojure (twin Stonewyrm), as "clojure file.clj" runs it: commas are
 * blanks and [ ] read as a vector, admitted only as the bindings of let,
 * loop and dotimes and the parameters of defn. nil, true and false are
 * symbols no quoted datum can name apart from themselves; only nil and
 * false are false, and the empty list is () (not nil). def and defn are
 * top-level only, and a name resolves only to definitions above it, as
 * clojure compiles one form at a time (so a function cannot call a later
 * one). let and loop bind in turn; loop runs as a local function whose
 * recur, admitted only in tail position (of a loop or a defn), jumps back
 * with new values; a named call to oneself is a real call, as in clojure.
 * cond takes flat test/form pairs with :else last; if-not, when-not, do
 * and dotimes are in. Built-ins: + - * / quot rem mod inc dec abs min max
 * (one argument comes back unchecked, as clojure does), = (structural,
 * variadic) not= == < > <= >= (one argument is true), zero? pos? neg?
 * even? odd? number? integer? int? symbol? nil? some? true? false? not
 * seq? empty?, list cons first rest next second count conj reverse on
 * lists and nil (anything else traps DOMAIN where clojure throws), and
 * println print prn pr newline (values separated by spaces; prn and pr
 * quote strings). Refused: vectors, maps, sets and keywords (but :else) as
 * values, characters, regexes, #(...), deref, metadata, syntax-quote, fn
 * and functions as values (map, filter, reduce, apply), docstrings,
 * multi-arity, variadic and destructuring parameters, redefinition, atoms,
 * set!, try/throw, lazy sequences (concat, range, for, doseq), strings as
 * values (str, format), interop, namespaces and octal literals. A long
 * past +-2^62 traps OVERFLOW where clojure goes on to 2^63, and / traps
 * INEXACT where clojure makes a ratio. Stonewyrm writes no commas, one
 * semicolon per comment, [ ] vectors on one line, and each cond pair on a
 * line of its own. */
#include "zl_int.h"

#define L_NODES  4096
#define L_TOPS   2048
#define L_FNS    256
#define L_GLOB   256
#define L_SCOPE  512
#define L_FIX    2048
#define L_OBS    1024
#define L_EDGE   1024
#define L_LOCALS 2048
#define L_PARAMS 64
#define L_STRBUF 1024
#define L_NAME   64

/* Dialects, as bits: which of them a table entry belongs to. D_RFULL marks
 * a Racket name that only "#lang racket" provides (racket/list), not
 * "#lang racket/base". */
#define D_LISP  1
#define D_SCM   2
#define D_CL    4
#define D_RKT   8
#define D_CLJ   16
#define D_RFULL 32
#define DL      (D_LISP | D_CL)
#define DS      (D_SCM | D_RKT)
#define DA      (DL | DS)
#define CLJ(w)  ((w)->dial == D_CLJ)

enum { K_NUM = 1, K_SYM, K_STR, K_LIST, K_QUOTE, K_TRUE, K_FALSE, K_COMMENT, K_VEC };
/* node flags; the compiler sets all but F_DOT for the twin printer */
enum {
    F_DOT = 1,     /* the last child is the tail of a dotted list */
    F_BLOCK = 2,   /* a body form: elements past k go on their own lines */
    F_LAMDEF = 4,  /* (define f (lambda ...)): written (define (f ...) ...) */
    F_ALIAS = 8,   /* a synonym: written as its canonical name */
    F_BARE = 16,   /* a LISP binding without a value: written (X NIL) */
    F_UNQ = 32,    /* a quoted datum that means itself: written bare */
    F_SYNTAX = 64, /* an empty parameter or binding list: written () */
    F_PAIRS = 128, /* Clojure cond: each test and its form on a line of their own */
};
enum { X_EFFECT, X_TEST, X_VALUE, X_RESULT };
enum { B_VAR = 1, B_FN };
enum { T_FORM = 0, T_FN, T_VAR };

typedef struct {
    int64_t v; /* number; symbol, string, comment: source offset; define: its function */
    int32_t kid, next;
    uint32_t line, col;
    uint16_t len;
    uint8_t kind, flags, k, alias;
    uint16_t pad;
} lnode_t;

typedef struct {
    int32_t name, params, body, at; /* nodes; params: the first parameter */
    int32_t entry;                  /* address of its ENTER, -1 until emitted */
    int32_t top;                    /* defining top-level form, -1 when local */
    uint16_t nparams;
    uint8_t level, pkind; /* level of its frame; params are names (0), bindings (1)
                             or a Clojure vector of names and values (2) */
    uint8_t unspec;
} lfn_t;

typedef struct {
    int32_t name, top, slot;
} lglob_t;

typedef struct {
    int32_t name, v; /* B_VAR: frame offset; B_FN: function */
    uint8_t kind, level;
} lbind_t;

typedef struct {
    int32_t at, fid; /* fid < 0: helper -fid-1 */
} lfix_t;

typedef struct {
    int32_t fid;
    uint32_t line, col;
} lobs_t;

typedef struct {
    int32_t from, to;
} ledge_t;

enum {
    H_LENGTH,
    H_REVERSE,
    H_APPEND,
    H_EQUAL,
    H_MEMQ,
    H_MEMBER,
    H_ASSQ,
    H_ASSOC,
    H_RPRINT,
    H_RTOP,
    H_SEQ,
    H__COUNT
};

typedef struct {
    /* zeroed for every compile */
    zl_cc_t *c;
    zl_out_t *tw;
    const char *src;
    size_t len, pos, lstart;
    uint32_t line;
    int lisp;       /* LISP or Common Lisp: NIL is false and the empty list */
    int dial;       /* D_LISP, D_SCM, D_CL, D_RKT or D_CLJ */
    int rbase;      /* Racket: #lang racket/base (else #lang racket) */
    size_t langlen; /* Racket: the length of the #lang line's name */
    int64_t F;      /* the false value: NIL or #f */
    int64_t V;      /* what an unspecified value pushes: #f, or Racket's void */
    int64_t T;      /* the true value: T, #t or Clojure's true */
    int64_t NIL;    /* what nil is: the empty list, or Clojure's nil */
    int32_t curtop; /* Clojure: the top-level form being compiled (names resolve
                       only to definitions before it) */
    uint32_t nnodes, ntops, nforms, nfns, nglob, nscope, nfix, nobs, nedge, nscr;
    int32_t helper[H__COUNT];
    uint8_t hused[H__COUNT];
    int32_t scratch;
    struct {
        int32_t fid;
        uint32_t level, nloc, maxloc;
    } cur;
    uint32_t depth;
    size_t tcol;
    /* tables: always written before they are read */
    lnode_t nodes[L_NODES];
    int32_t tops[L_TOPS]; /* top-level forms and comments, in order */
    int32_t form[L_TOPS]; /* top-level forms */
    uint8_t fkind[L_TOPS];
    int32_t fref[L_TOPS];
    lfn_t fns[L_FNS];
    lglob_t glob[L_GLOB];
    lbind_t scope[L_SCOPE];
    lfix_t fix[L_FIX];
    lobs_t obs[L_OBS];
    ledge_t edge[L_EDGE];
    int32_t scr[L_NODES];
    uint32_t fcalls[L_FNS][L_FNS / 32], frefs[L_FNS][L_GLOB / 32];
    char sbuf[L_STRBUF];         /* a decoded string */
    char fbuf[L_STRBUF];         /* a format control */
    char qbuf[2 * L_STRBUF + 2]; /* a quoted string */
    size_t qlen;
} lw_t;

ZL_STATIC_ASSERT(sizeof(lw_t) <= sizeof(((zl_work_t *) 0)->fe), lw_fits);

/* ------------------------------------------------------------ tables */

/* Special forms. */
enum {
    S_QUOTE = 1,
    S_IF,
    S_COND,
    S_WHEN,
    S_UNLESS,
    S_AND,
    S_OR,
    S_BEGIN,
    S_LET,
    S_LETSTAR,
    S_SETQ,
    S_SET,
    S_DOTIMES,
    S_DO,
    S_LABELS,
    S_DEFUN,
    S_DEFVAR,
    S_DEFINE,
    S_LAMBDA,
    S_INCF,
    S_DECF,
    S_PUSH,
    S_POP,
    S_CASE,
    S_LOOP,
    S_RECUR,
    S_IFNOT
};

typedef struct {
    const char *name; /* lower case */
    uint8_t dial;     /* D_* bits */
    uint8_t code;
    const char *canon; /* the twin's spelling of a synonym */
} lspec_t;

static const lspec_t specs[] = {
    {"quote", DA | D_CLJ, S_QUOTE, 0},
    {"if", DA | D_CLJ, S_IF, 0},
    {"cond", DA | D_CLJ, S_COND, 0},
    {"when", DA | D_CLJ, S_WHEN, 0},
    {"unless", DA, S_UNLESS, 0},
    {"and", DA | D_CLJ, S_AND, 0},
    {"or", DA | D_CLJ, S_OR, 0},
    {"progn", DL, S_BEGIN, 0},
    {"begin", DS, S_BEGIN, 0},
    {"let", DA | D_CLJ, S_LET, 0},
    {"let*", DA, S_LETSTAR, 0},
    {"setq", DL, S_SETQ, 0},
    {"setf", DL, S_SETQ, "setq"},
    {"set!", DS, S_SET, 0},
    {"dotimes", DL | D_CLJ, S_DOTIMES, 0},
    {"do", DA, S_DO, 0},
    {"labels", DL, S_LABELS, 0},
    {"defun", DL, S_DEFUN, 0},
    {"defvar", DL, S_DEFVAR, 0},
    {"defparameter", DL, S_DEFVAR, 0},
    {"define", DS, S_DEFINE, 0},
    {"lambda", DA, S_LAMBDA, 0},
    {"case", D_CL, S_CASE, 0},
    {"incf", DL, S_INCF, 0},
    {"decf", DL, S_DECF, 0},
    {"push", DL, S_PUSH, 0},
    {"pop", DL, S_POP, 0},
    {"do", D_CLJ, S_BEGIN, 0},
    {"loop", D_CLJ, S_LOOP, 0},
    {"recur", D_CLJ, S_RECUR, 0},
    {"def", D_CLJ, S_DEFVAR, 0},
    {"defn", D_CLJ, S_DEFUN, 0},
    {"fn", D_CLJ, S_LAMBDA, 0},
    {"when-not", D_CLJ, S_UNLESS, 0},
    {"if-not", D_CLJ, S_IFNOT, 0},
};

/* Built-in procedures. */
enum {
    B_ADD = 1,
    B_SUB,
    B_MUL,
    B_DIV,
    B_ABS,
    B_MIN,
    B_MAX,
    B_INC,
    B_DEC,
    B_FLOOR,
    B_TRUNC,
    B_CEIL,
    B_MODF,
    B_MODT,
    B_EXPT,
    B_NUMEQ,
    B_NUMNE,
    B_LT,
    B_GT,
    B_LE,
    B_GE,
    B_ZEROP,
    B_PLUSP,
    B_MINUSP,
    B_EVENP,
    B_ODDP,
    B_NUMBERP,
    B_SYMBOLP,
    B_NULL,
    B_NOT,
    B_CONSP,
    B_ATOM,
    B_LISTP,
    B_EQ,
    B_EQUAL,
    B_CXR,
    B_CONS,
    B_LIST,
    B_LENGTH,
    B_APPEND,
    B_REVERSE,
    B_MEMQ,
    B_MEMBER,
    B_ASSQ,
    B_ASSOC,
    B_SETCAR,
    B_SETCDR,
    B_PRINT,
    B_PRIN1,
    B_PRINC,
    B_TERPRI,
    B_FORMAT,
    B_DISPLAY,
    B_WRITE,
    B_NEWLINE,
    B_DISPLAYLN,
    B_PRINTF,
    B_RPRINT,
    B_RPRINTLN,
    B_VOID,
    B_RLIST,
    B_CEQ,
    B_CNE,
    B_CSYMP,
    B_NILP,
    B_SOMEP,
    B_TRUEP,
    B_FALSEP,
    B_CEMPTY,
    B_CCONS,
    B_CFIRST,
    B_CREST,
    B_CNEXT,
    B_CSECOND,
    B_CCOUNT,
    B_CONJ,
    B_CREVERSE,
    B_CPRINT
};

typedef struct {
    const char *name; /* lower case */
    uint8_t dial, op;
    int8_t min, max; /* argument count; max -1: any */
    const char *canon;
} lbi_t;

static const lbi_t bis[] = {
    {"+", DA, B_ADD, 0, -1, 0},
    {"-", DA, B_SUB, 1, -1, 0},
    {"*", DA, B_MUL, 0, -1, 0},
    {"/", DA, B_DIV, 1, -1, 0},
    {"abs", DA, B_ABS, 1, 1, 0},
    {"min", DA, B_MIN, 1, -1, 0},
    {"max", DA, B_MAX, 1, -1, 0},
    {"1+", DL, B_INC, 1, 1, 0},
    {"1-", DL, B_DEC, 1, 1, 0},
    {"floor", DL, B_FLOOR, 1, 2, 0},
    {"truncate", DL, B_TRUNC, 1, 2, 0},
    {"ceiling", DL, B_CEIL, 1, 2, 0},
    {"mod", DL, B_MODF, 2, 2, 0},
    {"rem", DL, B_MODT, 2, 2, 0},
    {"quotient", DS, B_TRUNC, 2, 2, 0},
    {"remainder", DS, B_MODT, 2, 2, 0},
    {"modulo", DS, B_MODF, 2, 2, 0},
    {"floor-quotient", D_SCM, B_FLOOR, 2, 2, 0},
    {"floor-remainder", D_SCM, B_MODF, 2, 2, 0},
    {"truncate-quotient", D_SCM, B_TRUNC, 2, 2, 0},
    {"truncate-remainder", D_SCM, B_MODT, 2, 2, 0},
    {"expt", DA, B_EXPT, 2, 2, 0},
    {"=", DA, B_NUMEQ, 2, -1, 0},
    {"/=", DL, B_NUMNE, 2, 2, 0},
    {"<", DA, B_LT, 2, -1, 0},
    {">", DA, B_GT, 2, -1, 0},
    {"<=", DA, B_LE, 2, -1, 0},
    {">=", DA, B_GE, 2, -1, 0},
    {"zerop", DL, B_ZEROP, 1, 1, 0},
    {"zero?", DS, B_ZEROP, 1, 1, 0},
    {"plusp", DL, B_PLUSP, 1, 1, 0},
    {"positive?", DS, B_PLUSP, 1, 1, 0},
    {"minusp", DL, B_MINUSP, 1, 1, 0},
    {"negative?", DS, B_MINUSP, 1, 1, 0},
    {"evenp", DL, B_EVENP, 1, 1, 0},
    {"even?", DS, B_EVENP, 1, 1, 0},
    {"oddp", DL, B_ODDP, 1, 1, 0},
    {"odd?", DS, B_ODDP, 1, 1, 0},
    {"numberp", DL, B_NUMBERP, 1, 1, 0},
    {"integerp", DL, B_NUMBERP, 1, 1, 0},
    {"number?", DS, B_NUMBERP, 1, 1, 0},
    {"integer?", DS, B_NUMBERP, 1, 1, 0},
    {"symbolp", DL, B_SYMBOLP, 1, 1, 0},
    {"symbol?", DS, B_SYMBOLP, 1, 1, 0},
    {"null", DL, B_NULL, 1, 1, 0},
    {"not", DL, B_NULL, 1, 1, 0},
    {"null?", DS, B_NULL, 1, 1, 0},
    {"not", DS, B_NOT, 1, 1, 0},
    {"consp", DL, B_CONSP, 1, 1, 0},
    {"pair?", DS, B_CONSP, 1, 1, 0},
    {"atom", DL, B_ATOM, 1, 1, 0},
    {"listp", DL, B_LISTP, 1, 1, 0},
    {"eq", DL, B_EQ, 2, 2, 0},
    {"eql", DL, B_EQ, 2, 2, 0},
    {"eq?", DS, B_EQ, 2, 2, 0},
    {"eqv?", DS, B_EQ, 2, 2, 0},
    {"equal", DL, B_EQUAL, 2, 2, 0},
    {"equal?", DS, B_EQUAL, 2, 2, 0},
    {"cons", DA, B_CONS, 2, 2, 0},
    {"list", DA, B_LIST, 0, -1, 0},
    {"length", DA, B_LENGTH, 1, 1, 0},
    {"append", DA, B_APPEND, 0, -1, 0},
    {"reverse", DA, B_REVERSE, 1, 1, 0},
    {"first", DL, B_CXR, 1, 1, "car"},
    {"rest", DL, B_CXR, 1, 1, "cdr"},
    {"second", DL, B_CXR, 1, 1, "cadr"},
    {"third", DL, B_CXR, 1, 1, "caddr"},
    {"member", DL, B_MEMQ, 2, 2, 0},
    {"assoc", DL, B_ASSQ, 2, 2, 0},
    {"memq", DS, B_MEMQ, 2, 2, 0},
    {"memv", DS, B_MEMQ, 2, 2, 0},
    {"member", DS, B_MEMBER, 2, 2, 0},
    {"assq", DS, B_ASSQ, 2, 2, 0},
    {"assv", DS, B_ASSQ, 2, 2, 0},
    {"assoc", DS, B_ASSOC, 2, 2, 0},
    {"rplaca", DL, B_SETCAR, 2, 2, 0},
    {"rplacd", DL, B_SETCDR, 2, 2, 0},
    {"set-car!", D_SCM, B_SETCAR, 2, 2, 0},
    {"set-cdr!", D_SCM, B_SETCDR, 2, 2, 0},
    {"print", DL, B_PRINT, 1, 1, 0},
    {"prin1", DL, B_PRIN1, 1, 1, 0},
    {"princ", DL, B_PRINC, 1, 1, 0},
    {"terpri", DL, B_TERPRI, 0, 0, 0},
    {"format", DL, B_FORMAT, 2, -1, 0},
    {"display", DS, B_DISPLAY, 1, 1, 0},
    {"write", DS, B_WRITE, 1, 1, 0},
    {"newline", DS, B_NEWLINE, 0, 0, 0},
    {"add1", D_RKT, B_INC, 1, 1, 0},
    {"sub1", D_RKT, B_DEC, 1, 1, 0},
    {"displayln", D_RKT, B_DISPLAYLN, 1, 1, 0},
    {"printf", D_RKT, B_PRINTF, 1, -1, 0},
    {"print", D_RKT, B_RPRINT, 1, 1, 0},
    {"println", D_RKT, B_RPRINTLN, 1, 1, 0},
    {"void", D_RKT, B_VOID, 0, 0, 0},
    {"first", D_RKT | D_RFULL, B_RLIST, 1, 1, 0},
    {"rest", D_RKT | D_RFULL, B_RLIST, 1, 1, 0},
    {"second", D_RKT | D_RFULL, B_RLIST, 1, 1, 0},
    {"third", D_RKT | D_RFULL, B_RLIST, 1, 1, 0},
    {"empty?", D_RKT | D_RFULL, B_NULL, 1, 1, "null?"},
    {"cons?", D_RKT | D_RFULL, B_CONSP, 1, 1, "pair?"},
    {"+", D_CLJ, B_ADD, 0, -1, 0},
    {"-", D_CLJ, B_SUB, 1, -1, 0},
    {"*", D_CLJ, B_MUL, 0, -1, 0},
    {"/", D_CLJ, B_DIV, 1, -1, 0},
    {"abs", D_CLJ, B_ABS, 1, 1, 0},
    {"min", D_CLJ, B_MIN, 1, -1, 0},
    {"max", D_CLJ, B_MAX, 1, -1, 0},
    {"inc", D_CLJ, B_INC, 1, 1, 0},
    {"dec", D_CLJ, B_DEC, 1, 1, 0},
    {"quot", D_CLJ, B_TRUNC, 2, 2, 0},
    {"rem", D_CLJ, B_MODT, 2, 2, 0},
    {"mod", D_CLJ, B_MODF, 2, 2, 0},
    {"=", D_CLJ, B_CEQ, 1, -1, 0},
    {"not=", D_CLJ, B_CNE, 1, -1, 0},
    {"==", D_CLJ, B_NUMEQ, 1, -1, 0},
    {"<", D_CLJ, B_LT, 1, -1, 0},
    {">", D_CLJ, B_GT, 1, -1, 0},
    {"<=", D_CLJ, B_LE, 1, -1, 0},
    {">=", D_CLJ, B_GE, 1, -1, 0},
    {"zero?", D_CLJ, B_ZEROP, 1, 1, 0},
    {"pos?", D_CLJ, B_PLUSP, 1, 1, 0},
    {"neg?", D_CLJ, B_MINUSP, 1, 1, 0},
    {"even?", D_CLJ, B_EVENP, 1, 1, 0},
    {"odd?", D_CLJ, B_ODDP, 1, 1, 0},
    {"number?", D_CLJ, B_NUMBERP, 1, 1, 0},
    {"integer?", D_CLJ, B_NUMBERP, 1, 1, 0},
    {"int?", D_CLJ, B_NUMBERP, 1, 1, 0},
    {"symbol?", D_CLJ, B_CSYMP, 1, 1, 0},
    {"nil?", D_CLJ, B_NILP, 1, 1, 0},
    {"some?", D_CLJ, B_SOMEP, 1, 1, 0},
    {"true?", D_CLJ, B_TRUEP, 1, 1, 0},
    {"false?", D_CLJ, B_FALSEP, 1, 1, 0},
    {"not", D_CLJ, B_NOT, 1, 1, 0},
    {"seq?", D_CLJ, B_LISTP, 1, 1, 0},
    {"empty?", D_CLJ, B_CEMPTY, 1, 1, 0},
    {"cons", D_CLJ, B_CCONS, 2, 2, 0},
    {"list", D_CLJ, B_LIST, 0, -1, 0},
    {"first", D_CLJ, B_CFIRST, 1, 1, 0},
    {"rest", D_CLJ, B_CREST, 1, 1, 0},
    {"next", D_CLJ, B_CNEXT, 1, 1, 0},
    {"second", D_CLJ, B_CSECOND, 1, 1, 0},
    {"count", D_CLJ, B_CCOUNT, 1, 1, 0},
    {"conj", D_CLJ, B_CONJ, 1, -1, 0},
    {"reverse", D_CLJ, B_CREVERSE, 1, 1, 0},
    {"println", D_CLJ, B_CPRINT, 0, -1, 0},
    {"print", D_CLJ, B_CPRINT, 0, -1, 0},
    {"prn", D_CLJ, B_CPRINT, 0, -1, 0},
    {"pr", D_CLJ, B_CPRINT, 0, -1, 0},
    {"newline", D_CLJ, B_CPRINT, 0, 0, 0},
};

/* Names refused by what they are. */
static const char *const why[] = {
    "procedures are not values in this subset (lambda, funcall, apply, mapping)",
    "macros are not in this subset",
    "continuations and non-local exits are not in this subset",
    "floating point is not in the integer subset",
    "strings are admitted only as literals passed to output",
    "vectors, arrays and hash tables are not in this subset",
    "multiple values are not in this subset",
    "this form is not in the subset",
    "pairs are immutable in Racket (mcons is not in this subset)",
    "mutable references (atoms, refs, set!) are not in this subset",
};
enum { R_PROC, R_MACRO, R_CONT, R_FLOAT, R_STRING, R_VECTOR, R_VALUES, R_OTHER, R_IMMUT, R_REF };

static const struct {
    const char *name;
    uint8_t dial, why;
} refused[] = {
    {"function", DL, R_PROC},
    {"funcall", DL, R_PROC},
    {"apply", DA, R_PROC},
    {"mapcar", DL, R_PROC},
    {"mapc", DL, R_PROC},
    {"maplist", DL, R_PROC},
    {"mapcan", DL, R_PROC},
    {"reduce", DL, R_PROC},
    {"remove-if", DL, R_PROC},
    {"sort", DL, R_PROC},
    {"flet", DL, R_PROC},
    {"map", DS, R_PROC},
    {"for-each", DS, R_PROC},
    {"defmacro", DL, R_MACRO},
    {"macrolet", DL, R_MACRO},
    {"loop", DL, R_MACRO},
    {"define-syntax", DS, R_MACRO},
    {"let-syntax", DS, R_MACRO},
    {"syntax-rules", DS, R_MACRO},
    {"block", DL, R_CONT},
    {"return", DL, R_CONT},
    {"return-from", DL, R_CONT},
    {"tagbody", DL, R_CONT},
    {"go", DL, R_CONT},
    {"catch", DL, R_CONT},
    {"throw", DL, R_CONT},
    {"unwind-protect", DL, R_CONT},
    {"error", DA, R_CONT},
    {"call/cc", DS, R_CONT},
    {"call-with-current-continuation", DS, R_CONT},
    {"dynamic-wind", DS, R_CONT},
    {"raise", DS, R_CONT},
    {"exit", DS, R_CONT},
    {"sqrt", DA, R_FLOAT},
    {"exp", DA, R_FLOAT},
    {"log", DA, R_FLOAT},
    {"sin", DA, R_FLOAT},
    {"cos", DA, R_FLOAT},
    {"float", DL, R_FLOAT},
    {"round", DA, R_FLOAT},
    {"exact->inexact", DS, R_FLOAT},
    {"inexact", DS, R_FLOAT},
    {"string", DA, R_STRING},
    {"concatenate", DL, R_STRING},
    {"string=", DL, R_STRING},
    {"make-string", DA, R_STRING},
    {"string-append", DS, R_STRING},
    {"string-length", DS, R_STRING},
    {"number->string", DS, R_STRING},
    {"symbol->string", DS, R_STRING},
    {"write-line", DL, R_STRING},
    {"write-string", DA, R_STRING},
    {"make-array", DL, R_VECTOR},
    {"aref", DL, R_VECTOR},
    {"vector", DA, R_VECTOR},
    {"make-vector", DS, R_VECTOR},
    {"vector-ref", DS, R_VECTOR},
    {"make-hash-table", DA, R_VECTOR},
    {"gethash", DL, R_VECTOR},
    {"values", DA, R_VALUES},
    {"multiple-value-bind", DL, R_VALUES},
    {"call-with-values", DS, R_VALUES},
    {"let-values", DS, R_VALUES},
    {"defconstant", DL, R_OTHER},
    {"defstruct", DL, R_OTHER},
    {"defclass", DL, R_OTHER},
    {"defmethod", DL, R_OTHER},
    {"case", D_LISP | DS, R_OTHER},
    {"typecase", DL, R_OTHER},
    {"prog1", DL, R_OTHER},
    {"eval", DA, R_OTHER},
    {"read", DA, R_OTHER},
    {"random", DA, R_OTHER},
    {"declare", DL, R_OTHER},
    {"write", DL, R_OTHER},
    {"fresh-line", DL, R_OTHER},
    {"nth", DL, R_OTHER},
    {"last", DL, R_OTHER},
    {"letrec", DS, R_OTHER},
    {"letrec*", DS, R_OTHER},
    {"list-ref", DS, R_OTHER},
    {"list-tail", DS, R_OTHER},
    {"delay", DS, R_OTHER},
    {"force", DS, R_OTHER},
    {"define-record-type", DS, R_OTHER},
    {"set-car!", D_RKT, R_IMMUT},
    {"set-cdr!", D_RKT, R_IMMUT},
    {"mcons", D_RKT, R_IMMUT},
    {"set-mcar!", D_RKT, R_IMMUT},
    {"set-mcdr!", D_RKT, R_IMMUT},
    {"for", D_RKT, R_OTHER},
    {"for/list", D_RKT, R_OTHER},
    {"for/fold", D_RKT, R_OTHER},
    {"for/sum", D_RKT, R_OTHER},
    {"match", D_RKT, R_MACRO},
    {"struct", D_RKT, R_OTHER},
    {"define-values", D_RKT, R_VALUES},
    {"hash", D_RKT, R_VECTOR},
    {"make-hash", D_RKT, R_VECTOR},
    {"format", D_RKT, R_STRING},
    {"foldl", D_RKT, R_PROC},
    {"foldr", D_RKT, R_PROC},
    {"filter", D_RKT, R_PROC},
    {"build-list", D_RKT, R_PROC},
    {"range", D_RKT, R_OTHER},
    {"floor", D_RKT, R_FLOAT},
    {"ceiling", D_RKT, R_FLOAT},
    {"truncate", D_RKT, R_FLOAT},
    {"map", D_CLJ, R_PROC},
    {"filter", D_CLJ, R_PROC},
    {"reduce", D_CLJ, R_PROC},
    {"apply", D_CLJ, R_PROC},
    {"partial", D_CLJ, R_PROC},
    {"comp", D_CLJ, R_PROC},
    {"for", D_CLJ, R_PROC},
    {"doseq", D_CLJ, R_OTHER},
    {"letfn", D_CLJ, R_PROC},
    {"defmacro", D_CLJ, R_MACRO},
    {"->", D_CLJ, R_MACRO},
    {"->>", D_CLJ, R_MACRO},
    {"declare", D_CLJ, R_OTHER},
    {"case", D_CLJ, R_OTHER},
    {"condp", D_CLJ, R_OTHER},
    {"while", D_CLJ, R_OTHER},
    {"atom", D_CLJ, R_REF},
    {"swap!", D_CLJ, R_REF},
    {"reset!", D_CLJ, R_REF},
    {"deref", D_CLJ, R_REF},
    {"set!", D_CLJ, R_REF},
    {"ref", D_CLJ, R_REF},
    {"try", D_CLJ, R_CONT},
    {"throw", D_CLJ, R_CONT},
    {"str", D_CLJ, R_STRING},
    {"format", D_CLJ, R_STRING},
    {"printf", D_CLJ, R_STRING},
    {"subs", D_CLJ, R_STRING},
    {"vector", D_CLJ, R_VECTOR},
    {"vec", D_CLJ, R_VECTOR},
    {"hash-map", D_CLJ, R_VECTOR},
    {"assoc", D_CLJ, R_VECTOR},
    {"get", D_CLJ, R_VECTOR},
    {"nth", D_CLJ, R_OTHER},
    {"last", D_CLJ, R_OTHER},
    {"range", D_CLJ, R_OTHER},
    {"seq", D_CLJ, R_OTHER},
    {"list?", D_CLJ, R_OTHER},
    {"double", D_CLJ, R_FLOAT},
    {"float", D_CLJ, R_FLOAT},
    {"rand", D_CLJ, R_OTHER},
    {"defrecord", D_CLJ, R_OTHER},
    {"ns", D_CLJ, R_OTHER},
    {"comment", D_CLJ, R_OTHER},
    {"values", D_CLJ, R_VALUES},
    {"concat", D_CLJ, R_OTHER},
    {"fn?", D_CLJ, R_PROC},
    {"identity", D_CLJ, R_PROC},
    {"println-str", D_CLJ, R_STRING},
    {"keyword", D_CLJ, R_OTHER},
    {"lazy-seq", D_CLJ, R_OTHER},
    {"if-let", D_CLJ, R_OTHER},
    {"when-let", D_CLJ, R_OTHER},
    {"defonce", D_CLJ, R_OTHER},
    {"binding", D_CLJ, R_OTHER},
    {"let*", D_CLJ, R_OTHER},
    {"loop*", D_CLJ, R_OTHER},
    {"fn*", D_CLJ, R_PROC},
    {"var", D_CLJ, R_OTHER},
    {"new", D_CLJ, R_OTHER},
    {"assert", D_CLJ, R_OTHER},
};

/* ------------------------------------------------------------ helpers */

#define TRY(x)                                                                                     \
    do {                                                                                           \
        int s_ = (x);                                                                              \
        if (s_) return s_;                                                                         \
    } while (0)

static lnode_t *N(lw_t *w, int32_t n)
{
    return &w->nodes[n];
}

static int ok(lw_t *w)
{
    return w->c->failed ? w->c->d->status : ZL_OK;
}

static int lfail(lw_t *w, int32_t n, int st, const char *msg)
{
    if (n >= 0) {
        w->c->line = N(w, n)->line;
        w->c->col = N(w, n)->col;
    }
    return zl_fail(w->c, st, msg);
}

static int32_t E(lw_t *w, int op, int64_t a, int32_t b)
{
    return zl_emit(w->c, op, a, b);
}

static int32_t here(lw_t *w)
{
    return (int32_t) w->c->p->ncode;
}

static void patch_here(lw_t *w, int32_t at)
{
    zl_patch(w->c, at, here(w));
}

static int32_t nx(lw_t *w, int32_t n)
{
    return n < 0 ? -1 : N(w, n)->next;
}

static uint32_t count(lw_t *w, int32_t first)
{
    uint32_t k = 0;
    for (; first >= 0; first = N(w, first)->next) k++;
    return k;
}

static int lc(lw_t *w, int ch)
{
    return (w->lisp && ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
}

/* Is node n the symbol s (lower case; LISP ignores case)? */
static int sym_is(lw_t *w, int32_t n, const char *s)
{
    if (n < 0 || N(w, n)->kind != K_SYM) return 0;
    const char *p = w->src + N(w, n)->v;
    size_t i = 0;
    for (; i < N(w, n)->len; i++)
        if (!s[i] || lc(w, (unsigned char) p[i]) != s[i]) return 0;
    return s[i] == 0;
}

static int name_eq(lw_t *w, int32_t a, int32_t b)
{
    if (N(w, a)->len != N(w, b)->len) return 0;
    const char *p = w->src + N(w, a)->v, *q = w->src + N(w, b)->v;
    for (size_t i = 0; i < N(w, a)->len; i++)
        if (lc(w, (unsigned char) p[i]) != lc(w, (unsigned char) q[i])) return 0;
    return 1;
}

static int dial_ok(lw_t *w, int dial)
{
    return (dial & w->dial) != 0 && !((dial & D_RFULL) && w->rbase);
}

static int find_spec(lw_t *w, int32_t n)
{
    for (size_t i = 0; i < sizeof specs / sizeof specs[0]; i++)
        if (dial_ok(w, specs[i].dial) && sym_is(w, n, specs[i].name)) return (int) i;
    return -1;
}

/* c[ad]{1,4}r: the path letters, or 0. */
static int cxr_len(const char *p, size_t n, int (*low)(lw_t *, int), lw_t *w)
{
    if (n < 3 || n > 6 || low(w, (unsigned char) p[0]) != 'c' ||
        low(w, (unsigned char) p[n - 1]) != 'r')
        return 0;
    for (size_t i = 1; i + 1 < n; i++) {
        int ch = low(w, (unsigned char) p[i]);
        if (ch != 'a' && ch != 'd') return 0;
    }
    return (int) n - 2;
}

static int find_bi(lw_t *w, int32_t n)
{
    for (size_t i = 0; i < sizeof bis / sizeof bis[0]; i++)
        if (dial_ok(w, bis[i].dial) && sym_is(w, n, bis[i].name)) return (int) i;
    return -1;
}

static int is_cxr(lw_t *w, int32_t n)
{
    return !CLJ(w) && N(w, n)->kind == K_SYM &&
           cxr_len(w->src + N(w, n)->v, N(w, n)->len, lc, w) > 0;
}

static int find_refused(lw_t *w, int32_t n)
{
    for (size_t i = 0; i < sizeof refused / sizeof refused[0]; i++)
        if (dial_ok(w, refused[i].dial) && sym_is(w, n, refused[i].name)) return (int) i;
    return -1;
}

/* A name the language itself owns (LISP: a standard function or macro). */
static int is_standard(lw_t *w, int32_t n)
{
    return find_spec(w, n) >= 0 || find_bi(w, n) >= 0 || is_cxr(w, n) || find_refused(w, n) >= 0;
}

static int is_const(lw_t *w, int32_t n)
{
    if (CLJ(w)) return sym_is(w, n, "nil") || sym_is(w, n, "true") || sym_is(w, n, "false");
    return w->lisp && (sym_is(w, n, "nil") || sym_is(w, n, "t"));
}

static const char *const_msg(lw_t *w)
{
    return CLJ(w) ? "nil, true and false are constants, not names"
                  : "NIL and T are constants, not variables";
}

/* Clojure: the value of nil, true or false (n is_const), else 0. */
static int64_t clj_const(lw_t *w, int32_t n)
{
    if (!CLJ(w) || N(w, n)->kind != K_SYM) return 0;
    return sym_is(w, n, "nil")     ? w->NIL
           : sym_is(w, n, "true")  ? w->T
           : sym_is(w, n, "false") ? w->F
                                   : 0;
}

/* Params of a function: pkind 2 (a Clojure loop) has a value after each name. */
static int32_t pnext(lw_t *w, int32_t p, int pkind)
{
    return pkind == 2 ? nx(w, nx(w, p)) : nx(w, p);
}

/* LISP empty list: () or NIL. */
static int is_empty(lw_t *w, int32_t n)
{
    return (N(w, n)->kind == K_LIST && N(w, n)->kid < 0) || (w->lisp && sym_is(w, n, "nil"));
}

/* ------------------------------------------------------------ reader */

static int rfail(lw_t *w, int st, const char *msg)
{
    w->c->line = w->line;
    w->c->col = (uint32_t) (w->pos - w->lstart) + 1;
    return zl_fail(w->c, st, msg);
}

static int32_t newnode(lw_t *w, int kind)
{
    if (w->nnodes >= L_NODES) {
        rfail(w, ZL_E_LIMIT, "program too large for the LISP reader");
        return -1;
    }
    lnode_t *n = &w->nodes[w->nnodes];
    zl_bzero(n, sizeof *n);
    n->kind = (uint8_t) kind;
    n->kid = n->next = -1;
    n->line = w->line;
    n->col = (uint32_t) (w->pos - w->lstart) + 1;
    return (int32_t) w->nnodes++;
}

static int delim(lw_t *w, int ch)
{
    if (CLJ(w)) /* ' and # may appear inside a Clojure symbol */
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',' || ch == '(' ||
               ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '"' ||
               ch == ';' || ch == '`' || ch == '~' || ch == '@' || ch == '^' || ch == '\\';
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '(' || ch == ')' ||
           ch == '"' || ch == ';' || ch == '\'' || ch == '`' || ch == ',' ||
           (w->dial == D_RKT && (ch == '[' || ch == ']' || ch == '{' || ch == '}'));
}

/* Skip blanks and comments. At the top level a comment is kept. */
static int skip(lw_t *w, int top)
{
    while (w->pos < w->len) {
        char ch = w->src[w->pos];
        if (ch == '\n') {
            w->pos++;
            w->line++;
            w->lstart = w->pos;
        } else if (ch == ' ' || ch == '\t' || ch == '\r' || (ch == ',' && CLJ(w))) {
            w->pos++;
        } else if (ch == ';') {
            int32_t n = top ? newnode(w, K_COMMENT) : 0;
            if (n < 0) return ZL_E_LIMIT;
            while (w->pos < w->len &&
                   (w->src[w->pos] == ';' || w->src[w->pos] == ' ' || w->src[w->pos] == '\t'))
                w->pos++;
            size_t a = w->pos;
            while (w->pos < w->len && w->src[w->pos] != '\n') w->pos++;
            size_t b = w->pos;
            while (b > a && zl_isspace((unsigned char) w->src[b - 1])) b--;
            if (top) {
                if (b - a > ZL_MAX_LINE) return rfail(w, ZL_E_LIMIT, "comment too long");
                N(w, n)->v = (int64_t) a;
                N(w, n)->len = (uint16_t) (b - a);
                if (w->ntops >= L_TOPS) return rfail(w, ZL_E_LIMIT, "too many top-level forms");
                w->tops[w->ntops++] = n;
            }
        } else {
            break;
        }
    }
    return ZL_OK;
}

static int32_t rdatum(lw_t *w, uint32_t depth);

static int32_t rlist(lw_t *w, uint32_t depth)
{
    int32_t n = newnode(w, K_LIST), prev = -1;
    uint32_t k = 0;
    if (n < 0) return -1;
    char close = w->src[w->pos] == '[' ? ']' : ')';
    if (CLJ(w) && close == ']') N(w, n)->kind = K_VEC;
    w->pos++;
    for (;;) {
        if (skip(w, 0)) return -1;
        if (w->pos >= w->len) {
            rfail(w, ZL_E_SYNTAX, close == ']' ? "a ']' is missing" : "a ')' is missing");
            return -1;
        }
        if (w->src[w->pos] == ')' || ((w->dial & (D_RKT | D_CLJ)) && w->src[w->pos] == ']')) {
            if (w->src[w->pos] != close) {
                rfail(w, ZL_E_SYNTAX, "a list opened with ( closes with ), with [ with ]");
                return -1;
            }
            w->pos++;
            break;
        }
        int32_t e;
        if (!CLJ(w) && w->src[w->pos] == '.' &&
            (w->pos + 1 >= w->len || delim(w, w->src[w->pos + 1]))) {
            if (!k) {
                rfail(w, ZL_E_SYNTAX, "a dot needs an element before it");
                return -1;
            }
            w->pos++;
            if ((e = rdatum(w, depth + 1)) < 0) return -1;
            if (skip(w, 0)) return -1;
            if (w->pos >= w->len || w->src[w->pos] != close) {
                rfail(w, ZL_E_SYNTAX, "exactly one element follows a dot");
                return -1;
            }
            w->pos++;
            N(w, prev)->next = e;
            N(w, n)->flags |= F_DOT;
            k++;
            break;
        }
        if ((e = rdatum(w, depth + 1)) < 0) return -1;
        if (prev < 0)
            N(w, n)->kid = e;
        else
            N(w, prev)->next = e;
        prev = e;
        k++;
    }
    /* (quote x) reads as 'x */
    int32_t h = N(w, n)->kid;
    if (N(w, n)->kind == K_LIST && k == 2 && !(N(w, n)->flags & F_DOT) && sym_is(w, h, "quote")) {
        N(w, n)->kind = K_QUOTE;
        N(w, n)->kid = N(w, h)->next;
    }
    return n;
}

static int32_t rstring(lw_t *w)
{
    int32_t n = newnode(w, K_STR);
    if (n < 0) return -1;
    size_t a = ++w->pos;
    while (w->pos < w->len && w->src[w->pos] != '"') {
        unsigned char ch = (unsigned char) w->src[w->pos];
        if (ch == '\n') {
            rfail(w, ZL_E_UNSUPPORTED, "a line break inside a string is not in this subset");
            return -1;
        }
        if (ch < 32 || ch > 126) {
            rfail(w, ZL_E_UNSUPPORTED, "only printable ASCII is admitted in strings");
            return -1;
        }
        if (ch == '\\') {
            char e = w->pos + 1 < w->len ? w->src[w->pos + 1] : 0;
            if (!(e == '"' || e == '\\' || (!w->lisp && e == 'n'))) {
                rfail(w, ZL_E_UNSUPPORTED,
                      w->lisp ? "only \\\" and \\\\ are admitted as string escapes"
                              : "only \\\" \\\\ and \\n are admitted as string escapes");
                return -1;
            }
            w->pos++;
        }
        w->pos++;
    }
    if (w->pos >= w->len) {
        rfail(w, ZL_E_SYNTAX, "unterminated string");
        return -1;
    }
    if (w->pos - a > L_STRBUF) {
        rfail(w, ZL_E_LIMIT, "string too long");
        return -1;
    }
    N(w, n)->v = (int64_t) a;
    N(w, n)->len = (uint16_t) (w->pos - a);
    w->pos++;
    return n;
}

static int same(const char *t, size_t n, const char *s)
{
    size_t i = 0;
    for (; i < n; i++)
        if (!s[i] || t[i] != s[i]) return 0;
    return s[i] == 0;
}

static int32_t rhash(lw_t *w)
{
    size_t a = w->pos, b = a + 1;
    while (b < w->len && !delim(w, w->src[b])) b++;
    const char *t = w->src + a;
    size_t n = b - a;
    if (CLJ(w)) {
        char d = a + 1 < w->len ? w->src[a + 1] : 0;
        rfail(w, ZL_E_UNSUPPORTED,
              d == '{'    ? "sets are not in this subset"
              : d == '('  ? "anonymous functions #(...) are not in this subset"
              : d == '_'  ? "#_ (discard) is not in this subset"
              : d == '"'  ? "regular expressions are not in this subset"
              : d == '\'' ? "#' (a var) is not in this subset"
                          : "this # reader syntax is not in this subset");
        return -1;
    }
    if (!w->lisp) {
        int k = 0;
        if (same(t, n, "#t") || same(t, n, "#true"))
            k = K_TRUE;
        else if (same(t, n, "#f") || same(t, n, "#false"))
            k = K_FALSE;
        if (k) {
            int32_t x = newnode(w, k);
            w->pos = b;
            return x;
        }
    }
    char ch = a + 1 < w->len ? w->src[a + 1] : 0;
    rfail(w, ZL_E_UNSUPPORTED,
          ch == '('    ? "vectors are not in this subset"
          : ch == '\\' ? "characters are not in this subset"
          : ch == '\'' ? "#' (a function as a value) is not in this subset"
          : ch == '|'  ? "#| block comments |# are not in this subset"
          : ch == ';'  ? "#; datum comments are not in this subset"
                       : "this # reader syntax is not in this subset");
    return -1;
}

static int sym_char(int ch)
{
    if (zl_isalnum(ch)) return 1;
    for (const char *p = "-+*/<>=!?_%&$~^.@"; *p; p++)
        if (ch == *p) return 1;
    return 0;
}

static int32_t ratom(lw_t *w)
{
    size_t a = w->pos, b = a;
    while (b < w->len && !delim(w, w->src[b])) b++;
    const char *t = w->src + a;
    size_t n = b - a, i = 0;
    int neg = 0;
    if (n > 1 && (t[0] == '+' || t[0] == '-')) {
        neg = t[0] == '-';
        i = 1;
    }
    size_t d = i;
    while (d < n && zl_isdigit((unsigned char) t[d])) d++;
    int numeric = zl_isdigit((unsigned char) t[i]) ||
                  (i < n && t[i] == '.' && i + 1 < n && zl_isdigit((unsigned char) t[i + 1]));
    if (numeric && d == n && d > i) {
        int64_t v = 0;
        if (CLJ(w) && t[i] == '0' && n - i > 1) {
            rfail(w, ZL_E_UNSUPPORTED, "octal literals (a leading 0) are not in this subset");
            return -1;
        }
        for (size_t k = i; k < n; k++) {
            int dg = t[k] - '0';
            if (v > (ZL_V_REF - 1 - dg) / 10) {
                rfail(w, ZL_E_UNSUPPORTED,
                      "integer outside +-2^62 (bignums are not in this subset)");
                return -1;
            }
            v = v * 10 + dg;
        }
        int32_t x = newnode(w, K_NUM);
        if (x < 0) return -1;
        N(w, x)->v = neg ? -v : v;
        w->pos = b;
        return x;
    }
    if (numeric && !(w->lisp && n == 2 && t[0] == '1' && (t[1] == '+' || t[1] == '-'))) {
        rfail(w, ZL_E_UNSUPPORTED,
              "this number syntax (a float, a ratio or an exponent) is not in the integer subset");
        return -1;
    }
    int plain = same(t, n, ":else") || same(t, n, "/") || same(t, n, "&");
    for (size_t k = 0; CLJ(w) && !plain && k < n; k++) {
        int ch = (unsigned char) t[k];
        const char *msg = 0;
        if (ch == ':')
            msg = "keywords are not in this subset (only :else, in cond)";
        else if (ch == '.' || ch == '/')
            msg = "namespaces and Java interop are not in this subset";
        else if (!zl_isalnum(ch) && ch != '*' && ch != '+' && ch != '!' && ch != '-' && ch != '_' &&
                 ch != '?' && ch != '<' && ch != '>' && ch != '=')
            msg = "this character in a name is not in this subset";
        if (msg) {
            w->pos = a + k;
            rfail(w, ZL_E_UNSUPPORTED, msg);
            return -1;
        }
    }
    for (size_t k = 0; !CLJ(w) && k < n; k++) {
        if (t[k] == ':') {
            w->pos = a + k;
            rfail(w, ZL_E_UNSUPPORTED, "keywords and package prefixes are not in this subset");
            return -1;
        }
        if (t[k] == '|') {
            w->pos = a + k;
            rfail(w, ZL_E_UNSUPPORTED, "escaped symbols |...| are not in this subset");
            return -1;
        }
        if (!sym_char((unsigned char) t[k])) {
            w->pos = a + k;
            rfail(w, ZL_E_SYNTAX, "unexpected character");
            return -1;
        }
    }
    if (n == 1 && t[0] == '.') {
        rfail(w, ZL_E_SYNTAX, "a dot outside a list");
        return -1;
    }
    if (n > L_NAME) {
        rfail(w, ZL_E_LIMIT, "name too long");
        return -1;
    }
    int32_t x = newnode(w, K_SYM);
    if (x < 0) return -1;
    N(w, x)->v = (int64_t) a;
    N(w, x)->len = (uint16_t) n;
    w->pos = b;
    return x;
}

static int32_t rdatum(lw_t *w, uint32_t depth)
{
    if (depth > ZL_MAX_DEPTH) {
        rfail(w, ZL_E_LIMIT, "lists nested too deeply");
        return -1;
    }
    if (skip(w, 0)) return -1;
    if (w->pos >= w->len) {
        rfail(w, ZL_E_SYNTAX, "a datum is missing at the end of the program");
        return -1;
    }
    char ch = w->src[w->pos];
    if (CLJ(w)) {
        const char *msg = ch == ']'   ? "unexpected ']'"
                          : ch == '{' ? "maps are not in this subset"
                          : ch == '}' ? "unexpected '}'"
                          : ch == '`' || ch == '~'
                              ? "syntax-quote and unquote (macro templates) are not in this subset"
                          : ch == '@'  ? "@ (deref) is not in this subset"
                          : ch == '^'  ? "metadata (^) is not in this subset"
                          : ch == '\\' ? "characters are not in this subset"
                                       : 0;
        if (ch == '[') return rlist(w, depth);
        if (msg) {
            rfail(w, ch == ']' || ch == '}' ? ZL_E_SYNTAX : ZL_E_UNSUPPORTED, msg);
            return -1;
        }
    }
    if (w->dial == D_RKT && (ch == '[' || ch == ']' || ch == '{' || ch == '}')) {
        if (ch == '[') return rlist(w, depth);
        rfail(w, ch == ']' ? ZL_E_SYNTAX : ZL_E_UNSUPPORTED,
              ch == ']' ? "unexpected ']'" : "braces as list brackets are not in this subset");
        return -1;
    }
    switch (ch) {
    case '(':
        return rlist(w, depth);
    case ')':
        rfail(w, ZL_E_SYNTAX, "unexpected ')'");
        return -1;
    case '\'': {
        int32_t n = newnode(w, K_QUOTE);
        if (n < 0) return -1;
        w->pos++;
        int32_t d = rdatum(w, depth + 1);
        if (d < 0) return -1;
        N(w, n)->kid = d;
        return n;
    }
    case '"':
        return rstring(w);
    case '`':
    case ',':
        rfail(w, ZL_E_UNSUPPORTED, "backquote and comma (macro templates) are not in this subset");
        return -1;
    case '#':
        return rhash(w);
    case '|':
        rfail(w, ZL_E_UNSUPPORTED, "escaped symbols |...| are not in this subset");
        return -1;
    default:
        return ratom(w);
    }
}

/* Racket: the first line is "#lang racket" or "#lang racket/base". */
static int read_lang(lw_t *w)
{
    static const char *const want = "#lang racket";
    size_t i = 0, e;
    for (; want[i]; i++)
        if (i >= w->len || w->src[i] != want[i])
            return rfail(w, i < 5 ? ZL_E_SYNTAX : ZL_E_UNSUPPORTED,
                         i < 5 ? "a Racket program starts with #lang racket or #lang racket/base"
                               : "only #lang racket and #lang racket/base are in this subset");
    e = i;
    if (w->len - i >= 5 && same(w->src + i, 5, "/base")) {
        w->rbase = 1;
        e = i + 5;
    }
    w->pos = e;
    while (w->pos < w->len && zl_isspace((unsigned char) w->src[w->pos])) w->pos++;
    if (w->pos < w->len && w->src[w->pos] != '\n')
        return rfail(w, ZL_E_UNSUPPORTED,
                     "only #lang racket and #lang racket/base are in this subset");
    w->langlen = e - 6;
    return ZL_OK;
}

static int read_program(lw_t *w)
{
    if (w->dial == D_RKT) TRY(read_lang(w));
    for (;;) {
        if (skip(w, 1)) return ok(w);
        if (w->pos >= w->len) return ZL_OK;
        int32_t n = rdatum(w, 0);
        if (n < 0) return ok(w);
        if (w->ntops >= L_TOPS) return lfail(w, n, ZL_E_LIMIT, "too many top-level forms");
        w->tops[w->ntops++] = n;
        w->form[w->nforms++] = n;
    }
}

/* ------------------------------------------------------------ names and scopes */

static int32_t find_scope(lw_t *w, int32_t sym, int fn)
{
    for (uint32_t i = w->nscope; i-- > 0;)
        if (name_eq(w, w->scope[i].name, sym) &&
            (!w->lisp || (w->scope[i].kind == B_FN) == (fn != 0)))
            return (int32_t) i;
    return -1;
}

static int32_t find_topfn(lw_t *w, int32_t sym)
{
    for (uint32_t i = 0; i < w->nfns; i++)
        if (w->fns[i].top >= 0 && w->fns[i].top <= w->curtop && name_eq(w, w->fns[i].name, sym))
            return (int32_t) i;
    return -1;
}

static int32_t find_glob(lw_t *w, int32_t sym)
{
    for (uint32_t i = 0; i < w->nglob; i++)
        if (w->glob[i].top < w->curtop && name_eq(w, w->glob[i].name, sym)) return (int32_t) i;
    return -1;
}

static int bind(lw_t *w, int32_t name, int kind, int32_t v, int32_t at)
{
    if (w->nscope >= L_SCOPE) return lfail(w, at, ZL_E_LIMIT, "too many names in scope");
    lbind_t *b = &w->scope[w->nscope++];
    b->name = name;
    b->kind = (uint8_t) kind;
    b->v = v;
    b->level = (uint8_t) (kind == B_FN ? w->fns[v].level : w->cur.level);
    return ZL_OK;
}

static int alloc_local(lw_t *w, int32_t at, uint32_t *off)
{
    *off = 0;
    if (w->cur.nloc >= L_LOCALS) return lfail(w, at, ZL_E_LIMIT, "too many local variables");
    *off = w->cur.nloc++;
    if (w->cur.nloc > w->cur.maxloc) w->cur.maxloc = w->cur.nloc;
    return ZL_OK;
}

/* A name that a let, a parameter or a loop may bind. */
static int check_binder(lw_t *w, int32_t n)
{
    if (CLJ(w) && (N(w, n)->kind == K_VEC || N(w, n)->kind == K_LIST))
        return lfail(w, n, ZL_E_UNSUPPORTED, "destructuring is not in this subset");
    if (CLJ(w) && sym_is(w, n, "&"))
        return lfail(w, n, ZL_E_UNSUPPORTED, "variadic functions (&) are not in this subset");
    if (CLJ(w) && sym_is(w, n, ":else"))
        return lfail(w, n, ZL_E_UNSUPPORTED,
                     "keywords are not in this subset (only :else, in cond)");
    if (N(w, n)->kind != K_SYM) return lfail(w, n, ZL_E_SYNTAX, "a variable name is expected");
    if (is_const(w, n)) return lfail(w, n, ZL_E_SYNTAX, const_msg(w));
    if (w->lisp && N(w, n)->len && w->src[N(w, n)->v] == '&')
        return lfail(w, n, ZL_E_UNSUPPORTED,
                     "lambda-list keywords (&optional, &rest, &key) are not in this subset");
    if (w->lisp && find_glob(w, n) >= 0)
        return lfail(
            w, n, ZL_E_UNSUPPORTED,
            "binding a defvar'd (special) variable is dynamic binding, not in this subset");
    if (!w->lisp && find_spec(w, n) >= 0)
        return lfail(w, n, ZL_E_UNSUPPORTED, "rebinding a syntax keyword is not in this subset");
    return ZL_OK;
}

/* Parameters: a chain of names, or (pkind 1) of (name init) bindings. */
static int32_t pname(lw_t *w, int32_t p, int pkind)
{
    return pkind == 1 ? N(w, p)->kid : p;
}

static int check_params(lw_t *w, int32_t first, int pkind, int32_t at, uint16_t *np)
{
    uint32_t k = 0;
    for (int32_t p = first; p >= 0; p = pnext(w, p, pkind), k++) {
        int32_t nm = pname(w, p, pkind);
        TRY(check_binder(w, nm));
        for (int32_t q = first; q != p; q = pnext(w, q, pkind))
            if (name_eq(w, pname(w, q, pkind), nm))
                return lfail(w, nm, CLJ(w) ? ZL_E_UNSUPPORTED : ZL_E_SYNTAX,
                             "a name is bound twice in one list");
    }
    if (k > L_PARAMS) return lfail(w, at, ZL_E_LIMIT, "too many parameters");
    *np = (uint16_t) k;
    return ZL_OK;
}

static int add_fn(lw_t *w, int32_t name, int32_t params, int pkind, int32_t body, int32_t at,
                  uint32_t level, int32_t top, int32_t *fid)
{
    if (w->nfns >= L_FNS) return lfail(w, at, ZL_E_LIMIT, "too many functions");
    if (level > ZL_MAX_DEPTH) return lfail(w, at, ZL_E_LIMIT, "functions nested too deeply");
    lfn_t *f = &w->fns[w->nfns];
    zl_bzero(f, sizeof *f);
    TRY(check_params(w, params, pkind, at, &f->nparams));
    f->name = name;
    f->params = params;
    f->pkind = (uint8_t) pkind;
    f->body = body;
    f->at = at;
    f->entry = -1;
    f->top = top;
    f->level = (uint8_t) level;
    *fid = (int32_t) w->nfns++;
    return ZL_OK;
}

/* ------------------------------------------------------------ code helpers */

static int add_fix(lw_t *w, int32_t at, int32_t fid, int32_t n)
{
    if (at < 0) return ok(w);
    if (w->nfix >= L_FIX) return lfail(w, n, ZL_E_LIMIT, "too many calls");
    w->fix[w->nfix].at = at;
    w->fix[w->nfix].fid = fid;
    w->nfix++;
    return ZL_OK;
}

static int hcall(lw_t *w, int h, int32_t n)
{
    if (w->scratch < 0 && (w->scratch = zl_slots(w->c, 9)) < 0) return ok(w);
    w->hused[h] = 1;
    if (h == H_MEMBER || h == H_ASSOC) w->hused[H_EQUAL] = 1;
    return add_fix(w, E(w, ZL_CALL, 0, 0), -h - 1, n);
}

/* Push the false value or a flag for "is the value on the stack true". */
static void to_flag(lw_t *w)
{
    if (CLJ(w)) { /* nil and false are both false */
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_PUSH, w->NIL, 0);
        E(w, ZL_NE, 0, 0);
        E(w, ZL_SWAP, 0, 0);
    }
    E(w, ZL_PUSH, w->F, 0);
    E(w, ZL_NE, 0, 0);
    if (CLJ(w)) E(w, ZL_AND, 0, 0);
}

/* A value is on the stack: make it what the context wants. */
static int fin_value(lw_t *w, int ctx)
{
    if (ctx == X_EFFECT)
        E(w, ZL_DROP, 0, 0);
    else if (ctx == X_TEST)
        to_flag(w);
    return ok(w);
}

/* A 0/1 flag is on the stack. */
static int fin_flag(lw_t *w, int ctx)
{
    if (ctx == X_EFFECT)
        E(w, ZL_DROP, 0, 0);
    else if (ctx != X_TEST) {
        int32_t j = E(w, ZL_JZ, 0, 0);
        E(w, ZL_PUSH, w->T, 0);
        int32_t k = E(w, ZL_JMP, 0, 0);
        patch_here(w, j);
        E(w, ZL_PUSH, w->F, 0);
        patch_here(w, k);
    }
    return ok(w);
}

/* Nothing is on the stack: the form's value is NIL (LISP) or unspecified
 * (Scheme), which may flow out of a procedure but is never used. */
static int fin_none(lw_t *w, int ctx, int32_t at)
{
    if (ctx == X_EFFECT) return ok(w);
    if (w->lisp || CLJ(w)) { /* nil */
        E(w, ZL_PUSH, ctx == X_TEST ? 0 : w->NIL, 0);
        return ok(w);
    }
    if (ctx == X_RESULT && (w->cur.fid >= 0 || w->dial == D_RKT)) {
        /* Racket's top level prints a value unless it is void */
        if (w->cur.fid >= 0) w->fns[w->cur.fid].unspec = 1;
        E(w, ZL_PUSH, w->V, 0);
        return ok(w);
    }
    return lfail(w, at, ZL_E_UNSUPPORTED,
                 w->dial == D_RKT
                     ? "the value of this form is void in Racket: use it only as a statement"
                     : "the value of this form is unspecified in Scheme: use it only as a "
                       "statement");
}

static void guard_num(lw_t *w, int isnum)
{
    if (!isnum) { /* = and EQ do not check their operands: make a reference trap */
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_ADD, 0, 0);
    }
}

static int32_t chain(lw_t *w, int op, int32_t prev)
{
    int32_t at = E(w, op, prev, 0);
    return at < 0 ? prev : at;
}

static void chain_patch(lw_t *w, int32_t at)
{
    while (at >= 0) {
        int32_t nxt = (int32_t) w->c->p->code[at].a;
        w->c->p->code[at].a = here(w);
        at = nxt;
    }
}

/* ------------------------------------------------------------ strings */

/* Decode a string literal into sbuf; returns its length. */
static size_t decode(lw_t *w, int32_t n)
{
    const char *p = w->src + N(w, n)->v;
    size_t k = 0;
    for (size_t i = 0; i < N(w, n)->len; i++) {
        char ch = p[i];
        if (ch == '\\') {
            ch = p[++i];
            if (ch == 'n') ch = '\n';
        }
        w->sbuf[k++] = ch;
    }
    return k;
}

/* The string as prin1 / write print it (and as the twin writes it). */
static void encode(const char *s, size_t n, void (*put)(void *, const char *, size_t), void *ctx)
{
    put(ctx, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '"' || s[i] == '\\')
            put(ctx, "\\", 1);
        else if (s[i] == '\n') {
            put(ctx, "\\n", 2);
            continue;
        }
        put(ctx, s + i, 1);
    }
    put(ctx, "\"", 1);
}

static void qb_put(void *ctx, const char *s, size_t n)
{
    lw_t *w = (lw_t *) ctx;
    for (size_t i = 0; i < n && w->qlen < sizeof w->qbuf; i++) w->qbuf[w->qlen++] = s[i];
}

static int out_text(lw_t *w, const char *s, size_t n, int32_t at)
{
    if (!n) return ok(w);
    int32_t k = zl_addstr(w->c, s, n);
    if (k < 0) return lfail(w, at, ZL_E_LIMIT, "too many strings");
    E(w, ZL_OUTS, k, 0);
    return ok(w);
}

/* Print a string literal, raw (princ, display) or quoted (prin1, write). */
static int out_str(lw_t *w, int32_t n, int quoted)
{
    size_t k = decode(w, n);
    if (!quoted) return out_text(w, w->sbuf, k, n);
    w->qlen = 0;
    encode(w->sbuf, k, qb_put, w);
    return out_text(w, w->qbuf, w->qlen, n);
}

/* ------------------------------------------------------------ quoted data */

static int data_sym_ok(lw_t *w, int32_t n)
{
    const char *p = w->src + N(w, n)->v;
    if (!zl_isalpha((unsigned char) p[0])) return 0;
    for (size_t i = 0; i < N(w, n)->len; i++) {
        int ch = (unsigned char) p[i];
        if (!zl_isalnum(ch) && ch != '-' && ch != '*' && ch != '+' && ch != '/' && ch != '<' &&
            ch != '>' && ch != '=' && ch != '!' && ch != '?' && ch != '_')
            return 0;
    }
    return 1;
}

/* Code that pushes a quoted datum (built afresh each time it runs). */
static int datum(lw_t *w, int32_t n)
{
    lnode_t *x = N(w, n);
    switch (x->kind) {
    case K_NUM:
        E(w, ZL_PUSH, x->v, 0);
        return ok(w);
    case K_TRUE:
    case K_FALSE:
        E(w, ZL_PUSH, x->kind == K_TRUE ? ZL_V_TRUE : ZL_V_FALSE, 0);
        return ok(w);
    case K_STR:
        return lfail(w, n, ZL_E_UNSUPPORTED, why[R_STRING]);
    case K_QUOTE:
        return lfail(w, n, ZL_E_UNSUPPORTED, "a quote inside quoted data is not in this subset");
    case K_VEC:
        return lfail(w, n, ZL_E_UNSUPPORTED, "vectors are not values in this subset");
    case K_SYM: {
        if (sym_is(w, n, w->lisp ? "nil" : "")) {
            E(w, ZL_PUSH, ZL_V_NIL, 0);
            return ok(w);
        }
        if (CLJ(w) && is_const(w, n)) { /* Clojure: '(1 nil true) holds nil and true */
            E(w, ZL_PUSH, clj_const(w, n), 0);
            return ok(w);
        }
        if (w->lisp && sym_is(w, n, "t")) {
            E(w, ZL_PUSH, ZL_V_TRUE, 0);
            return ok(w);
        }
        if (!data_sym_ok(w, n))
            return lfail(
                w, n, ZL_E_UNSUPPORTED,
                "a quoted symbol starts with a letter and has no punctuation but -*+/<>=!?_");
        char b[L_NAME];
        for (size_t i = 0; i < x->len; i++) {
            int ch = (unsigned char) w->src[x->v + (int64_t) i];
            b[i] = (char) (w->lisp ? zl_upper(ch) : ch);
        }
        int32_t k = zl_addstr(w->c, b, x->len);
        if (k < 0) return lfail(w, n, ZL_E_LIMIT, "too many symbols");
        E(w, ZL_PUSH, ZL_V_SYM + k, 0);
        return ok(w);
    }
    case K_LIST: {
        uint32_t base = w->nscr, m = 0;
        int32_t tail = -1;
        for (int32_t e = x->kid; e >= 0; e = N(w, e)->next) {
            if ((x->flags & F_DOT) && N(w, e)->next < 0) {
                tail = e;
                break;
            }
            if (w->nscr >= L_NODES) return lfail(w, n, ZL_E_LIMIT, "quoted data too large");
            w->scr[w->nscr++] = e;
            m++;
        }
        if (tail >= 0)
            TRY(datum(w, tail));
        else
            E(w, ZL_PUSH, ZL_V_NIL, 0);
        for (uint32_t i = m; i-- > 0;) {
            TRY(datum(w, w->scr[base + i]));
            E(w, ZL_SWAP, 0, 0);
            E(w, ZL_CONS, 0, 0);
        }
        w->nscr = base;
        return ok(w);
    }
    default:
        return lfail(w, n, ZL_E_SYNTAX, "not a datum");
    }
}

/* ------------------------------------------------------------ expressions */

static int cx(lw_t *w, int32_t n, int ctx, int *isnum);
static int body(lw_t *w, int32_t first, int ctx, int32_t at);
static int emit_fn(lw_t *w, int32_t fid);

static int cv(lw_t *w, int32_t n)
{
    return cx(w, n, X_VALUE, 0);
}

/* A sequence: all but the last for effect. */
static int seq(lw_t *w, int32_t first, int ctx, int32_t at)
{
    if (first < 0) {
        if (!w->lisp && !CLJ(w))
            return lfail(w, at, ZL_E_SYNTAX, "a body needs at least one expression");
        return fin_none(w, ctx, at);
    }
    for (int32_t e = first; e >= 0; e = N(w, e)->next)
        TRY(cx(w, e, N(w, e)->next < 0 ? ctx : X_EFFECT, 0));
    return ok(w);
}

/* Loop bodies in LISP are tagbodies: a bare atom there is a go tag. () reads as
   the symbol NIL, so it is a tag too (and its twin prints NIL). */
static int loop_body(lw_t *w, int32_t first)
{
    for (int32_t e = first; e >= 0; e = N(w, e)->next) {
        int k = N(w, e)->kind;
        if (w->lisp && (k != K_LIST || N(w, e)->kid < 0) && k != K_QUOTE && k != K_STR)
            return lfail(w, e, ZL_E_UNSUPPORTED,
                         "an atom in a loop body is a go tag, which is not in this subset");
        TRY(cx(w, e, X_EFFECT, 0));
    }
    return ok(w);
}

static int load_var(lw_t *w, int32_t n)
{
    int32_t b = find_scope(w, n, 0);
    if (b >= 0) {
        if (w->scope[b].kind == B_FN) return lfail(w, n, ZL_E_UNSUPPORTED, why[R_PROC]);
        E(w, ZL_LOADF, w->scope[b].v, (int32_t) (w->cur.level - w->scope[b].level));
        return ok(w);
    }
    int32_t g = find_glob(w, n);
    if (g >= 0) {
        E(w, ZL_LOAD, w->glob[g].slot, 0);
        return ok(w);
    }
    if (!w->lisp && (find_topfn(w, n) >= 0 || find_bi(w, n) >= 0 || is_cxr(w, n)))
        return lfail(w, n, ZL_E_UNSUPPORTED, why[R_PROC]);
    return lfail(w, n, ZL_E_UNDEFINED, "undefined variable");
}

static int store_var(lw_t *w, int32_t n)
{
    if (N(w, n)->kind != K_SYM) return lfail(w, n, ZL_E_SYNTAX, "a variable name is expected");
    if (is_const(w, n)) return lfail(w, n, ZL_E_SYNTAX, const_msg(w));
    int32_t b = find_scope(w, n, 0);
    if (b >= 0) {
        if (w->scope[b].kind == B_FN) return lfail(w, n, ZL_E_UNSUPPORTED, why[R_PROC]);
        E(w, ZL_STOREF, w->scope[b].v, (int32_t) (w->cur.level - w->scope[b].level));
        return ok(w);
    }
    int32_t g = find_glob(w, n);
    if (g >= 0) {
        E(w, ZL_STORE, w->glob[g].slot, 0);
        return ok(w);
    }
    if (!w->lisp && find_topfn(w, n) >= 0) return lfail(w, n, ZL_E_UNSUPPORTED, why[R_PROC]);
    return lfail(w, n, ZL_E_UNDEFINED, "assignment to an undefined variable");
}

/* Bookkeeping after a call to a user function whose value is on the stack. */
static int call_done(lw_t *w, int32_t fid, int ctx, int32_t n)
{
    if (!w->lisp) {
        if (ctx == X_VALUE || ctx == X_TEST) {
            if (w->nobs >= L_OBS) return lfail(w, n, ZL_E_LIMIT, "too many calls");
            w->obs[w->nobs].fid = fid;
            w->obs[w->nobs].line = N(w, n)->line;
            w->obs[w->nobs].col = N(w, n)->col;
            w->nobs++;
        } else if (ctx == X_RESULT && w->cur.fid >= 0) {
            if (w->nedge >= L_EDGE) return lfail(w, n, ZL_E_LIMIT, "too many calls");
            w->edge[w->nedge].from = w->cur.fid;
            w->edge[w->nedge].to = fid;
            w->nedge++;
        }
    }
    return fin_value(w, ctx == X_RESULT ? X_VALUE : ctx);
}

/* A call of a user function with the arguments args (inits of a named let). */
static int ucall(lw_t *w, int32_t n, int32_t fid, int32_t args, int pkind, int ctx)
{
    lfn_t *f = &w->fns[fid];
    if (count(w, args) != f->nparams)
        return lfail(w, n, ZL_E_SYNTAX, "wrong number of arguments for this function");
    if (ctx == X_RESULT && fid == w->cur.fid && !CLJ(w)) { /* a self tail call is a jump */
        for (int32_t a = args; a >= 0; a = N(w, a)->next) TRY(cv(w, a));
        for (uint32_t i = f->nparams; i-- > 0;) E(w, ZL_STOREF, i, 0);
        E(w, ZL_JMP, f->entry + 1, 0);
        return ok(w);
    }
    E(w, ZL_LINK, w->cur.level - (f->level - 1u), 0);
    for (int32_t a = args; a >= 0; a = N(w, a)->next)
        TRY(cv(w, pkind ? N(w, N(w, a)->kid)->next : a));
    TRY(add_fix(w, E(w, ZL_CALL, 0, 0), fid, n));
    return call_done(w, fid, ctx, n);
}

/* print / prin1 / princ / display / write of one argument. */
static int out_form(lw_t *w, int32_t n, int op, int32_t a, int ctx)
{
    int dial = w->lisp ? ZL_DIALECT_LISP : ZL_DIALECT_SCHEME;
    int ln = op == B_DISPLAYLN || op == B_RPRINTLN, rp = op == B_RPRINT || op == B_RPRINTLN;
    if (N(w, a)->kind == K_STR) {
        if (w->lisp && ctx != X_EFFECT) return lfail(w, a, ZL_E_UNSUPPORTED, why[R_STRING]);
        if (op == B_PRINT) E(w, ZL_NL, 0, 0);
        TRY(out_str(w, a, op == B_PRINT || op == B_PRIN1 || op == B_WRITE || rp));
        if (op == B_PRINT) TRY(out_text(w, " ", 1, n));
        if (ln) E(w, ZL_NL, 0, 0);
        return fin_none(w, ctx, n);
    }
    TRY(cv(w, a));
    if (w->lisp && ctx != X_EFFECT) E(w, ZL_DUP, 0, 0);
    if (op == B_PRINT) E(w, ZL_NL, 0, 0);
    if (rp)
        TRY(hcall(w, H_RPRINT, n));
    else
        E(w, ZL_OUTV, dial, 0);
    if (op == B_PRINT) TRY(out_text(w, " ", 1, n));
    if (ln) E(w, ZL_NL, 0, 0);
    if (w->lisp) return ctx == X_EFFECT ? ok(w) : fin_value(w, ctx);
    return fin_none(w, ctx, n);
}

/* (format t "control" args...): ~a ~d ~s ~% ~~ only. Arguments are
 * evaluated first, as for any call, then the output is written. */
static int format_form(lw_t *w, int32_t n, int32_t a, int ctx)
{
    int rkt = w->dial == D_RKT; /* (printf "control" args...): ~a ~s ~v ~n ~% ~~ */
    int32_t ctl = rkt ? a : nx(w, a), args = nx(w, ctl);
    if (!rkt && !sym_is(w, a, "t"))
        return lfail(w, a, ZL_E_UNSUPPORTED,
                     "format to a destination other than T is not in this subset");
    if (ctl < 0 || N(w, ctl)->kind != K_STR)
        return lfail(w, ctl < 0 ? n : ctl, ZL_E_UNSUPPORTED,
                     "the format control must be a string literal");
    uint32_t saved = w->cur.nloc, base = w->cur.nloc, k = 0, na = count(w, args);
    for (int32_t e = args; e >= 0; e = N(w, e)->next) {
        uint32_t off;
        TRY(alloc_local(w, e, &off));
        if (N(w, e)->kind == K_STR) continue;
        TRY(cv(w, e));
        E(w, ZL_STOREF, off, 0);
    }
    char *text = w->fbuf;
    size_t len = decode(w, ctl), t = 0;
    zl_bcopy(text, w->sbuf, len);
    int32_t e = args;
    for (size_t i = 0; i < len; i++) {
        if (text[i] != '~') continue;
        TRY(out_text(w, text + t, i - t, ctl));
        if (++i >= len) return lfail(w, ctl, ZL_E_SYNTAX, "a format string ends with ~");
        int d = zl_upper((unsigned char) text[i]);
        t = i + 1;
        if (d == '%' || (rkt && d == 'N'))
            E(w, ZL_NL, 0, 0);
        else if (d == '~')
            TRY(out_text(w, "~", 1, ctl));
        else if (d == 'A' || d == 'S' || (rkt ? d == 'V' : d == 'D')) {
            if (e < 0)
                return lfail(w, ctl, ZL_E_UNSUPPORTED,
                             "format arguments must match its directives");
            if (N(w, e)->kind == K_STR)
                TRY(out_str(w, e, d != 'A' && d != 'D'));
            else {
                E(w, ZL_LOADF, base + k, 0);
                if (d == 'V')
                    TRY(hcall(w, H_RPRINT, n));
                else
                    E(w, ZL_OUTV, rkt ? ZL_DIALECT_SCHEME : ZL_DIALECT_LISP, 0);
            }
            e = N(w, e)->next;
            k++;
        } else
            return lfail(w, ctl, ZL_E_UNSUPPORTED,
                         rkt ? "printf directives other than ~a ~s ~v ~n ~% ~~ are not in this "
                               "subset"
                             : "format directives other than ~a ~d ~s ~% ~~ are not in this "
                               "subset");
    }
    TRY(out_text(w, text + t, len - t, ctl));
    if (k != na)
        return lfail(w, ctl, ZL_E_UNSUPPORTED, "format arguments must match its directives");
    w->cur.nloc = saved;
    return fin_none(w, ctx, n);
}

static int compare(lw_t *w, int op, int32_t args, uint32_t na, int ctx, int32_t n)
{
    int num;
    if (na == 1) { /* Clojure: (< x) is true, x unchecked */
        TRY(cx(w, args, X_EFFECT, 0));
        E(w, ZL_PUSH, 1, 0);
        return fin_flag(w, ctx);
    }
    if (na == 2) {
        TRY(cx(w, args, X_VALUE, &num));
        if (op == ZL_EQ || op == ZL_NE) guard_num(w, num);
        TRY(cx(w, N(w, args)->next, X_VALUE, &num));
        if (op == ZL_EQ || op == ZL_NE) guard_num(w, num);
        E(w, op, 0, 0);
        return fin_flag(w, ctx);
    }
    uint32_t saved = w->cur.nloc, base = w->cur.nloc, i = 0;
    for (int32_t e = args; e >= 0; e = N(w, e)->next) {
        uint32_t off;
        TRY(alloc_local(w, n, &off));
    }
    for (int32_t e = args; e >= 0; e = N(w, e)->next, i++) {
        TRY(cx(w, e, X_VALUE, &num));
        if (op == ZL_EQ) guard_num(w, num);
        E(w, ZL_STOREF, base + i, 0);
    }
    for (i = 0; i + 1 < na; i++) {
        E(w, ZL_LOADF, base + i, 0);
        E(w, ZL_LOADF, base + i + 1, 0);
        E(w, op, 0, 0);
        if (i) E(w, ZL_AND, 0, 0);
    }
    w->cur.nloc = saved;
    return fin_flag(w, ctx);
}

/* Fold a variadic arithmetic operator over the arguments. */
static int fold(lw_t *w, int op, int32_t args)
{
    for (int32_t e = args; e >= 0; e = N(w, e)->next) {
        TRY(cv(w, e));
        if (e != args) E(w, op, 0, 0);
    }
    return ok(w);
}

/* Clojure: a list from a seqable value on the stack (nil is the empty list). */
static int seqv(lw_t *w, int32_t n)
{
    return hcall(w, H_SEQ, n);
}

/* Clojure: a list on the stack; the empty list becomes nil. */
static void nil_if_empty(lw_t *w)
{
    E(w, ZL_DUP, 0, 0);
    E(w, ZL_ISA, ZL_ISA_NULL, 0);
    int32_t j = E(w, ZL_JZ, 0, 0);
    E(w, ZL_DROP, 0, 0);
    E(w, ZL_PUSH, w->NIL, 0);
    patch_here(w, j);
}

/* Clojure: the first element of a list on the stack, nil when it is empty. */
static void clj_first(lw_t *w)
{
    E(w, ZL_DUP, 0, 0);
    E(w, ZL_ISA, ZL_ISA_NULL, 0);
    int32_t j = E(w, ZL_JZ, 0, 0);
    E(w, ZL_DROP, 0, 0);
    E(w, ZL_PUSH, w->NIL, 0);
    int32_t k = E(w, ZL_JMP, 0, 0);
    patch_here(w, j);
    E(w, ZL_CAR, 0, 0);
    patch_here(w, k);
}

/* Evaluate the arguments (but strings) into fresh locals from base. */
static int args_to_locals(lw_t *w, int32_t args, int strs, uint32_t *base)
{
    *base = w->cur.nloc;
    for (int32_t e = args; e >= 0; e = N(w, e)->next) {
        uint32_t off;
        TRY(alloc_local(w, e, &off));
        if (strs && N(w, e)->kind == K_STR) continue;
        TRY(cv(w, e));
        E(w, ZL_STOREF, off, 0);
    }
    return ok(w);
}

/* Clojure's println print prn pr newline: the arguments separated by
 * spaces; prn and pr write strings in quotes. */
static int clj_print(lw_t *w, int32_t n, int32_t head, int32_t a, int ctx)
{
    int ln = sym_is(w, head, "println") || sym_is(w, head, "prn") || sym_is(w, head, "newline");
    int q = sym_is(w, head, "prn") || sym_is(w, head, "pr");
    uint32_t saved = w->cur.nloc, base, i = 0;
    TRY(args_to_locals(w, a, 1, &base));
    for (int32_t e = a; e >= 0; e = N(w, e)->next, i++) {
        if (i) TRY(out_text(w, " ", 1, n));
        if (N(w, e)->kind == K_STR)
            TRY(out_str(w, e, q));
        else {
            E(w, ZL_LOADF, base + i, 0);
            E(w, ZL_OUTV, ZL_DIALECT_SCHEME, 0);
        }
    }
    if (ln) E(w, ZL_NL, 0, 0);
    w->cur.nloc = saved;
    return fin_none(w, ctx, n);
}

/* Clojure's built-ins that differ from LISP's and Scheme's. */
static int clj_bcall(lw_t *w, int32_t n, int op, int32_t head, int32_t a, uint32_t na, int ctx,
                     int *isnum)
{
    int32_t a2 = nx(w, a);
    uint32_t saved = w->cur.nloc, base;
    switch (op) {
    case B_CEQ:
    case B_CNE:
        if (na == 1) {
            TRY(cx(w, a, X_EFFECT, 0));
            E(w, ZL_PUSH, op == B_CEQ, 0);
            return fin_flag(w, ctx);
        }
        TRY(args_to_locals(w, a, 0, &base));
        for (uint32_t i = 0; i + 1 < na; i++) {
            E(w, ZL_LOADF, base + i, 0);
            E(w, ZL_LOADF, base + i + 1, 0);
            TRY(hcall(w, H_EQUAL, n));
            if (i) E(w, ZL_AND, 0, 0);
        }
        if (op == B_CNE) E(w, ZL_NOT, 0, 0);
        w->cur.nloc = saved;
        return fin_flag(w, ctx);
    case B_CSYMP: { /* nil, true and false are not symbols */
        uint32_t off;
        TRY(cv(w, a));
        TRY(alloc_local(w, a, &off));
        E(w, ZL_STOREF, off, 0);
        E(w, ZL_LOADF, off, 0);
        E(w, ZL_ISA, ZL_ISA_SYMBOL, 0);
        const int64_t k[3] = {w->NIL, w->T, w->F};
        for (int i = 0; i < 3; i++) {
            E(w, ZL_LOADF, off, 0);
            E(w, ZL_PUSH, k[i], 0);
            E(w, ZL_NE, 0, 0);
            E(w, ZL_AND, 0, 0);
        }
        w->cur.nloc = saved;
        return fin_flag(w, ctx);
    }
    case B_NILP:
    case B_SOMEP:
    case B_TRUEP:
    case B_FALSEP:
        TRY(cv(w, a));
        E(w, ZL_PUSH, op == B_TRUEP ? w->T : op == B_FALSEP ? w->F : w->NIL, 0);
        E(w, op == B_SOMEP ? ZL_NE : ZL_EQ, 0, 0);
        return fin_flag(w, ctx);
    case B_NOT:
        TRY(cv(w, a));
        to_flag(w);
        E(w, ZL_NOT, 0, 0);
        return fin_flag(w, ctx);
    case B_CEMPTY:
        TRY(cv(w, a));
        TRY(seqv(w, n));
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        return fin_flag(w, ctx);
    case B_CCONS:
        TRY(cv(w, a));
        TRY(cv(w, a2));
        TRY(seqv(w, n));
        E(w, ZL_CONS, 0, 0);
        break;
    case B_CFIRST:
    case B_CSECOND:
        TRY(cv(w, a));
        TRY(seqv(w, n));
        if (op == B_CSECOND) E(w, ZL_CDR, 1, 0);
        clj_first(w);
        break;
    case B_CREST:
    case B_CNEXT:
        TRY(cv(w, a));
        TRY(seqv(w, n));
        E(w, ZL_CDR, 1, 0);
        if (op == B_CNEXT) nil_if_empty(w);
        break;
    case B_CCOUNT:
    case B_CREVERSE:
        TRY(cv(w, a));
        TRY(seqv(w, n));
        TRY(hcall(w, op == B_CCOUNT ? H_LENGTH : H_REVERSE, n));
        if (isnum) *isnum = op == B_CCOUNT;
        break;
    case B_CONJ: /* onto the front of a list, one element after another */
        if (na == 1) {
            TRY(cv(w, a));
            break;
        }
        TRY(args_to_locals(w, a, 0, &base));
        E(w, ZL_LOADF, base, 0);
        TRY(seqv(w, n));
        for (uint32_t i = 1; i < na; i++) {
            E(w, ZL_LOADF, base + i, 0);
            E(w, ZL_SWAP, 0, 0);
            E(w, ZL_CONS, 0, 0);
        }
        w->cur.nloc = saved;
        break;
    case B_CPRINT:
        return clj_print(w, n, head, a, ctx);
    default:
        return lfail(w, n, ZL_E_ARG, "internal: unknown built-in");
    }
    return fin_value(w, ctx);
}

static int bcall(lw_t *w, int32_t n, int bi, int ctx, int *isnum)
{
    const lbi_t *b = &bis[bi];
    int32_t head = N(w, n)->kid, a = N(w, head)->next;
    uint32_t na = count(w, a);
    int op = b->op;
    if ((int) na < b->min || (b->max >= 0 && (int) na > b->max))
        return lfail(w, n, ZL_E_SYNTAX, "wrong number of arguments");
    if (b->canon && !(w->dial == D_CL && op == B_CXR)) { /* Braidwyrm writes first, rest */
        N(w, head)->flags |= F_ALIAS;
        N(w, head)->alias = (uint8_t) (bi + 1);
    }
    int32_t a2 = nx(w, a);
    if (op >= B_CEQ || (CLJ(w) && op == B_NOT)) return clj_bcall(w, n, op, head, a, na, ctx, isnum);
    switch (op) {
    case B_ADD:
    case B_MUL:
        if (na == 0)
            E(w, ZL_PUSH, op == B_ADD ? 0 : 1, 0);
        else {
            TRY(fold(w, op == B_ADD ? ZL_ADD : ZL_MUL, a));
            if (na == 1) { /* (+ x) is x, checked to be a number */
                E(w, ZL_PUSH, op == B_ADD ? 0 : 1, 0);
                E(w, op == B_ADD ? ZL_ADD : ZL_MUL, 0, 0);
            }
        }
        break;
    case B_SUB:
        TRY(fold(w, ZL_SUB, a));
        if (na == 1) E(w, ZL_NEG, 0, 0);
        break;
    case B_DIV:
        if (na == 1) E(w, ZL_PUSH, 1, 0);
        TRY(cv(w, a));
        if (na == 1) E(w, ZL_DIVX, 0, 0);
        for (int32_t e = a2; e >= 0; e = N(w, e)->next) {
            TRY(cv(w, e));
            E(w, ZL_DIVX, 0, 0);
        }
        break;
    case B_MIN:
    case B_MAX: {
        int num;
        TRY(cx(w, a, X_VALUE, &num));
        if (na == 1 && !CLJ(w)) guard_num(w, num); /* Clojure: (min x) is x */
        for (int32_t e = a2; e >= 0; e = N(w, e)->next) {
            TRY(cv(w, e));
            E(w, op == B_MIN ? ZL_MIN : ZL_MAX, 0, 0);
        }
        break;
    }
    case B_ABS:
        TRY(cv(w, a));
        E(w, ZL_ABS, 0, 0);
        break;
    case B_INC:
    case B_DEC:
        TRY(cv(w, a));
        E(w, ZL_PUSH, 1, 0);
        E(w, op == B_INC ? ZL_ADD : ZL_SUB, 0, 0);
        break;
    case B_FLOOR:
    case B_TRUNC:
    case B_CEIL:
        TRY(cv(w, a));
        if (op == B_CEIL) E(w, ZL_NEG, 0, 0);
        if (na == 2)
            TRY(cv(w, a2));
        else
            E(w, ZL_PUSH, 1, 0);
        E(w, op == B_TRUNC ? ZL_DIVT : ZL_DIVF, 0, 0);
        if (op == B_CEIL) E(w, ZL_NEG, 0, 0);
        break;
    case B_MODF:
    case B_MODT:
    case B_EXPT:
        TRY(cv(w, a));
        TRY(cv(w, a2));
        E(w, op == B_MODF ? ZL_MODF : op == B_MODT ? ZL_MODT : ZL_POW, op == B_EXPT, 0);
        break;
    case B_NUMEQ:
        return compare(w, ZL_EQ, a, na, ctx, n);
    case B_NUMNE:
        return compare(w, ZL_NE, a, na, ctx, n);
    case B_LT:
        return compare(w, ZL_LT, a, na, ctx, n);
    case B_GT:
        return compare(w, ZL_GT, a, na, ctx, n);
    case B_LE:
        return compare(w, ZL_LE, a, na, ctx, n);
    case B_GE:
        return compare(w, ZL_GE, a, na, ctx, n);
    case B_ZEROP:
        TRY(cv(w, a));
        E(w, ZL_SGN, 0, 0); /* checks the number; 0 stays 0 */
        E(w, ZL_NOT, 0, 0);
        return fin_flag(w, ctx);
    case B_PLUSP:
    case B_MINUSP:
        TRY(cv(w, a));
        E(w, ZL_PUSH, 0, 0);
        E(w, op == B_PLUSP ? ZL_GT : ZL_LT, 0, 0);
        return fin_flag(w, ctx);
    case B_EVENP:
    case B_ODDP:
        TRY(cv(w, a));
        E(w, ZL_PUSH, 2, 0);
        E(w, ZL_MODF, 0, 0);
        if (op == B_EVENP) E(w, ZL_NOT, 0, 0);
        return fin_flag(w, ctx);
    case B_NUMBERP:
    case B_CONSP:
    case B_NULL:
    case B_ATOM:
        TRY(cv(w, a));
        E(w, ZL_ISA, op == B_NUMBERP ? ZL_ISA_NUMBER : op == B_NULL ? ZL_ISA_NULL : ZL_ISA_PAIR, 0);
        if (op == B_ATOM) E(w, ZL_NOT, 0, 0);
        return fin_flag(w, ctx);
    case B_SYMBOLP:
        TRY(cv(w, a));
        if (w->lisp) { /* NIL and T are symbols too */
            E(w, ZL_DUP, 0, 0);
            E(w, ZL_ISA, ZL_ISA_SYMBOL, 0);
            E(w, ZL_SWAP, 0, 0);
            E(w, ZL_DUP, 0, 0);
            E(w, ZL_ISA, ZL_ISA_NULL, 0);
            E(w, ZL_SWAP, 0, 0);
            E(w, ZL_PUSH, ZL_V_TRUE, 0);
            E(w, ZL_EQ, 0, 0);
            E(w, ZL_OR, 0, 0);
            E(w, ZL_OR, 0, 0);
        } else
            E(w, ZL_ISA, ZL_ISA_SYMBOL, 0);
        return fin_flag(w, ctx);
    case B_LISTP:
        TRY(cv(w, a));
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        E(w, ZL_SWAP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        E(w, ZL_OR, 0, 0);
        return fin_flag(w, ctx);
    case B_NOT:
        TRY(cv(w, a));
        E(w, ZL_PUSH, ZL_V_FALSE, 0);
        E(w, ZL_EQ, 0, 0);
        return fin_flag(w, ctx);
    case B_EQ:
    case B_EQUAL:
        TRY(cv(w, a));
        TRY(cv(w, a2));
        if (op == B_EQ)
            E(w, ZL_EQ, 0, 0);
        else
            TRY(hcall(w, H_EQUAL, n));
        return fin_flag(w, ctx);
    case B_CXR: {
        const char *p = b->canon; /* first rest second third */
        size_t len = 0;
        while (p[len]) len++;
        TRY(cv(w, a));
        for (size_t i = len - 1; i-- > 1;) E(w, p[i] == 'a' ? ZL_CAR : ZL_CDR, w->lisp, 0);
        break;
    }
    case B_CONS:
        TRY(cv(w, a));
        TRY(cv(w, a2));
        E(w, ZL_CONS, 0, 0);
        break;
    case B_LIST:
        for (int32_t e = a; e >= 0; e = N(w, e)->next) TRY(cv(w, e));
        E(w, ZL_PUSH, ZL_V_NIL, 0);
        for (uint32_t i = 0; i < na; i++) E(w, ZL_CONS, 0, 0);
        break;
    case B_LENGTH:
    case B_REVERSE:
        TRY(cv(w, a));
        TRY(hcall(w, op == B_LENGTH ? H_LENGTH : H_REVERSE, n));
        break;
    case B_APPEND:
        if (na == 0) E(w, ZL_PUSH, ZL_V_NIL, 0);
        for (int32_t e = a; e >= 0; e = N(w, e)->next) TRY(cv(w, e));
        for (uint32_t i = 1; i < na; i++) TRY(hcall(w, H_APPEND, n));
        break;
    case B_MEMQ:
    case B_MEMBER:
    case B_ASSQ:
    case B_ASSOC:
        TRY(cv(w, a));
        TRY(cv(w, a2));
        TRY(hcall(w,
                  op == B_MEMQ     ? H_MEMQ
                  : op == B_MEMBER ? H_MEMBER
                  : op == B_ASSQ   ? H_ASSQ
                                   : H_ASSOC,
                  n));
        break;
    case B_SETCAR:
    case B_SETCDR:
        TRY(cv(w, a));
        TRY(cv(w, a2));
        if (w->lisp && ctx != X_EFFECT) { /* rplaca returns the pair */
            E(w, ZL_OVER, 0, 0);
            E(w, ZL_SWAP, 0, 0);
        }
        E(w, op == B_SETCAR ? ZL_SETCAR : ZL_SETCDR, 0, 0);
        if (w->lisp && ctx != X_EFFECT) return fin_value(w, ctx);
        return fin_none(w, ctx, n);
    case B_PRINT:
    case B_PRIN1:
    case B_PRINC:
    case B_DISPLAY:
    case B_WRITE:
    case B_DISPLAYLN:
    case B_RPRINT:
    case B_RPRINTLN:
        return out_form(w, n, op, a, ctx);
    case B_PRINTF:
        return format_form(w, n, a, ctx);
    case B_VOID:
        return fin_none(w, ctx, n);
    case B_RLIST: { /* Racket's first rest second third: the argument must be a list */
        char c0 = w->src[N(w, head)->v];
        TRY(cv(w, a));
        E(w, ZL_DUP, 0, 0);
        TRY(hcall(w, H_LENGTH, n)); /* traps on an improper list */
        E(w, ZL_DROP, 0, 0);
        if (c0 != 'f') E(w, ZL_CDR, 0, 0);
        if (c0 == 't') E(w, ZL_CDR, 0, 0);
        if (c0 != 'r') E(w, ZL_CAR, 0, 0);
        break;
    }
    case B_TERPRI:
    case B_NEWLINE:
        E(w, ZL_NL, 0, 0);
        return fin_none(w, ctx, n);
    case B_FORMAT:
        return format_form(w, n, a, ctx);
    default:
        return lfail(w, n, ZL_E_ARG, "internal: unknown built-in");
    }
    if (isnum) *isnum = op <= B_EXPT || op == B_LENGTH;
    return fin_value(w, ctx);
}

/* ------------------------------------------------------------ special forms */

static int f_if(lw_t *w, int32_t n, int32_t a, uint32_t na, int ctx, int neg)
{
    if (w->dial == D_RKT && na != 3)
        return lfail(w, n, ZL_E_SYNTAX, "if takes a test, a then form and an else form in Racket");
    if (na < 2 || na > 3)
        return lfail(w, n, ZL_E_SYNTAX, "if takes a test, a then form and an optional else form");
    int32_t th = nx(w, a), el = nx(w, th);
    TRY(cx(w, a, X_TEST, 0));
    int32_t j = E(w, neg ? ZL_JNZ : ZL_JZ, 0, 0);
    TRY(cx(w, th, ctx, 0));
    if (el < 0 && ctx == X_EFFECT) {
        patch_here(w, j);
        return ok(w);
    }
    int32_t k = E(w, ZL_JMP, 0, 0);
    patch_here(w, j);
    if (el >= 0)
        TRY(cx(w, el, ctx, 0));
    else
        TRY(fin_none(w, ctx, n));
    patch_here(w, k);
    return ok(w);
}

static int f_ccond(lw_t *w, int32_t n, int32_t a, int ctx);
static int f_clet(lw_t *w, int32_t n, int32_t a, int ctx);

static int f_cond(lw_t *w, int32_t n, int32_t a, int ctx)
{
    if (CLJ(w)) return f_ccond(w, n, a, ctx);
    int32_t end = -1;
    int has_else = 0;
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 1;
    for (int32_t cl = a; cl >= 0; cl = N(w, cl)->next) {
        if (N(w, cl)->kind != K_LIST || N(w, cl)->kid < 0 || (N(w, cl)->flags & F_DOT))
            return lfail(w, cl, ZL_E_SYNTAX, "a cond clause is a list (test form ...)");
        int32_t t = N(w, cl)->kid, bd = N(w, t)->next;
        if (!w->lisp && sym_is(w, bd, "=>"))
            return lfail(w, bd, ZL_E_UNSUPPORTED, "cond clauses with => are not in this subset");
        /* LISP: 'T is written T by the twin, so it is the else clause too */
        if ((!w->lisp && sym_is(w, t, "else")) ||
            (w->lisp &&
             (sym_is(w, t, "t") || (N(w, t)->kind == K_QUOTE && sym_is(w, N(w, t)->kid, "t"))))) {
            if (N(w, t)->kind == K_QUOTE) N(w, t)->flags |= F_UNQ;
            if (!w->lisp && N(w, cl)->next >= 0)
                return lfail(w, cl, ZL_E_SYNTAX, "else must be the last cond clause");
            if (bd < 0) {
                if (!w->lisp) return lfail(w, cl, ZL_E_SYNTAX, "an else clause needs a body");
                E(w, ZL_PUSH, ZL_V_TRUE, 0);
                TRY(fin_value(w, ctx == X_RESULT ? X_VALUE : ctx));
            } else
                TRY(seq(w, bd, ctx, cl));
            end = chain(w, ZL_JMP, end);
            has_else = 1;
            continue;
        }
        if (bd < 0) { /* (test): the value of the test */
            if (ctx == X_EFFECT || ctx == X_TEST) {
                TRY(cx(w, t, X_TEST, 0));
                if (ctx == X_TEST) E(w, ZL_DUP, 0, 0);
                end = chain(w, ZL_JNZ, end);
                if (ctx == X_TEST) E(w, ZL_DROP, 0, 0);
            } else {
                TRY(cv(w, t));
                E(w, ZL_DUP, 0, 0);
                to_flag(w);
                end = chain(w, ZL_JNZ, end);
                E(w, ZL_DROP, 0, 0);
            }
            continue;
        }
        TRY(cx(w, t, X_TEST, 0));
        int32_t j = E(w, ZL_JZ, 0, 0);
        TRY(seq(w, bd, ctx, cl));
        end = chain(w, ZL_JMP, end);
        patch_here(w, j);
    }
    if (!has_else) TRY(fin_none(w, ctx, n));
    chain_patch(w, end);
    return ok(w);
}

static int f_when(lw_t *w, int32_t n, int32_t a, int unless, int ctx)
{
    if (a < 0) return lfail(w, n, ZL_E_SYNTAX, "when and unless need a test");
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 2;
    TRY(cx(w, a, X_TEST, 0));
    int32_t j = E(w, unless ? ZL_JNZ : ZL_JZ, 0, 0);
    TRY(seq(w, N(w, a)->next, ctx, n));
    if (ctx == X_EFFECT) {
        patch_here(w, j);
        return ok(w);
    }
    int32_t k = E(w, ZL_JMP, 0, 0);
    patch_here(w, j);
    TRY(fin_none(w, ctx, n));
    patch_here(w, k);
    return ok(w);
}

static int f_andor(lw_t *w, int32_t a, uint32_t na, int is_or, int ctx)
{
    if (na == 0) {
        E(w, ZL_PUSH, is_or ? (CLJ(w) ? w->NIL : w->F) : w->T, 0);
        return fin_value(w, ctx == X_RESULT ? X_VALUE : ctx);
    }
    int32_t out = -1, end = -1;
    for (int32_t e = a; e >= 0; e = N(w, e)->next) {
        if (N(w, e)->next < 0) {
            TRY(cx(w, e, ctx, 0));
            break;
        }
        if (ctx == X_EFFECT) {
            TRY(cx(w, e, X_TEST, 0));
            end = chain(w, is_or ? ZL_JNZ : ZL_JZ, end);
        } else if (ctx == X_TEST || (!is_or && !CLJ(w))) {
            TRY(cx(w, e, X_TEST, 0));
            out = chain(w, is_or ? ZL_JNZ : ZL_JZ, out);
        } else { /* or: the first true value; Clojure's and: the first false one */
            TRY(cv(w, e));
            E(w, ZL_DUP, 0, 0);
            to_flag(w);
            end = chain(w, is_or ? ZL_JNZ : ZL_JZ, end);
            E(w, ZL_DROP, 0, 0);
        }
    }
    if (out >= 0) {
        int32_t k = E(w, ZL_JMP, 0, 0);
        chain_patch(w, out);
        if (ctx == X_TEST)
            E(w, ZL_PUSH, is_or, 0);
        else
            E(w, ZL_PUSH, w->F, 0);
        patch_here(w, k);
    }
    chain_patch(w, end);
    return ok(w);
}

/* One let binding: LISP admits X, (X) and (X init); Scheme (x init). */
static int binding(lw_t *w, int32_t b, int32_t *name, int32_t *init)
{
    *init = -1;
    if (w->lisp && N(w, b)->kind == K_SYM) {
        *name = b;
        N(w, b)->flags |= F_BARE;
        return ok(w);
    }
    uint32_t k = N(w, b)->kind == K_LIST && !(N(w, b)->flags & F_DOT) ? count(w, N(w, b)->kid) : 0;
    if (k == 2 || (w->lisp && k == 1)) {
        *name = N(w, b)->kid;
        if (k == 2)
            *init = N(w, *name)->next;
        else
            N(w, b)->flags |= F_BARE;
        if (N(w, *name)->kind != K_SYM)
            return lfail(w, *name, ZL_E_SYNTAX, "a variable name is expected");
        return ok(w);
    }
    return lfail(w, b, ZL_E_SYNTAX, "a binding is (name value)");
}

/* A binding or parameter list: a proper list, or LISP's NIL. */
static int binding_list(lw_t *w, int32_t b, int32_t *first)
{
    if (w->lisp && sym_is(w, b, "nil")) {
        N(w, b)->flags |= F_SYNTAX;
        *first = -1;
        return ok(w);
    }
    if (N(w, b)->kind != K_LIST || (N(w, b)->flags & F_DOT))
        return lfail(w, b, ZL_E_SYNTAX, "a list of bindings or parameters is expected");
    if (N(w, b)->kid < 0) N(w, b)->flags |= F_SYNTAX;
    *first = N(w, b)->kid;
    return ok(w);
}

static int f_named_let(lw_t *w, int32_t n, int32_t name, int ctx)
{
    int32_t bl = nx(w, name), first, fid;
    if (bl < 0) return lfail(w, n, ZL_E_SYNTAX, "a named let needs bindings and a body");
    TRY(binding_list(w, bl, &first));
    for (int32_t b = first; b >= 0; b = N(w, b)->next) {
        int32_t nm, init;
        TRY(binding(w, b, &nm, &init));
    }
    TRY(check_binder(w, name));
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 3;
    TRY(add_fn(w, name, first, 1, nx(w, bl), n, w->cur.level + 1, -1, &fid));
    int32_t j = E(w, ZL_JMP, 0, 0);
    uint32_t sc = w->nscope;
    TRY(bind(w, name, B_FN, fid, n));
    TRY(emit_fn(w, fid));
    w->nscope = sc;
    patch_here(w, j);
    return ucall(w, n, fid, first, 1, ctx);
}

static int f_let(lw_t *w, int32_t n, int32_t a, int star, int ctx)
{
    if (CLJ(w)) return f_clet(w, n, a, ctx);
    if (a < 0) return lfail(w, n, ZL_E_SYNTAX, "let needs a list of bindings");
    if (!star && !w->lisp && N(w, a)->kind == K_SYM) return f_named_let(w, n, a, ctx);
    int32_t first;
    TRY(binding_list(w, a, &first));
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 2;
    uint32_t sc = w->nscope, nl = w->cur.nloc, nb = 0;
    for (int32_t b = first; b >= 0; b = N(w, b)->next, nb++) {
        int32_t nm, init;
        TRY(binding(w, b, &nm, &init));
        TRY(check_binder(w, nm));
        if (!star)
            for (int32_t q = first; q != b; q = N(w, q)->next) {
                int32_t qn, qi;
                TRY(binding(w, q, &qn, &qi));
                if (name_eq(w, qn, nm))
                    return lfail(w, nm, ZL_E_SYNTAX, "a name is bound twice in one list");
            }
        if (init >= 0)
            TRY(cv(w, init));
        else
            E(w, ZL_PUSH, ZL_V_NIL, 0);
        if (star) {
            uint32_t off;
            TRY(alloc_local(w, b, &off));
            E(w, ZL_STOREF, off, 0);
            TRY(bind(w, nm, B_VAR, (int32_t) off, b));
        }
    }
    if (!star) {
        uint32_t base = w->cur.nloc, off;
        for (uint32_t i = 0; i < nb; i++) TRY(alloc_local(w, n, &off));
        for (uint32_t i = nb; i-- > 0;) E(w, ZL_STOREF, base + i, 0);
        uint32_t i = 0;
        for (int32_t b = first; b >= 0; b = N(w, b)->next, i++) {
            int32_t nm, init;
            TRY(binding(w, b, &nm, &init));
            TRY(bind(w, nm, B_VAR, (int32_t) (base + i), b));
        }
    }
    TRY(body(w, nx(w, a), ctx, n));
    w->nscope = sc;
    w->cur.nloc = nl;
    return ok(w);
}

static int f_setq(lw_t *w, int32_t n, int32_t a, uint32_t na, int ctx, int scheme_set)
{
    if (scheme_set ? na != 2 : (na & 1))
        return lfail(w, n, ZL_E_SYNTAX,
                     scheme_set ? "set! takes a name and a value"
                                : "setq takes names and values in pairs");
    if (sym_is(w, N(w, n)->kid, "setf")) {
        for (int32_t p = a; p >= 0; p = nx(w, N(w, p)->next))
            if (N(w, p)->kind != K_SYM)
                return lfail(w, p, ZL_E_UNSUPPORTED,
                             "setf of a place other than a variable is not in this subset");
    }
    if (na == 0) return fin_none(w, ctx, n);
    for (int32_t p = a; p >= 0; p = nx(w, N(w, p)->next)) {
        int32_t v = N(w, p)->next;
        if (N(w, p)->kind != K_SYM) return lfail(w, p, ZL_E_SYNTAX, "a variable name is expected");
        TRY(cv(w, v));
        if (N(w, v)->next < 0 && ctx != X_EFFECT && !scheme_set) E(w, ZL_DUP, 0, 0);
        TRY(store_var(w, p));
    }
    if (scheme_set) return fin_none(w, ctx, n);
    return ctx == X_EFFECT ? ok(w) : fin_value(w, ctx);
}

/* incf decf push pop on a variable. */
static int f_modify(lw_t *w, int32_t n, int code, int32_t a, uint32_t na, int ctx, int *isnum)
{
    int32_t place = code == S_PUSH ? nx(w, a) : a;
    if (code == S_PUSH ? na != 2 : code == S_POP ? na != 1 : (na < 1 || na > 2))
        return lfail(w, n, ZL_E_SYNTAX, "wrong number of arguments");
    if (N(w, place)->kind != K_SYM)
        return lfail(w, place, ZL_E_UNSUPPORTED, "only a variable can be modified in this subset");
    int keep = ctx != X_EFFECT;
    if (code == S_POP) {
        TRY(load_var(w, place));
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_CDR, 1, 0);
        TRY(store_var(w, place));
        E(w, ZL_CAR, 1, 0);
        return fin_value(w, ctx);
    }
    if (code == S_PUSH) {
        TRY(cv(w, a));
        TRY(load_var(w, place));
        E(w, ZL_CONS, 0, 0);
    } else {
        TRY(load_var(w, place));
        if (na == 2)
            TRY(cv(w, nx(w, a)));
        else
            E(w, ZL_PUSH, 1, 0);
        E(w, code == S_INCF ? ZL_ADD : ZL_SUB, 0, 0);
        if (isnum) *isnum = 1;
    }
    if (keep) E(w, ZL_DUP, 0, 0);
    TRY(store_var(w, place));
    return keep ? fin_value(w, ctx) : ok(w);
}

static int f_dotimes(lw_t *w, int32_t n, int32_t a, int ctx)
{
    uint32_t k = a >= 0 && N(w, a)->kind == (CLJ(w) ? K_VEC : K_LIST) && !(N(w, a)->flags & F_DOT)
                     ? count(w, N(w, a)->kid)
                     : 0;
    if (CLJ(w) && k != 2) return lfail(w, n, ZL_E_SYNTAX, "dotimes takes [name count] and a body");
    if (k < 2 || k > 3)
        return lfail(w, n, ZL_E_SYNTAX, "dotimes takes (variable count [result]) and a body");
    int32_t var = N(w, a)->kid, lim = N(w, var)->next, res = N(w, lim)->next;
    TRY(check_binder(w, var));
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 2;
    uint32_t sc = w->nscope, nl = w->cur.nloc, ol, oi;
    TRY(cv(w, lim));
    TRY(alloc_local(w, n, &ol));
    TRY(alloc_local(w, n, &oi));
    E(w, ZL_STOREF, ol, 0);
    E(w, ZL_PUSH, 0, 0);
    E(w, ZL_STOREF, oi, 0);
    TRY(bind(w, var, B_VAR, (int32_t) oi, var));
    int32_t top = here(w);
    E(w, ZL_LOADF, oi, 0);
    E(w, ZL_LOADF, ol, 0);
    E(w, ZL_LT, 0, 0);
    int32_t j = E(w, ZL_JZ, 0, 0);
    TRY(loop_body(w, nx(w, a)));
    E(w, ZL_LOADF, oi, 0);
    E(w, ZL_PUSH, 1, 0);
    E(w, ZL_ADD, 0, 0);
    E(w, ZL_STOREF, oi, 0);
    E(w, ZL_JMP, top, 0);
    patch_here(w, j);
    if (res >= 0)
        TRY(cx(w, res, ctx, 0));
    else
        TRY(fin_none(w, ctx, n));
    w->nscope = sc;
    w->cur.nloc = nl;
    return ok(w);
}

static int f_do(lw_t *w, int32_t n, int32_t a, int ctx)
{
    int32_t first, endc = nx(w, a);
    if (endc < 0)
        return lfail(w, n, ZL_E_SYNTAX, "do takes bindings, (test result ...) and a body");
    TRY(binding_list(w, a, &first));
    if (N(w, endc)->kind != K_LIST || N(w, endc)->kid < 0 || (N(w, endc)->flags & F_DOT))
        return lfail(w, endc, ZL_E_SYNTAX, "do needs (test result ...)");
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 3;
    uint32_t sc = w->nscope, nl = w->cur.nloc, nb = 0, base, off;
    for (int32_t b = first; b >= 0; b = N(w, b)->next, nb++) {
        uint32_t k =
            N(w, b)->kind == K_LIST && !(N(w, b)->flags & F_DOT) ? count(w, N(w, b)->kid) : 0;
        if (k < 2 || k > 3) return lfail(w, b, ZL_E_SYNTAX, "a do binding is (name init [step])");
        int32_t nm = N(w, b)->kid;
        TRY(check_binder(w, nm));
        for (int32_t q = first; q != b; q = N(w, q)->next)
            if (name_eq(w, N(w, q)->kid, nm))
                return lfail(w, nm, ZL_E_SYNTAX, "a name is bound twice in one list");
        TRY(cv(w, N(w, nm)->next));
    }
    base = w->cur.nloc;
    for (uint32_t i = 0; i < nb; i++) TRY(alloc_local(w, n, &off));
    for (uint32_t i = nb; i-- > 0;) E(w, ZL_STOREF, base + i, 0);
    uint32_t i = 0;
    for (int32_t b = first; b >= 0; b = N(w, b)->next, i++)
        TRY(bind(w, N(w, b)->kid, B_VAR, (int32_t) (base + i), b));
    int32_t top = here(w);
    TRY(cx(w, N(w, endc)->kid, X_TEST, 0));
    int32_t j = E(w, ZL_JNZ, 0, 0);
    TRY(loop_body(w, nx(w, endc)));
    uint32_t sbase = w->nscr;
    i = 0;
    for (int32_t b = first; b >= 0; b = N(w, b)->next, i++) {
        int32_t step = nx(w, N(w, N(w, b)->kid)->next);
        if (step < 0) continue;
        TRY(cv(w, step));
        if (w->nscr >= L_NODES) return lfail(w, n, ZL_E_LIMIT, "too many do variables");
        w->scr[w->nscr++] = (int32_t) i;
    }
    while (w->nscr > sbase)
        E(w, ZL_STOREF, base + (uint32_t) w->scr[--w->nscr], 0); /* in reverse */
    E(w, ZL_JMP, top, 0);
    patch_here(w, j);
    int32_t res = N(w, N(w, endc)->kid)->next;
    if (res >= 0)
        TRY(seq(w, res, ctx, n));
    else
        TRY(fin_none(w, ctx, n));
    w->nscope = sc;
    w->cur.nloc = nl;
    return ok(w);
}

/* The functions (name (params) body...) of a labels form, bound before
 * any is compiled so they can call each other. */
static int local_fns(lw_t *w, int32_t first)
{
    for (int32_t d = first; d >= 0; d = N(w, d)->next) {
        int32_t name, params, bd, fid;
        {
            if (N(w, d)->kind != K_LIST || count(w, N(w, d)->kid) < 2 || (N(w, d)->flags & F_DOT))
                return lfail(w, d, ZL_E_SYNTAX,
                             "a labels function is (name (parameters) body ...)");
            name = N(w, d)->kid;
            TRY(binding_list(w, N(w, name)->next, &params));
            bd = N(w, N(w, name)->next)->next;
            N(w, d)->flags |= F_BLOCK;
            N(w, d)->k = 2;
            if (N(w, name)->kind != K_SYM)
                return lfail(w, name, ZL_E_SYNTAX, "a function name is expected");
            if (is_standard(w, name) || is_const(w, name))
                return lfail(w, name, ZL_E_UNSUPPORTED,
                             "redefining a standard function is not in this subset");
            for (int32_t q = first; q != d; q = N(w, q)->next)
                if (name_eq(w, N(w, q)->kid, name))
                    return lfail(w, name, ZL_E_SYNTAX, "a function is defined twice");
            TRY(add_fn(w, name, params, 0, bd, d, w->cur.level + 1, -1, &fid));
            N(w, d)->v = fid;
            TRY(bind(w, name, B_FN, fid, d));
        }
    }
    return ok(w);
}

static int f_labels(lw_t *w, int32_t n, int32_t a, int ctx)
{
    int32_t first;
    if (a < 0) return lfail(w, n, ZL_E_SYNTAX, "labels needs a list of functions");
    TRY(binding_list(w, a, &first));
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 2;
    uint32_t sc = w->nscope;
    TRY(local_fns(w, first));
    for (int32_t d = first; d >= 0; d = N(w, d)->next) {
        int32_t j = E(w, ZL_JMP, 0, 0);
        TRY(emit_fn(w, (int32_t) N(w, d)->v));
        patch_here(w, j);
    }
    TRY(seq(w, nx(w, a), ctx, n));
    w->nscope = sc;
    return ok(w);
}

/* Braidwyrm's spellings (alias codes from A_CL), and Dialectwyrm's '(). */
enum { A_CL = 200, A_EMPTY = 210 };
static const char *const cl_canon[] = {"setf", "first", "rest", "second", "third", "otherwise"};

static void set_alias(lw_t *w, int32_t h, int code)
{
    N(w, h)->flags |= F_ALIAS;
    N(w, h)->alias = (uint8_t) code;
}

/* A case key: a number or a symbol (compared with EQL, as CASE does). */
static int case_key(lw_t *w, int32_t k)
{
    int kind = N(w, k)->kind;
    if (kind == K_NUM || (kind == K_SYM && !sym_is(w, k, "nil"))) return ok(w);
    return lfail(w, k, ZL_E_UNSUPPORTED,
                 "case keys other than numbers and symbols (and NIL) are not in this subset");
}

/* Common Lisp (case key (keys form ...) ... [(otherwise form ...)]). */
static int f_case(lw_t *w, int32_t n, int32_t a, int ctx)
{
    if (a < 0) return lfail(w, n, ZL_E_SYNTAX, "case needs a key form");
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 2;
    uint32_t nl = w->cur.nloc, off;
    int32_t end = -1;
    int has_else = 0;
    TRY(cv(w, a));
    TRY(alloc_local(w, a, &off));
    E(w, ZL_STOREF, off, 0);
    for (int32_t cl = N(w, a)->next; cl >= 0; cl = N(w, cl)->next) {
        if (N(w, cl)->kind != K_LIST || N(w, cl)->kid < 0 || (N(w, cl)->flags & F_DOT))
            return lfail(w, cl, ZL_E_SYNTAX, "a case clause is a list (keys form ...)");
        int32_t keys = N(w, cl)->kid, bd = N(w, keys)->next;
        if (sym_is(w, keys, "t") || sym_is(w, keys, "otherwise")) {
            if (sym_is(w, keys, "t")) set_alias(w, keys, A_CL + 5);
            if (N(w, cl)->next >= 0)
                return lfail(w, keys, ZL_E_SYNTAX, "otherwise must be the last case clause");
            TRY(seq(w, bd, ctx, cl));
            has_else = 1;
            break;
        }
        int32_t hit = -1;
        if (N(w, keys)->kind == K_LIST) {
            if (N(w, keys)->kid < 0 || (N(w, keys)->flags & F_DOT))
                return lfail(w, keys, ZL_E_UNSUPPORTED,
                             "an empty or dotted key list is not in this subset");
            for (int32_t k = N(w, keys)->kid; k >= 0; k = N(w, k)->next) {
                TRY(case_key(w, k));
                E(w, ZL_LOADF, off, 0);
                TRY(datum(w, k));
                E(w, ZL_EQ, 0, 0);
                hit = chain(w, ZL_JNZ, hit);
            }
        } else {
            TRY(case_key(w, keys));
            E(w, ZL_LOADF, off, 0);
            TRY(datum(w, keys));
            E(w, ZL_EQ, 0, 0);
            hit = chain(w, ZL_JNZ, hit);
        }
        int32_t miss = E(w, ZL_JMP, 0, 0);
        chain_patch(w, hit);
        TRY(seq(w, bd, ctx, cl));
        end = chain(w, ZL_JMP, end);
        patch_here(w, miss);
    }
    if (!has_else) TRY(fin_none(w, ctx, n));
    chain_patch(w, end);
    w->cur.nloc = nl;
    return ok(w);
}

/* Clojure: a vector of names and values in pairs. */
static int vec_pairs(lw_t *w, int32_t n, int32_t a, const char *what)
{
    if (a < 0 || N(w, a)->kind != K_VEC) return lfail(w, a < 0 ? n : a, ZL_E_SYNTAX, what);
    if (count(w, N(w, a)->kid) & 1)
        return lfail(w, a, ZL_E_SYNTAX, "a binding vector holds names and values in pairs");
    return ok(w);
}

/* Bind the names of [name value ...] in turn, each value seeing the names before it. */
static int bind_pairs(lw_t *w, int32_t first)
{
    for (int32_t p = first; p >= 0; p = nx(w, nx(w, p))) {
        uint32_t off;
        TRY(check_binder(w, p));
        TRY(cv(w, nx(w, p)));
        TRY(alloc_local(w, p, &off));
        E(w, ZL_STOREF, off, 0);
        TRY(bind(w, p, B_VAR, (int32_t) off, p));
    }
    return ok(w);
}

/* Clojure (let [name value ...] body ...). */
static int f_clet(lw_t *w, int32_t n, int32_t a, int ctx)
{
    TRY(vec_pairs(w, n, a, "let takes a vector of bindings [name value ...] and a body"));
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 2;
    uint32_t sc = w->nscope, nl = w->cur.nloc;
    TRY(bind_pairs(w, N(w, a)->kid));
    TRY(seq(w, nx(w, a), ctx, n));
    w->nscope = sc;
    w->cur.nloc = nl;
    return ok(w);
}

/* Clojure (loop [name value ...] body ...): the names are bound as by let,
 * then a local function of them runs the body, and recur in the body's
 * tail position jumps back to its start with new values. */
static int f_loop(lw_t *w, int32_t n, int32_t a, int ctx)
{
    int32_t fid;
    TRY(vec_pairs(w, n, a, "loop takes a vector of bindings [name value ...] and a body"));
    N(w, n)->flags |= F_BLOCK;
    N(w, n)->k = 2;
    uint32_t sc = w->nscope, nl = w->cur.nloc;
    TRY(bind_pairs(w, N(w, a)->kid));
    TRY(add_fn(w, N(w, n)->kid, N(w, a)->kid, 2, nx(w, a), n, w->cur.level + 1, -1, &fid));
    int32_t j = E(w, ZL_JMP, 0, 0);
    TRY(emit_fn(w, fid));
    patch_here(w, j);
    E(w, ZL_LINK, w->cur.level - (w->fns[fid].level - 1u), 0);
    for (int32_t p = N(w, a)->kid; p >= 0; p = nx(w, nx(w, p))) TRY(load_var(w, p));
    TRY(add_fix(w, E(w, ZL_CALL, 0, 0), fid, n));
    w->nscope = sc;
    w->cur.nloc = nl;
    return call_done(w, fid, ctx, n);
}

/* Clojure (recur value ...): only in the tail position of a loop or a defn. */
static int f_recur(lw_t *w, int32_t n, int32_t a, uint32_t na, int ctx)
{
    if (ctx != X_RESULT || w->cur.fid < 0)
        return lfail(w, n, ZL_E_SYNTAX,
                     "recur is admitted only in the tail position of a loop or a defn");
    lfn_t *f = &w->fns[w->cur.fid];
    if (na != f->nparams)
        return lfail(w, n, ZL_E_SYNTAX, "recur needs a value for each name of its loop or defn");
    for (int32_t e = a; e >= 0; e = N(w, e)->next) TRY(cv(w, e));
    for (uint32_t i = f->nparams; i-- > 0;) E(w, ZL_STOREF, i, 0);
    E(w, ZL_JMP, f->entry + 1, 0);
    return ok(w);
}

/* Clojure (cond test form ... [:else form]). */
static int f_ccond(lw_t *w, int32_t n, int32_t a, int ctx)
{
    int32_t end = -1;
    int has_else = 0;
    if (count(w, a) & 1) return lfail(w, n, ZL_E_SYNTAX, "cond takes tests and forms in pairs");
    N(w, n)->flags |= F_PAIRS;
    for (int32_t t = a; t >= 0; t = nx(w, nx(w, t))) {
        int32_t e = nx(w, t);
        if (sym_is(w, t, ":else")) {
            if (nx(w, e) >= 0)
                return lfail(w, t, ZL_E_UNSUPPORTED, "clauses after :else are not in this subset");
            TRY(cx(w, e, ctx, 0));
            has_else = 1;
            break;
        }
        TRY(cx(w, t, X_TEST, 0));
        int32_t j = E(w, ZL_JZ, 0, 0);
        TRY(cx(w, e, ctx, 0));
        end = chain(w, ZL_JMP, end);
        patch_here(w, j);
    }
    if (!has_else) TRY(fin_none(w, ctx, n));
    chain_patch(w, end);
    return ok(w);
}

static int special(lw_t *w, int32_t n, int si, int ctx, int *isnum)
{
    int32_t h = N(w, n)->kid, a = N(w, h)->next;
    uint32_t na = count(w, a);
    if (w->dial == D_CL) {
        if (sym_is(w, h, "setq")) set_alias(w, h, A_CL);
    } else if (specs[si].canon)
        set_alias(w, h, 128 + si);
    switch (specs[si].code) {
    case S_CASE:
        return f_case(w, n, a, ctx);
    case S_QUOTE:
        return lfail(w, n, ZL_E_SYNTAX, "quote takes one datum");
    case S_IF:
    case S_IFNOT:
        return f_if(w, n, a, na, ctx, specs[si].code == S_IFNOT);
    case S_LOOP:
        return f_loop(w, n, a, ctx);
    case S_RECUR:
        return f_recur(w, n, a, na, ctx);
    case S_COND:
        return f_cond(w, n, a, ctx);
    case S_WHEN:
    case S_UNLESS:
        return f_when(w, n, a, specs[si].code == S_UNLESS, ctx);
    case S_AND:
    case S_OR:
        return f_andor(w, a, na, specs[si].code == S_OR, ctx);
    case S_BEGIN:
        N(w, n)->flags |= F_BLOCK;
        N(w, n)->k = 1;
        if (!w->lisp && !CLJ(w) && a < 0)
            return lfail(w, n, ZL_E_SYNTAX, "(begin) needs an expression");
        return seq(w, a, ctx, n);
    case S_LET:
    case S_LETSTAR:
        return f_let(w, n, a, specs[si].code == S_LETSTAR, ctx);
    case S_SETQ:
    case S_SET:
        return f_setq(w, n, a, na, ctx, specs[si].code == S_SET);
    case S_INCF:
    case S_DECF:
    case S_PUSH:
    case S_POP:
        return f_modify(w, n, specs[si].code, a, na, ctx, isnum);
    case S_DOTIMES:
        return f_dotimes(w, n, a, ctx);
    case S_DO:
        return f_do(w, n, a, ctx);
    case S_LABELS:
        return f_labels(w, n, a, ctx);
    case S_LAMBDA:
        return lfail(w, n, ZL_E_UNSUPPORTED, why[R_PROC]);
    case S_DEFUN:
    case S_DEFVAR:
        return lfail(w, n, ZL_E_UNSUPPORTED,
                     CLJ(w) ? "def and defn are admitted only at the top level"
                            : "defun and defvar are admitted only at the top level");
    default:
        return lfail(w, n, ZL_E_SYNTAX,
                     "define is admitted only at the top level or at the start of a body");
    }
}

static int call(lw_t *w, int32_t n, int ctx, int *isnum)
{
    int32_t h = N(w, n)->kid;
    int si = find_spec(w, h);
    if (si >= 0) return special(w, n, si, ctx, isnum);
    int32_t b = find_scope(w, h, 1);
    if (b >= 0) {
        if (w->scope[b].kind != B_FN)
            return lfail(w, h, ZL_E_UNSUPPORTED,
                         "calling a variable: procedures are not values in this subset");
        return ucall(w, n, w->scope[b].v, N(w, h)->next, 0, ctx);
    }
    int32_t f = find_topfn(w, h);
    if (f >= 0) return ucall(w, n, f, N(w, h)->next, 0, ctx);
    if (!w->lisp && find_glob(w, h) >= 0)
        return lfail(w, h, ZL_E_UNSUPPORTED,
                     "calling a variable: procedures are not values in this subset");
    int bi = find_bi(w, h);
    if (bi >= 0) return bcall(w, n, bi, ctx, isnum);
    if (is_cxr(w, h)) {
        int32_t a = N(w, h)->next;
        if (count(w, a) != 1) return lfail(w, n, ZL_E_SYNTAX, "wrong number of arguments");
        TRY(cv(w, a));
        size_t len = N(w, h)->len;
        const char *p = w->src + N(w, h)->v;
        for (size_t i = len - 1; i-- > 1;)
            E(w, lc(w, (unsigned char) p[i]) == 'a' ? ZL_CAR : ZL_CDR, w->lisp, 0);
        if (w->dial == D_CL) /* Braidwyrm: car cdr cadr caddr are first rest second third */
            for (int k = 1; k <= 4; k++)
                if (sym_is(w, h, (const char *const[]){"car", "cdr", "cadr", "caddr"}[k - 1]))
                    set_alias(w, h, A_CL + k);
        return fin_value(w, ctx);
    }
    int r = find_refused(w, h);
    if (r >= 0) return lfail(w, h, ZL_E_UNSUPPORTED, why[refused[r].why]);
    return lfail(w, h, ZL_E_UNDEFINED, "undefined function");
}

static int cx(lw_t *w, int32_t n, int ctx, int *isnum)
{
    lnode_t *x = N(w, n);
    uint32_t saved = w->c->line;
    int s = ZL_OK;
    if (isnum) *isnum = 0;
    if (++w->depth > 2 * ZL_MAX_DEPTH + 8)
        return lfail(w, n, ZL_E_LIMIT, "forms nested too deeply");
    w->c->line = x->line;
    switch (x->kind) {
    case K_NUM:
        E(w, ZL_PUSH, x->v, 0);
        if (isnum) *isnum = 1;
        s = fin_value(w, ctx);
        break;
    case K_TRUE:
    case K_FALSE:
        E(w, ZL_PUSH, x->kind == K_TRUE ? ZL_V_TRUE : ZL_V_FALSE, 0);
        s = fin_value(w, ctx);
        break;
    case K_STR:
        s = lfail(w, n, ZL_E_UNSUPPORTED, why[R_STRING]);
        break;
    case K_QUOTE: {
        int32_t d = x->kid;
        int k = N(w, d)->kind;
        if (k == K_NUM || k == K_TRUE || k == K_FALSE ||
            (w->lisp && (is_empty(w, d) || sym_is(w, d, "t"))) || (CLJ(w) && is_const(w, d)))
            x->flags |= F_UNQ;
        if (k == K_NUM && isnum) *isnum = 1;
        if ((s = datum(w, d)) == ZL_OK) s = fin_value(w, ctx);
        break;
    }
    case K_VEC:
        s = lfail(w, n, ZL_E_UNSUPPORTED, "vectors are not values in this subset");
        break;
    case K_SYM:
        if (CLJ(w) && sym_is(w, n, ":else")) {
            s = lfail(w, n, ZL_E_UNSUPPORTED,
                      "keywords are not in this subset (only :else, in cond)");
            break;
        }
        if (sym_is(w, n, w->lisp ? "nil" : "")) {
            E(w, ZL_PUSH, ZL_V_NIL, 0);
        } else if (CLJ(w) && is_const(w, n)) {
            E(w, ZL_PUSH, clj_const(w, n), 0);
        } else if (w->lisp && sym_is(w, n, "t")) {
            E(w, ZL_PUSH, ZL_V_TRUE, 0);
        } else if (w->dial == D_RKT &&
                   (sym_is(w, n, "null") || (!w->rbase && sym_is(w, n, "empty"))) &&
                   find_scope(w, n, 0) < 0 && find_glob(w, n) < 0) {
            E(w, ZL_PUSH, ZL_V_NIL, 0); /* Dialectwyrm writes '() */
            set_alias(w, n, A_EMPTY);
        } else if ((s = load_var(w, n)) != ZL_OK)
            break;
        s = fin_value(w, ctx);
        break;
    case K_LIST:
        if (x->kid < 0) {
            if (!w->lisp && !CLJ(w)) {
                s = lfail(w, n, ZL_E_SYNTAX, "() must be quoted in Scheme: '()");
                break;
            }
            E(w, ZL_PUSH, ZL_V_NIL, 0);
            s = fin_value(w, ctx);
            break;
        }
        if (x->flags & F_DOT) {
            s = lfail(w, n, ZL_E_SYNTAX, "a dotted list is not a form");
            break;
        }
        if (N(w, x->kid)->kind != K_SYM) {
            int hk = N(w, x->kid)->kind;
            s = lfail(w, x->kid, hk == K_LIST || hk == K_VEC ? ZL_E_UNSUPPORTED : ZL_E_SYNTAX,
                      hk == K_LIST  ? why[R_PROC]
                      : hk == K_VEC ? "vectors are not values in this subset"
                                    : "a function name is expected");
            break;
        }
        if (CLJ(w) && sym_is(w, x->kid, ":else")) {
            s = lfail(w, x->kid, ZL_E_UNSUPPORTED,
                      "keywords are not in this subset (only :else, in cond)");
            break;
        }
        s = call(w, n, ctx, isnum);
        break;
    default:
        s = lfail(w, n, ZL_E_SYNTAX, "not a form");
        break;
    }
    w->depth--;
    w->c->line = saved;
    return s ? s : ok(w);
}

/* ------------------------------------------------------------ bodies and functions */

/* Scheme (define (name p ...) body ...) or (define name (lambda (p ...) body ...)). */
static int fn_define(lw_t *w, int32_t d, int32_t *name, int32_t *params, int32_t *bd)
{
    int32_t h = N(w, N(w, d)->kid)->next;
    if (h < 0) return 0;
    if (N(w, h)->kind == K_LIST && N(w, h)->kid >= 0) {
        if (N(w, h)->flags & F_DOT) return -1;
        *name = N(w, h)->kid;
        *params = N(w, *name)->next;
        *bd = N(w, h)->next;
        return 1;
    }
    int32_t v = N(w, h)->next;
    if (N(w, h)->kind == K_SYM && v >= 0 && N(w, v)->next < 0 && N(w, v)->kind == K_LIST &&
        sym_is(w, N(w, v)->kid, "lambda")) {
        int32_t pl = N(w, N(w, v)->kid)->next;
        if (pl < 0 || N(w, pl)->kind != K_LIST || (N(w, pl)->flags & F_DOT)) return -1;
        *name = h;
        *params = N(w, pl)->kid;
        *bd = N(w, pl)->next;
        N(w, d)->flags |= F_LAMDEF;
        return 1;
    }
    return 0;
}

static int is_define(lw_t *w, int32_t e)
{
    return (w->dial & DS) && N(w, e)->kind == K_LIST && sym_is(w, N(w, e)->kid, "define");
}

static int body(lw_t *w, int32_t first, int ctx, int32_t at)
{
    uint32_t sc = w->nscope, nl = w->cur.nloc;
    int32_t e = first;
    /* Scheme internal defines: procedures are bound first, so they can call each other */
    for (int32_t d = first; d >= 0 && is_define(w, d); d = N(w, d)->next) {
        int32_t name, params, bd, fid;
        int k = fn_define(w, d, &name, &params, &bd);
        if (k < 0)
            return lfail(w, d, ZL_E_UNSUPPORTED, "variadic procedures are not in this subset");
        if (!k) continue;
        if (N(w, name)->kind != K_SYM)
            return lfail(w, name, ZL_E_SYNTAX, "a procedure name is expected");
        TRY(check_binder(w, name));
        if (bd < 0) return lfail(w, d, ZL_E_SYNTAX, "a body needs at least one expression");
        N(w, d)->flags |= F_BLOCK;
        N(w, d)->k = 2;
        for (uint32_t i = sc; i < w->nscope; i++)
            if (name_eq(w, w->scope[i].name, name))
                return lfail(w, name, ZL_E_SYNTAX, "a name is defined twice in one body");
        TRY(add_fn(w, name, params, 0, bd, d, w->cur.level + 1, -1, &fid));
        N(w, d)->v = fid;
        TRY(bind(w, name, B_FN, fid, d));
    }
    for (; e >= 0 && is_define(w, e); e = N(w, e)->next) {
        int32_t name, params, bd;
        if (fn_define(w, e, &name, &params, &bd) > 0) {
            int32_t j = E(w, ZL_JMP, 0, 0);
            TRY(emit_fn(w, (int32_t) N(w, e)->v));
            patch_here(w, j);
            continue;
        }
        int32_t nm = N(w, N(w, e)->kid)->next;
        uint32_t off;
        if (count(w, N(w, e)->kid) != 3)
            return lfail(w, e, ZL_E_SYNTAX, "define takes a name and a value");
        TRY(check_binder(w, nm));
        for (uint32_t i = sc; i < w->nscope; i++)
            if (name_eq(w, w->scope[i].name, nm))
                return lfail(w, nm, ZL_E_SYNTAX, "a name is defined twice in one body");
        TRY(cv(w, N(w, nm)->next));
        TRY(alloc_local(w, e, &off));
        E(w, ZL_STOREF, off, 0);
        TRY(bind(w, nm, B_VAR, (int32_t) off, e));
    }
    TRY(seq(w, e, ctx, at));
    w->nscope = sc;
    w->cur.nloc = nl;
    return ok(w);
}

static int emit_fn(lw_t *w, int32_t fid)
{
    lfn_t *f = &w->fns[fid];
    uint32_t sc = w->nscope, saved_line = w->c->line;
    int32_t ofid = w->cur.fid;
    uint32_t olevel = w->cur.level, onloc = w->cur.nloc, omax = w->cur.maxloc;
    w->c->line = N(w, f->at)->line;
    w->cur.fid = fid;
    w->cur.level = f->level;
    w->cur.nloc = w->cur.maxloc = f->nparams;
    f->entry = E(w, ZL_ENTER, 0, f->nparams);
    uint32_t i = 0;
    for (int32_t p = f->params; p >= 0; p = pnext(w, p, f->pkind), i++)
        TRY(bind(w, pname(w, p, f->pkind), B_VAR, (int32_t) i, p));
    TRY(body(w, f->body, X_RESULT, f->at));
    E(w, ZL_LEAVE, 0, 0);
    E(w, ZL_RET, 0, 0);
    if (f->entry >= 0) w->c->p->code[f->entry].a = w->cur.maxloc;
    w->nscope = sc;
    w->cur.fid = ofid;
    w->cur.level = olevel;
    w->cur.nloc = onloc;
    w->cur.maxloc = omax;
    w->c->line = saved_line;
    return ok(w);
}

/* ------------------------------------------------------------ top level */

/* Clojure (defn name [params] body ...) or (def name value), top-level form i. */
static int clj_def(lw_t *w, int32_t n, uint32_t i)
{
    int32_t h = N(w, n)->kid, name = N(w, h)->next, fid;
    int defn = sym_is(w, h, "defn");
    uint32_t k = count(w, h);
    if (name < 0 || N(w, name)->kind != K_SYM)
        return lfail(w, name < 0 ? n : name, ZL_E_SYNTAX, "a name is expected");
    if (is_standard(w, name) || is_const(w, name) || sym_is(w, name, ":else") ||
        sym_is(w, name, "&"))
        return lfail(w, name, ZL_E_UNSUPPORTED, "redefining a standard name is not in this subset");
    if (find_glob(w, name) >= 0 || find_topfn(w, name) >= 0)
        return lfail(w, name, ZL_E_UNSUPPORTED, "redefining a name is not in this subset");
    int32_t v = N(w, name)->next;
    if (defn) {
        if (v >= 0 && N(w, v)->kind == K_STR)
            return lfail(w, v, ZL_E_UNSUPPORTED, "docstrings are not in this subset");
        if (v >= 0 && N(w, v)->kind == K_LIST)
            return lfail(w, v, ZL_E_UNSUPPORTED, "multi-arity functions are not in this subset");
        if (v < 0 || N(w, v)->kind != K_VEC)
            return lfail(w, v < 0 ? n : v, ZL_E_SYNTAX,
                         "defn takes a name, [parameters] and a body");
        N(w, n)->flags |= F_BLOCK;
        N(w, n)->k = 3;
        TRY(add_fn(w, name, N(w, v)->kid, 0, N(w, v)->next, n, 1, (int32_t) i, &fid));
        w->fkind[i] = T_FN;
        w->fref[i] = fid;
        return ok(w);
    }
    if (k == 2) return lfail(w, n, ZL_E_UNSUPPORTED, "def without a value is not in this subset");
    if (k == 4 && N(w, v)->kind == K_STR)
        return lfail(w, v, ZL_E_UNSUPPORTED, "docstrings are not in this subset");
    if (k != 3) return lfail(w, n, ZL_E_SYNTAX, "def takes a name and a value");
    if (w->nglob >= L_GLOB) return lfail(w, n, ZL_E_LIMIT, "too many global variables");
    int32_t slot = zl_slots(w->c, 1);
    if (slot < 0) return ok(w);
    w->glob[w->nglob].name = name;
    w->glob[w->nglob].top = (int32_t) i;
    w->glob[w->nglob].slot = slot;
    w->fkind[i] = T_VAR;
    w->fref[i] = (int32_t) w->nglob++;
    return ok(w);
}

static int collect(lw_t *w)
{
    for (uint32_t i = 0; i < w->nforms; i++) {
        int32_t n = w->form[i], h = N(w, n)->kind == K_LIST ? N(w, n)->kid : -1;
        w->fkind[i] = T_FORM;
        if (h < 0) continue;
        int32_t name = N(w, h)->next, fid;
        if (CLJ(w) && (sym_is(w, h, "defn") || sym_is(w, h, "def"))) {
            TRY(clj_def(w, n, i));
            continue;
        }
        if (w->lisp && sym_is(w, h, "defun")) {
            if (count(w, h) < 3 || (N(w, n)->flags & F_DOT))
                return lfail(w, n, ZL_E_SYNTAX, "defun takes a name, parameters and a body");
            if (N(w, name)->kind != K_SYM)
                return lfail(w, name, ZL_E_SYNTAX, "a function name is expected");
            if (is_standard(w, name) || is_const(w, name))
                return lfail(w, name, ZL_E_UNSUPPORTED,
                             "redefining a standard function is not in this subset");
            if (find_topfn(w, name) >= 0)
                return lfail(w, name, ZL_E_SYNTAX, "a function is defined twice");
            int32_t params;
            TRY(binding_list(w, N(w, name)->next, &params));
            N(w, n)->flags |= F_BLOCK;
            N(w, n)->k = 3;
            TRY(add_fn(w, name, params, 0, N(w, N(w, name)->next)->next, n, 1, (int32_t) i, &fid));
            w->fkind[i] = T_FN;
            w->fref[i] = fid;
        } else if ((w->lisp && (sym_is(w, h, "defvar") || sym_is(w, h, "defparameter"))) ||
                   ((w->dial & DS) && sym_is(w, h, "define"))) {
            int32_t params, bd;
            int k = w->lisp ? 0 : fn_define(w, n, &name, &params, &bd);
            if (k < 0)
                return lfail(w, n, ZL_E_UNSUPPORTED, "variadic procedures are not in this subset");
            if (k == 0 && count(w, h) != 3) {
                if (w->lisp && count(w, h) == 2)
                    return lfail(w, n, ZL_E_UNSUPPORTED,
                                 "defvar without a value is not in this subset");
                return lfail(w, n, ZL_E_SYNTAX,
                             w->lisp ? "defvar takes a name and a value"
                                     : "define takes a name and a value");
            }
            if ((N(w, n)->flags & F_DOT) || N(w, name)->kind != K_SYM)
                return lfail(w, name, ZL_E_SYNTAX, "a name is expected");
            if (w->lisp ? (is_standard(w, name) || is_const(w, name)) : find_spec(w, name) >= 0)
                return lfail(w, name, ZL_E_UNSUPPORTED,
                             w->lisp ? "redefining a standard name is not in this subset"
                                     : "redefining a syntax keyword is not in this subset");
            if (find_glob(w, name) >= 0 || (!w->lisp && find_topfn(w, name) >= 0))
                return lfail(w, name, ZL_E_SYNTAX, "a name is defined twice");
            if (k) {
                if (bd < 0) return lfail(w, n, ZL_E_SYNTAX, "a body needs at least one expression");
                N(w, n)->flags |= F_BLOCK;
                N(w, n)->k = 2;
                TRY(add_fn(w, name, params, 0, bd, n, 1, (int32_t) i, &fid));
                w->fkind[i] = T_FN;
                w->fref[i] = fid;
                continue;
            }
            if (w->nglob >= L_GLOB) return lfail(w, n, ZL_E_LIMIT, "too many global variables");
            int32_t slot = zl_slots(w->c, 1);
            if (slot < 0) return ok(w);
            w->glob[w->nglob].name = name;
            w->glob[w->nglob].top = (int32_t) i;
            w->glob[w->nglob].slot = slot;
            w->fkind[i] = T_VAR;
            w->fref[i] = (int32_t) w->nglob++;
        }
    }
    return ok(w);
}

/* Every function and global a node may reach, by name (over-approximate). */
static void scan(lw_t *w, int32_t n, uint32_t *calls, uint32_t *refs)
{
    for (; n >= 0; n = N(w, n)->next) {
        lnode_t *x = N(w, n);
        if (x->kind == K_SYM) {
            for (uint32_t f = 0; f < w->nfns; f++)
                if (w->fns[f].top >= 0 && name_eq(w, w->fns[f].name, n))
                    calls[f / 32] |= 1u << (f % 32);
            for (uint32_t g = 0; g < w->nglob; g++)
                if (name_eq(w, w->glob[g].name, n)) refs[g / 32] |= 1u << (g % 32);
        } else if (x->kind == K_LIST) {
            scan(w, x->kid, calls, refs);
        }
    }
}

static int bit(const uint32_t *s, uint32_t i)
{
    return (s[i / 32] >> (i % 32)) & 1;
}

/* A top-level form may only run functions and read globals whose
 * definitions ran before it (sbcl and guile would stop there). */
static int reach(lw_t *w)
{
    static const char *const late_fn = "a function is called before its definition has run";
    static const char *const late_var = "a global variable is used before its definition has run";
    uint32_t nf = w->nfns;
    for (uint32_t f = 0; f < nf; f++) {
        zl_bzero(w->fcalls[f], sizeof w->fcalls[f]);
        zl_bzero(w->frefs[f], sizeof w->frefs[f]);
        if (w->fns[f].top >= 0) scan(w, w->fns[f].body, w->fcalls[f], w->frefs[f]);
    }
    for (uint32_t i = 0; i < w->nforms; i++) {
        uint32_t calls[L_FNS / 32], refs[L_GLOB / 32], seen[L_FNS / 32];
        int32_t n = w->form[i];
        if (w->fkind[i] == T_FN) continue;
        zl_bzero(calls, sizeof calls);
        zl_bzero(refs, sizeof refs);
        zl_bzero(seen, sizeof seen);
        if (w->fkind[i] == T_VAR) {
            scan(w, N(w, w->glob[w->fref[i]].name)->next, calls, refs); /* the value */
        } else
            scan(w, n, calls, refs);
        for (uint32_t g = 0; g < w->nglob; g++)
            if (bit(refs, g) && w->glob[g].top >= (int32_t) i)
                return lfail(w, n, ZL_E_UNDEFINED, late_var);
        for (int changed = 1; changed;) { /* the transitive closure of calls */
            changed = 0;
            for (uint32_t f = 0; f < nf; f++) {
                if (!bit(calls, f) || bit(seen, f)) continue;
                seen[f / 32] |= 1u << (f % 32);
                changed = 1;
                if (w->fns[f].top > (int32_t) i) return lfail(w, n, ZL_E_UNDEFINED, late_fn);
                for (uint32_t g = 0; g < w->nglob; g++)
                    if (bit(w->frefs[f], g) && w->glob[g].top >= (int32_t) i)
                        return lfail(w, n, ZL_E_UNDEFINED, late_var);
                for (uint32_t k = 0; k < L_FNS / 32; k++) calls[k] |= w->fcalls[f][k];
            }
        }
    }
    return ok(w);
}

/* ------------------------------------------------------------ run-time helpers */

static int32_t S(lw_t *w, int k)
{
    return w->scratch + k;
}

/* Helpers take their arguments on the stack and leave one result; they
 * use scratch slots (never call back into the program, so never nest
 * except member/assoc calling equal, which use other slots). */
static int emit_helper(lw_t *w, int h)
{
    int32_t top, j1, j2;
    int strict = 0;
    w->helper[h] = here(w);
    switch (h) {
    case H_LENGTH:
        E(w, ZL_STORE, S(w, 0), 0);
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_STORE, S(w, 1), 0);
        top = E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_CDR, strict, 0);
        E(w, ZL_STORE, S(w, 0), 0);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_ADD, 0, 0);
        E(w, ZL_STORE, S(w, 1), 0);
        E(w, ZL_JMP, top, 0);
        patch_here(w, j1);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_RET, 0, 0);
        break;
    case H_REVERSE:
        E(w, ZL_STORE, S(w, 0), 0);
        E(w, ZL_PUSH, ZL_V_NIL, 0);
        E(w, ZL_STORE, S(w, 1), 0);
        top = E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_CAR, strict, 0);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_CONS, 0, 0);
        E(w, ZL_STORE, S(w, 1), 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_CDR, strict, 0);
        E(w, ZL_STORE, S(w, 0), 0);
        E(w, ZL_JMP, top, 0);
        patch_here(w, j1);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_RET, 0, 0);
        break;
    case H_APPEND: /* copy the first list, share the second */
        E(w, ZL_STORE, S(w, 1), 0);
        E(w, ZL_STORE, S(w, 0), 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j1 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_RET, 0, 0);
        patch_here(w, j1);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_CAR, strict, 0);
        E(w, ZL_PUSH, ZL_V_NIL, 0);
        E(w, ZL_CONS, 0, 0);
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_STORE, S(w, 2), 0);
        E(w, ZL_STORE, S(w, 3), 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_CDR, strict, 0);
        E(w, ZL_STORE, S(w, 0), 0);
        top = E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_LOAD, S(w, 3), 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_CAR, strict, 0);
        E(w, ZL_PUSH, ZL_V_NIL, 0);
        E(w, ZL_CONS, 0, 0);
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_STORE, S(w, 4), 0);
        E(w, ZL_SETCDR, 0, 0);
        E(w, ZL_LOAD, S(w, 4), 0);
        E(w, ZL_STORE, S(w, 3), 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_CDR, strict, 0);
        E(w, ZL_STORE, S(w, 0), 0);
        E(w, ZL_JMP, top, 0);
        patch_here(w, j1);
        E(w, ZL_LOAD, S(w, 3), 0);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_SETCDR, 0, 0);
        E(w, ZL_LOAD, S(w, 2), 0);
        E(w, ZL_RET, 0, 0);
        break;
    case H_EQUAL: { /* pending (cdr, cdr) pairs wait on the data stack; S2 counts them */
        int32_t same, diff, dl, yes, no;
        E(w, ZL_STORE, S(w, 1), 0);
        E(w, ZL_STORE, S(w, 0), 0);
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_STORE, S(w, 2), 0);
        top = E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_EQ, 0, 0);
        same = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j1 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_CDR, 0, 0);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_CDR, 0, 0);
        E(w, ZL_LOAD, S(w, 2), 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_ADD, 0, 0);
        E(w, ZL_STORE, S(w, 2), 0);
        E(w, ZL_LOAD, S(w, 0), 0);
        E(w, ZL_CAR, 0, 0);
        E(w, ZL_STORE, S(w, 0), 0);
        E(w, ZL_LOAD, S(w, 1), 0);
        E(w, ZL_CAR, 0, 0);
        E(w, ZL_STORE, S(w, 1), 0);
        E(w, ZL_JMP, top, 0);
        patch_here(w, same);
        E(w, ZL_LOAD, S(w, 2), 0);
        yes = E(w, ZL_JZ, 0, 0);
        E(w, ZL_STORE, S(w, 1), 0);
        E(w, ZL_STORE, S(w, 0), 0);
        E(w, ZL_LOAD, S(w, 2), 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_SUB, 0, 0);
        E(w, ZL_STORE, S(w, 2), 0);
        E(w, ZL_JMP, top, 0);
        diff = here(w);
        patch_here(w, j1);
        zl_patch(w->c, j2, diff);
        dl = E(w, ZL_LOAD, S(w, 2), 0);
        no = E(w, ZL_JZ, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_LOAD, S(w, 2), 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_SUB, 0, 0);
        E(w, ZL_STORE, S(w, 2), 0);
        E(w, ZL_JMP, dl, 0);
        patch_here(w, yes);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_RET, 0, 0);
        patch_here(w, no);
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_RET, 0, 0);
        break;
    }
    case H_MEMQ:
    case H_MEMBER:
    case H_ASSQ:
    case H_ASSOC: {
        int assoc = h == H_ASSQ || h == H_ASSOC, eq = h == H_MEMQ || h == H_ASSQ;
        int32_t next = -1;
        E(w, ZL_STORE, S(w, 7), 0);
        E(w, ZL_STORE, S(w, 6), 0);
        top = E(w, ZL_LOAD, S(w, 7), 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        j1 = E(w, ZL_JNZ, 0, 0);
        E(w, ZL_LOAD, S(w, 7), 0);
        E(w, ZL_CAR, 0, 0);
        if (assoc) {
            E(w, ZL_STORE, S(w, 8), 0);
            if (w->lisp) { /* LISP's assoc skips NIL elements */
                E(w, ZL_LOAD, S(w, 8), 0);
                E(w, ZL_ISA, ZL_ISA_NULL, 0);
                next = E(w, ZL_JNZ, 0, 0);
            }
            E(w, ZL_LOAD, S(w, 8), 0);
            E(w, ZL_CAR, 0, 0);
        }
        E(w, ZL_LOAD, S(w, 6), 0);
        if (eq)
            E(w, ZL_EQ, 0, 0);
        else
            add_fix(w, E(w, ZL_CALL, 0, 0), -H_EQUAL - 1, -1);
        j2 = E(w, ZL_JNZ, 0, 0);
        if (next >= 0) patch_here(w, next);
        E(w, ZL_LOAD, S(w, 7), 0);
        E(w, ZL_CDR, 0, 0);
        E(w, ZL_STORE, S(w, 7), 0);
        E(w, ZL_JMP, top, 0);
        patch_here(w, j2);
        E(w, ZL_LOAD, S(w, assoc ? 8 : 7), 0);
        E(w, ZL_RET, 0, 0);
        patch_here(w, j1);
        E(w, ZL_PUSH, w->F, 0);
        E(w, ZL_RET, 0, 0);
        break;
    }
    case H_SEQ: /* Clojure: nil is the empty list, a list is itself, else a trap */
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_PUSH, w->NIL, 0);
        E(w, ZL_EQ, 0, 0);
        j1 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_PUSH, ZL_V_NIL, 0);
        E(w, ZL_RET, 0, 0);
        patch_here(w, j1);
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        E(w, ZL_OR, 0, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_RET, 0, 0);
        patch_here(w, j2);
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        break;
    case H_RTOP: /* Racket's top level: void prints nothing, anything else a line */
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_PUSH, w->V, 0);
        E(w, ZL_EQ, 0, 0);
        j1 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_DROP, 0, 0);
        E(w, ZL_RET, 0, 0);
        patch_here(w, j1);
        /* fall through */
    case H_RPRINT: { /* Racket's print: lists and symbols get a quote */
        int32_t q = zl_addstr(w->c, "'", 1);
        if (q < 0) return lfail(w, -1, ZL_E_LIMIT, "too many strings");
        E(w, ZL_DUP, 0, 0);
        E(w, ZL_ISA, ZL_ISA_PAIR, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_ISA, ZL_ISA_NULL, 0);
        E(w, ZL_OR, 0, 0);
        E(w, ZL_OVER, 0, 0);
        E(w, ZL_ISA, ZL_ISA_SYMBOL, 0);
        E(w, ZL_OR, 0, 0);
        j2 = E(w, ZL_JZ, 0, 0);
        E(w, ZL_OUTS, q, 0);
        patch_here(w, j2);
        E(w, ZL_OUTV, ZL_DIALECT_SCHEME, 0);
        if (h == H_RTOP) E(w, ZL_NL, 0, 0);
        E(w, ZL_RET, 0, 0);
        break;
    }
    }
    return ok(w);
}

static int gen(lw_t *w)
{
    zl_cc_t *c = w->c;
    w->cur.fid = -1;
    w->cur.level = 1;
    E(w, ZL_LINK, 0, 0);
    int32_t enter = E(w, ZL_ENTER, 0, 0); /* the top level's own frame */
    for (uint32_t i = 0; i < w->nforms; i++) {
        int32_t n = w->form[i];
        c->line = N(w, n)->line;
        if (CLJ(w)) w->curtop = (int32_t) i; /* names resolve to definitions before it */
        if (w->fkind[i] == T_FN) continue;
        if (w->fkind[i] == T_VAR) {
            lglob_t *g = &w->glob[w->fref[i]];
            TRY(cv(w, N(w, g->name)->next));
            E(w, ZL_STORE, g->slot, 0);
            continue;
        }
        if (w->dial == D_RKT) { /* a module prints each top-level value that is not void */
            if (N(w, n)->kind == K_LIST && sym_is(w, N(w, n)->kid, "begin"))
                return lfail(w, n, ZL_E_UNSUPPORTED,
                             "a top-level begin splices its forms into the module, which is "
                             "not in this subset");
            TRY(cx(w, n, X_RESULT, 0));
            TRY(hcall(w, H_RTOP, n));
            continue;
        }
        TRY(cx(w, n, X_EFFECT, 0));
    }
    E(w, ZL_HALT, 0, 0);
    if (enter >= 0) c->p->code[enter].a = w->cur.maxloc;
    for (uint32_t i = 0; i < w->nforms; i++)
        if (w->fkind[i] == T_FN) {
            if (CLJ(w)) w->curtop = (int32_t) i;
            TRY(emit_fn(w, w->fref[i]));
        }
    c->line = 0;
    for (int h = 0; h < H__COUNT; h++)
        if (w->hused[h]) TRY(emit_helper(w, h));
    TRY(ok(w));
    for (uint32_t k = 0; k < w->nfix; k++) {
        int32_t f = w->fix[k].fid;
        zl_patch(c, w->fix[k].at, f >= 0 ? w->fns[f].entry : w->helper[-f - 1]);
    }
    if (!w->lisp) { /* unspecified values flowing out of procedures */
        for (int changed = 1; changed;) {
            changed = 0;
            for (uint32_t k = 0; k < w->nedge; k++)
                if (w->fns[w->edge[k].to].unspec && !w->fns[w->edge[k].from].unspec) {
                    w->fns[w->edge[k].from].unspec = 1;
                    changed = 1;
                }
        }
        for (uint32_t k = 0; k < w->nobs; k++)
            if (w->fns[w->obs[k].fid].unspec) {
                c->line = w->obs[k].line;
                c->col = w->obs[k].col;
                return zl_fail(
                    c, ZL_E_UNSUPPORTED,
                    w->dial == D_RKT
                        ? "this procedure's value may be void in Racket (it ends in display, "
                          "set!, a when, ...): call it only as a statement or at the top level"
                        : "this procedure's value is unspecified in Scheme (it ends in display, "
                          "set!, an if without else, ...): call it only as a statement");
            }
    }
    return ok(w);
}

/* ------------------------------------------------------------ twin */

static void tput(lw_t *w, const char *s, size_t n)
{
    zl_put(w->tw, s, n);
    for (size_t i = 0; i < n; i++) w->tcol = s[i] == '\n' ? 0 : w->tcol + 1;
}

static void tputs(lw_t *w, const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    tput(w, s, n);
}

static void tw_put(void *ctx, const char *s, size_t n)
{
    tput((lw_t *) ctx, s, n);
}

static void tname(lw_t *w, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int u = (unsigned char) s[i];
        char ch = (char) (w->dial == D_LISP                         ? zl_upper(u)
                          : w->dial == D_CL && u >= 'A' && u <= 'Z' ? u + 32
                                                                    : u);
        tput(w, &ch, 1);
    }
}

static void tindent(lw_t *w, size_t col)
{
    tput(w, "\n", 1);
    for (size_t i = 0; i < col; i++) tput(w, " ", 1);
}

static void pnode(lw_t *w, int32_t n);

/* Does the twin of n span more than one line? */
static int multiline(lw_t *w, int32_t n)
{
    lnode_t *x = N(w, n);
    if (x->kind != K_LIST && x->kind != K_VEC) return 0;
    if ((x->flags & F_PAIRS) && x->kid >= 0 && N(w, x->kid)->next >= 0) return 1;
    if ((x->flags & (F_BLOCK | F_LAMDEF)) && count(w, x->kid) > ((x->flags & F_LAMDEF) ? 2u : x->k))
        return 1;
    for (int32_t e = x->kid; e >= 0; e = N(w, e)->next)
        if (multiline(w, e)) return 1;
    return 0;
}

static void pbare(lw_t *w, int32_t name)
{
    tput(w, "(", 1);
    pnode(w, name);
    tputs(w, w->dial == D_CL ? " nil)" : " NIL)");
}

static void pnode(lw_t *w, int32_t n)
{
    lnode_t *x = N(w, n);
    switch (x->kind) {
    case K_NUM: {
        char b[24];
        tput(w, b, zl_itoa(x->v, b));
        break;
    }
    case K_TRUE:
        tputs(w, "#t");
        break;
    case K_FALSE:
        tputs(w, "#f");
        break;
    case K_STR:
        encode(w->sbuf, decode(w, n), tw_put, w);
        break;
    case K_QUOTE:
        if (!(x->flags & F_UNQ)) tput(w, "'", 1);
        pnode(w, x->kid);
        break;
    case K_SYM:
        if (x->flags & F_SYNTAX)
            tputs(w, "()");
        else if (x->flags & F_BARE) {
            x->flags &= (uint8_t) ~F_BARE;
            pbare(w, n);
            x->flags |= F_BARE;
        } else if (x->flags & F_ALIAS) {
            const char *s = x->alias == A_EMPTY ? "'()"
                            : x->alias >= A_CL  ? cl_canon[x->alias - A_CL]
                            : x->alias >= 128   ? specs[x->alias - 128].canon
                                                : bis[x->alias - 1].canon;
            size_t k = 0;
            while (s[k]) k++;
            tname(w, s, k);
        } else
            tname(w, w->src + x->v, x->len);
        break;
    case K_LIST: {
        if (x->kid < 0) {
            tputs(w, !w->lisp || (x->flags & F_SYNTAX) ? "()" : w->dial == D_CL ? "nil" : "NIL");
            break;
        }
        if (x->flags & F_BARE) {
            pbare(w, x->kid);
            break;
        }
        size_t open = w->tcol;
        int32_t e = x->kid;
        if (x->flags & F_PAIRS) { /* Clojure cond: a test and its form per line */
            tput(w, "(", 1);
            pnode(w, e);
            for (e = N(w, e)->next; e >= 0; e = N(w, N(w, e)->next)->next) {
                tindent(w, open + 2);
                pnode(w, e);
                tput(w, " ", 1);
                pnode(w, N(w, e)->next);
            }
            tput(w, ")", 1);
            break;
        }
        if (x->flags &
            F_LAMDEF) { /* (define f (lambda (p ...) b ...)) -> (define (f p ...) b ...) */
            int32_t name = N(w, e)->next, lam = N(w, name)->next, pl = N(w, N(w, lam)->kid)->next;
            tput(w, "(", 1);
            pnode(w, e);
            tputs(w, " (");
            pnode(w, name);
            for (int32_t p = N(w, pl)->kid; p >= 0; p = N(w, p)->next) {
                tput(w, " ", 1);
                pnode(w, p);
            }
            tput(w, ")", 1);
            for (int32_t b = N(w, pl)->next; b >= 0; b = N(w, b)->next) {
                tindent(w, open + 2);
                pnode(w, b);
            }
            tput(w, ")", 1);
            break;
        }
        uint32_t k = (x->flags & F_BLOCK) ? x->k : multiline(w, n) ? 2 : 0xffffffffu, i = 0;
        tput(w, "(", 1);
        for (; e >= 0; e = N(w, e)->next, i++) {
            if (i) {
                if (i >= k)
                    tindent(w, open + 2);
                else if ((x->flags & F_DOT) && N(w, e)->next < 0)
                    tputs(w, " . ");
                else
                    tput(w, " ", 1);
            }
            pnode(w, e);
        }
        tput(w, ")", 1);
        break;
    }
    case K_VEC: { /* Clojure: a binding or parameter vector, on one line */
        tput(w, "[", 1);
        for (int32_t e = x->kid; e >= 0; e = N(w, e)->next) {
            if (e != x->kid) tput(w, " ", 1);
            pnode(w, e);
        }
        tput(w, "]", 1);
        break;
    }
    case K_COMMENT:
        tput(w, ";", 1);
        if (x->len) {
            tput(w, " ", 1);
            tput(w, w->src + x->v, x->len);
        }
        break;
    }
}

static void print_twin(lw_t *w)
{
    if (w->dial == D_RKT) {
        tputs(w, "#lang ");
        tput(w, w->src + 6, w->langlen);
        tput(w, "\n", 1);
    }
    for (uint32_t i = 0; i < w->ntops; i++) {
        pnode(w, w->tops[i]);
        tput(w, "\n", 1);
    }
}

/* ------------------------------------------------------------ entry */

int zl_lisp_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    lw_t *w = (lw_t *) work;
    zl_bzero(w, offsetof(lw_t, nodes));
    w->c = c;
    w->tw = tw;
    w->src = src;
    w->len = len;
    w->line = 1;
    int lang = c->p->lang;
    w->dial = lang == ZL_LANG_LISP         ? D_LISP
              : lang == ZL_LANG_COMMONLISP ? D_CL
              : lang == ZL_LANG_RACKET     ? D_RKT
              : lang == ZL_LANG_CLOJURE    ? D_CLJ
                                           : D_SCM;
    w->lisp = (w->dial & DL) != 0;
    w->F = w->lisp ? ZL_V_NIL : ZL_V_FALSE;
    w->T = ZL_V_TRUE;
    w->NIL = ZL_V_NIL;
    w->curtop = 0x7fffffff;
    w->scratch = -1;
    c->p->word_bits = 62;
    if (CLJ(w)) { /* nil, true and false: symbols no quoted datum can name */
        static const char *const nm[3] = {"nil", "true", "false"};
        int64_t *v[3] = {&w->NIL, &w->T, &w->F};
        for (int i = 0; i < 3; i++) {
            int32_t k = zl_addstr(c, nm[i], (size_t) (i ? 4 + (i == 2) : 3));
            if (k < 0) return ok(w);
            *v[i] = ZL_V_SYM + k;
        }
    }
    w->V = w->F;
    if (w->dial == D_RKT) { /* void: a symbol no program can name */
        int32_t k = zl_addstr(c, "#<void>", 7);
        if (k < 0) return ok(w);
        w->V = ZL_V_SYM + k;
    }
    TRY(read_program(w));
    TRY(collect(w));
    if (!CLJ(w)) TRY(reach(w)); /* Clojure resolves each name as it compiles (gen) */
    TRY(gen(w));
    if (tw) print_twin(w);
    return ok(w);
}
