/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxsh_core.h — the paraconsistent shell's language and logic.
 *
 * zxsh is a command shell whose commands return a VERDICT, not a bare exit
 * code. A verdict is the 64-state hexagram tag of kernel/src/lpres/lpres_hex.h:
 * an lpres base (true / false / both / neither), an evidence polarity
 * (negentropic / neutral / entropic / forbidden) and a lifecycle phase. What
 * runs next is always lpx_action(tag), one lookup in lpres_hex's table:
 *
 *   S1 VERDICTS. A finished command's tag has phase MANIFEST. The host maps a
 *      process's end to a tag (zxsh_host.c): exit 0 TRUE, 1..125 FALSE,
 *      126/127 (could not run) NEITHER, a crash signal FORBIDDEN. A program
 *      may report its own base/polarity on the verdict channel; it can never
 *      cover a crash.
 *   S2 PIPELINES. a | b | c is the lpx_and of every stage, so a failing
 *      producer is not hidden by a succeeding consumer.
 *   S3 AND/OR. a && b runs b only when lpx_action(a) is PROCEED; a || b runs
 *      b only when it is REFUSE. WAIT and ESCALATE (both, neither, a weakening
 *      truth) run neither side: the rest of the and/or list is HELD (S5).
 *   S4 CROSS-CHECK. a <?> b runs both pipelines with their output captured.
 *      The bases are joined by knowledge (lpres bit encoding, T|F = B). Two
 *      TRUE runs with different output are BOTH. Agreement is negentropic
 *      (two witnesses converge), disagreement entropic.
 *   S5 HOLDS. A held continuation keeps its source text, its tag and an id.
 *      It starts at phase POTENTIAL (waiting). `resolve ID VERDICT` or
 *      `drop ID` walks it EMERGING (decided), MANIFEST (applied: the rest
 *      runs as if its condition had returned VERDICT, or nothing runs for a
 *      drop), DISSOLVING (cleared), one Gray step each through lpx_advance. Resolving needs the RESOLVE capability,
 *      which a script (non-interactive run) does not have: a person decides.
 *   S6 FORBIDDEN. A forbidden tag appends a record (tag, command digest) to
 *      the lpx_qlog quarantine chain and ABORTS the rest of the input: no
 *      later command in that line or script runs. The record is handed to
 *      the host to persist. A full log refuses the append and the shell
 *      stays aborted (fail closed).
 *   S7 IF. `if L then A [else B] [both C] [neither D] fi` chooses by the
 *      condition's action: PROCEED A, REFUSE B. Otherwise the base picks
 *      `both` or `neither` when that clause is present, else the if is held.
 *      An if whose chosen branch is absent is TRUE (nothing was asked).
 *      Inside an if's clauses `both` and `neither` are keywords; the verdict
 *      builtins are also spelled `verdict both`, which works anywhere.
 *   S8 LOOPS. A for loop's verdict is the lpx_and of its iterations.
 *   S9 CAPABILITIES. The session holds a mask (exec, read, write, net,
 *      resolve). `cap drop` only narrows it; `sandbox CAPS -- cmd` runs one
 *      command with the intersection. A refusal for a missing capability is
 *      FALSE/neutral (an ordinary refusal). These are checks inside the
 *      shell: a program it runs still has the operating system's permissions.
 *
 * The core has no allocation and no stdio; the host supplies output, process
 * execution and persistence through zxsh_ops_t. It uses lpres_hex for every
 * logical operation, so the shell and the kernel share one truth table.
 */
#ifndef ZXSH_CORE_H
#define ZXSH_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lpres_hex.h"

#define ZXSH_MAX_TOKENS 1024u
#define ZXSH_MAX_STAGES 8u
#define ZXSH_MAX_ARGS 32u
#define ZXSH_ARENA 16384u
#define ZXSH_MAX_VARS 64u
#define ZXSH_NAME_MAX 32u
#define ZXSH_VALUE_MAX 256u
#define ZXSH_MAX_HELD 32u
#define ZXSH_HOLD_TEXT 512u
#define ZXSH_CAPTURE 8192u
#define ZXSH_WHY_MAX 128u
#define ZXSH_QLOG_CAP 64u
#define ZXSH_MAX_DEPTH 16u

/* S9 */
#define ZXSH_CAP_EXEC (1u << 0)
#define ZXSH_CAP_READ (1u << 1)
#define ZXSH_CAP_WRITE (1u << 2)
#define ZXSH_CAP_NET (1u << 3)
#define ZXSH_CAP_RESOLVE (1u << 4)
#define ZXSH_CAP_ALL 0x1Fu

/* Results of zxsh_eval. */
typedef enum {
    ZXSH_OK = 0,        /* parsed and evaluated (the verdict is in sh->last) */
    ZXSH_NEED_MORE = 1, /* input ends inside a construct: read another line */
    ZXSH_SYNTAX = 2,    /* syntax error, nothing ran (sh->why says where)    */
    ZXSH_ABORTED = 3,   /* a forbidden verdict stopped evaluation (S6)       */
    ZXSH_EXIT = 4       /* the `exit` builtin ran                             */
} zxsh_status_t;

typedef struct {
    int argc;
    char *argv[ZXSH_MAX_ARGS + 1]; /* NULL-terminated */
    uint32_t glob;                 /* bit i: argv[i] had unquoted * ? [ */
    const char *in;                /* < path, or NULL */
    const char *out;               /* > or >> path, or NULL */
    bool append;
} zxsh_stage_t;

typedef struct zxsh zxsh_t;

typedef struct {
    /* Write to the shell's stdout (fd 1) or stderr (fd 2). */
    void (*out)(void *u, int fd, const char *s, size_t n);
    /* Run a pipeline of n stages. When cap is non-NULL, the last stage's
     * stdout goes to cap (at most capmax bytes, *caplen set) instead of the
     * terminal. Returns the S1/S2 tag; may write a short reason into why.
     * The host also runs its own builtins here (cd, history, zxv, ...). */
    lpx_t (*exec)(void *u, zxsh_t *sh, const zxsh_stage_t *st, unsigned n, char *cap,
                  uint32_t capmax, uint32_t *caplen, char *why, uint32_t whymax);
    /* Export a variable to the environment of programs (may be NULL). */
    void (*setenv)(void *u, const char *name, const char *value);
    /* Persist one quarantine record (may be NULL). */
    void (*quarantine)(void *u, const lpx_qrec_t *rec, const char *cmd);
} zxsh_ops_t;

typedef struct {
    bool used;
    uint32_t id;
    lpx_t tag;         /* the condition's verdict, phase per S5 */
    bool is_if;        /* text is an if body ("then ... fi") */
    char text[ZXSH_HOLD_TEXT]; /* "&& rest" / "|| rest" / "then ... fi" */
} zxsh_hold_t;

typedef struct {
    char name[ZXSH_NAME_MAX];
    char value[ZXSH_VALUE_MAX];
} zxsh_var_t;

typedef struct {
    uint8_t kind;
    uint32_t off, len;
} zxsh_tok_t;

struct zxsh {
    const zxsh_ops_t *ops;
    void *u;
    uint32_t caps;
    bool interactive;

    lpx_t last;                 /* the last finished command's verdict */
    char why[ZXSH_WHY_MAX];     /* its reason */
    int exit_code;              /* for `exit` */

    zxsh_var_t vars[ZXSH_MAX_VARS];
    zxsh_hold_t held[ZXSH_MAX_HELD];
    uint32_t next_hold;

    lpx_qrec_t qrec[ZXSH_QLOG_CAP];
    lpx_qlog_t qlog;

    /* evaluation state (one input at a time; nested resolve re-enters) */
    const char *src;
    zxsh_tok_t tok[ZXSH_MAX_TOKENS];
    uint32_t ntok, pos;
    bool aborted, need_more, syntax, exiting, locked;
    bool in_clause;             /* inside an if's clauses: both/neither are keywords */
    unsigned depth;
    char last_why[ZXSH_WHY_MAX];
    char pending[ZXSH_HOLD_TEXT + 32]; /* a resolved hold, run after this input */
    bool has_pending;
    char *cap_buf;              /* builtin output captured for <?> */
    uint32_t *cap_len;
    char arena[ZXSH_ARENA];
    uint32_t arena_used;
    char capa[ZXSH_CAPTURE], capb[ZXSH_CAPTURE];
};

void zxsh_init(zxsh_t *sh, const zxsh_ops_t *ops, void *u, bool interactive);

/* Evaluate one input (a line, several lines, or a whole script). */
zxsh_status_t zxsh_eval(zxsh_t *sh, const char *src);

/* Variables. */
const char *zxsh_get(const zxsh_t *sh, const char *name);
bool zxsh_set(zxsh_t *sh, const char *name, const char *value);

/* A verdict's short letter (T F B N, '!' when forbidden) and long form. */
char zxsh_letter(lpx_t t);
const char *zxsh_base_word(lpx_t t);

/* Count of live holds. */
unsigned zxsh_held_count(const zxsh_t *sh);

/* Capability names <-> bits ("exec,read" -> mask); 0 for an unknown name. */
uint32_t zxsh_cap_parse(const char *list);

/* The tag the host uses for a finished command. */
static inline lpx_t zxsh_tag(uint8_t base, uint8_t pol)
{
    return lpx_make(base, pol, LPX_PH_MANIFEST);
}

#endif /* ZXSH_CORE_H */
