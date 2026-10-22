/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* witness_model.h — Witness, the audit log, as a model (zxm.h): a record
 * of what changed through zxvd, what it refused (held crossings), which AI
 * actions ran and what failed, gathered from zxvd's journal (journal.query,
 * kept across restarts) or, with no journal, from its recent events
 * (log.tail). Each entry is linked to the one before it; Witness checks
 * that chain itself (witness_rules.h WR1) and asks zxvd to recompute the
 * journal's hash chain (journal.verify), and joins the two witnesses with
 * the kernel's knowledge join (lpres_kjoin): agreement keeps the value, a
 * witness that says nothing adds nothing, and a disagreement is held
 * (BOTH), never resolved by picking one. sys.info stamps the record with
 * zxvd's clock and host. Read only. No drawing, no allocation.
 *
 *   WM1 The record is oldest first, up to WITNESS_KEEP entries: the newest
 *       ones that match the window asked for. `more` says older ones
 *       were left out.
 *   WM2 An entry's index is zxvd's seq; its prev is the index of the entry
 *       before it in the record (0 for the first). An entry whose seq does
 *       not fit 18 digits is not a record (ZXM_E_PROTOCOL).
 *   WM3 Kinds (witness_classify): an error is a FAULT; a warning a REFUSED
 *       crossing (zxvd logs refusals as warnings); an rpc event of a
 *       swarm.* method an AI action; any other rpc event "<method> done" a
 *       CHANGE; anything else OTHER.
 *   WM4 Nothing is kept from a gather that fails: the record shown stays. */
#ifndef WITNESS_MODEL_H
#define WITNESS_MODEL_H

#include "lpres.h"
#include "witness_rules.h"
#include "zxm.h"

#define WITNESS_KEEP    512u /* entries in the record */
#define WITNESS_PAGE    100u /* journal events asked for at once */
#define WITNESS_TAIL    256u /* log.tail events asked for at once */
#define WITNESS_TEXT    160u
#define WITNESS_HOST    255u
#define WITNESS_SUMMARY 383u

enum { WITNESS_INFO, WITNESS_WARN, WITNESS_ERROR };
enum {
    WITNESS_NONE = 0,
    WITNESS_CHANGE = 1,  /* a change made through zxvd */
    WITNESS_REFUSED = 2, /* a crossing zxvd refused and held */
    WITNESS_AI = 3,      /* an AI action zxvd ran (swarm.*) */
    WITNESS_FAULT = 4,   /* an error */
    WITNESS_OTHER = 5,
    WITNESS_KINDS = 6
};

typedef struct {
    uint64_t index, prev, time_ms;
    uint8_t kind, level;
    char source[16];
    char text[WITNESS_TEXT + 1];
} witness_entry_t;

/* journal.verify's answer */
typedef struct {
    bool done, on, ok;
    uint64_t entries, first_seq, last_seq, broken_at;
    char why[96];
} witness_seal_t;

typedef struct {
    zxm_link_t *link;
    witness_entry_t e[WITNESS_KEEP]; /* oldest first (WM1) */
    uint32_t n;
    bool gathered;    /* a gather succeeded */
    bool kept;        /* from the journal; false: zxvd's recent events only */
    bool more;        /* older events were left out */
    uint64_t dropped; /* events zxvd's ring no longer held (log.tail) */
    uint64_t since_ms, until_ms;
    uint64_t as_of_ms; /* zxvd's clock at the gather (sys.info) */
    char host[WITNESS_HOST + 1];
    uint32_t count[WITNESS_KINDS];
    uint32_t broken; /* the first entry not linked; n when the chain is intact */
    witness_seal_t seal;
    lpres_state_t verdict; /* the two witnesses joined (witness_seal) */
    char summary[WITNESS_SUMMARY + 1];
} witness_t;

/* WM3: the kind of one event. */
uint8_t witness_classify(uint8_t level, const char *source, const char *text);

void witness_init(witness_t *w, zxm_link_t *link);
/* Gather the record of events between since_ms and until_ms (0: no
 * bound), from the journal when zxvd keeps one, else from log.tail. */
int witness_gather(witness_t *w, uint64_t since_ms, uint64_t until_ms);
/* The record's own chain: the first entry not linked, w->n when intact. */
uint32_t witness_chain(const witness_t *w);
/* How many entries of the record are of this kind. */
int64_t witness_count(const witness_t *w, uint8_t kind);
/* Ask zxvd to recompute the journal's hash chain and join it with the
 * record's own chain into w->verdict (ZXM_E_STATE before a gather). */
int witness_seal(witness_t *w);
/* One line for the view: the counts and both witnesses. */
const char *witness_summarize(witness_t *w);

#endif
