/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* rookery_rules.h — Rookery's rules in C, the reference its Eve, KRON,
 * Rust (WebAssembly) and MYRA blocks are checked against
 * (blocks/rookery). Pure integer functions, no state: the model
 * (rookery_model.c) and the language test (tests/rookery_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs. A letter is one Web4 envelope (zxvd's web4.post): its payload is
 * "Subject: " + subject + "\n\n" + body, at most 2048 bytes.
 *
 *   KR1 MAY THIS BE SENT: the recipient's card verified (verified, the
 *       answer of web4.card_verify for the card the letter goes to, is
 *       exactly 1), the payload within zxvd's limit (room, 2048 minus the
 *       payload's bytes, is 0..2048) and the owner's consent present for
 *       every scope the letter needs (consentgap, the scopes it needs that
 *       the owner has not consented to, personal data and money as zxvd's
 *       web4.post holds them, is 0).
 *   KR2 A LETTER'S SIZE LIMITS, each field against its own range: the
 *       subject 0..200 bytes and the body 0..1837 bytes, so that any
 *       subject and body inside them make a payload of at most 2048 bytes
 *       (9 + 200 + 2 + 1837).
 *   KR3 WHY A MAIL ADDRESS DOES NOT FIT, the first that applies, from
 *       counts of its bytes: size (the whole address), ats (its '@'
 *       bytes), local (bytes before the first '@', all of them when there
 *       is none), domain (bytes after the last '@', 0 when there is none),
 *       longest and shortest (the longest and shortest label of the
 *       domain, split on '.', both 0 when the domain is empty) and bad (the
 *       bytes not allowed where they stand). 1 the address is shorter than
 *       3 bytes or past 254; 2 not exactly one '@'; 3 the local part empty
 *       or past 64 bytes; 4 the domain empty or past 253 bytes; 5 a label
 *       empty or past 63 bytes; 6 a byte not allowed; 0 none. Counts that
 *       cannot come from one address (a negative count, more parts than
 *       bytes, a shortest label longer than the longest, or the longest
 *       longer than the domain) are no measurement: refused.
 *   KR4 A LETTER RECORD of ^ZXROOKERY holds integers of at most the places
 *       its pieces declare, sign aside (blocks/rookery/record.myra): folder
 *       1, thread 10, letter 10, size 4, stamp 12.
 *   KR5 THE SCOPES a letter needs: personal data when it carries personal
 *       data, money when it moves an amount above 0; the gap is how many of
 *       them the owner has not consented to.
 *   KR6 THE TRASH: a letter put in the Trash at `stamp` is offered for
 *       removal after 30 days; the days left are Lockbox's days_left of
 *       stamp + 30 days at now (lockbox_rules.h LR2). Advice only: nothing
 *       is removed but by the owner. */
#ifndef ROOKERY_RULES_H
#define ROOKERY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lockbox_rules.h"

#define ROOKERY_REFUSED      (-1)
#define ROOKERY_PAYLOAD_MAX  2048                  /* KR1: web4.post's payload */
#define ROOKERY_HEAD         9                     /* KR2: "Subject: " */
#define ROOKERY_GAP          2                     /* KR2: "\n\n" */
#define ROOKERY_SUBJECT_MAX  200                   /* KR2 */
#define ROOKERY_BODY_MAX     1837                  /* KR2: 2048 - 9 - 200 - 2 */
#define ROOKERY_ADDR_MIN     3                     /* KR3: a@b */
#define ROOKERY_ADDR_MAX     254                   /* KR3 */
#define ROOKERY_LOCAL_MAX    64                    /* KR3 */
#define ROOKERY_DOMAIN_MAX   253                   /* KR3 */
#define ROOKERY_LABEL_MAX    63                    /* KR3 */
#define ROOKERY_FOLDER_FIELD 9                     /* KR4: i1 */
#define ROOKERY_ID_FIELD     INT64_C(9999999999)   /* KR4: i10 */
#define ROOKERY_SIZE_FIELD   9999                  /* KR4: i4 */
#define ROOKERY_STAMP_FIELD  INT64_C(999999999999) /* KR4: i12 */
#define ROOKERY_TRASH_DAYS   30                    /* KR6 */

enum {
    ROOKERY_ADDR_FINE,
    ROOKERY_ADDR_LENGTH,
    ROOKERY_ADDR_AT,
    ROOKERY_ADDR_LOCAL,
    ROOKERY_ADDR_DOMAIN,
    ROOKERY_ADDR_LABEL,
    ROOKERY_ADDR_BYTE
};

/* KR1 */
static inline bool rookery_letter_send_ok(int64_t verified, int64_t room, int64_t consentgap)
{
    return verified == 1 && room >= 0 && room <= ROOKERY_PAYLOAD_MAX && consentgap == 0;
}

/* KR2 */
static inline bool rookery_letter_size_ok(int64_t subject, int64_t body)
{
    return subject >= 0 && subject <= ROOKERY_SUBJECT_MAX && body >= 0 && body <= ROOKERY_BODY_MAX;
}

/* KR3 */
static inline int64_t rookery_addr_fault(int64_t size, int64_t ats, int64_t local, int64_t domain,
                                         int64_t longest, int64_t shortest, int64_t bad)
{
    if (size < 0 || ats < 0 || local < 0 || domain < 0 || longest < 0 || shortest < 0 || bad < 0)
        return ROOKERY_REFUSED;
    /* every count is at most size here, so nothing below can overflow */
    if (ats > size || local > size || domain > size || bad > size || local + domain > size - ats ||
        shortest > longest || longest > domain)
        return ROOKERY_REFUSED;
    if (size < ROOKERY_ADDR_MIN || size > ROOKERY_ADDR_MAX) return ROOKERY_ADDR_LENGTH;
    if (ats != 1) return ROOKERY_ADDR_AT;
    if (local < 1 || local > ROOKERY_LOCAL_MAX) return ROOKERY_ADDR_LOCAL;
    if (domain < 1 || domain > ROOKERY_DOMAIN_MAX) return ROOKERY_ADDR_DOMAIN;
    if (shortest < 1 || longest > ROOKERY_LABEL_MAX) return ROOKERY_ADDR_LABEL;
    if (bad > 0) return ROOKERY_ADDR_BYTE;
    return ROOKERY_ADDR_FINE;
}

static inline bool rookery_fits(int64_t v, int64_t most)
{
    return v >= -most && v <= most;
}

/* KR4 */
static inline bool rookery_letter_record_ok(int64_t folder, int64_t thread, int64_t letter,
                                            int64_t size, int64_t stamp)
{
    return rookery_fits(folder, ROOKERY_FOLDER_FIELD) && rookery_fits(thread, ROOKERY_ID_FIELD) &&
           rookery_fits(letter, ROOKERY_ID_FIELD) && rookery_fits(size, ROOKERY_SIZE_FIELD) &&
           rookery_fits(stamp, ROOKERY_STAMP_FIELD);
}

/* KR5 */
static inline int64_t rookery_consent_gap(bool pii, int64_t amount, bool consented)
{
    if (consented) return 0;
    return (pii ? 1 : 0) + (amount > 0 ? 1 : 0);
}

/* KR6: LOCKBOX_REFUSED when stamp + 30 days or now is past Lockbox's stamps */
static inline int64_t rookery_trash_days(int64_t stamp, int64_t now)
{
    const int64_t keep = (int64_t) ROOKERY_TRASH_DAYS * LOCKBOX_DAY;
    if (stamp < 0 || stamp > LOCKBOX_STAMP_MAX - keep) return LOCKBOX_REFUSED;
    return lockbox_days_left(stamp + keep, now);
}

#endif
