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
 *   J8 LINKS. A read entry carries its own hash and the one before it
 *      (prev), so a reader can check the chain without this code.
 *   J9 AUDIT. zjr_append records a decision with the actor who approved it:
 *      source "audit", text "<actor> <sig> <what>". sig is 128 hex digits:
 *      the Ed25519 signature, by the journal's audit key, of the 32-byte
 *      SHA-256 of the previous line's hash followed by "<seq> <ms> <level>
 *      audit <actor> <what>" (J3's hash with the text "<actor> <what>").
 *      So an entry is signed with its place in the chain, and the line's own
 *      hash covers the signature. The key's 32-byte seed is
 *      <dir>/audit.key ("zxv-audit-key 1 <64 hex>", 0600), made on first use;
 *      the crypto is the caller's (zjr_set_signer: zxvd uses libzxv). zxvd
 *      records the actor it is given: the signature says the journal took
 *      this entry, in this place, under this name, not that the person
 *      named approved it. zxvd_log events cannot use the source "audit"
 *      (J1 events from it are written as "audit_").
 *  J10 AUDIT VERIFY. zjr_verify also checks every audit signature against
 *      the key; one that does not match, or an audit line that does not
 *      parse, is a break like a changed line. Without a signer, the chain
 *      is still checked and the result says the signatures were not.
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

#define ZJR_ACTOR_MAX 32u  /* J9: bytes of an actor: letters, digits and . _ - @ : + */
#define ZJR_AUDIT_MAX 200u /* J9: bytes of what was approved (printable, no newline) */
#define ZJR_TEXT_MAX  (ZJR_ACTOR_MAX + 1u + 128u + 1u + ZJR_AUDIT_MAX)

typedef struct {
    uint64_t seq, ms;
    uint8_t level;
    char source[16];
    char text[ZJR_TEXT_MAX + 1];   /* an audit entry's: what was approved (J9) */
    uint8_t prev[32], hash[32];    /* J8 */
    char actor[ZJR_ACTOR_MAX + 1]; /* J9: "" unless an audit entry */
    uint8_t sig[64];
} zjr_entry_t;

/* J9: the crypto behind audit entries. Each returns true on success. */
typedef struct {
    bool (*keygen)(void *ctx, uint8_t pub[32], uint8_t seed[32]);
    bool (*pub)(void *ctx, const uint8_t seed[32], uint8_t pub[32]);
    bool (*sign)(void *ctx, const uint8_t seed[32], const uint8_t msg[32], uint8_t sig[64]);
    bool (*verify)(void *ctx, const uint8_t pub[32], const uint8_t msg[32], const uint8_t sig[64]);
    void *ctx;
} zjr_signer_t;
/* NULL: none (audit entries are refused, and verify cannot check them). */
void zjr_set_signer(const zjr_signer_t *s);

enum {
    ZJR_E_OFF = -1,    /* no journal open */
    ZJR_E_ARG = -2,    /* a bad actor, text or level */
    ZJR_E_SIGNER = -3, /* no signer, or the key could not be made or read */
    ZJR_E_IO = -4      /* the disk refused the line (the journal stops, J5) */
};
/* J9: append a signed audit entry; *out (may be NULL) gets it as read. */
int zjr_append(const char *actor, const char *text, uint8_t level, zjr_entry_t *out);
/* J9: the audit key's public half; ZJR_E_OFF / ZJR_E_SIGNER. */
int zjr_audit_key(uint8_t pub[32]);

/* Up to cap entries, newest first; *more when older ones also match. */
int zjr_query(const zjr_query_t *q, zjr_entry_t *out, uint32_t cap, uint32_t *n, bool *more);

typedef struct {
    uint32_t segments;
    uint64_t entries, first_seq, last_seq;
    bool ok;
    uint64_t broken_at; /* the seq where the chain breaks; 0 when ok */
    char why[96];
    uint64_t audit;    /* J10: audit entries read */
    bool sigs_checked; /* J10: false when there was no signer or key */
} zjr_verify_t;
int zjr_verify(zjr_verify_t *v);

#endif
