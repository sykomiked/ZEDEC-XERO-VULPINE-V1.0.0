/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_journal.h — the journal: zxvd's events kept on disk across restarts,
 * chained by hashes so a changed or missing line shows, behind the Wake
 * console's history (journal.query, journal.verify).
 *
 *   J1 WHAT. Every event zxvd_log records (zxvd_log.h L2: what happened,
 *      never a request's parameters or a reply's content), and a line each
 *      time the journal opens.
 *   J2 WHERE. Segment files <dir>/<N>.jnl in a folder of the owner's
 *      (created 0700), and <dir>/head naming the newest and the oldest kept
 *      ("<newest> <oldest>"). Each start begins a
 *      new segment, and a segment that reaches the size limit is followed by
 *      a new one. The oldest segments go once all of them together pass the
 *      total limit (16 MiB unless set otherwise); the newest always stays.
 *   J3 CHAINED. A segment starts "zxv-journal 1 <next seq> <prev hash>"; each
 *      line is "<seq> <ms> <level> <source> <hash> <text>", where hash is the
 *      SHA-256 of the previous line's hash (the header's, for the first)
 *      followed by "<seq> <ms> <level> <source> <text>". The very first
 *      prev hash is 64 zeros.
 *   J4 SEQUENCE. seq counts from 1 across restarts with no gaps.
 *   J5 WRITES. Each line is appended and flushed on its own. A line cut
 *      short by a crash ends what is read of its segment, and the chain goes
 *      on from the last whole line.
 *   J6 READS. Newest first, filtered by time, level, source and text, a
 *      page at a time ("before": the oldest seq already shown).
 *   J7 VERIFY. Recomputes every hash from the oldest kept segment on (the
 *      lines before it were let go by J2) and reports the first break.
 *
 * zxvd uses one thread (zxvd_log.h L3); so does this. */
#ifndef ZXV_JOURNAL_H
#define ZXV_JOURNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "zxvd_log.h"

#define ZJR_DIR_MAX  900u
#define ZJR_SEGMENT  (1024u * 1024u)
#define ZJR_TOTAL    (16u * 1024u * 1024u)
#define ZJR_FIND_MAX 63u

/* Open the journal in `dir` and send zxvd_log's events to it; NULL or ""
 * closes it. 0, or -1 when the folder cannot be made or written. */
int zjr_open(const char *dir);
void zjr_close(void);
bool zjr_on(void);
/* Size limits (tests); 0 restores the default. */
void zjr_set_limits(uint32_t segment_bytes, uint32_t total_bytes);

typedef struct {
    uint64_t since_ms, until_ms; /* 0: no bound */
    uint8_t min_level;           /* zxvd_log_level_t */
    const char *source;          /* exact; NULL or "": any */
    const char *text;            /* in the source or text, ASCII case ignored */
    uint64_t before;             /* only seq below it; 0: from the newest */
} zjr_query_t;

typedef struct {
    uint64_t seq, ms;
    uint8_t level;
    char source[16];
    char text[ZXVD_LOG_TEXT + 1];
} zjr_entry_t;

/* Up to cap entries, newest first; *more when older ones also match. */
int zjr_query(const zjr_query_t *q, zjr_entry_t *out, uint32_t cap, uint32_t *n, bool *more);

typedef struct {
    uint32_t segments;
    uint64_t entries, first_seq, last_seq;
    bool ok;
    uint64_t broken_at; /* the seq where the chain breaks; 0 when ok */
    char why[96];
} zjr_verify_t;
int zjr_verify(zjr_verify_t *v);

#endif
