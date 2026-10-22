/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ramble_model.h — Ramble, the Web4 viewer, as a model (zxm.h): the
 * owner's envelope feed, an envelope's signed bytes and their check,
 * content by its CID, agent cards, and the envelopes Chiglet holds for the
 * owner's consent. The rules are ramble_rules.h.
 *
 * zxvd methods used: web4.open, web4.feed, web4.envelope, web4.verify,
 * web4.consent, web4.cid, web4.card_verify, aipi.get and ledger.balance.
 * No drawing, no allocation, integers only.
 *
 *   RM1 THE FEED is read a page at a time (web4.feed, at most 50, newest
 *       first; next pages on). Every envelope is indexed as a ^ZXRAMBLE
 *       record that fits RR2 before any of the page is kept: one that does
 *       not fit, or an answer that is no feed, keeps the page before.
 *   RM2 NOTHING HELD LEAVES. A held or denied envelope's bytes are never
 *       asked for (web4.envelope refuses them too); only a signed one's.
 *       Its check is web4.verify's, as zxvd gives it.
 *   RM3 CONSENT IS THE OWNER'S ACT. Ramble never grants or denies by
 *       itself: web4.consent is called only on the owner's confirmed word,
 *       for an envelope held in the feed, for 1..86400 seconds (zxvd's
 *       limit). A grant's scope is what the envelope needs (RR3) and its
 *       ceiling the envelope's own amount, so the grant covers it (RR1)
 *       and nothing more. What it covers later is advice (ramble_covers);
 *       money moves only through the owner's books, never from here.
 *   RM4 CONTENT BY CID. A CID's text is checked byte by byte against
 *       Seedbank's rule (seedbank.cid_byte, SR2) before it is used; then
 *       it is turned into aipi's block name ("z" + base58btc of the same
 *       binary CID) and fetched (aipi.get, at most 65536 bytes); the bytes
 *       are kept only when they are as many as aipi says. The CID of bytes
 *       is web4.cid's, and is kept only when it fits SR2.
 *   RM5 MONEY ADVICE. What an account would hold after a held envelope's
 *       amount left it (ledger.balance, then Reckoner's statement_close
 *       with the amount as the one debit): shown, never posted.
 *   RM6 NOTICES. zxvd has no notification method, so the model composes a
 *       held envelope's notice text only; the view shows it.
 *
 * zxvd has no method that fetches a page over HTTP, delivers an envelope
 * to another companion, or reads back a grant's expiry (web4.verify gives
 * the scope and ceiling only): Ramble keeps the expiry of the grants it
 * asked for itself, and none of the others is invented. */
#ifndef RAMBLE_MODEL_H
#define RAMBLE_MODEL_H

#include "ramble_rules.h"
#include "seedbank_rules.h"
#include "zxm.h"

#define RAMBLE_FEED_MAX    50u    /* web4.feed: the largest page */
#define RAMBLE_GRANTS      64u    /* zxvd: at most 64 unexpired grants */
#define RAMBLE_HEX_ID      64u    /* an agent_id: 32 bytes as hex */
#define RAMBLE_NAME_MAX    31u    /* a feed's or a card's name */
#define RAMBLE_PAYLOAD_MAX 2048u  /* web4.post: a payload's bytes */
#define RAMBLE_REASON_MAX  63u    /* web4.verify's reason */
#define RAMBLE_ENV_MAX     16384u /* one envelope's bytes kept */
#define RAMBLE_GET_MAX     SEEDBANK_GET_MAX
#define RAMBLE_BLOCK_MAX   127u  /* aipi's text form of a CID */
#define RAMBLE_URI_MAX     79u   /* "ipfs://" + a CID text */
#define RAMBLE_DATA_MAX    8000u /* web4.cid's data: 16000 hex digits fit one request */
#define RAMBLE_PK_HEX      3904u /* an ML-DSA-65 public key as hex */
#define RAMBLE_SIG_HEX     6618u /* its binding signature as hex */
#define RAMBLE_NOTICE_MAX  255u
#define RAMBLE_NOW_MAX     INT64_C(253402300799) /* zxvd's "now": the last second of 9999 */

enum { RAMBLE_REQUEST, RAMBLE_RESPONSE, RAMBLE_STREAM, RAMBLE_CONSENT };
enum { RAMBLE_SIGNED, RAMBLE_HELD, RAMBLE_DENIED };

typedef struct {
    uint32_t id;
    uint8_t kind, status;
    char from[RAMBLE_HEX_ID + 1], to[RAMBLE_HEX_ID + 1];
    char tool[RAMBLE_NAME_MAX + 1];
    bool pii, final;
    int64_t amount;   /* minor units */
    int64_t ts;       /* seconds since 1970 when signed; -1 none yet */
    uint32_t consent; /* the CONSENT entry that answered it; 0 none */
    uint32_t answers; /* a CONSENT entry: the envelope it answers; 0 none */
    int8_t grant;     /* a CONSENT entry: 1 granted, 0 denied; -1 none */
    uint32_t bytes;   /* signed length, 0 while held or denied */
    char payload[RAMBLE_PAYLOAD_MAX + 1];
} ramble_env_t;

/* A grant the owner made through Ramble (RM3). */
typedef struct {
    uint32_t id;     /* the envelope it answered */
    int64_t scope;   /* RR3 */
    int64_t ceiling; /* minor units */
    int64_t expires; /* seconds since 1970 */
} ramble_grant_t;

/* web4.verify's or web4.card_verify's answer. */
typedef struct {
    bool done, valid;
    char reason[RAMBLE_REASON_MAX + 1];
    bool consent;      /* a consent token is attached */
    int64_t scope;     /* its scope (RR3); -1 none */
    int64_t max_minor; /* its ceiling; -1 none */
    char agent_id[RAMBLE_HEX_ID + 1];
} ramble_verdict_t;

typedef struct {
    zxm_link_t *link;
    /* web4.open */
    bool open;
    char agent_id[RAMBLE_HEX_ID + 1];
    char name[RAMBLE_NAME_MAX + 1];
    /* RM1: the page shown */
    ramble_env_t env[RAMBLE_FEED_MAX];
    uint32_t n;
    uint32_t next; /* 'before' for the next page; 0 none */
    uint32_t total, held;
    /* RM3 */
    ramble_grant_t grant[RAMBLE_GRANTS];
    uint32_t ngrants;
    /* RM2 */
    uint8_t raw[RAMBLE_ENV_MAX];
    uint32_t raw_len, raw_id;
    ramble_verdict_t verdict, card;
    /* RM4 */
    char cid[SEEDBANK_CID_LEN + 1];
    char uri[RAMBLE_URI_MAX + 1];
    uint8_t got[RAMBLE_GET_MAX];
    uint32_t got_len;
    char got_cid[SEEDBANK_CID_LEN + 1];
    /* RM5 */
    int64_t balance, after;
    /* RM6 */
    char notice[RAMBLE_NOTICE_MAX + 1];
} ramble_t;

void ramble_init(ramble_t *r, zxm_link_t *link);

/* web4.open: the owner's feed, named name (NULL: zxvd's default) at now
 * (-1: zxvd's clock). */
int ramble_open(ramble_t *r, const char *name, int64_t now);

/* RM1: one page of the feed: before 0 for the newest, limit 1..50, status
 * -1 for any or RAMBLE_SIGNED, RAMBLE_HELD or RAMBLE_DENIED. */
int ramble_feed(ramble_t *r, uint32_t before, uint32_t limit, int32_t status);
/* The envelope with this id on the page shown; -1 none. */
int32_t ramble_find(const ramble_t *r, uint32_t id);

/* RM2: a signed envelope's bytes (r->raw), and web4.verify's check of it. */
int ramble_envelope(ramble_t *r, uint32_t id);
int ramble_verify(ramble_t *r, uint32_t id);

/* RM3: answer a held envelope, only on the owner's confirmed word:
 * grant for expires_in seconds (1..86400) from now, or deny. */
int ramble_consent(ramble_t *r, uint32_t id, bool grant, int64_t expires_in, int64_t now,
                   bool owner_confirmed);
/* RM3: does the owner's grant for envelope id cover an action needing pii
 * and amount at now (RR1)? Advice only; false when there is no grant. */
bool ramble_covers(const ramble_t *r, uint32_t id, bool pii, int64_t amount, int64_t now);

/* RM4: aipi's block name of a CID's text ("b" + base32, SR2); 0, or
 * ZXM_E_ARG when the text does not fit SR2 or is no CIDv1 raw sha2-256. */
int ramble_cid_to_block(const char *cid, char out[RAMBLE_BLOCK_MAX + 1]);
/* RM4: web4.cid of data[0..len) (at most RAMBLE_DATA_MAX bytes). */
int ramble_cid_of(ramble_t *r, const uint8_t *data, uint32_t len);
/* RM4: the bytes a CID's text names (r->got). */
int ramble_fetch(ramble_t *r, const char *cid);

/* web4.card_verify (r->card); agent_id NULL to leave it out. */
int ramble_card_verify(ramble_t *r, const char *name, uint64_t bound_ms, const char *pk_hex,
                       const char *sig_hex, const char *agent_id);

/* RM5: what account would hold after envelope id's amount left it
 * (r->balance, r->after). */
int ramble_money_after(ramble_t *r, uint32_t account, uint32_t id);

/* RM6: the notice for a held envelope on the page (r->notice). */
int ramble_notice(ramble_t *r, uint32_t id);

#endif
