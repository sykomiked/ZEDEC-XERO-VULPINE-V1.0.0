/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* rookery_model.h — Rookery, mail, as a model (zxm.h): letters as signed
 * Web4 envelopes to another companion, read from the owner's feed and
 * checked, with drafts and kept copies in the Hoard. The rules are
 * rookery_rules.h (KR1-KR6).
 *
 * zxvd methods used: web4.card_verify, web4.post, web4.consent, web4.feed,
 * web4.verify, files.mkdir, files.list, files.write and ledger.balance.
 * No drawing, no allocation, integers only.
 *
 *   RK1 A LETTER IS COMPOSED before anything is asked of zxvd: its address
 *       counted byte by byte and judged (KR3), its subject and body inside
 *       their limits (KR2), its payload "Subject: " + subject + "\n\n" +
 *       body. A letter that does not fit is refused, and the letter before
 *       is kept.
 *   RK2 THE RECIPIENT'S CARD is checked (web4.card_verify, with the
 *       agent_id the letter goes to). Its answer is what KR1 calls
 *       verified; a card checked for another agent_id verifies nothing.
 *   RK3 A LETTER IS POSTED only when KR1 holds: the card verified, the
 *       payload within 2048 bytes and the owner's consent present for
 *       every scope the letter needs (KR5). It goes as a REQUEST (thread
 *       0, a new thread) or as a RESPONSE whose req_id is its thread.
 *       zxvd holds a letter with personal data or money unsigned; the
 *       owner's consent is then given on their confirmed word only
 *       (web4.consent, 1..86400 seconds), never by Rookery itself.
 *   RK4 THE FEED is read a page at a time (web4.feed, at most 50, newest
 *       first). Only letters (tool "mail", REQUEST or RESPONSE) are kept,
 *       each filed by folder and thread as a ^ZXROOKERY record that fits
 *       KR4; a page with one that does not fit keeps the page before. A
 *       letter's signature is web4.verify's, as zxvd gives it.
 *   RK5 THE HOARD keeps drafts and copies under Rookery/<Folder>/<name>
 *       .letter, written whole (files.write); a folder's letters are listed
 *       (files.list) and each filed as a ^ZXROOKERY record (KR4).
 *   RK6 ADVICE ONLY: the days a letter in the Trash has left before it is
 *       offered for removal (KR6, Lockbox's days_left), and what the paying
 *       account would hold after a letter's amount left it (ledger.balance,
 *       then Coffer's balance with the amount as the movement). Nothing is
 *       removed, and no money moves, from here.
 *   RK7 NOTICES. zxvd has no notification method, so a held letter's
 *       notice is composed only; the view shows it.
 *
 * zxvd has no mail transport, no delivery of an envelope to another
 * companion and no notification method; none is invented. */
#ifndef ROOKERY_MODEL_H
#define ROOKERY_MODEL_H

#include "rookery_rules.h"
#include "zxm.h"

#define ROOKERY_FEED_MAX   50u   /* web4.feed: the largest page */
#define ROOKERY_FILES_MAX  64u   /* letters listed from one folder */
#define ROOKERY_HEX_ID     64u   /* an agent_id: 32 bytes as hex */
#define ROOKERY_NAME_MAX   31u   /* a card's or a letter file's name */
#define ROOKERY_PK_HEX     3904u /* an ML-DSA-65 public key as hex */
#define ROOKERY_SIG_HEX    6618u /* its binding signature as hex */
#define ROOKERY_REASON_MAX 63u
#define ROOKERY_PATH_MAX   127u
#define ROOKERY_NOTICE_MAX 255u
#define ROOKERY_THREAD_MAX UINT32_MAX /* a thread is a feed id */
#define ROOKERY_GRANT_MAX  86400      /* web4.consent's expires_in */
#define ROOKERY_NOW_MAX    INT64_C(253402300799)
#define ROOKERY_AMOUNT_MAX INT64_C(999999999999999999) /* Coffer's 18 digits */

enum { ROOKERY_INBOX, ROOKERY_DRAFTS, ROOKERY_SENT, ROOKERY_HELD, ROOKERY_TRASH, ROOKERY_FOLDERS };
enum { ROOKERY_ST_SIGNED, ROOKERY_ST_HELD, ROOKERY_ST_DENIED };

/* RK1: an address counted (KR3's inputs) */
typedef struct {
    int64_t size, ats, local, domain, longest, shortest, bad;
} rookery_addr_t;

/* a letter, composed or read from the feed */
typedef struct {
    uint32_t id;     /* its feed id; 0 not posted */
    uint32_t thread; /* the feed id that began its thread; 0 a new thread */
    uint8_t folder, status;
    char to[ROOKERY_HEX_ID + 1], from[ROOKERY_HEX_ID + 1];
    char addr[ROOKERY_ADDR_MAX + 1];
    char subject[ROOKERY_SUBJECT_MAX + 1];
    char body[ROOKERY_BODY_MAX + 1];
    bool pii;
    int64_t amount; /* minor units it moves; 0 none */
    int64_t stamp;  /* seconds since 1970 when signed; -1 none yet */
    uint32_t size;  /* the payload's bytes */
} rookery_letter_t;

/* RK2: a companion's card, as web4.agent_card makes it */
typedef struct {
    char name[ROOKERY_NAME_MAX + 1];
    uint64_t bound_ms;
    char pk_hex[ROOKERY_PK_HEX + 1];
    char sig_hex[ROOKERY_SIG_HEX + 1];
    char agent_id[ROOKERY_HEX_ID + 1];
} rookery_card_t;

/* RK5: one letter file of a folder */
typedef struct {
    char name[ROOKERY_NAME_MAX + 1];
    uint32_t size;
    int64_t mtime;
    int64_t days; /* the Trash: days left before it is offered for removal (KR6); -1 elsewhere */
} rookery_file_t;

typedef struct {
    zxm_link_t *link;
    /* RK1 */
    rookery_letter_t draft;
    bool composed;
    char payload[ROOKERY_PAYLOAD_MAX + 1];
    /* RK2 */
    char card_id[ROOKERY_HEX_ID + 1]; /* the agent_id last checked */
    int64_t verified;                 /* 1 valid, 0 not; KR1 */
    char reason[ROOKERY_REASON_MAX + 1];
    /* RK3 */
    uint32_t posted; /* the feed id of the letter last posted */
    uint8_t posted_status;
    /* RK4 */
    rookery_letter_t page[ROOKERY_FEED_MAX];
    uint32_t n, next;
    bool sig_valid; /* web4.verify's answer */
    /* RK5 */
    rookery_file_t files[ROOKERY_FILES_MAX];
    uint32_t nfiles;
    /* RK6 */
    int64_t balance, after;
    /* RK7 */
    char notice[ROOKERY_NOTICE_MAX + 1];
} rookery_t;

void rookery_init(rookery_t *r, zxm_link_t *link);

/* RK1: count an address (out) and judge it (KR3): the fault, or
 * ROOKERY_REFUSED for no address (NULL, or past 254 bytes counted up to
 * 255). */
int64_t rookery_address(const char *addr, rookery_addr_t *out);

/* RK1: compose a letter: 0, or ZXM_E_ARG when the address has a fault, the
 * subject or body passes its limit, or the amount is outside 0..18 digits;
 * the letter before is kept then. */
int rookery_compose(rookery_t *r, const char *addr, const char *subject, const char *body, bool pii,
                    int64_t amount, uint32_t thread);

/* RK2: web4.card_verify of the card the letter goes to (r->verified). */
int rookery_check_card(rookery_t *r, const rookery_card_t *card);

/* RK3: post the composed letter to agent_id (the card checked last) at
 * now (-1: zxvd's clock). consented is the owner's confirmed word for its
 * personal data and money. ZXM_E_STATE, and nothing sent, when KR1 does
 * not hold. */
int rookery_send(rookery_t *r, const char *agent_id, bool consented, int64_t now);

/* RK3: the owner's answer to a held letter on the page (or the one just
 * posted): grant for expires_in seconds, or deny; only on their confirmed
 * word. */
int rookery_consent(rookery_t *r, uint32_t id, bool grant, int64_t expires_in, int64_t now,
                    bool owner_confirmed);

/* RK4: one page of letters: before 0 for the newest, limit 1..50; own is
 * the owner's agent_id (their own letters are filed as sent or held). */
int rookery_feed(rookery_t *r, uint32_t before, uint32_t limit, const char *own);
/* RK4: the letters of one thread on the page, newest first, into ids;
 * how many there are (at most cap are written). */
uint32_t rookery_thread(const rookery_t *r, uint32_t thread, uint32_t *ids, uint32_t cap);
/* RK4: web4.verify of a letter on the page (r->sig_valid, r->reason). */
int rookery_verify(rookery_t *r, uint32_t id);

/* RK5: Rookery/ and its five folders (made when missing). */
int rookery_folders(rookery_t *r);
/* RK5: write the composed letter whole as Rookery/<folder>/<name>.letter. */
int rookery_keep(rookery_t *r, uint8_t folder, const char *name);
/* RK5: the letters of one folder (r->files), each filed (KR4); in the
 * Trash, the days each has left at now (KR6, RK6). */
int rookery_list(rookery_t *r, uint8_t folder, int64_t now);

/* RK6: what account would hold after the composed letter's amount left
 * it (r->balance, r->after). */
int rookery_money_after(rookery_t *r, uint32_t account);

/* RK7: the notice for the letter last posted, when it is held. */
int rookery_notice(rookery_t *r);

#endif
