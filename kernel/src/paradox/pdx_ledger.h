/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pdx_ledger.h — the contradiction-tolerant ledger (roadmap 6.6b, P1.1-P1.4).
 * Design note: docs/PARACONSISTENT_LEDGER.md.
 *
 *   L1 HELD, NEVER EXPLODED. Two signed claims on one slot (sender, nonce)
 *      are a double spend: the slot is Both. Both claims are kept, the
 *      largest amount sits in escrow, and nothing settles until a published
 *      rule, the witnesses (the paradox engine's neutral operator), the
 *      arbiter or the owner picks one. Only one claim per slot ever settles.
 *   L2 CONSERVATION. Liabilities L are every balance plus escrow, summed from
 *      scratch after every epoch of the replay; A = L + E must hold exactly,
 *      with A and E fixed at genesis. If it does not, the state is
 *      FORBIDDEN: the ledger halts, writes a quarantine record and refuses
 *      every later call.
 *   L3 STATE IS A FUNCTION OF THE EVENT SET. Claims and votes are kept as
 *      sets in canonical order (epoch, then slot or voter, then id). The
 *      balances, the escrow and every claim's state are a deterministic
 *      replay of the events at or below the horizon, epoch by epoch, each
 *      epoch seeing only events up to itself. Merge is set union, so any
 *      merge order gives the same digest, and moving the horizon forward
 *      never changes an earlier epoch's outcome.
 *   L4 CONDITIONAL. Events after the horizon move no money (PENDING). A
 *      partitioned node keeps its horizon and accepts claims as PENDING; on
 *      reconnect, claims that cannot all be honoured are decided by the
 *      replay (Both slots go to the funnel, unfundable spends are
 *      INSUFFICIENT and quarantined). Events at or below the horizon are
 *      refused as LATE.
 *
 * Moving the horizon is the caller's finality protocol: it may move only
 * when every peer's events up to it are known. Signature checks are the
 * caller's; the ledger is given the result. Freestanding C11: no libc, no
 * allocation, no floating point, no division; every array is the caller's.
 */
#ifndef PDX_LEDGER_H
#define PDX_LEDGER_H

#include <stdbool.h>
#include <stdint.h>
#include "paradox.h"

#define PLG_HASH       32u
#define PLG_MAX_BRANCH 8u /* the largest conflict_limit (witness branches) */

typedef struct {
    uint8_t id[PLG_HASH]; /* hash of the signed claim: binds everything below */
    uint32_t epoch;       /* >= 1; epoch 0 is genesis */
    uint32_t from, to;    /* account numbers */
    uint32_t nonce;       /* the sender's spend slot */
    uint64_t amount;
    uint64_t sig_seq; /* signed sequence number: the default rule's order */
} plg_claim_t;

enum { PLG_V_WITNESS = 0, PLG_V_ARBITER = 1, PLG_V_OWNER = 2 };

typedef struct {
    uint8_t claim[PLG_HASH]; /* the claim it is about */
    uint32_t epoch;
    uint32_t voter;
    uint8_t kind;  /* PLG_V_* */
    bool positive; /* witness: for or against; arbiter and owner: always true */
} plg_vote_t;

typedef enum {
    PLG_CS_PENDING = 0, /* conditional: after the horizon */
    PLG_CS_SETTLED,     /* won its slot and was paid */
    PLG_CS_LOST,        /* its slot went to another claim, or had closed */
    PLG_CS_HELD,        /* in a dispute; the funds are in escrow */
    PLG_CS_FROZEN,      /* dispute past the claim limit: owner only */
    PLG_CS_INSUFFICIENT /* the sender could not fund it: quarantined */
} plg_cstate_t;

typedef enum {
    PLG_ADDED = 0,       /* stored; alone on its slot */
    PLG_ADDED_BOTH,      /* stored; its slot now has two or more claims */
    PLG_DUPLICATE,       /* already in the set; nothing changed */
    PLG_OK,              /* plg_advance: horizon moved */
    PLG_REFUSED_INVALID, /* bad signature, bad account, zero amount, wrong voter ... */
    PLG_REFUSED_LATE,    /* epoch at or below the horizon */
    PLG_REFUSED_FUNDS,   /* local claim beyond the sender's final balance less pending */
    PLG_FULL,            /* a store or log is full: halt */
    PLG_HALTED,          /* the ledger halted earlier */
    PLG_FORBIDDEN        /* plg_advance: A != L + E; the ledger halted */
} plg_res_t;

/* quarantine reason codes (lpx_qlog reason) */
enum { PLG_Q_INSUFFICIENT = 1, PLG_Q_EQUIVOCATION = 2, PLG_Q_CONSERVATION = 3 };

/* operator-log op codes (after paradox.h's 1..7) */
enum { PLG_OP_CLAIM = 8, PLG_OP_VOTE = 9, PLG_OP_ADVANCE = 10 };

typedef struct {
    plg_claim_t c;
    uint8_t st;     /* plg_cstate_t at the horizon */
    uint8_t logged; /* its quarantine record is written */
} plg_entry_t;

typedef struct {
    uint32_t n_acct;
    const uint64_t *genesis; /* n_acct balances */
    uint64_t assets, equity; /* A and E; A must equal sum(genesis) + E */
    uint32_t n_witness;      /* witness voters are 0 .. n_witness-1 */
    uint32_t arbiter, owner; /* their voter ids */
    uint32_t conflict_limit; /* 2 .. PLG_MAX_BRANCH: more claims -> FROZEN */
    uint32_t decisive;       /* witness lead a claim needs (>= 1) */
    bool first_sig_rule;     /* default rule: lowest signed sequence wins */
} plg_cfg_t;

typedef struct {
    plg_cfg_t cfg;
    uint64_t liab0; /* L at genesis */
    plg_entry_t *cl;
    uint32_t cl_cap, n_cl;
    plg_vote_t *vt;
    uint32_t vt_cap, n_vt;
    uint64_t *bal;  /* n_acct: balances at the horizon */
    uint64_t *hold; /* cl_cap: escrow per slot, at the slot's first claim */
    uint64_t escrow;
    uint32_t horizon;
    bool halted;
    uint8_t digest[PLG_HASH]; /* state digest at the horizon */
    pdx_log_t *log;
    lpx_qlog_t *q;
} plg_t;

/* False for a bad config (no accounts, sum(genesis) overflowing or not equal
 * to A - E, a conflict limit outside 2..PLG_MAX_BRANCH, missing storage). */
bool plg_init(plg_t *lg, const plg_cfg_t *cfg, plg_entry_t *claims, uint32_t claim_cap,
              plg_vote_t *votes, uint32_t vote_cap, uint64_t *bal, uint64_t *hold, pdx_log_t *log,
              lpx_qlog_t *q);

/* Add a claim. sig_ok is the caller's signature check. local: a claim made
 * on this node, which must fit the sender's final balance less their other
 * pending slots; claims arriving by merge are never refused for funds. */
plg_res_t plg_claim(plg_t *lg, const plg_claim_t *c, bool sig_ok, bool local);
/* Add a vote (any claim id; the claim may arrive later). */
plg_res_t plg_vote(plg_t *lg, const plg_vote_t *v, bool sig_ok);
/* Set union: add every event of src to dst. Returns PLG_OK, PLG_FULL or
 * PLG_HALTED; *added and *late (may be NULL) count what happened. */
plg_res_t plg_merge(plg_t *dst, const plg_t *src, uint32_t *added, uint32_t *late);
/* Move the horizon (never back), replay, check A = L + E, quarantine newly
 * final INSUFFICIENT and LOST claims, log the digest. */
plg_res_t plg_advance(plg_t *lg, uint32_t horizon);

/* The claim's index, or -1. */
int32_t plg_find(const plg_t *lg, const uint8_t id[PLG_HASH]);
/* The hexagram tag for a claim state (design note table). */
lpx_t plg_tag(uint8_t st);
/* L recomputed from scratch; false on overflow. */
bool plg_liabilities(const plg_t *lg, uint64_t *out);
/* A = L + E right now. */
bool plg_conserved(const plg_t *lg);
const char *plg_state_name(uint8_t st);
const char *plg_res_name(plg_res_t r);

#endif /* PDX_LEDGER_H */
