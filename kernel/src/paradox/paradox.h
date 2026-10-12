/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* paradox.h — the paradox engine (roadmap 6.6b, steps P0.1-P0.4).
 *
 * A paradox is a unit of work whose lpres state is Both (or Neither). This
 * module handles it one level up: it never decides whether the claim is
 * "really" true. It reads the 64-state tag (lpres_hex.h) and the evidence,
 * gives the paradox one of four KINDS, and runs a fixed, logged operator for
 * that kind. There is no new truth type: the kind is carried in the tag's
 * polarity bits and in the classification result.
 *
 *   KINDS (P0.1)
 *     CONSTRUCTIVE  Both, negentropic (the evidence margin is growing).
 *     DESTRUCTIVE   Both, entropic (margin shrinking, or evidence stale).
 *     NEUTRAL       Both, neutral (steady) past the potential phase; or
 *                   Neither (missing information).
 *     SPECIAL       Both, neutral, potential phase (no trend yet), with no
 *                   registry signature: the unclassified frontier.
 *   A signature found in the registry (SHA-256 of a canonical description
 *   of the anomaly) overrides the trend. True and False are not paradoxes
 *   (PDX_KIND_NONE).
 *
 *   FORBIDDEN stays separate. A tag with forbidden polarity marks a broken
 *   invariant: every operator refuses it and rolls back, whatever the kind.
 *
 *   OPERATORS (P0.2). Each returns an outcome and appends one record to a
 *   hash-chained log (pdx_log_t), so a run can be replayed and checked. When
 *   the log is full the operator changes nothing (except the always-safe
 *   rollback of destructive work) and returns PDX_OUT_LOG_FULL: halt.
 *     constructive  a bounded parameter update. Applied only when every
 *                   proposed value is inside its published [lo, hi];
 *                   otherwise a proposal for the owner. The values before
 *                   the update are kept, and the next evaluation rolls them
 *                   back if the tension grew. The API has no balance input,
 *                   so it cannot change a balance.
 *     destructive   detection by step limit, budget, cycle (a repeated
 *                   state fingerprint) and an invariant callback; the work
 *                   is quarantined in lpres_hex's hashed quarantine log, the
 *                   state is restored from its checkpoint, and a RepairProof
 *                   carries the hashes and the signature for peers. The API
 *                   has no sender route: it contains, it never replies.
 *     neutral       up to N candidate branches are kept; nothing settles
 *                   until one candidate has decisive evidence or the owner
 *                   picks one.
 *     special       an append-only frontier record (never edited or erased),
 *                   a draft classification for review, and on acceptance a
 *                   NEW registry entry. Drafts that touch money or devices
 *                   need the owner; others may pass by consensus.
 *
 *   COMPLEX PARADOX (P0.4). A graph of up to PDX_GRAPH_MAX nodes with
 *   dependency edges and an integer tension pair (magnitude, direction).
 *   Nodes are sorted by their id (the hash of the signed input) and each
 *   pass resolves in a fixed order: destructive and forbidden first
 *   (contained; every dependant, transitively, tainted), then special
 *   (sandboxed; dependants wait), then neutral (held), then constructive
 *   (applied only when no contained, sandboxed, waiting or held node reaches
 *   it). Containment is sticky. Each pass extends a SHA-256 chain. At the
 *   step limit the graph escalates to the owner.
 *
 * HONEST LIMITS. This is contradiction-tolerant bookkeeping, not a way to
 * make both sides of a contradiction true. Detection is exactly the checks
 * listed (step limit, budget, cycle, invariant callback, registry lookup);
 * an attack those checks do not see is not detected. Signing the log heads
 * and sending signatures to peers are the caller's jobs. Freestanding C11:
 * no libc, no allocation, no floating point, no division; every array is
 * the caller's, with a fixed capacity.
 */
#ifndef PARADOX_H
#define PARADOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../lpres/lpres_hex.h"

#define PDX_HASH 32u

typedef enum {
    PDX_KIND_CONSTRUCTIVE = 0,
    PDX_KIND_DESTRUCTIVE = 1,
    PDX_KIND_NEUTRAL = 2,
    PDX_KIND_SPECIAL = 3,
    PDX_KIND_NONE = 4 /* True or False: not a paradox */
} pdx_kind_t;

typedef enum {
    PDX_OUT_APPLIED = 0,        /* constructive update applied */
    PDX_OUT_KEPT,               /* an applied update survived its evaluation */
    PDX_OUT_PROPOSAL_FOR_OWNER, /* outside the bounds or unapproved: the owner decides */
    PDX_OUT_ROLLED_BACK,        /* tension grew, or forbidden: previous values restored */
    PDX_OUT_CONTAINED,          /* destructive: quarantined and rolled back */
    PDX_OUT_DEFERRED,           /* neutral: still holding every branch */
    PDX_OUT_SETTLED,            /* neutral: one branch chosen */
    PDX_OUT_RECORDED,           /* special: frontier record written, or draft made */
    PDX_OUT_ACCEPTED,           /* special: reviewed draft joined the registry */
    PDX_OUT_PENDING,            /* graph: more passes needed */
    PDX_OUT_RESOLVED,           /* graph: every node reached a final state */
    PDX_OUT_ESCALATE_TO_OWNER,  /* graph: step limit reached, the graph goes to the owner */
    PDX_OUT_REFUSED,            /* wrong kind or bad input; nothing changed */
    PDX_OUT_LOG_FULL            /* a log is full: halt (fail closed) */
} pdx_out_t;

/* The lpres_hex action an outcome amounts to (proceed, wait, refuse,
 * escalate, rollback). */
lpx_action_t pdx_out_action(pdx_out_t o);
const char *pdx_kind_name(uint8_t kind);
const char *pdx_out_name(pdx_out_t o);

/* ---- the operator log: append-only, hash-chained ---- */
enum {
    PDX_OP_CONSTRUCTIVE = 1,
    PDX_OP_DESTRUCTIVE = 2,
    PDX_OP_NEUTRAL = 3,
    PDX_OP_SPECIAL = 4,
    PDX_OP_DRAFT = 5,
    PDX_OP_REVIEW = 6,
    PDX_OP_GRAPH = 7
};

typedef struct {
    uint64_t seq;
    uint8_t op, kind, tag, outcome;
    uint32_t aux;        /* op-specific count: steps, branch, pass ... */
    uint8_t a[PDX_HASH]; /* op-specific digest (see each operator) */
    uint8_t b[PDX_HASH]; /* op-specific digest */
    uint8_t prev[PDX_HASH];
    uint8_t hash[PDX_HASH];
} pdx_rec_t;

typedef struct {
    pdx_rec_t *rec;
    uint32_t cap, n;
    uint8_t head[PDX_HASH]; /* hash of the last record (zeros: empty) */
} pdx_log_t;

void pdx_log_init(pdx_log_t *l, pdx_rec_t *storage, uint32_t cap);
/* a or b may be NULL (zeros). False when full or l is NULL. */
bool pdx_log_append(pdx_log_t *l, uint8_t op, uint8_t kind, lpx_t tag, uint8_t outcome,
                    uint32_t aux, const uint8_t a[PDX_HASH], const uint8_t b[PDX_HASH]);
/* Recompute every link and the head; false if anything was altered. */
bool pdx_log_verify(const pdx_log_t *l);

/* ---- P0.1: signature registry and classifier ---- */

/* SHA-256 of "zxv/pdx-sig/v1" || data: the signature of an anomaly, from a
 * canonical description the caller builds. */
void pdx_signature(const uint8_t *data, size_t len, uint8_t out[PDX_HASH]);

#define PDX_SRC_SEED     0xFFFFFFFFFFFFFFFFull /* entry not from a frontier record */
#define PDX_DRAFT_MONEY  1u
#define PDX_DRAFT_DEVICE 2u

typedef struct {
    uint8_t sig[PDX_HASH];
    uint8_t kind;   /* constructive, destructive or neutral */
    uint8_t reason; /* caller's code */
    uint8_t flags;  /* PDX_DRAFT_* */
    uint64_t src;   /* frontier record seq it was learned from, or PDX_SRC_SEED */
} pdx_sig_t;

typedef struct {
    pdx_sig_t *ent;
    uint32_t cap, n;
} pdx_registry_t;

void pdx_reg_init(pdx_registry_t *r, pdx_sig_t *storage, uint32_t cap);
/* Append-only: an existing signature is never edited (false), SPECIAL and
 * NONE are not registry kinds (false), and a full registry refuses. */
bool pdx_reg_add(pdx_registry_t *r, const uint8_t sig[PDX_HASH], uint8_t kind, uint8_t reason,
                 uint8_t flags, uint64_t src);
/* Index of the signature, or -1. */
int32_t pdx_reg_find(const pdx_registry_t *r, const uint8_t sig[PDX_HASH]);

/* Evidence counts before and after, as lpres keeps them, and whether the
 * newest evidence is past the caller's freshness limit. */
typedef struct {
    uint32_t pos0, neg0, pos1, neg1;
    bool stale;
} pdx_evidence_t;

typedef struct {
    uint8_t kind;      /* pdx_kind_t */
    lpx_t tag;         /* the input tag with the kind in its polarity bits */
    bool forbidden;    /* the input tag was forbidden: refuse and roll back */
    int32_t reg_index; /* registry entry that decided the kind, or -1 */
} pdx_class_t;

/* The kind a tag reads as on its own (Level 1): Both by polarity (neutral
 * at potential phase is SPECIAL), Neither NEUTRAL, True/False NONE. A
 * forbidden tag reads as NONE: forbidden is not a kind. */
uint8_t pdx_kind_of_tag(lpx_t t);

/* Classify. ev NULL means no evidence trend (polarity neutral). sig NULL
 * means no signature to look up. A known signature overrides the trend for
 * a Both or a Neither. The result's tag keeps base and phase and carries
 * the kind's polarity (constructive negentropic, destructive entropic,
 * neutral and special neutral); a forbidden input stays forbidden (X4). */
pdx_class_t pdx_classify(lpx_t tag, const pdx_evidence_t *ev, const pdx_registry_t *reg,
                         const uint8_t sig[PDX_HASH]);

/* ---- P0.2 constructive: bounded parameter update ---- */
typedef struct {
    int64_t *val;      /* live params, n entries (shares, weights, timings; never balances) */
    int64_t *prev;     /* values before the last applied update, n entries */
    const int64_t *lo; /* published bounds, n entries each */
    const int64_t *hi;
    uint32_t n;
    bool armed;          /* an applied update awaits its next evaluation */
    uint32_t tension_at; /* tension when it was applied */
} pdx_params_t;

/* One evaluation, then (if synth is not NULL) one proposed update.
 *   forbidden class      -> an armed update is restored; ROLLED_BACK.
 *   kind not constructive -> REFUSED.
 *   armed and tension > tension_at -> previous values restored; ROLLED_BACK
 *                           (the new synth is not considered this round).
 *   armed otherwise      -> the update is kept (disarmed); KEPT if synth NULL.
 *   synth outside [lo,hi] for any entry, or lo > hi -> PROPOSAL_FOR_OWNER, no change.
 *   else                 -> prev = val, val = synth, armed; APPLIED.
 * Record: aux = n, a = SHA-256 of the values now live, b = SHA-256 of synth. */
pdx_out_t pdx_constructive(pdx_params_t *p, const int64_t *synth, uint32_t tension,
                           const pdx_class_t *c, pdx_log_t *log);

/* ---- P0.2 destructive: detect, quarantine, roll back, prove ---- */
enum {
    PDX_TRIP_STEPS = 1u,     /* steps >= step_limit */
    PDX_TRIP_BUDGET = 2u,    /* spent >= budget */
    PDX_TRIP_CYCLE = 4u,     /* a state fingerprint repeated */
    PDX_TRIP_INVARIANT = 8u, /* the invariant callback said no */
    PDX_TRIP_FORBIDDEN = 16u /* the tag was already forbidden */
};

typedef bool (*pdx_invariant_fn)(const uint8_t *state, uint32_t len, void *ctx);

typedef struct {
    uint32_t steps, step_limit; /* a limit of 0 means none */
    uint32_t spent, budget;     /* a budget of 0 means none */
    const uint64_t *trace;      /* state fingerprints in visit order */
    uint32_t trace_n;
    pdx_invariant_fn invariant; /* may be NULL */
    void *ctx;
} pdx_watch_t;

typedef struct {
    uint8_t what[PDX_HASH];   /* SHA-256 binding everything below (layout in paradox.c) */
    uint8_t before[PDX_HASH]; /* SHA-256 of the state as found */
    uint8_t after[PDX_HASH];  /* SHA-256 of the state after rollback */
    uint8_t sig[PDX_HASH];    /* signature to share with peers */
    uint32_t steps;
    uint32_t trips; /* PDX_TRIP_* */
    uint64_t qseq;  /* the quarantine record */
} pdx_repair_t;

/* SHA-256 of "zxv/pdx-state/v1" || len (u32 LE) || state: the digest the
 * RepairProof's before and after carry (state may be NULL only when len is 0). */
void pdx_state_hash(const uint8_t *state, uint32_t len, uint8_t out[PDX_HASH]);

/* Which checks trip. */
uint32_t pdx_detect(const pdx_watch_t *w, const uint8_t *state, uint32_t len);

/* Contain when the class is destructive or forbidden, or any check trips;
 * otherwise REFUSED. Containment: the quarantine log gets the tag (marked
 * forbidden, as X6 requires), the trips as reason, the submitter and the
 * before-hash; state is restored from checkpoint; the proof is filled; the
 * operator log gets aux = steps, a = proof.what, b = proof.sig. pattern is
 * the canonical description of the trap (signature = pdx_signature of it).
 * A full log still restores the state, then returns LOG_FULL. */
pdx_out_t pdx_destructive(const pdx_class_t *c, const pdx_watch_t *w, uint8_t *state,
                          const uint8_t *checkpoint, uint32_t len, const uint8_t *pattern,
                          uint32_t pattern_len, uint32_t submitter, lpx_qlog_t *q, pdx_log_t *log,
                          pdx_repair_t *proof);

/* ---- P0.2 neutral: deferral with branch tracking ---- */
typedef struct {
    uint8_t id[PDX_HASH]; /* the candidate's digest */
    uint32_t pos, neg;    /* evidence for and against it */
} pdx_branch_t;

typedef struct {
    pdx_branch_t *br;
    uint32_t cap, n;
    uint32_t decisive; /* net-evidence lead a candidate needs (>= 1) */
    int32_t settled;   /* chosen branch, -1 while deferred */
} pdx_hold_t;

void pdx_hold_init(pdx_hold_t *h, pdx_branch_t *storage, uint32_t cap, uint32_t decisive);
/* Add a candidate (a duplicate id is the same candidate). False when full,
 * settled or NULL. */
bool pdx_hold_add(pdx_hold_t *h, const uint8_t id[PDX_HASH]);
/* Evidence for or against a candidate. False when unknown or settled. */
bool pdx_hold_evidence(pdx_hold_t *h, const uint8_t id[PDX_HASH], bool positive);
/* The candidate whose net evidence (pos - neg) leads every other by at
 * least `decisive` (alone: net >= decisive), or -1. */
int32_t pdx_hold_decisive(const pdx_hold_t *h);
/* Settle on decisive evidence, or on owner_pick (an index, -1 for none).
 * Forbidden -> ROLLED_BACK, nothing settles; not neutral -> REFUSED.
 * Record: aux = settled index (or n), a = SHA-256 of the branch set,
 * b = settled id (zeros while deferred). */
pdx_out_t pdx_neutral(pdx_hold_t *h, const pdx_class_t *c, int32_t owner_pick, pdx_log_t *log);

/* ---- P0.2 special: frontier record, draft, review ---- */
typedef struct {
    uint32_t rec; /* the frontier record it classifies */
    uint8_t rec_hash[PDX_HASH];
    uint8_t sig[PDX_HASH];
    uint8_t kind;   /* proposed kind */
    uint8_t reason; /* reason code */
    uint8_t flags;  /* PDX_DRAFT_MONEY | PDX_DRAFT_DEVICE */
} pdx_draft_t;

/* Write the anomaly to the frontier (its own pdx_log_t, op SPECIAL,
 * a = sig, b = subject digest); it is never edited. Also a record in log
 * (may be the same log). *rec_out = the frontier seq. */
pdx_out_t pdx_special(pdx_log_t *frontier, const pdx_class_t *c, const uint8_t sig[PDX_HASH],
                      const uint8_t subject[PDX_HASH], pdx_log_t *log, uint32_t *rec_out);
/* A draft classification for frontier record `rec`: RECORDED, or REFUSED
 * for an unknown record or a kind that is not constructive, destructive or
 * neutral. Record: aux = rec, a = the record's hash, b = sig. */
pdx_out_t pdx_draft(const pdx_log_t *frontier, uint32_t rec, uint8_t kind, uint8_t reason,
                    uint8_t flags, pdx_draft_t *d, pdx_log_t *log);
/* Review: the draft must still match its frontier record. Money/device
 * drafts need owner_ok; others owner_ok or consensus_ok. Without approval
 * -> PROPOSAL_FOR_OWNER. Accepted -> a new registry entry (src = rec), ACCEPTED; a
 * signature already in the registry -> REFUSED (entries are never edited). */
pdx_out_t pdx_review(pdx_registry_t *reg, const pdx_log_t *frontier, const pdx_draft_t *d,
                     bool owner_ok, bool consensus_ok, pdx_log_t *log);

/* ---- P0.4 complex paradox ---- */
#define PDX_GRAPH_MAX 64u
#define PDX_MAX_DEPS  4u

typedef struct {
    uint32_t mag;
    int8_t dir; /* -1, 0, +1 on the imaginary axis (swarm_emotion valence) */
} pdx_tension_t;

typedef enum {
    PDX_NS_OPEN = 0,    /* not yet resolved */
    PDX_NS_CLEAR,       /* not a paradox (kind NONE) */
    PDX_NS_CONTAINED,   /* destructive: contained (final) */
    PDX_NS_ROLLED_BACK, /* forbidden: refused and rolled back (final) */
    PDX_NS_TAINTED,     /* depends on contained work (final) */
    PDX_NS_SANDBOXED,   /* special: recorded, awaiting review */
    PDX_NS_WAITING,     /* depends on a sandboxed node */
    PDX_NS_HELD,        /* neutral: branches kept */
    PDX_NS_BLOCKED,     /* constructive reached by a held or waiting node */
    PDX_NS_APPLIED      /* constructive: synthesis applied (final) */
} pdx_node_state_t;

typedef struct {
    uint8_t id[PDX_HASH]; /* hash of the signed input: the sort key */
    uint8_t kind;         /* pdx_kind_t */
    bool forbidden;
    pdx_tension_t t;
    uint8_t ndeps;                        /* <= PDX_MAX_DEPS */
    uint8_t deps[PDX_MAX_DEPS][PDX_HASH]; /* ids this node depends on */
} pdx_node_t;

/* An evidence update for one node. Several for the same node in one batch
 * merge by "worst wins" (forbidden > destructive > special > neutral >
 * constructive > none; tension: the larger magnitude, then the larger
 * direction), so a batch gives the same result in any order. */
typedef struct {
    uint8_t id[PDX_HASH];
    uint8_t kind;
    bool forbidden;
    pdx_tension_t t;
} pdx_update_t;

typedef struct {
    const pdx_node_t *node; /* the caller's nodes, any order (not modified) */
    uint32_t n;
    uint8_t ord[PDX_GRAPH_MAX];  /* canonical order: indices sorted by id */
    uint64_t dep[PDX_GRAPH_MAX]; /* by canonical position: what it depends on */
    uint8_t kind[PDX_GRAPH_MAX]; /* by canonical position: current kind */
    uint8_t forb[PDX_GRAPH_MAX];
    pdx_tension_t t[PDX_GRAPH_MAX];
    uint8_t st[PDX_GRAPH_MAX]; /* pdx_node_state_t */
    uint32_t pass, step_limit;
    pdx_tension_t total;     /* net tension of the open nodes after the last pass */
    uint8_t chain[PDX_HASH]; /* SHA-256 chain over every pass */
} pdx_graph_t;

/* Load and sort. False for n > PDX_GRAPH_MAX, a duplicate id, ndeps >
 * PDX_MAX_DEPS, or a dependency id that names no node in the set. */
bool pdx_graph_load(pdx_graph_t *g, const pdx_node_t *nodes, uint32_t n, uint32_t step_limit);
/* Apply a batch of updates (order-free, see pdx_update_t). Contained,
 * rolled-back and tainted nodes stay so. False if an id is unknown (that
 * update is skipped; the rest apply). */
bool pdx_graph_update(pdx_graph_t *g, const pdx_update_t *u, uint32_t n);
/* One pass in the fixed order. RESOLVED when every node is final (clear,
 * contained, rolled back, tainted or applied); else ESCALATE_TO_OWNER once
 * pass == step_limit; else PENDING. Record: aux = pass, a = the pass
 * digest (every node's id, kind and state in canonical order, and the
 * total), b = the new chain. */
pdx_out_t pdx_graph_pass(pdx_graph_t *g, pdx_log_t *log);
/* State of the node with this id, or -1. */
int32_t pdx_graph_state(const pdx_graph_t *g, const uint8_t id[PDX_HASH]);

#endif /* PARADOX_H */
