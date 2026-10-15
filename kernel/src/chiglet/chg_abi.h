/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_abi.h — the chiglet's system ABI: typed calls from the chiglet (or the
 * person) to native kernel operations, each one gated before it runs.
 *
 * The chiglet is the system's intelligence and governor (docs/
 * ARCHITECTURE.md): at the end of boot the console belongs to the chiglet
 * session, and the chiglet drives the machine through this table instead of
 * a shell's string commands. The model never touches a register: its output
 * is a typed call, the call is checked, and only then does the kernel's own
 * deterministic code (drivers, the cell fabric) do the work. IRQs, page
 * tables and DMA addressing stay kernel code.
 *
 *   A1 OPS. A fixed table: STORAGE_DMA_READ, FRAMEBUFFER_BLIT, CELL_RELOAD,
 *      NETWORK_PULSE, CELL_QUARANTINE, MODEL_EXEC. Each has an exact argument count and
 *      per-argument bounds, and a kind: LOOK (reads the disk or redraws the
 *      person's own screen; changes nothing that persists) or CHANGE
 *      (changes the running machine or reaches outside it). Indexes are
 *      part of the format: append, never renumber.
 *   A2 TEXT. A call is written "<op> <n> ..." with decimal unsigned
 *      arguments, one space apart: the form a grammar-constrained model
 *      emits and a person can type. Anything else does not parse.
 *   A3 GATE. Three checks, joined with lpres_conjoin (the chain order, so
 *      only TRUE authorises): the arguments are in bounds; the session's
 *      grants allow the op (grants bind the chiglet; a call the person types
 *      is their own); and the rulebook's verdict on the call (NEITHER when
 *      no rule speaks). FALSE refuses. TRUE runs a LOOK call, and runs a
 *      CHANGE call the person typed. Every other case is HELD: a CHANGE call
 *      from the chiglet, a NEITHER (no rule says yes: "not sure") or a BOTH
 *      (the checks disagree: shown to the person, never averaged).
 *   A4 HOLD. A held call keeps its sequence number and a SHA-256 digest of
 *      (seq, op, args). It runs only when the person accepts that exact
 *      digest (at least its first CHGA_ACCEPT_MIN bytes), and the gate is
 *      checked again at that moment: the accept answers "not sure" and a
 *      dispute, but a grant withdrawn since, or a rule that now says FALSE,
 *      refuses it. Refusing drops it. Slots are bounded (CHGA_HELD).
 *   A5 ORDER. Check the call, then reserve: a journal slot (and a held slot
 *      when it will be held). A full journal or held table is backpressure:
 *      CHGA_E_FULL and nothing is written. Then run the kernel's handler,
 *      then commit the journal record. The journal is a bounded ring
 *      (head - tail >= CHGA_JOURNAL is full) the kernel drains with
 *      chga_pop.
 *   A6 FAULTS. A handler's failure is recorded in the journal with its
 *      code; it never stops the session. A faulted cell is quarantined by
 *      the fabric's own fault model, not by a panic.
 *   A7 MODEL_EXEC <coord> <event>. Run the model at a coordinate on one
 *      input event: "EXEC @coordinate <event>" of the IP-BIOS design. coord
 *      is a 64-bit Grid Chain word in domain 0x60 (models): domain 8 bits,
 *      place 16, item 8, extension 16, format 8, language 8, high byte
 *      first. The word is a key the kernel resolves to a model cell, never
 *      an address or code. event is the oseq ordinal of the input (>= 1).
 *      It is a LOOK call: it reads and computes and changes nothing that
 *      persists; what the model answers is data, and anything it asks the
 *      machine to do comes back through chga_submit as a chiglet call. The
 *      kernel's handler resolves and runs the cell; a coordinate with no
 *      model refuses in the handler (A6), it never guesses.
 *   A8 EVENT CHAIN. Every journal record is folded into a hash chain as
 *      it is committed: head = SHA-256("zxv/chga/event/v1" || head ||
 *      record bytes), with the record encoded by chga_rec_encode
 *      (CHGA_REC_BYTES, little-endian). Each record keeps the head after it
 *      (rec.head), so a reader that has the records in order recomputes
 *      every head and finds any change, removal or reordering. The chain
 *      starts from 32 zero bytes. It is the same fold as the chronicle's
 *      (kernel/src/chronicle) over this session's own calls.
 *
 * Freestanding C11: no libc, no allocation, no float.
 */
#ifndef ZXV_CHG_ABI_H
#define ZXV_CHG_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../lpres/lpres.h"

#define CHGA_ARGS         4u
#define CHGA_HELD         8u
#define CHGA_JOURNAL      32u
#define CHGA_DIGEST       32u
#define CHGA_ACCEPT_MIN   8u  /* bytes of the digest the person must give */
#define CHGA_READ_MAX     8u  /* sectors per STORAGE_DMA_READ */
#define CHGA_CELLS_MAX    64u /* cell index bound */
#define CHGA_SCREEN_MAX   16384u
#define CHGA_DOMAIN_MODEL 0x60u /* A7: the Grid Chain domain of models */
#define CHGA_REC_BYTES    (8u + 1u + 1u + 1u + 1u + 4u + 1u + 8u * CHGA_ARGS) /* A8 */

enum {                            /* A1 */
       CHGA_STORAGE_DMA_READ = 0, /* lba count */
       CHGA_FRAMEBUFFER_BLIT,     /* x y w h */
       CHGA_CELL_RELOAD,          /* cell */
       CHGA_NETWORK_PULSE,        /* (none) */
       CHGA_CELL_QUARANTINE,      /* cell */
       CHGA_MODEL_EXEC,           /* coord event (A7) */
       CHGA_OPS
};

enum { CHGA_K_LOOK = 0, CHGA_K_CHANGE = 1 };
enum { CHGA_FROM_CHIGLET = 0, CHGA_FROM_PERSON = 1 };

enum {
    CHGA_OK = 0,
    CHGA_E_ARG = -1,    /* malformed call or bad parameters */
    CHGA_E_FULL = -2,   /* A5 backpressure */
    CHGA_E_NOHELD = -3, /* no held call with that sequence number */
    CHGA_E_DIGEST = -4  /* A4: the digest does not match */
};

/* what happened to a call (journal and result) */
enum {
    CHGA_RAN = 0,   /* the handler returned 0 */
    CHGA_FAILED,    /* the handler returned nonzero (A6) */
    CHGA_REFUSED,   /* the gate said FALSE */
    CHGA_HELD_CALL, /* waiting for the person */
    CHGA_DROPPED    /* the person refused it */
};

typedef struct {
    uint8_t op;
    uint8_t nargs;
    uint64_t arg[CHGA_ARGS];
} chga_call_t;

/* the rulebook: its verdict on a call (NEITHER when no rule speaks) */
typedef lpres_state_t (*chga_rule_fn)(void *ctx, const chga_call_t *c);
/* the kernel's handler for an op: 0 on success, else a code it chooses */
typedef int (*chga_run_fn)(void *ctx, const chga_call_t *c);

typedef struct {
    bool used;
    uint64_t seq;
    chga_call_t call;
    uint8_t digest[CHGA_DIGEST];
} chga_held_t;

typedef struct {
    uint64_t seq;
    uint8_t op;
    uint8_t from;
    uint8_t outcome;       /* CHGA_RAN .. CHGA_DROPPED */
    lpres_state_t verdict; /* A3 */
    int32_t rc;            /* the handler's code when it ran */
    uint8_t nargs;
    uint64_t arg[CHGA_ARGS];   /* the call's arguments */
    uint8_t head[CHGA_DIGEST]; /* A8: the chain's head after this record */
} chga_rec_t;

typedef struct {
    uint32_t grants; /* bit i: the chiglet may call op i */
    chga_rule_fn rule;
    void *rule_ctx;
    chga_run_fn run;
    void *run_ctx;
    uint64_t seq; /* last sequence number given out */
    chga_held_t held[CHGA_HELD];
    chga_rec_t journal[CHGA_JOURNAL];
    uint32_t head, tail;
    uint8_t chain[CHGA_DIGEST]; /* A8: the head of the event chain */
} chga_session_t;

typedef struct {
    uint64_t seq;
    uint8_t outcome;
    lpres_state_t verdict;
    int32_t rc;
    uint8_t digest[CHGA_DIGEST]; /* set when held */
} chga_result_t;

/* A1/A2: names */
const char *chga_op_name(uint32_t op); /* "" out of range */
int chga_op_find(const char *name, size_t len);
int chga_op_kind(uint32_t op); /* -1 out of range */
uint8_t chga_op_nargs(uint32_t op);
/* A2: parse "<op> <n> ..."; CHGA_OK or CHGA_E_ARG */
int chga_parse(const char *s, size_t len, chga_call_t *c);

/* rule and run may not be NULL; grants is the chiglet's starting set */
int chga_init(chga_session_t *s, uint32_t grants, chga_rule_fn rule, void *rule_ctx,
              chga_run_fn run, void *run_ctx);
void chga_set_grants(chga_session_t *s, uint32_t grants);

/* A3/A5: submit a call; res says what happened */
int chga_submit(chga_session_t *s, const chga_call_t *c, uint8_t from, chga_result_t *res);
/* A4: the person accepts a held call by its digest (dlen >= CHGA_ACCEPT_MIN) */
int chga_accept(chga_session_t *s, uint64_t seq, const uint8_t *digest, size_t dlen,
                chga_result_t *res);
/* A4: the person refuses a held call */
int chga_refuse(chga_session_t *s, uint64_t seq);
/* the held call with that sequence number, or NULL */
const chga_held_t *chga_held(const chga_session_t *s, uint64_t seq);
/* A5: the oldest journal record; false when the journal is empty */
bool chga_pop(chga_session_t *s, chga_rec_t *out);
/* A8: the record's bytes: seq LE8, op, from, outcome, verdict, rc LE4,
 * nargs, then CHGA_ARGS arguments LE8 (unused ones 0) */
void chga_rec_encode(const chga_rec_t *r, uint8_t out[CHGA_REC_BYTES]);
/* A8: the head after a record: SHA-256(tag || prev || record bytes) */
void chga_chain_next(const uint8_t prev[CHGA_DIGEST], const chga_rec_t *r,
                     uint8_t out[CHGA_DIGEST]);
/* A8: the session's current head */
const uint8_t *chga_chain_head(const chga_session_t *s);

#endif /* ZXV_CHG_ABI_H */
