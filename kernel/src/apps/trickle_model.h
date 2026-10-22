/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* trickle_model.h — Trickle, streaming payments and tithes, as a model
 * (zxm.h): standing streams (a total paid out over a count of periods)
 * and tithes (a share of each income given on) that the owner sets up,
 * each period paid into the owner's books and read back from them. The
 * books are zxvd's (ledger.*), reached through Coffer's model; the
 * schedule is the model's, since zxvd has no stream method.
 *
 *   TQ1 MONEY IS INTEGERS. Every amount is an int64 count of minor units
 *       of the paying account's currency; the rules are trickle_rules.h
 *       (TK1-TK3). A value that does not fit is refused, never rounded,
 *       cut short or wrapped, and no unit is lost to rounding: a stream
 *       owes floor(total * tick / ticks) less what it has paid (TK1), and
 *       a tithe carries its remainder to the next income (TK2).
 *   TQ2 THE OWNER DECIDES. A stream or tithe starts, changes or stops,
 *       the per-day cap is set, and a period is paid only with
 *       owner_confirmed; it is checked after the arguments and before
 *       anything is sent, and without it the call answers ZXM_E_STATE,
 *       sends nothing and changes nothing (the notice says what waits).
 *   TQ3 A PAYMENT IS ALLOWED (TK3, pay.eve and pay.sutra) when the period
 *       owes 1 or more, the paying account stays at or above zero, the
 *       owner's per-day cap for streams is not passed, the stream does
 *       not pay past its total and its approval stands. Every post, a
 *       stream's or a tithe's, is first Coffer's transfer rule
 *       (coffer.transfer_ok: the paying balance and the same per-day cap),
 *       then ledger.post, referenced "TRICKLE S<id> T<tick>" for a stream
 *       and "TRICKLE G<id> N<k>" for a tithe; the books are read again
 *       after it (coffer_load).
 *   TQ4 WHAT IS PAID IS READ FROM THE BOOKS. trickle_stream_reconcile
 *       sums the paying account's entries (ledger.entries) whose
 *       reference names the stream; a sum past the stream's total is
 *       refused (ZXM_E_PROTOCOL) and the model keeps what it had.
 *   TQ5 THE DAY. What the model posts today is counted against the cap
 *       (Coffer's spent and day_cap); trickle_set_day with a new day
 *       starts the count again.
 *
 * Missing from zxvd, not invented: a standing-order or stream method (the
 * schedule is kept here), and a notification method (the notice is text
 * for the view). No drawing, no allocation, integers only. */
#ifndef TRICKLE_MODEL_H
#define TRICKLE_MODEL_H

#include "coffer_model.h"
#include "trickle_rules.h"
#include "zxm.h"

#define TRK_STREAMS    16u
#define TRK_TITHES     8u
#define TRK_NOTICE_MAX 255u

typedef struct {
    bool used;     /* the slot holds a stream, running or stopped */
    bool approved; /* running: the owner's approval stands */
    uint32_t id;   /* its number in the references (TQ3) */
    uint32_t from, to;
    int64_t total; /* minor units over the whole stream */
    int64_t ticks; /* periods, 1 .. TRK_TICKS_MAX */
    int64_t tick;  /* the last tick paid up to */
    int64_t paid;  /* what it has paid (TQ4) */
} trk_stream_t;

typedef struct {
    bool used, approved;
    uint32_t id;
    uint32_t from, to;
    int64_t tnum, tden; /* the share, tnum / tden */
    int64_t carry;      /* TK2: tden-ths of a minor unit carried */
    int64_t given;      /* the tithes posted */
    uint32_t incomes;   /* incomes tithed */
} trk_tithe_t;

typedef struct {
    zxm_link_t *link;
    coffer_t books; /* the books through Coffer */
    trk_stream_t s[TRK_STREAMS];
    trk_tithe_t g[TRK_TITHES];
    uint32_t next_id;
    char notice[TRK_NOTICE_MAX + 1];
} trickle_t;

/* day_cap: the owner's per-day cap on what streams and tithes post, in
 * minor units (0 .. TRK_AMOUNT_MAX; anything else takes Coffer's default). */
void trickle_init(trickle_t *t, zxm_link_t *link, int64_t day_cap);
/* The books' accounts and balances (coffer_load). */
int trickle_load(trickle_t *t);
/* TQ5: the day, days since 1970. */
int trickle_set_day(trickle_t *t, int64_t day);
/* TQ2: the per-day cap, 0 .. TRK_AMOUNT_MAX. */
int trickle_set_cap(trickle_t *t, int64_t day_cap, bool owner_confirmed);

/* TQ2: a stream of total (1 .. TRK_TOTAL_MAX) over ticks periods (1 ..
 * TRK_TICKS_MAX) from the owner's holder account from to the account to,
 * both in one currency; *slot is where it is kept. */
int trickle_stream_start(trickle_t *t, uint32_t from, uint32_t to, int64_t total, int64_t ticks,
                         bool owner_confirmed, uint32_t *slot);
/* TQ2: a running stream's new total and ticks; refused when what it has
 * paid is past what the new terms owe at its last tick (it never claws
 * back). */
int trickle_stream_change(trickle_t *t, uint32_t slot, int64_t total, int64_t ticks,
                          bool owner_confirmed);
/* TQ2: a running stream stopped; what it paid stays paid. */
int trickle_stream_stop(trickle_t *t, uint32_t slot, bool owner_confirmed);
/* TK1: what the stream owes at tick (its last tick .. ticks); asks nothing. */
int trickle_stream_due_at(const trickle_t *t, uint32_t slot, int64_t tick, int64_t *due);
/* TQ2, TQ3: pay what the stream owes at tick (ledger.post); *entry is the
 * posting's id, 0 when nothing was owed (nothing posted). */
int trickle_stream_pay(trickle_t *t, uint32_t slot, int64_t tick, bool owner_confirmed,
                       uint64_t *entry);
/* TQ4: what the stream has paid, from the books. */
int trickle_stream_reconcile(trickle_t *t, uint32_t slot);

/* TQ2: a tithe of tnum / tden (1 <= tnum <= tden <= TRK_TICKS_MAX) of each
 * income, from the owner's holder account from to the account to. */
int trickle_tithe_start(trickle_t *t, uint32_t from, uint32_t to, int64_t tnum, int64_t tden,
                        bool owner_confirmed, uint32_t *slot);
int trickle_tithe_stop(trickle_t *t, uint32_t slot, bool owner_confirmed);
/* TQ2, TQ3: the tithe of one income (0 .. TRK_TOTAL_MAX) posted and its
 * remainder carried; *entry 0 when the tithe is 0 (the carry still moves). */
int trickle_tithe_pay(trickle_t *t, uint32_t slot, int64_t income, bool owner_confirmed,
                      uint64_t *entry);

#endif
