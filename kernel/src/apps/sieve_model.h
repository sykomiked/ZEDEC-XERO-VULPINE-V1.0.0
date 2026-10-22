/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sieve_model.h — Sieve, AI suggestions held until accepted, as a model
 * (zxm.h): every held suggestion (zxm.h M3) in one place. The rules are
 * sieve_rules.h.
 *
 * zxvd methods used: swarm.ask, play.draft, web4.feed, web4.consent and
 * journal.query. No drawing, no allocation, integers only.
 *
 *   SM1 HOLDING. A tagged answer (swarm.ask), an untrusted draft
 *       (play.draft) and every envelope zxvd holds for consent (web4.feed,
 *       status held) enter the table as PENDING, each with its source, the
 *       app it is for, when it was made, how long it is held (SR4) and the
 *       version of its target it was made against. Every limit is checked
 *       and room is reserved before anything is asked: a full table is
 *       backpressure (ZXM_E_TOOBIG) and nothing is sent. A row that is no
 *       ^ZXSIEVE record (SR5) is never kept. Nothing held is ever run or
 *       applied by Sieve.
 *   SM2 STATES: PENDING (1), ACCEPTED (2), REJECTED (3), as zxm.h's
 *       zxm_held_t, and APPLIED (4).
 *   SM3 THE OWNER'S WORD. Accept, reject, apply and sweep change kept
 *       state, so each takes owner_confirmed, checked after the argument
 *       checks and before anything is sent: without it ZXM_E_STATE and
 *       nothing changes. Only a PENDING suggestion with time left whose
 *       target has not changed since it was made may be accepted. An
 *       envelope's accept is the owner's consent (web4.consent, grant, for
 *       the seconds the hold has left, its own scope and amount, so the
 *       grant covers it and nothing more: Ramble's consent_covers, RR1);
 *       its reject is a deny. The others are accepted on Sieve's side.
 *   SM4 AN ACCEPT IS LOGGED. Every accept, reject and apply is written to
 *       a bounded ring of log lines (head - tail >= SIEVE_LOG is full: the
 *       action is refused before it is done, so nothing happens that is
 *       not logged). The view takes the lines out (sieve_log_take) in
 *       order. zxvd journals each web4.consent call itself ("rpc",
 *       "web4.consent done in N ms"); sieve_journal reads those back
 *       (journal.query) as what zxvd kept of the consent answers.
 *   SM5 APPLYING. A suggestion is applied only when SR1 says it may be
 *       (accepted, not expired, target unchanged since): the text is handed
 *       to the caller, which applies it; Sieve changes no document.
 *   SM6 COUNTS. Held for an app is SR3 over the table; held by source is
 *       Witness's kind_count (WR2), used as it is.
 *   SM7 NOTICES. zxvd has no method that posts a notice, so the notice of
 *       a held suggestion is only composed; the view shows it. */
#ifndef SIEVE_MODEL_H
#define SIEVE_MODEL_H

#include "sieve_rules.h"
#include "zxm.h"

#define SIEVE_MAX        64u /* suggestions held at once */
#define SIEVE_LOG        64u /* SM4: log lines not yet taken */
#define SIEVE_TEXT_MAX   ZXM_ANSWER_MAX
#define SIEVE_NAME_MAX   63u   /* a template or tool name */
#define SIEVE_DRAFT_REQ  1023u /* play.draft's request */
#define SIEVE_FEED_MAX   50u   /* web4.feed's largest page */
#define SIEVE_NOTICE_MAX 255u

enum {
    SIEVE_PENDING = ZXM_HELD_PENDING,
    SIEVE_ACCEPTED = ZXM_HELD_ACCEPTED,
    SIEVE_REJECTED = ZXM_HELD_REJECTED,
    SIEVE_APPLIED
};

typedef struct {
    uint32_t id; /* Sieve's own, from 1 */
    uint8_t source, state;
    int64_t app;     /* the app slot it is for */
    uint32_t ref;    /* an envelope's feed id; 0 for the others */
    int64_t made;    /* seconds since 1970 */
    int64_t ttl;     /* seconds held */
    int64_t version; /* the target's version it was made against */
    int64_t decided; /* when it was accepted or rejected; -1 not yet */
    bool pii;        /* an envelope's scope and amount (minor units) */
    int64_t amount;
    bool model;                    /* a draft: false when a stand-in answered */
    char what[SIEVE_NAME_MAX + 1]; /* a template, an envelope's tool, "" */
    char text[SIEVE_TEXT_MAX + 1];
    uint32_t len;
} sieve_item_t;

/* SM4: one log line */
typedef struct {
    uint32_t id;
    uint8_t source;
    uint8_t state; /* what it became: ACCEPTED, REJECTED or APPLIED */
    int64_t app;
    int64_t at;
    int64_t version;
} sieve_log_t;

typedef struct {
    zxm_link_t *link;
    sieve_item_t item[SIEVE_MAX];
    uint32_t n;
    uint32_t next_id;
    uint32_t added; /* the last sieve_gather's new envelopes */
    sieve_log_t log[SIEVE_LOG];
    uint64_t head, tail; /* SM4: the ring */
    /* SM4: journal.query */
    bool journal_on, journal_more;
    uint32_t journal_consents; /* consent answers zxvd journaled (grant or deny) */
    uint64_t journal_newest;   /* the newest matching line's seq; 0 none */
    char notice[SIEVE_NOTICE_MAX + 1];
} sieve_t;

void sieve_init(sieve_t *s, zxm_link_t *link);

/* SM1: ask the swarm (swarm.ask) for app at now, held for ttl seconds,
 * against the target's version; the answer is held, never applied. */
int sieve_ask(sieve_t *s, int64_t app, const char *prompt, int64_t now, int64_t ttl,
              int64_t version);
/* SM1: a draft of a template's draft file (play.draft), held the same way. */
int sieve_draft(sieve_t *s, int64_t app, const char *template_id, const char *request, int64_t now,
                int64_t ttl, int64_t version);
/* SM1: every envelope zxvd holds for consent (web4.feed, status held, one
 * page of at most 50), each held from now for ttl seconds for app; those
 * already held are not added twice (s->added counts the new ones). */
int sieve_gather(sieve_t *s, int64_t app, int64_t now, int64_t ttl);

/* The suggestion with this id in the table; -1 none. */
int32_t sieve_find(const sieve_t *s, uint32_t id);

/* SM3: the owner's accept or reject, at now, with the target's version. */
int sieve_accept(sieve_t *s, uint32_t id, int64_t now, int64_t version, bool owner_confirmed);
int sieve_reject(sieve_t *s, uint32_t id, int64_t now, bool owner_confirmed);

/* SR1 for the suggestion id at now against the target's version now. */
bool sieve_may_apply(const sieve_t *s, uint32_t id, int64_t now, int64_t version);
/* SM5: hand an accepted suggestion's text to the caller to apply. */
int sieve_apply(sieve_t *s, uint32_t id, int64_t now, int64_t version, bool owner_confirmed,
                const char **text, uint32_t *len);

/* SM1: drop what is decided or lapsed at now, making room. */
int sieve_sweep(sieve_t *s, int64_t now, bool owner_confirmed);

/* SM6: suggestions still held (PENDING with time left) for app at now
 * (SR3); and pending ones from one source (Witness's kind_count). */
int64_t sieve_held_for(const sieve_t *s, int64_t app, int64_t now);
int64_t sieve_held_by_source(const sieve_t *s, int64_t source);

/* SM4: the oldest log line not yet taken; ZXM_E_STATE when none. */
int sieve_log_take(sieve_t *s, sieve_log_t *out);
/* SM4: the consent answers zxvd journaled (journal.query, source "rpc",
 * text "web4.consent"; its line does not say grant or deny) at or after
 * since_ms (0: no bound), newest first, up to 500. */
int sieve_journal(sieve_t *s, uint64_t since_ms);

/* SM7: the notice of a pending suggestion at now (s->notice). */
int sieve_notice(sieve_t *s, uint32_t id, int64_t now);

#endif
