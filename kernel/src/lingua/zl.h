/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl.h — Lingua: one shared intermediate form for every language ZXV runs.
 *
 * Each source language has a front-end that compiles a program into the
 * Lingua IR (zl_prog_t), a small stack machine over exact integers. One
 * checked runtime (zl_run) executes the IR for every language, so every
 * language gets the same guarantees at once: fuel-metered, bounded memory,
 * no floating point, overflow and division faults trapped, never wrapped.
 *
 * Each language also has an AI twin: its canonical spelling. The twin is
 * the language itself with exactly one way to write each construct
 * (upper-case keywords, single spaces, minimal parentheses, one statement
 * per line), so a model that writes the twin needs a far smaller grammar
 * mask, and a person who knows the language reads it without training.
 *
 *   zl_twin(lang, src)       canonical twin text of a program
 *   zl_compile(twin, ...)    accepted only if the text is already canonical
 *
 * The twin and the original compile to byte-identical IR (zl_prog_equal),
 * so a program written by a model and one written by a person in the 1960s
 * are checked to be the same program, not merely similar.
 *
 * GUARANTEES (each tested in test_zl.c and fuzzed in fuzz_zl.c)
 *   L1  Freestanding: no libc, no allocation, no floating point. The caller
 *       owns every byte (zl_prog_t, zl_work_t, the output buffer).
 *       Recursion is bounded by ZL_MAX_DEPTH (expression nesting).
 *   L2  Fail closed: an input a front-end does not admit is refused with a
 *       negative status, a line and column, and a reason. Constructs outside
 *       the subset (floating-point literals, string variables, ...) are
 *       refused by name, never guessed.
 *   L3  Every compiled program passes zl_verify, and zl_run re-checks every
 *       stack access, slot index, jump and call at run time.
 *   L4  Exact: integer overflow, division by zero, inexact division where a
 *       language's division is real-valued, and array bounds all trap with
 *       a status; nothing wraps or rounds.
 *   L5  Twin round trip: zl_twin(twin(x)) == twin(x) byte for byte, and
 *       twin(x) compiles to IR equal to x's.
 *   L6  Deterministic: one program, one input, one fuel budget give one
 *       output and one status on every machine.
 */
#ifndef ZL_H
#define ZL_H

#include <stddef.h>
#include <stdint.h>

/* ---- status codes: lingua owns -2601 .. -2699 ---- */
typedef enum {
    ZL_OK = 0,
    ZL_E_ARG = -2601,         /* NULL pointer or unknown language */
    ZL_E_SYNTAX = -2602,      /* the front-end's grammar does not admit it */
    ZL_E_UNSUPPORTED = -2603, /* valid in the language, outside the subset */
    ZL_E_NONCANON = -2604,    /* twin input that is not in canonical form */
    ZL_E_LIMIT = -2605,       /* a fixed capacity (code, slots, names) is full */
    ZL_E_UNDEFINED = -2606,   /* jump to a missing label, unknown word */
    ZL_E_VERIFY = -2607,      /* zl_verify rejected the IR */
    /* run-time traps */
    ZL_E_OVERFLOW = -2620, /* result outside the program's integer width */
    ZL_E_DIVZERO = -2621,
    ZL_E_INEXACT = -2622, /* real division or power with a remainder */
    ZL_E_BOUNDS = -2623,  /* array index or address out of range */
    ZL_E_STACK = -2624,   /* data stack underflow or overflow */
    ZL_E_RSTACK = -2625,  /* call depth exceeded or RETURN without GOSUB */
    ZL_E_FUEL = -2626,    /* instruction budget spent */
    ZL_E_OUTPUT = -2627,  /* output buffer full */
    ZL_E_INPUT = -2628,   /* input list exhausted */
    ZL_E_DOMAIN = -2629,  /* e.g. a zero DO step, a negative power, CAR of a number */
    ZL_E_HEAP = -2630,    /* list cells or block cells exhausted */
    ZL_E_THROW = -2631    /* THROW with no CATCH active */
} zl_status_t;

/* ---- languages ---- */
/* One per line of zl_langs.def (names, twins, front-ends, extensions). */
typedef enum {
#define ZL_LANG_X(id, n, name, twin, fe, ext, rt) ZL_LANG_##id = n,
#include "zl_langs.def"
#undef ZL_LANG_X
    ZL_LANG__COUNT /* one past the last; the ids are dense */
} zl_lang_t;

/* A twin is selected by OR-ing this flag into the language. */
#define ZL_TWIN 0x100

const char *zl_lang_name(int lang); /* "FORTRAN", "BASIC", "Forth" */
const char *zl_twin_name(int lang); /* "Reckonwyrm", "Hatchling", "Cairndrake" */
const char *zl_status_name(int status);

/* ---- capacities (fixed; a program that needs more is refused) ---- */
#define ZL_MAX_CODE     8192 /* IR instructions */
#define ZL_MAX_SLOTS    4096 /* integer memory cells (variables + arrays) */
#define ZL_MAX_STR      256  /* string literals */
#define ZL_MAX_STRBYTES 8192
#define ZL_MAX_STACK    256   /* data stack depth */
#define ZL_MAX_CALLS    1024  /* call (GOSUB / word / procedure) depth */
#define ZL_MAX_FRAMES   16384 /* procedure frame cells (locals, links) */
#define ZL_MAX_CELLS    16384 /* list (cons) cells */
#define ZL_MAX_BLK      65536 /* block cells: vectors and strings made at run time */
#define ZL_MAX_DEPTH    48    /* expression and control nesting */
#define ZL_MAX_LINE     512   /* longest source line or statement */

/* ---- the IR ---- */
typedef enum {
    ZL_HALT = 0,
    ZL_PUSH,   /* a: value */
    ZL_LOAD,   /* a: slot */
    ZL_STORE,  /* a: slot                       (pops value) */
    ZL_LOADX,  /* a: base, b: length            (pops index; checks 0<=i<b) */
    ZL_STOREX, /* a: base, b: length            (pops value, then index) */
    ZL_ADD,
    ZL_SUB,
    ZL_MUL,
    ZL_DIVT,
    ZL_MODT, /* truncating (FORTRAN, C) */
    ZL_DIVF,
    ZL_MODF, /* floored (Forth as gforth does it) */
    ZL_DIVX, /* exact: traps ZL_E_INEXACT on a remainder (BASIC) */
    ZL_POW,  /* a: 0 truncating negative exponents, 1 exact */
    ZL_NEG,
    ZL_ABS,
    ZL_SGN,
    ZL_MIN,
    ZL_MAX,
    ZL_EQ,
    ZL_NE,
    ZL_LT,
    ZL_LE,
    ZL_GT,
    ZL_GE, /* push 1 or 0 */
    ZL_AND,
    ZL_OR,
    ZL_XOR,
    ZL_INVERT, /* bitwise */
    ZL_NOT,    /* logical: 0 -> 1, otherwise 0 */
    ZL_DUP,
    ZL_DROP,
    ZL_SWAP,
    ZL_OVER,
    ZL_ROT,
    ZL_JMP,  /* a: target */
    ZL_JZ,   /* a: target                     (pops; jumps when zero) */
    ZL_JNZ,  /* a: target                     (pops; jumps when not zero) */
    ZL_CALL, /* a: target */
    ZL_RET,
    ZL_OUTI,   /* pops; a: format flags, b: minimum width */
    ZL_OUTS,   /* a: string index */
    ZL_OUTC,   /* pops a byte 0..255 */
    ZL_NL,     /* newline */
    ZL_TAB,    /* a: zone width: advance to the start of the next zone */
    ZL_IN,     /* pushes the next input value */
    ZL_LOADA,  /* pops an address (slot); checks it is < nslots */
    ZL_STOREA, /* pops an address, then a value */
    ZL_TRAP,   /* a: status to stop with (e.g. STOP with a code) */
    /* procedure frames (Pascal, ALGOL, Lisp, Logo). A frame holds its
     * locals; its static link is the handle (call depth) of the frame of
     * the enclosing procedure, so b ("hops") walks that many static links
     * out, 0 = this frame. The main program is frame 0, of size 0.
     * Calling: LINK hops, the arguments, CALL; the callee runs ENTER ...
     * LEAVE RET. Frames live until LEAVE: no closures outlive their frame. */
    ZL_ENTER, /* a: frame size, b: params: pops b args (last on top), then the static link */
    ZL_LEAVE, /* drop the current frame */
    ZL_LINK,  /* a: hops: pushes that frame's handle (the static link for a call) */
    ZL_LOADF, /* a: offset, b: hops */
    ZL_STOREF,
    ZL_ADDRF, /* a: offset, b: hops: pushes the cell's address for LOADA/STOREA */
    ZL_BOUND, /* a: length: traps BOUNDS unless 0 <= top < a (leaves it) */
    /* lists (Lisp, Scheme, Logo): values of 62-bit programs, see ZL_V_* */
    ZL_CONS, /* pops cdr, then car; pushes a new pair (reclaimed when unreachable: E7) */
    ZL_CAR,  /* a: 1 = the car of the empty list is the empty list (Lisp) */
    ZL_CDR,
    ZL_SETCAR, /* pops the value, then the pair */
    ZL_SETCDR,
    ZL_ISA,  /* a: ZL_ISA_*: pushes 1 or 0 */
    ZL_OUTV, /* pops a value and prints it; a: ZL_DIALECT_*, b: 1 = strings in quotes */
    /* E1 strings and E2 vectors (62-bit programs). A string is a literal
     * (ZL_V_STRC + string index) or a block made at run time; a vector is a
     * block of cells. Unreachable blocks and pairs are reclaimed (E7);
     * HEAP traps when what is reachable fills the space. */
    ZL_VNEW,  /* pops n; pushes a vector of n cells, each 0 */
    ZL_VLEN,  /* pops a vector or string; pushes its length (cells or bytes) */
    ZL_VREF,  /* pops index, then a vector or string; pushes the cell or byte (BOUNDS) */
    ZL_VSET,  /* pops value, index, vector; stores (strings are immutable: DOMAIN) */
    ZL_SCAT,  /* pops b, then a; pushes the string a followed by b */
    ZL_SSUB,  /* pops count, start, string; pushes bytes [start, start+count) (BOUNDS) */
    ZL_SCMP,  /* pops b, then a; pushes -1, 0 or 1 comparing the bytes */
    ZL_SCONV, /* a: ZL_SCONV_*: number to decimal string, byte to string, string to number */
    /* E3 procedures as values */
    ZL_CALLI, /* pops a procedure value (ZL_V_CODE + address) and calls it */
    ZL_JMPI,  /* pops a procedure value and jumps to it: a tail call through a
                 value (LEAVE first; a static tail call is LEAVE, JMP) */
    /* E4 exceptions */
    ZL_CATCH,   /* a: handler address: remembers the stack, calls and frames;
                   b: 1 = it also catches the run-time traps OVERFLOW, DIVZERO,
                   INEXACT, BOUNDS, DOMAIN and INPUT, with the status pushed
                   as the value (the nearest such CATCH, inner ones dropped) */
    ZL_UNCATCH, /* forgets the newest CATCH */
    ZL_THROW,   /* pops a value; unwinds to the newest CATCH, pushes the value, jumps */
    /* E6 ranges, input's end, shifts and wrapping, fixed-point output (any word size) */
    ZL_RANGE, /* a: max > 0; b: 0 traps OVERFLOW unless -a <= top <= a, 1 unless
                 -a-1 <= top <= a (two's complement); leaves the value */
    ZL_INEOF, /* pushes 1 when the input is exhausted, else 0 */
    ZL_SHL,   /* pops n (0..63), then x; pushes x * 2^n (OVERFLOW) */
    ZL_SHR,   /* pops n (0..63), then x; pushes floor(x / 2^n) */
    ZL_WRAP,  /* a: bits 1..63; b: 0 signed, 1 unsigned: top modulo 2^a into
                 [-2^(a-1), 2^(a-1)) or [0, 2^a) (OVERFLOW if the word is narrower) */
    ZL_OUTD,  /* pops x; prints x / 10^(a & 255) with that many decimals,
                 width a >> 8 (0..64); b: ZL_FMT_* flags (the trace sees x) */
    ZL_OP__COUNT
} zl_op_t;

/* Values in 62-bit programs (word_bits 62). Numbers are the integers
 * strictly inside +-2^62; everything else is a reference. Arithmetic and
 * ordering on a reference trap DOMAIN; EQ and NE compare identity. */
#define ZL_V_REF   ((int64_t) 1 << 62)
#define ZL_V_NIL   ZL_V_REF                         /* the empty list */
#define ZL_V_SYM   (ZL_V_REF + ((int64_t) 1 << 32)) /* + string index: a symbol */
#define ZL_V_TRUE  (ZL_V_REF + ((int64_t) 1 << 33)) /* T, #t, "true */
#define ZL_V_FALSE (ZL_V_TRUE + 1)                  /* #f, "false */
#define ZL_V_STRC  (ZL_V_REF + ((int64_t) 1 << 34)) /* + string index: a string literal */
#define ZL_V_BLK   (ZL_V_REF + ((int64_t) 1 << 35)) /* + block offset: a vector or string */
#define ZL_V_CODE  (ZL_V_REF + ((int64_t) 1 << 36)) /* + code address: a procedure */
enum {
    ZL_ISA_NUMBER = 0,
    ZL_ISA_PAIR,
    ZL_ISA_NULL,
    ZL_ISA_SYMBOL,
    ZL_ISA_STRING,
    ZL_ISA_VECTOR,
    ZL_ISA_PROC,
    ZL_ISA__COUNT
};
enum { ZL_SCONV_FROM_NUMBER = 0, ZL_SCONV_FROM_BYTE, ZL_SCONV_TO_NUMBER, ZL_SCONV__COUNT };
enum {
    ZL_DIALECT_LISP = 0,
    ZL_DIALECT_SCHEME,
    ZL_DIALECT_LOGO,
    ZL_DIALECT_LOGO_SHOW,
    ZL_DIALECT__COUNT
};

/* ZL_OUTI flags */
#define ZL_FMT_SIGNSPACE 1u /* a space before a non-negative number */
#define ZL_FMT_TRAIL     2u /* a space after the number */

typedef struct {
    uint8_t op;
    uint8_t pad[3];
    int32_t b;
    int64_t a;
    uint32_t line; /* source line, for traps and listings */
    uint32_t pad2;
} zl_ins_t;

typedef struct {
    int lang;           /* ZL_LANG_* (never with ZL_TWIN) */
    uint32_t word_bits; /* 32, 62 or 64: integer width checked on every result
                           (62: list programs, whose other values are references) */
    uint32_t ncode, nslots, nstr, strbytes;
    zl_ins_t code[ZL_MAX_CODE];
    uint32_t str_off[ZL_MAX_STR], str_len[ZL_MAX_STR];
    char str[ZL_MAX_STRBYTES];
} zl_prog_t;

typedef struct {
    int status;      /* ZL_OK or a negative zl_status_t */
    uint32_t line;   /* 1-based source line, 0 if not tied to one */
    uint32_t col;    /* 1-based column, 0 if not known */
    const char *msg; /* static English text */
} zl_diag_t;

/* Scratch space for compiling. The caller provides it so nothing is
 * allocated (about 470 KB); the fields are private to the module. */
typedef struct {
    zl_prog_t tmp;      /* zl_twin compiles here */
    uint64_t fe[32768]; /* front-end tables */
} zl_work_t;

/* Compile source text in `lang` (or `lang | ZL_TWIN` to accept only the
 * canonical twin spelling). On failure `p` is left zeroed and `d` says why. */
int zl_compile(int lang, const char *src, size_t len, zl_prog_t *p, zl_work_t *w, zl_diag_t *d);

/* Write the canonical twin of `src` into out[0..cap). *out_len gets the
 * length (not NUL-terminated; a NUL is added when there is room). */
int zl_twin(int lang, const char *src, size_t len, char *out, size_t cap, size_t *out_len,
            zl_work_t *w, zl_diag_t *d);

/* Structural check of an IR program (opcodes, operands, targets). */
int zl_verify(const zl_prog_t *p, zl_diag_t *d);

/* Byte-exact comparison of two programs' IR, string table and widths
 * (the source line numbers are not part of the comparison). */
int zl_prog_equal(const zl_prog_t *a, const zl_prog_t *b);

/* 64-bit FNV-1a digest of the same bytes zl_prog_equal compares. */
uint64_t zl_prog_digest(const zl_prog_t *p);

/* Precompiled programs (zl_pack.c): write a verified program as "zlir"
 * text, or read one back without a front-end. zl_unpack refuses text that
 * is malformed, fails zl_verify or does not match its digest, leaving `p`
 * zeroed. zl_pack NUL-terminates, so cap must exceed the text's length;
 * otherwise it returns ZL_E_LIMIT with *out_len = 0 and out empty. */
int zl_pack(const zl_prog_t *p, char *out, size_t cap, size_t *out_len);
int zl_unpack(const char *src, size_t len, zl_prog_t *p, zl_diag_t *d);
const char *zl_op_name(int op); /* "PUSH"; "?" when out of range */

/* Projections (zl_proj.c, docs/spec/ALPHABET.md): write a verified program
 * out as source for another target. The numbers are Alexandria language
 * numbers: Forth 3, C 101, x86-64 assembly 121. C and x86-64 run on zxp_rt
 * and match zl_run exactly (output, fuel, trap and trapping instruction);
 * Forth matches its output and whether it ends or stops. An instruction a
 * target does not take is refused (ZL_E_UNSUPPORTED), and so are list
 * programs (62-bit words). `name` is the C or assembly function name. */
enum { ZL_PROJ_FORTH = 3, ZL_PROJ_C = 101, ZL_PROJ_X86_64 = 121 };
int zl_project(int target, const zl_prog_t *p, const char *name, char *out, size_t cap,
               size_t *out_len, zl_diag_t *d);
/* Whether a target takes an instruction. */
int zl_project_op(int target, int op);

/* Optional record of every number the program printed, in order, so the
 * same algorithm written in different languages can be compared by value
 * even when their print formats differ. */
typedef struct {
    int64_t *v;
    size_t cap, n;
} zl_trace_t;

typedef struct {
    const int64_t *in; /* INPUT / READ values */
    size_t nin;
    int preload; /* nonzero: push in[0..nin) onto the data stack before the
                    first instruction instead (Forth takes its arguments
                    there); more than ZL_MAX_STACK traps ZL_E_STACK */
    char *out;   /* program output */
    size_t out_cap;
    size_t out_len;     /* set by zl_run */
    uint64_t fuel;      /* instructions allowed; 0 means 10^7 */
    uint64_t used;      /* set by zl_run */
    uint32_t trap_line; /* source line of the trapping instruction (of its
                           nearest caller when it has none) */
    uint32_t trap_pc;
    zl_trace_t *trace; /* may be NULL */
    /* machine state, owned by the caller so zl_run needs little stack */
    int64_t mem[ZL_MAX_SLOTS];
    int64_t stack[ZL_MAX_STACK];
    uint32_t rstack[ZL_MAX_CALLS];
    int64_t frames[ZL_MAX_FRAMES]; /* frame cells: locals and parameters */
    uint32_t fbase[ZL_MAX_CALLS], fstat[ZL_MAX_CALLS], fsize[ZL_MAX_CALLS]; /* per frame */
    int64_t heap[2 * ZL_MAX_CELLS]; /* pair k: car heap[2k], cdr heap[2k+1]; k >= 1 */
    int64_t blk[ZL_MAX_BLK]; /* block at o >= 1: blk[o] = length << 2 | kind, then its cells */
    uint32_t hpc[ZL_MAX_CALLS], hsp[ZL_MAX_CALLS], hrp[ZL_MAX_CALLS], hfd[ZL_MAX_CALLS]; /* CATCH */
    /* E7 reclamation: when the pairs or the blocks are full, a collector
     * keeps every pair and block reachable from the stack, the slots and
     * the frames, slides them down in order and rewrites every reference.
     * It cannot be observed (identity, EQ and printing are unchanged) and
     * costs no fuel; HEAP traps only when the live data fills the space. */
    uint32_t gc_fwd_p[ZL_MAX_CELLS];              /* a pair's new index, 0 = dead */
    uint32_t gc_fwd_b[ZL_MAX_BLK];                /* a block's new offset, 0 = dead */
    uint32_t gc_stack[ZL_MAX_CELLS + ZL_MAX_BLK]; /* objects whose children wait */
    uint32_t collections;                         /* set by zl_run */
} zl_run_t;

/* Run a program that passed zl_verify. Returns ZL_OK on HALT/END/STOP, or
 * the trap status; r->out holds what was printed up to that point. */
int zl_run(const zl_prog_t *p, zl_run_t *r);

#endif /* ZL_H */
