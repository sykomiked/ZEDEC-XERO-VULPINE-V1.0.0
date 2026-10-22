/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* forage_model.h — Forage, node bounties, as a model (zxm.h): work for this
 * node found on its board, ranked by value per minute and claimed inside
 * the node's swarm and power budgets, bounties of its own posted, and
 * serving other nodes turned on or off, every change on the owner's word.
 * The offers, the peer book, the swarm and the power ledger are zxvd's
 * (net.offers, net.command, peer.status, peer.serve, swarm.state,
 * power.status); the model keeps a checked copy for the view. The rules
 * are forage_rules.h (FR1-FR4).
 *
 *   FG1 WHAT IS READ IS CHECKED. An answer is taken whole or not at all: a
 *       count that is not a decimal of at most 18 digits, a NodeID that is
 *       not 64 hex digits, a resource, role or posture zxvd does not name,
 *       or a number out of its range is refused (ZXM_E_PROTOCOL) and the
 *       model keeps what it had.
 *   FG2 THE BOARD. zxvd has no bounty board, so a bounty another node
 *       posts (its NodeID, resource, units, gross value, minutes, swarm
 *       tokens a cycle and draw) is entered by the owner, up to
 *       FORAGE_BOUNTIES, each checked against FR4 and its ranges. Its net
 *       is FR1 of its value, and forage_rank places every bounty by FR3
 *       over the nets and minutes.
 *   FG3 THE BUDGETS. The owner sets the swarm tokens a cycle bounties may
 *       take and the node's power cap in milliwatts (1..FORAGE_NINE each,
 *       on the owner's word); until then no claim fits. The swarm's budget
 *       is held to the cycle's tokens scaled by the power posture
 *       (tokens_per_cycle * token_scale_permille / 1000, floored). A claim
 *       fits by FR2 on the margins: tokroom = that budget - the tokens of
 *       claims already sent - the work's, mwroom = the power cap - the
 *       node's draw now - the work's.
 *   FG4 THE OWNER APPROVES. forage_claim and forage_post only check and
 *       hold a command (a sell to the poster at the bounty's net, or a
 *       want at the net of the value) with a notice; nothing is sent. Only
 *       forage_approve with the owner's confirmation sends it
 *       (net.command); without it nothing is sent (ZXM_E_STATE). One waits
 *       at a time; forage_cancel drops it. Forage never sends a network
 *       mode.
 *   FG5 SERVING STAYS OFF. peer.serve is called only by forage_serve and
 *       forage_serve_stop, each on the owner's confirmation; nothing else
 *       in Forage turns serving on.
 *   FG6 THE WALLET AFTER. With the peer book read, a claim's notice shows
 *       coffer.balance(wallet, net) and a posted bounty's
 *       coffer.balance(wallet, -(net + fee(net))).
 *   FG7 THE RECORDS. Each claim and post the owner was asked about is kept
 *       in a bounded list of FORAGE_RECORDS (the oldest dropped first),
 *       each fitting FR4: waiting, then sent, refused by zxvd or
 *       cancelled.
 *
 * No drawing, no allocation, integers only. */
#ifndef FORAGE_MODEL_H
#define FORAGE_MODEL_H

#include "coffer_model.h"
#include "forage_rules.h"
#include "zxm.h"

#define FORAGE_BOUNTIES 32u  /* FG2 */
#define FORAGE_RECORDS  32u  /* FG7 */
#define FORAGE_WANTS    16u  /* net.offers' wants */
#define FORAGE_PENDING  32u  /* net.offers' outstanding */
#define FORAGE_NODE     64u  /* hex digits of a NodeID */
#define FORAGE_CMD_MAX  255u /* net.command's maxLength */
#define FORAGE_ANSWER   31u
#define FORAGE_NOTICE   255u

typedef enum { FORAGE_CLAIM = 1, FORAGE_POST = 2 } forage_kind_t;
typedef enum { FORAGE_COMPUTE = 1, FORAGE_STORAGE = 2 } forage_res_t;
typedef enum {
    FORAGE_WAITING = 0,
    FORAGE_SENT = 1,
    FORAGE_REFUSED_BY_ZXVD = 2,
    FORAGE_CANCELLED = 3,
} forage_status_t;

typedef struct {
    char peer[FORAGE_NODE + 1]; /* the node that posted it */
    forage_res_t resource;
    int64_t units, amount, minutes, tokens, mw;
    int64_t net;  /* FR1 of amount */
    int64_t rank; /* FR3; 0 until ranked */
    bool claimed; /* a claim of it was sent */
} forage_bounty_t;

typedef struct {
    int64_t no; /* its number, 0..FORAGE_RECORD_MAX, wrapping */
    forage_kind_t kind;
    int64_t tokens, mw, amount, minutes;
    forage_status_t status;
} forage_record_t;

typedef struct {
    bool pending;
    forage_kind_t kind;
    uint32_t bounty; /* a claim: its place on the board */
    int64_t amount, net, fee, tokens;
    bool projected; /* wallet_after is known (FG6) */
    int64_t wallet_after;
    char cmd[FORAGE_CMD_MAX + 1];
    uint32_t record; /* its place in rec */
} forage_held_t;

typedef struct {
    zxm_link_t *link;
    /* FG3: the owner's budgets, 0 until set */
    int64_t budget_tokens, budget_mw;
    /* swarm.state */
    bool swarm_known;
    int64_t cycle_tokens, bounties_returned, quest_contributed, quest_refused;
    /* power.status */
    bool power_known;
    int64_t draw_mw, scale_permille;
    /* peer.status, peer.serve */
    bool peer_known, serving, book_ok;
    int64_t serve_price, wallet;
    /* net.offers */
    bool offers_known;
    int64_t share_compute, share_storage;
    uint32_t nwant, nsell, nbuy; /* open wants; receipts waiting, sold and bought */
    /* FG2 */
    forage_bounty_t board[FORAGE_BOUNTIES];
    uint32_t nboard;
    bool ranked;
    /* FG4, FG7 */
    forage_held_t held;
    forage_record_t rec[FORAGE_RECORDS];
    uint32_t nrec;
    int64_t next_no;
    char answer[FORAGE_ANSWER + 1]; /* net.command's last result */
    char notice[FORAGE_NOTICE + 1];
} forage_t;

void forage_init(forage_t *f, zxm_link_t *link);

/* FG3: the swarm tokens a cycle bounties may take and the power cap in
 * milliwatts, each 1..FORAGE_NINE (ZXM_E_ARG otherwise), set only on the
 * owner's word (ZXM_E_STATE without it); nothing changes when refused. */
int forage_set_budget(forage_t *f, int64_t tokens, int64_t mw, bool owner_confirmed);

/* FG1: swarm.state, power.status, peer.status and net.offers. */
int forage_read_swarm(forage_t *f);
int forage_read_power(forage_t *f);
int forage_read_peer(forage_t *f);
int forage_read_offers(forage_t *f);

/* FG2: a bounty another node posted, onto the board; ZXM_E_ARG for a value
 * out of range, ZXM_E_STATE when the board is full. */
int forage_add_bounty(forage_t *f, const char *peer, forage_res_t resource, int64_t units,
                      int64_t amount, int64_t minutes, int64_t tokens, int64_t mw);
/* FG2: FR3 over the board; ZXM_E_STATE when it is empty. */
int forage_rank(forage_t *f);
/* FG3: the margins a bounty's claim would leave; false until the swarm and
 * the power ledger are read. */
bool forage_rooms(const forage_t *f, uint32_t bounty, int64_t *tokroom, int64_t *mwroom);
/* The best-ranked unclaimed bounty whose claim fits (ties: the earlier);
 * ZXM_E_STATE when the board is not ranked or none fits. */
int forage_best(const forage_t *f, uint32_t *bounty);

/* FG4: check a claim or a bounty of the node's own and hold it; nothing is
 * sent. */
int forage_claim(forage_t *f, uint32_t bounty);
int forage_post(forage_t *f, forage_res_t resource, int64_t units, int64_t amount);
/* FG4: send the held command on the owner's word; its record says what
 * zxvd did. */
int forage_approve(forage_t *f, bool owner_confirmed);
int forage_cancel(forage_t *f);

/* FG5: serve other nodes at price minor units an answer (0..FORAGE_NINE),
 * or stop; each only on the owner's word. */
int forage_serve(forage_t *f, int64_t price, bool owner_confirmed);
int forage_serve_stop(forage_t *f, bool owner_confirmed);

/* FG7: every record fits FR4. */
bool forage_records_fit(const forage_t *f);

#endif
